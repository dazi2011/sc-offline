#!/usr/bin/env python3
"""Read-only extraction of files from Star Citizen's Data.p4k (zip64, zstd or deflate entries).

usage: p4k-extract.py <Data.p4k> <out-dir> <path-glob> [...]
Never writes to the archive; opens it read-only through mmap.
"""
import fnmatch, mmap, os, struct, sys, zlib
from compression import zstd

def entries(m):
    eocd = m.rfind(b'PK\x05\x06', max(0, len(m) - (1 << 20)))
    if eocd < 0: sys.exit('no end of central directory')
    loc = m.rfind(b'PK\x06\x07', max(0, eocd - 64), eocd)
    if loc < 0: sys.exit('not a zip64 archive')
    z64 = struct.unpack_from('<Q', m, loc + 8)[0]
    if m[z64:z64 + 4] != b'PK\x06\x06': sys.exit('bad zip64 end record')
    count, cd_size, cd_off = struct.unpack_from('<QQQ', m, z64 + 32)
    pos = cd_off
    for _ in range(count):
        if m[pos:pos + 4] != b'PK\x01\x02': sys.exit(f'bad central directory entry at {pos}')
        flags, method = struct.unpack_from('<HH', m, pos + 8)
        csize, usize = struct.unpack_from('<II', m, pos + 20)
        nlen, xlen, clen = struct.unpack_from('<HHH', m, pos + 28)
        off = struct.unpack_from('<I', m, pos + 42)[0]
        name = bytes(m[pos + 46:pos + 46 + nlen]).decode('utf-8', 'replace')
        extra = m[pos + 46 + nlen:pos + 46 + nlen + xlen]
        i = 0
        while i + 4 <= len(extra):
            hid, hlen = struct.unpack_from('<HH', extra, i)
            if hid == 1:
                # CIG's archives pad this record; read only the 8-byte values that are present.
                n = max(0, min(hlen, len(extra) - i - 4) // 8)
                vals = list(struct.unpack_from('<' + 'Q' * n, extra, i + 4))
                if usize == 0xFFFFFFFF and vals: usize = vals.pop(0)
                if csize == 0xFFFFFFFF and vals: csize = vals.pop(0)
                if off == 0xFFFFFFFF and vals: off = vals.pop(0)
            i += 4 + hlen
        yield name, flags, method, csize, usize, off
        pos += 46 + nlen + xlen + clen

def read(m, method, csize, usize, off):
    nlen, xlen = struct.unpack_from('<HH', m, off + 26)
    data = bytes(m[off + 30 + nlen + xlen:off + 30 + nlen + xlen + csize])
    if method == 0: return data
    if method == 8: return zlib.decompress(data, -15)
    if method == 100: return zstd.decompress(data)
    raise ValueError(f'unsupported method {method}')

def main():
    p4k, out, globs = sys.argv[1], sys.argv[2], sys.argv[3:]
    with open(p4k, 'rb') as f:
        m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        for name, flags, method, csize, usize, off in entries(m):
            if not any(fnmatch.fnmatch(name.replace('\\', '/').lower(), g.lower()) for g in globs): continue
            if flags & 1:
                print(f'skip encrypted {name}'); continue
            data = read(m, method, csize, usize, off)
            dest = os.path.join(out, name.replace('\\', '/'))
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, 'wb') as g: g.write(data)
            print(f'{name} {len(data)} bytes')

main()
