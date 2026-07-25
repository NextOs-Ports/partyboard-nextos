# NextOS / Mali-450 (AArch64) cross toolchain for PartyBoard.
# Host cross sysroot is glibc 2.43, identical to the NextOS Elite target device,
# so the produced binaries run natively without a compat layer.
# Mirrors dusklight-nextos/cmake/nextos-aarch64-gcc10.toolchain.cmake but uses
# the host's aarch64-linux-gnu-gcc (GCC 16) since gcc-10 is not installed here;
# the glibc 2.43 match (not the gcc major) is what governs runtime compatibility.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP aarch64-linux-gnu-strip)

# Mali fbdev link libs (libGLESv2/libEGL) live in the staging sysroot; the rest
# (libc, etc.) come from the host aarch64 cross sysroot (glibc 2.43).
set(PARTYBOARD_NEXTOS_SYSROOT ${CMAKE_CURRENT_LIST_DIR}/../build/sysroot)
set(CMAKE_FIND_ROOT_PATH ${PARTYBOARD_NEXTOS_SYSROOT} /usr/aarch64-linux-gnu /usr/lib/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The Khronos GLES2/EGL headers are not in the aarch64 cross sysroot; ship them
# in the staging sysroot and force the path as a system include for every TU
# (imgui/RmlUi/Aurora GLES2 backend include <GLES2/gl2.h>, <EGL/egl.h>). Use
# CMAKE_<LANG>_FLAGS_INIT so the flag lands in every compile command regardless
# of which subproject owns the target (FetchContent deps included).
set(PARTYBOARD_NEXTOS_INCLUDE_FLAGS "-isystem ${PARTYBOARD_NEXTOS_SYSROOT}/include")
set(CMAKE_C_FLAGS_INIT "${PARTYBOARD_NEXTOS_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${PARTYBOARD_NEXTOS_INCLUDE_FLAGS}")
set(CMAKE_C_STANDARD_INCLUDE_DIRS ${PARTYBOARD_NEXTOS_SYSROOT}/include)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRS ${PARTYBOARD_NEXTOS_SYSROOT}/include)
