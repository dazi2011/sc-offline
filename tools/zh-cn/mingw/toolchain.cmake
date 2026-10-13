# CMake toolchain: Windows x64 binaries from macOS with clang in MinGW mode, the mingw-w64 sysroot
# and the LLD that ships with the Rust toolchain (no MSVC, no Windows SDK).
#
#   cmake -S tools/zh-cn/mingw -B build/zh-cn -DCMAKE_TOOLCHAIN_FILE=tools/zh-cn/mingw/toolchain.cmake
#
# Overrides (environment): SC_OFFLINE_LLVM (clang's bin directory), SC_OFFLINE_MINGW (the sysroot),
# SC_OFFLINE_RUST_TOOLCHAIN (the rustup toolchain whose rust-lld links).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(_sco_llvm "$ENV{SC_OFFLINE_LLVM}")
if(NOT _sco_llvm)
  set(_sco_llvm /opt/homebrew/opt/llvm/bin)
endif()
set(_sco_mingw "$ENV{SC_OFFLINE_MINGW}")
if(NOT _sco_mingw)
  set(_sco_mingw /opt/homebrew/opt/mingw-w64/toolchain-x86_64)
endif()
set(_sco_rust "$ENV{SC_OFFLINE_RUST_TOOLCHAIN}")
if(NOT _sco_rust)
  set(_sco_rust "$ENV{HOME}/.rustup/toolchains/stable-aarch64-apple-darwin")
endif()

set(CMAKE_C_COMPILER "${_sco_llvm}/clang")
set(CMAKE_CXX_COMPILER "${_sco_llvm}/clang++")
set(CMAKE_RC_COMPILER "${_sco_llvm}/llvm-windres")
set(CMAKE_AR "${_sco_llvm}/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "${_sco_llvm}/llvm-ranlib" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_SYSROOT "${_sco_mingw}")
set(CMAKE_RC_FLAGS_INIT "--target=x86_64-w64-mingw32 -I${_sco_mingw}/x86_64-w64-mingw32/include")
set(CMAKE_FIND_ROOT_PATH "${_sco_mingw}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# rust-lld loads libLLVM.dylib from the toolchain's lib directory through DYLD_FALLBACK_LIBRARY_PATH.
# make runs each link through /bin/sh, and macOS drops DYLD_* variables on the way into a system
# binary, so the variable is set by a wrapper right before the linker starts.
set(_sco_lld "${_sco_rust}/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld")
if(NOT EXISTS "${_sco_lld}")
  message(FATAL_ERROR "Rust LLD not found: ${_sco_lld} (set SC_OFFLINE_RUST_TOOLCHAIN)")
endif()
set(_sco_ld_wrapper "${CMAKE_BINARY_DIR}/ld.lld")
file(WRITE "${_sco_ld_wrapper}"
  "#!/bin/sh\nDYLD_FALLBACK_LIBRARY_PATH='${_sco_rust}/lib' exec '${_sco_lld}' \"$@\"\n")
file(CHMOD "${_sco_ld_wrapper}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
                                           WORLD_READ WORLD_EXECUTE)
foreach(_kind EXE SHARED MODULE)
  set(CMAKE_${_kind}_LINKER_FLAGS_INIT "-fuse-ld=lld --ld-path=${_sco_ld_wrapper}")
endforeach()
