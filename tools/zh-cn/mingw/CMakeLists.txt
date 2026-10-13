# sc-offline built with clang in MinGW mode, for machines without MSVC (macOS). The top-level
# CMakeLists.txt is MSVC-only; this project builds the same release set from the same sources:
#
#   dinput8.dll                           src/, src/builtins/, ImGui, sco-core (external/sco-core)
#   data/plugins/creative/creative.dll    plugins/creative (SDK headers only)
#   data/plugins/discord/discord.dll      plugins/discord
#   data/plugins/npc/npc.dll              plugins/npc (upstream builds it from the published SDK zip;
#   data/plugins/inspector/inspector.dll  plugins/inspector  here from the same headers in sco-core)
#
# The source lists of dinput8 and imgui are read from the top-level CMakeLists.txt, so a file
# upstream adds there is built here too. The optional bridges stay off, as in a release.
# tools/zh-cn/build-macos.sh configures, builds and checks it.
cmake_minimum_required(VERSION 3.20)
project(sc-offline-mingw LANGUAGES C CXX RC)

if(NOT MINGW)
  message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=tools/zh-cn/mingw/toolchain.cmake")
endif()

get_filename_component(SCO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${SCO_ROOT}/external/sco-core/CMakeLists.txt")
  message(FATAL_ERROR "external/sco-core is empty: run `git submodule update --init`")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# MSVC's Release is /O2.
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
# __try/__except (SEH) and the other Microsoft extensions, for sco-core's Windows sources too.
# -femulated-tls: mingw-w64's libstdc++ (GCC) keeps its thread_locals in emulated TLS
# (std::call_once's __once_callable among them); clang's default native TLS wouldn't link to them.
add_compile_options(-fms-extensions -femulated-tls)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# The sources of add_library(<target> ...) in the top-level CMakeLists.txt.
function(sco_upstream_sources out target)
  file(READ "${SCO_ROOT}/CMakeLists.txt" text)
  string(REGEX REPLACE "#[^\n]*" "" text "${text}")
  string(FIND "${text}" "add_library(${target} " at)
  if(at EQUAL -1)
    message(FATAL_ERROR "add_library(${target} ...) not found in ${SCO_ROOT}/CMakeLists.txt")
  endif()
  string(SUBSTRING "${text}" ${at} -1 text)
  string(FIND "${text}" ")" end)
  string(SUBSTRING "${text}" 0 ${end} text)
  string(REGEX MATCHALL "[A-Za-z0-9_./-]+\\.(cpp|c|rc|manifest)" files "${text}")
  if(NOT files)
    message(FATAL_ERROR "add_library(${target} ...) lists no sources")
  endif()
  set(abs "")
  foreach(f IN LISTS files)
    list(APPEND abs "${SCO_ROOT}/${f}")
  endforeach()
  set(${out} "${abs}" PARENT_SCOPE)
endfunction()

# --- sco-core ----------------------------------------------------------------------------------
set(SCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SCO_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory("${SCO_ROOT}/external/sco-core" sco-core EXCLUDE_FROM_ALL)

# What every sc-offline target had in the MSVC build: Unicode, _CONSOLE; the DLL adds _WINDLL.
set(SCO_DEFINES UNICODE _UNICODE _CONSOLE)
# The static runtime (libstdc++, libgcc, winpthread): the DLLs import only Windows' own DLLs.
set(SCO_LINK_OPTIONS -static -pthread -s -Wl,--no-insert-timestamp)

# --- ImGui and dinput8.dll ---------------------------------------------------------------------
sco_upstream_sources(IMGUI_SOURCES imgui)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_compile_definitions(imgui PRIVATE ${SCO_DEFINES} _WINDLL)
target_compile_options(imgui PRIVATE -w)

sco_upstream_sources(DINPUT8_SOURCES dinput8)
# The manifest is a source of the MSVC build (linker /MANIFESTINPUT); here it goes in as resource 2
# (ISOLATIONAWARE_MANIFEST_RESOURCE_ID), where the MSVC build puts a DLL's manifest.
set(_manifest "")
foreach(f IN LISTS DINPUT8_SOURCES)
  if(f MATCHES "\\.manifest$")
    set(_manifest "${f}")
  endif()
endforeach()
list(FILTER DINPUT8_SOURCES EXCLUDE REGEX "\\.manifest$")
if(_manifest)
  set(_manifest_rc "${CMAKE_BINARY_DIR}/dinput8_manifest.rc")
  file(WRITE "${_manifest_rc}" "2 24 \"${_manifest}\"\n")
  list(APPEND DINPUT8_SOURCES "${_manifest_rc}")
endif()

add_library(dinput8 SHARED ${DINPUT8_SOURCES})
set_target_properties(dinput8 PROPERTIES OUTPUT_NAME dinput8 PREFIX "" DEFINE_SYMBOL "")
target_compile_definitions(dinput8 PRIVATE ${SCO_DEFINES} _WINDLL)
# MSVC compiles the SSE4.2 CRC32 intrinsic hooks.cpp uses without a flag.
target_compile_options(dinput8 PRIVATE -mcrc32)
target_include_directories(dinput8 PRIVATE "${SCO_ROOT}/src")
target_link_options(dinput8 PRIVATE ${SCO_LINK_OPTIONS} -Wl,--subsystem,windows)
target_link_libraries(dinput8 PRIVATE imgui sco_app sco_lua sco_lua_game sco_hook sco_engine sco_pak
  d3d11 d3dcompiler dwmapi imm32 shlwapi advapi32 windowscodecs ole32 gdi32 user32 shell32 uuid)

# --- plugins: data/plugins/<id>/<id>.dll next to its plugin.ini ---------------------------------
function(sco_plugin id source ini)
  set(dir "${CMAKE_BINARY_DIR}/data/plugins/${id}")
  add_library(sc-offline-${id} MODULE ${source} ${ARGN})
  target_include_directories(sc-offline-${id} PRIVATE "${SCO_ROOT}/external/sco-core/include")
  target_compile_definitions(sc-offline-${id} PRIVATE ${SCO_DEFINES})
  target_link_options(sc-offline-${id} PRIVATE ${SCO_LINK_OPTIONS})
  set_target_properties(sc-offline-${id} PROPERTIES OUTPUT_NAME ${id} PREFIX "" SUFFIX ".dll"
    LIBRARY_OUTPUT_DIRECTORY "${dir}" RUNTIME_OUTPUT_DIRECTORY "${dir}")
  configure_file("${ini}" "${dir}/plugin.ini" COPYONLY)
endfunction()

sco_plugin(creative "${SCO_ROOT}/plugins/creative/creative.cpp" "${SCO_ROOT}/data/plugins/creative/plugin.ini")
sco_plugin(discord "${SCO_ROOT}/plugins/discord/discord.cpp" "${SCO_ROOT}/data/plugins/discord/plugin.ini")
# Upstream's release ships creative off (an empty "disabled" file next to its plugin.ini); this
# fork ships it on, so an earlier build's marker goes too.
file(REMOVE "${CMAKE_BINARY_DIR}/data/plugins/creative/disabled")
foreach(id npc inspector)
  sco_plugin(${id} "${SCO_ROOT}/plugins/${id}/external.cpp" "${SCO_ROOT}/plugins/${id}/plugin.ini"
    "${SCO_ROOT}/plugins/${id}/${id}.cpp")
endforeach()
