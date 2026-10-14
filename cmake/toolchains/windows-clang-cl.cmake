# Windows clang-cl + Microsoft ABI. All installation paths are caller inputs.
# Compatible with the project's CMake 3.20 minimum; no VS generator arguments.
set(CMAKE_SYSTEM_NAME Windows)
if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
    message(FATAL_ERROR "windows-clang-cl.cmake requires the single-config Ninja generator")
endif()
foreach(_input CONCORD_LLVM_ROOT CONCORD_MSVC_ROOT CONCORD_WINDOWS_SDK_ROOT
        CONCORD_WINDOWS_SDK_VERSION CONCORD_NINJA_EXECUTABLE CONCORD_CPU_ARCH)
    if(NOT DEFINED ${_input} OR "${${_input}}" STREQUAL "")
        message(FATAL_ERROR "Pass -D${_input}=... explicitly; no machine path is embedded")
    endif()
endforeach()
foreach(_path CONCORD_LLVM_ROOT CONCORD_MSVC_ROOT CONCORD_WINDOWS_SDK_ROOT CONCORD_NINJA_EXECUTABLE)
    file(TO_CMAKE_PATH "${${_path}}" ${_path})
endforeach()
if(CONCORD_CPU_ARCH STREQUAL "x64")
    set(CMAKE_SYSTEM_PROCESSOR AMD64)
    set(_triple x86_64-pc-windows-msvc)
elseif(CONCORD_CPU_ARCH STREQUAL "x86")
    set(CMAKE_SYSTEM_PROCESSOR x86)
    set(_triple i686-pc-windows-msvc)
else()
    message(FATAL_ERROR "CONCORD_CPU_ARCH must be x64 or x86")
endif()
if(NOT CMAKE_BUILD_TYPE MATCHES "^(Release|Debug)$")
    message(FATAL_ERROR "Pass -DCMAKE_BUILD_TYPE=Release or Debug; use a separate directory per configuration")
endif()
set(CMAKE_CXX_COMPILER "${CONCORD_LLVM_ROOT}/bin/clang-cl.exe")
set(CMAKE_CXX_COMPILER_TARGET "${_triple}")
set(CMAKE_LINKER "${CONCORD_LLVM_ROOT}/bin/lld-link.exe" CACHE FILEPATH "LLVM COFF linker")
set(CMAKE_AR "${CONCORD_LLVM_ROOT}/bin/llvm-lib.exe" CACHE FILEPATH "LLVM COFF librarian")
# Host tool architecture is independent of the requested target ISA.
set(CMAKE_RC_COMPILER "${CONCORD_WINDOWS_SDK_ROOT}/bin/${CONCORD_WINDOWS_SDK_VERSION}/x64/rc.exe")
set(CMAKE_MT "${CONCORD_WINDOWS_SDK_ROOT}/bin/${CONCORD_WINDOWS_SDK_VERSION}/x64/mt.exe" CACHE FILEPATH "SDK manifest tool")
set(CMAKE_MAKE_PROGRAM "${CONCORD_NINJA_EXECUTABLE}" CACHE FILEPATH "Ninja executable")
foreach(_tool CMAKE_CXX_COMPILER CMAKE_LINKER CMAKE_AR CMAKE_RC_COMPILER CMAKE_MT CMAKE_MAKE_PROGRAM)
    if(NOT EXISTS "${${_tool}}")
        message(FATAL_ERROR "Required ${_tool} does not exist: ${${_tool}}")
    endif()
endforeach()
# Explicit system includes and library searches work from an ordinary terminal,
# including on a later cmake --build invocation without a developer environment.
set(_sdk_inc "${CONCORD_WINDOWS_SDK_ROOT}/Include/${CONCORD_WINDOWS_SDK_VERSION}")
set(_sdk_lib "${CONCORD_WINDOWS_SDK_ROOT}/Lib/${CONCORD_WINDOWS_SDK_VERSION}")
set(CMAKE_CXX_FLAGS_INIT "")
foreach(_inc "${CONCORD_MSVC_ROOT}/include" "${_sdk_inc}/ucrt" "${_sdk_inc}/shared"
        "${_sdk_inc}/um" "${_sdk_inc}/winrt" "${_sdk_inc}/cppwinrt")
    if(NOT IS_DIRECTORY "${_inc}")
        message(FATAL_ERROR "Required system include directory is missing: ${_inc}")
    endif()
    string(APPEND CMAKE_CXX_FLAGS_INIT " /imsvc\"${_inc}\"")
