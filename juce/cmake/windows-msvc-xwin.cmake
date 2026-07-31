set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_SYSTEM_VERSION 10.0)

set(CMAKE_C_COMPILER /usr/bin/clang-cl-19)
set(CMAKE_CXX_COMPILER /usr/bin/clang-cl-19)
set(CMAKE_LINKER /usr/bin/lld-link)
set(CMAKE_AR /usr/bin/llvm-lib)
set(CMAKE_RC_COMPILER /usr/bin/llvm-rc)
set(CMAKE_MT /usr/bin/llvm-mt)

if(DEFINED ENV{HACHI_XWIN_ROOT} AND NOT "$ENV{HACHI_XWIN_ROOT}" STREQUAL "")
    set(XWIN_ROOT "$ENV{HACHI_XWIN_ROOT}")
else()
    set(XWIN_ROOT "/opt/hachishifter/xwin-msvc")
endif()
set(MSVC_VERSION "14.44.17.14")
set(WINSDK_VERSION "10.0.26100")
set(TARGET_TRIPLE "x86_64-pc-windows-msvc")

set(MSVC_INCLUDE "${XWIN_ROOT}/VC/Tools/MSVC/${MSVC_VERSION}/include")
set(MSVC_LIB "${XWIN_ROOT}/VC/Tools/MSVC/${MSVC_VERSION}/lib/x64")
# The bootstrap keeps a no-space alias beside the original "Windows Kits"
# directory so Ninja never splits linker arguments at the SDK path.
set(WINSDK_ROOT "${XWIN_ROOT}/WindowsKits/10")
set(WINSDK_INCLUDE "${WINSDK_ROOT}/Include/${WINSDK_VERSION}")
set(WINSDK_LIB "${WINSDK_ROOT}/Lib/${WINSDK_VERSION}")

foreach(required_path
        "${MSVC_INCLUDE}"
        "${MSVC_LIB}"
        "${WINSDK_INCLUDE}/ucrt"
        "${WINSDK_LIB}/um/x64")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "MSVC/xwin sysroot path is missing: ${required_path}")
    endif()
endforeach()

set(COMPILE_FLAGS
    "--target=${TARGET_TRIPLE}"
    "/clang:-fuse-ld=lld"
    "/vctoolsversion ${MSVC_VERSION}"
    "/winsdkversion ${WINSDK_VERSION}"
    "/winsysroot ${XWIN_ROOT}"
    "/D_CRT_SECURE_NO_WARNINGS"
    "/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS")
string(REPLACE ";" " " COMPILE_FLAGS "${COMPILE_FLAGS}")
set(CMAKE_C_FLAGS_INIT "${COMPILE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${COMPILE_FLAGS}")

set(CMAKE_RC_FLAGS_INIT
    "/I${MSVC_INCLUDE} /I${WINSDK_INCLUDE}/ucrt /I${WINSDK_INCLUDE}/shared /I${WINSDK_INCLUDE}/um /I${WINSDK_INCLUDE}/winrt")

set(LINK_FLAGS
    "/manifest:no"
    "-libpath:${MSVC_LIB}"
    "-libpath:${WINSDK_LIB}/ucrt/x64"
    "-libpath:${WINSDK_LIB}/um/x64")
string(REPLACE ";" " " LINK_FLAGS "${LINK_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${LINK_FLAGS}")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING "" FORCE)
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_DEBUG "/INCREMENTAL:NO" CACHE STRING "" FORCE)
    set(CMAKE_${kind}_LINKER_FLAGS_RELEASE "/INCREMENTAL:NO" CACHE STRING "" FORCE)
    set(CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO "/INCREMENTAL:NO" CACHE STRING "" FORCE)
    set(CMAKE_${kind}_LINKER_FLAGS_MINSIZEREL "/INCREMENTAL:NO" CACHE STRING "" FORCE)
endforeach()

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    XWIN_ROOT MSVC_VERSION WINSDK_VERSION TARGET_TRIPLE)
