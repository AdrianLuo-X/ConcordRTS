# Project-local SDL C build uses the same explicit LLVM/SDK/CRT components.
# The engine's original C++ toolchain and its immutable cache contract stay intact.
include("${CMAKE_CURRENT_LIST_DIR}/../toolchains/windows-clang-cl.cmake")
set(CMAKE_C_COMPILER "${CMAKE_CXX_COMPILER}")
set(CMAKE_C_COMPILER_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
set(CMAKE_C_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT}")
set(CMAKE_C_FLAGS_DEBUG "/Zi /Ob0 /Od" CACHE STRING "clang-cl C Debug")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "SDL C ABI runtime")