endforeach()
if(CONCORD_MSVC_COMPATIBILITY_VERSION)
    string(APPEND CMAKE_CXX_FLAGS_INIT " /clang:-fms-compatibility-version=${CONCORD_MSVC_COMPATIBILITY_VERSION}")
endif()
# Include ABI probes as well as product targets in the same FP/diagnostic contract.
string(APPEND CMAKE_CXX_FLAGS_INIT " /clang:-fno-fast-math /clang:-ffp-contract=off /clang:-Werror=unknown-argument /clang:-Werror=unused-command-line-argument")
set(_lib_flags "")
foreach(_lib "${CONCORD_MSVC_ROOT}/lib/${CONCORD_CPU_ARCH}"
        "${_sdk_lib}/ucrt/${CONCORD_CPU_ARCH}" "${_sdk_lib}/um/${CONCORD_CPU_ARCH}")
    if(NOT IS_DIRECTORY "${_lib}")
        message(FATAL_ERROR "Required target library directory is missing: ${_lib}")
    endif()
    string(APPEND _lib_flags " /libpath:\"${_lib}\"")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_lib_flags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_lib_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_lib_flags}")
set(CMAKE_RC_FLAGS_INIT "/I\"${_sdk_inc}/um\" /I\"${_sdk_inc}/shared\" /I\"${_sdk_inc}/ucrt\"")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_BUILD_TYPE
    CONCORD_LLVM_ROOT CONCORD_MSVC_ROOT CONCORD_WINDOWS_SDK_ROOT
    CONCORD_WINDOWS_SDK_VERSION CONCORD_NINJA_EXECUTABLE CONCORD_CPU_ARCH
    CONCORD_MSVC_COMPATIBILITY_VERSION)
# Refuse compiler/architecture/configuration switches within an existing cache.
set(_contract_identity "${CONCORD_CPU_ARCH}|${CMAKE_BUILD_TYPE}|${CONCORD_LLVM_ROOT}|${CONCORD_MSVC_ROOT}|${CONCORD_WINDOWS_SDK_ROOT}|${CONCORD_WINDOWS_SDK_VERSION}|${CONCORD_NINJA_EXECUTABLE}|${CONCORD_MSVC_COMPATIBILITY_VERSION}")
if(DEFINED CONCORD_CONFIGURED_TOOLCHAIN_IDENTITY AND
        NOT CONCORD_CONFIGURED_TOOLCHAIN_IDENTITY STREQUAL _contract_identity)
    message(FATAL_ERROR "This cache belongs to a different Clang toolchain/configuration; select a fresh build directory")
endif()
set(CONCORD_CONFIGURED_TOOLCHAIN_IDENTITY "${_contract_identity}" CACHE INTERNAL "Immutable toolchain/configuration identity")

# CMake's MSVC-like default adds /RTC1 before ABI probes. clang-cl does not
# implement it. Select the supported Debug flags before project()/try_compile.
set(CMAKE_CXX_FLAGS_DEBUG "/Zi /Ob0 /Od" CACHE STRING "clang-cl Debug flags; RTC unsupported")
# Cache overrides must not silently replace the explicitly selected LLVM/SDK tools.
foreach(_pair "CMAKE_LINKER|${CONCORD_LLVM_ROOT}/bin/lld-link.exe"
        "CMAKE_AR|${CONCORD_LLVM_ROOT}/bin/llvm-lib.exe"
        "CMAKE_MT|${CONCORD_WINDOWS_SDK_ROOT}/bin/${CONCORD_WINDOWS_SDK_VERSION}/x64/mt.exe"
        "CMAKE_MAKE_PROGRAM|${CONCORD_NINJA_EXECUTABLE}")
    string(REPLACE "|" ";" _fields "${_pair}")
    list(GET _fields 0 _name)
    list(GET _fields 1 _expected)
    file(TO_CMAKE_PATH "${${_name}}" _actual)
    if(NOT _actual STREQUAL _expected)
        message(FATAL_ERROR "${_name}=${_actual} conflicts with selected tool ${_expected}; use a new directory and consistent tool inputs")
    endif()
endforeach()
