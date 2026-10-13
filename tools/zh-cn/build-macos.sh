#!/bin/bash
# Builds sc-offline from this working tree on macOS, without MSVC: Homebrew clang in MinGW mode,
# the Homebrew mingw-w64 sysroot and the LLD shipped with the Rust toolchain, through the CMake
# project in tools/zh-cn/mingw (sco-core from external/sco-core, built from source too).
#
# Output (SC_OFFLINE_OUT, default build/zh-cn):
#   dinput8.dll                                  the mod, for the game's Bin64
#   data/plugins/<id>/<id>.dll + plugin.ini      creative, discord, npc, inspector (all on): copy into
#                                                the mod's data\plugins
#   BUILD-INFO.txt                               commits, toolchain, SHA-256 of every DLL
#
# Overrides: SC_OFFLINE_LLVM (or SC_OFFLINE_CXX), SC_OFFLINE_MINGW, SC_OFFLINE_RUST_TOOLCHAIN,
# SC_OFFLINE_CMAKE, SC_OFFLINE_OUT, JOBS.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [[ -n ${SC_OFFLINE_CXX:-} && -z ${SC_OFFLINE_LLVM:-} ]]; then export SC_OFFLINE_LLVM=$(dirname "$SC_OFFLINE_CXX"); fi
LLVM_BIN=${SC_OFFLINE_LLVM:-/opt/homebrew/opt/llvm/bin}
export SC_OFFLINE_LLVM=$LLVM_BIN
MINGW=${SC_OFFLINE_MINGW:-/opt/homebrew/opt/mingw-w64/toolchain-x86_64}
RUST_TOOLCHAIN=${SC_OFFLINE_RUST_TOOLCHAIN:-"$HOME/.rustup/toolchains/stable-aarch64-apple-darwin"}
LLD="$RUST_TOOLCHAIN/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld"
CMAKE=${SC_OFFLINE_CMAKE:-$(command -v cmake || echo /Applications/CMake.app/Contents/bin/cmake)}
OUT=${SC_OFFLINE_OUT:-"$ROOT/build/zh-cn"}
JOBS=${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}

[[ -x $LLVM_BIN/clang++ ]] || { echo "clang++ not found in $LLVM_BIN" >&2; exit 1; }
[[ -d $MINGW/x86_64-w64-mingw32/include ]] || { echo "mingw-w64 sysroot not found: $MINGW" >&2; exit 1; }
[[ -x $LLD ]] || { echo "Rust LLD not found: $LLD" >&2; exit 1; }
[[ -x $CMAKE ]] || { echo "cmake not found (set SC_OFFLINE_CMAKE)" >&2; exit 1; }
[[ -f $ROOT/external/sco-core/CMakeLists.txt ]] \
    || { echo "external/sco-core is empty: run git submodule update --init" >&2; exit 1; }
export SC_OFFLINE_MINGW=$MINGW SC_OFFLINE_RUST_TOOLCHAIN=$RUST_TOOLCHAIN

"$CMAKE" -S "$ROOT/tools/zh-cn/mingw" -B "$OUT" -G "Unix Makefiles" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/zh-cn/mingw/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$OUT" -j "$JOBS"
cp "$OUT/bin/dinput8.dll" "$OUT/dinput8.dll"

# --- checks -------------------------------------------------------------------------------------
fail() { echo "check failed: $*" >&2; exit 3; }
dlls=("$OUT/dinput8.dll")
for id in creative discord npc inspector; do
    dlls+=("$OUT/data/plugins/$id/$id.dll")
    [[ -f $OUT/data/plugins/$id/plugin.ini ]] || fail "data/plugins/$id/plugin.ini missing"
done
# This fork ships creative on: no "disabled" marker.
[[ ! -e $OUT/data/plugins/creative/disabled ]] || fail "data/plugins/creative/disabled present"
for dll in "${dlls[@]}"; do
    name=${dll#"$OUT/"}
    file "$dll" | grep -q 'PE32+ executable.*(DLL).*x86-64' || fail "$name is not a PE32+ x86-64 DLL"
    imports=$("$LLVM_BIN/llvm-objdump" -p "$dll" | awk '/DLL Name:/ {print $3}' | LC_ALL=C sort -u)
    if grep -Ei '^(libstdc\+\+|libgcc|libwinpthread|libc\+\+|libunwind)' <<<"$imports"; then
        fail "$name imports a MinGW runtime DLL"
    fi
    printf '%s imports: %s\n' "$name" "$(tr '\n' ' ' <<<"$imports")"
done
exports=$("$LLVM_BIN/llvm-readobj" --coff-exports "$OUT/dinput8.dll")
grep -Fq 'Name: DirectInput8Create' <<<"$exports" || fail "DirectInput8Create export missing"
grep -Fq 'ForwardedTo: C:\Windows\System32\dinput8.DirectInput8Create' <<<"$exports" \
    || fail "DirectInput8Create is not forwarded to System32"
for id in creative discord npc inspector; do
    plugin_exports=$("$LLVM_BIN/llvm-readobj" --coff-exports "$OUT/data/plugins/$id/$id.dll")
    for fn in sco_plugin_query sco_plugin_load sco_plugin_unload; do
        grep -Fq "Name: $fn" <<<"$plugin_exports" || fail "$id.dll does not export $fn"
    done
done
"$LLVM_BIN/llvm-readobj" --coff-resources "$OUT/dinput8.dll" | grep -q 'Type: MANIFEST (ID 24)' \
    || fail "dinput8.dll has no embedded manifest"
LC_ALL=C grep -aFq SegmentHeap "$OUT/dinput8.dll" || fail "dinput8.dll manifest lacks heapType SegmentHeap"
"$LLVM_BIN/llvm-readobj" --coff-resources "$OUT/dinput8.dll" | grep -q 'Type: VERSIONINFO' \
    || fail "dinput8.dll has no version resource"
# What upstream's CI checks of the release DLL: the optional bridges are off and NPC / inspector
# live in their own plugins, not in dinput8.dll.
for s in titanlink.pilot voxel_bridge.toggle inspector.read npc.spawn npc.observe; do
    ! LC_ALL=C grep -aFq "$s" "$OUT/dinput8.dll" || fail "dinput8.dll contains $s"
done

{
    printf 'commit=%s%s\n' "$(git -C "$ROOT" rev-parse HEAD)" \
        "$([[ -z $(git -C "$ROOT" status --porcelain -- src plugins external CMakeLists.txt tools/zh-cn/mingw) ]] || echo ' (modified)')"
    printf 'sco-core=%s\n' "$(git -C "$ROOT/external/sco-core" rev-parse HEAD)"
    printf 'compiler=%s\n' "$("$LLVM_BIN/clang++" --version | head -1)"
    printf 'linker=%s\n' "$(DYLD_FALLBACK_LIBRARY_PATH="$RUST_TOOLCHAIN/lib" "$LLD" --version | head -1)"
    for dll in "${dlls[@]}"; do
        printf 'sha256 %s %s\n' "$(shasum -a 256 "$dll" | awk '{print $1}')" "${dll#"$OUT/"}"
    done
} >"$OUT/BUILD-INFO.txt"
cat "$OUT/BUILD-INFO.txt"
echo "checks passed"
