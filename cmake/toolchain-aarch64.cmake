# cmake/toolchain-aarch64.cmake
#
# CMake cross-compilation toolchain for aarch64-suse-linux.
#
# Used by docker/Dockerfile.tumbleweed-aarch64 together with the
# cross-aarch64-gcc14 package from the standard Tumbleweed OSS repo.
#
# The cross-aarch64-gcc14 package ships with a pre-built glibc sysroot at
# /usr/aarch64-suse-linux/sys-root (crt1.o, libc, libgcc_s …).
# The Qt6/KF6 aarch64 headers + .so stubs are installed on top of this sysroot
# by the Dockerfile (extracted from the openSUSE ports mirror).
#
# Pass to cmake:
#   cmake -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchain-aarch64.cmake ...

# ── Target system description ─────────────────────────────────────────────────
set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ── Cross-compiler (installed by cross-aarch64-gcc14 package) ─────────────────
set(CMAKE_C_COMPILER   /usr/bin/aarch64-suse-linux-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/aarch64-suse-linux-g++)
set(CMAKE_AR           /usr/bin/aarch64-suse-linux-ar)
set(CMAKE_RANLIB       /usr/bin/aarch64-suse-linux-ranlib)
set(CMAKE_STRIP        /usr/bin/aarch64-suse-linux-strip)

# ── Sysroot ───────────────────────────────────────────────────────────────────
# The cross-aarch64-gcc14 package ships glibc crt objects + stubs at:
#   /usr/aarch64-suse-linux/sys-root/
# The Dockerfile installs Qt6/KF6 aarch64 headers + .so stubs into:
#   /usr/aarch64-suse-linux/sys-root/usr/
# So we use the gcc-provided sys-root as CMAKE_SYSROOT; all aarch64 content
# (both glibc and Qt6/KF6) lives under it.
set(CMAKE_SYSROOT /usr/aarch64-suse-linux/sys-root)

# Tell the linker where to look for aarch64 shared libraries / stubs.
# --allow-shlib-undefined: suppress "undefined reference in .so" errors that
# occur during cross-compilation when transitive dependencies of Qt6/KF6 libs
# (libdbus, libicu, libpng, libglib, libxcb …) are not present in the sysroot.
# These symbols are present at runtime on the target aarch64 system.
set(CMAKE_EXE_LINKER_FLAGS_INIT    "-Wl,-rpath-link,/usr/aarch64-suse-linux/sys-root/usr/lib64 -Wl,--allow-shlib-undefined")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,-rpath-link,/usr/aarch64-suse-linux/sys-root/usr/lib64 -Wl,--allow-shlib-undefined")

# ── Find-root-path settings ───────────────────────────────────────────────────
# ONLY look inside the sysroot for headers/libraries; use the host for programs
# (cmake, ninja, moc, rcc, uic …).
# ONLY for packages: force all find_package() calls to search only inside the
# sysroot.  Qt6 cross-compilation uses QT_HOST_PATH to override this for host
# tool packages (Qt6CoreTools etc.) by temporarily prepending QT_HOST_PATH to
# CMAKE_PREFIX_PATH and CMAKE_FIND_ROOT_PATH inside _qt_internal_find_tool_dependencies.
# Using BOTH here causes the host's newer Qt6 CMake configs (e.g. Qt6Qml, Qt6QmlIntegration)
# to be found alongside the sysroot ones, creating conflicting ALIAS targets.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ── Cross-compilation: try-compile workaround ────────────────────────────────
# By default CMake tries to link an executable for try_compile/check_xxx.
# During cross-compilation the linker cannot produce an executable for aarch64
# on an x86_64 host (no QEMU).  Use STATIC_LIBRARY instead so the link step
# is skipped and try_compile / check_cxx_source_compiles succeed.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Pre-set EGL / GLESv2 library paths with FORCE so Qt6's FindGLESv2.cmake
# find_library() calls don't search the host and come up empty.
# The .so symlinks live in the sysroot (from Mesa-libEGL-devel /
# Mesa-libGLESv2-devel / libglvnd-devel aarch64 RPMs).
set(EGL_INCLUDE_DIR    "${CMAKE_SYSROOT}/usr/include"             CACHE PATH     "EGL include dir"   FORCE)
set(EGL_LIBRARY        "${CMAKE_SYSROOT}/usr/lib64/libEGL.so"     CACHE FILEPATH "EGL library"        FORCE)
set(GLESv2_INCLUDE_DIR "${CMAKE_SYSROOT}/usr/include"             CACHE PATH     "GLESv2 include dir" FORCE)
set(GLESv2_LIBRARY     "${CMAKE_SYSROOT}/usr/lib64/libGLESv2.so"  CACHE FILEPATH "GLESv2 library"     FORCE)

# ── Qt6 cross-compilation hints ───────────────────────────────────────────────
# Qt6's CMake config files are inside the sysroot; we need to point Qt's
# host-tool wrappers (moc, rcc, uic) at the x86_64 versions.
#
# The host (x86_64) and the sysroot (aarch64) may have different Qt6 patch
# versions because Tumbleweed rolls the host packages independently of the
# aarch64 ports mirror.  When the versions differ, the host's Qt6CoreTools
# CMake config may use newer CMake helper functions (e.g.
# _qt_internal_should_include_targets introduced in Qt 6.11) that are not yet
# defined in the sysroot's copy of QtPublicCMakeHelpers.cmake.
#
# Work-around: include the host's QtPublicCMakeHelpers.cmake early (it is a
# pure collection of cmake function definitions with no dependencies) so that
# any helper function the host's CoreTools config needs is already defined when
# find_package(Qt6CoreTools) runs.
if(EXISTS "/usr/lib64/cmake/Qt6/QtPublicCMakeHelpers.cmake")
    include("/usr/lib64/cmake/Qt6/QtPublicCMakeHelpers.cmake")
endif()

if(NOT DEFINED QT_HOST_PATH)
    # qt6-base-common-devel installs /usr/libexec/qt6/moc (x86_64 binary).
    # Use /usr as the host Qt path so Qt6's cross-compile machinery picks up
    # the correct moc/rcc/uic executables.
    if(EXISTS "/usr/libexec/qt6/moc")
        set(QT_HOST_PATH "/usr")
    elseif(EXISTS "/usr/lib64/qt6/bin/moc")
        set(QT_HOST_PATH "/usr")
    else()
        set(QT_HOST_PATH "${CMAKE_SYSROOT}/usr")
    endif()
endif()
