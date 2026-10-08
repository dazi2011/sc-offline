#!/bin/bash
# Builds dinput8.dll from this working tree on macOS, without MSVC: Homebrew clang in MinGW mode,
# the Homebrew mingw-w64 sysroot, and the LLD shipped with the Rust toolchain (it accepts the
# MSVC-style /export forwarder in .drectve that GNU ld rejects). Output: build/zh-cn/dinput8.dll.
#
# Overrides: SC_OFFLINE_CXX, SC_OFFLINE_MINGW, SC_OFFLINE_RUST_TOOLCHAIN, SC_OFFLINE_OUT, JOBS.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
CXX=${SC_OFFLINE_CXX:-/opt/homebrew/opt/llvm/bin/clang++}
LLVM_BIN=$(dirname "$CXX")
MINGW=${SC_OFFLINE_MINGW:-/opt/homebrew/opt/mingw-w64/toolchain-x86_64}
RUST_TOOLCHAIN=${SC_OFFLINE_RUST_TOOLCHAIN:-"$HOME/.rustup/toolchains/stable-aarch64-apple-darwin"}
LLD="$RUST_TOOLCHAIN/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
OUT=${SC_OFFLINE_OUT:-"$ROOT/build/zh-cn"}
OBJ="$OUT/obj"
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}

[[ -x $CXX ]] || { echo "clang++ not found: $CXX" >&2; exit 1; }
[[ -d $MINGW/x86_64-w64-mingw32/include ]] || { echo "mingw-w64 sysroot not found: $MINGW" >&2; exit 1; }
[[ -x $LLD ]] || { echo "Rust LLD not found: $LLD" >&2; exit 1; }
# rust-lld loads libLLVM.dylib from the toolchain's lib directory.
export DYLD_FALLBACK_LIBRARY_PATH="$RUST_TOOLCHAIN/lib"

# The source list is the vcxproj's, so a file added upstream is picked up here too.
sources=()
while IFS= read -r src; do sources+=("${src//\\//}"); done < <(
    sed -n 's/.*<ClCompile Include="\([^"]*\.cpp\)".*/\1/p' "$ROOT/src/sc-offline-dll.vcxproj")
(( ${#sources[@]} )) || { echo "no ClCompile entries in src/sc-offline-dll.vcxproj" >&2; exit 1; }

# Mirrors Release|x64: C++20, NDEBUG, _CONSOLE, Unicode, static runtime.
# MSVC allows the SSE4.2 CRC32 intrinsic hooks.cpp uses without a flag; clang needs -mcrc32.
cxxflags=(
    --target=x86_64-w64-mingw32 --sysroot="$MINGW" -std=c++20 -O2 -fms-extensions -mcrc32
    -DNDEBUG -D_CONSOLE -DUNICODE -D_UNICODE -I"$ROOT/src" -I"$ROOT/src/third_party/imgui"
)

# Worker mode for the parallel compile below: build-macos.sh --compile <source> <object>.
if [[ ${1:-} == --compile ]]; then
    exec "$CXX" "${cxxflags[@]}" -c "$ROOT/src/$2" -o "$3"
fi

rm -rf "$OBJ"
mkdir -p "$OBJ"
objects=()
for src in "${sources[@]}"; do objects+=("$OBJ/${src//\//_}.o"); done
for i in "${!sources[@]}"; do printf '%s\0%s\0' "${sources[$i]}" "${objects[$i]}"; done \
    | xargs -0 -n 2 -P "$JOBS" "$0" --compile

"$CXX" --target=x86_64-w64-mingw32 --sysroot="$MINGW" -fuse-ld=lld --ld-path="$LLD" \
    -shared -static -pthread -s -Wl,--no-insert-timestamp -o "$OUT/dinput8.dll" "${objects[@]}" \
    -lshlwapi -ladvapi32 -ld3d11 -ld3dcompiler -lwindowscodecs -lole32 -lgdi32 -ldwmapi \
    -luser32 -lshell32 -limm32 -luuid

# Checks: a PE32+ x86-64 DLL, the DirectInput8Create forwarder, no MinGW runtime DLL imports.
file "$OUT/dinput8.dll" | grep -q 'PE32+ executable.*(DLL).*x86-64' || { echo "not a PE32+ x86-64 DLL" >&2; exit 3; }
"$LLVM_BIN/llvm-readobj" --coff-exports "$OUT/dinput8.dll" >"$OUT/exports.txt"
grep -Fq 'Name: DirectInput8Create' "$OUT/exports.txt" || { echo "DirectInput8Create export missing" >&2; exit 3; }
grep -Fq 'ForwardedTo: C:\Windows\System32\dinput8.DirectInput8Create' "$OUT/exports.txt" \
    || { echo "DirectInput8Create is not forwarded" >&2; exit 3; }
"$LLVM_BIN/llvm-objdump" -p "$OUT/dinput8.dll" | awk '/DLL Name:/ {print $3}' | LC_ALL=C sort -u >"$OUT/imports.txt"
if grep -Ei '^(libstdc\+\+|libgcc|libwinpthread)' "$OUT/imports.txt"; then
    echo "MinGW runtime DLL dependency found" >&2
    exit 3
fi

{
    printf 'commit=%s%s\n' "$(git -C "$ROOT" rev-parse HEAD)" \
        "$([[ -z $(git -C "$ROOT" status --porcelain -- src) ]] || echo ' (src modified)')"
    printf 'compiler=%s\n' "$("$CXX" --version | head -1)"
    printf 'linker=%s\n' "$("$LLD" --version | head -1)"
    printf 'sha256=%s\n' "$(shasum -a 256 "$OUT/dinput8.dll" | awk '{print $1}')"
} >"$OUT/BUILD-INFO.txt"
cat "$OUT/BUILD-INFO.txt"
printf 'imports: %s\n' "$(tr '\n' ' ' <"$OUT/imports.txt")"
