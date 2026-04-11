cmake_minimum_required(VERSION 3.20)

# ── KDE Frameworks version selection ─────────────────────────────────────────
# Pass -DUSE_KF=5   → Qt5 + KF5   (e.g. openSUSE Leap 15.6, requires KDE:Frameworks5 OBS repo)
# Pass -DUSE_KF=6   → Qt6 + KF6   (e.g. openSUSE Leap 16.0, requires KDE:Extra OBS repo)
# Pass -DUSE_KF=0   → Qt5, NO KF  (e.g. openSUSE Leap 15.6, standard OSS repo only)
# Pass -DUSE_KF=qt6 → Qt6, NO KF  (e.g. Ubuntu 24.04 LTS)
# Default: auto-detect (KF6 > KF5 > Qt5-only)
set(USE_KF "" CACHE STRING "KDE Frameworks major version to use (5, 6, 0=Qt5 no KF, qt6=Qt6 no KF, empty=auto-detect)")

project(fritzhome
    VERSION 1.0.0
    DESCRIPTION "Fritz!Box Smart Home Manager"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Determine Qt/KF version ───────────────────────────────────────────────────
if(USE_KF STREQUAL "5")
    set(_KF_MAJOR 5)
    set(_QT_MAJOR 5)
    set(_USE_KF ON)
elseif(USE_KF STREQUAL "6")
    set(_KF_MAJOR 6)
    set(_QT_MAJOR 6)
    set(_USE_KF ON)
elseif(USE_KF STREQUAL "0")
    # Explicit Qt5-only, no KF
    set(_KF_MAJOR 0)
    set(_QT_MAJOR 5)
    set(_USE_KF OFF)
elseif(USE_KF STREQUAL "qt6")
    # Qt6, no KDE Frameworks (e.g. Ubuntu 24.04 LTS)
    set(_KF_MAJOR 0)
    set(_QT_MAJOR 6)
    set(_USE_KF OFF)
elseif(USE_KF STREQUAL "")
    # Auto-detect: try KF6, then KF5, then Qt5-only
    find_package(KF6 QUIET COMPONENTS CoreAddons)
    if(KF6_FOUND)
        set(_KF_MAJOR 6)
        set(_QT_MAJOR 6)
        set(_USE_KF ON)
        message(STATUS "Auto-detected KDE Frameworks 6")
    else()
        find_package(ECM QUIET NO_MODULE)
        if(ECM_FOUND)
            find_package(KF5 QUIET COMPONENTS CoreAddons)
        endif()
        if(KF5_FOUND)
            set(_KF_MAJOR 5)
            set(_QT_MAJOR 5)
            set(_USE_KF ON)
            message(STATUS "Auto-detected KDE Frameworks 5")
        else()
            set(_KF_MAJOR 0)
            set(_QT_MAJOR 5)
            set(_USE_KF OFF)
            message(STATUS "No KDE Frameworks found — building with Qt5 only")
        endif()
    endif()
else()
    message(FATAL_ERROR "USE_KF must be '5', '6', '0', 'qt6', or empty (auto-detect). Got: '${USE_KF}'")
endif()

message(STATUS "Building with Qt${_QT_MAJOR}${_USE_KF}:+ + KF${_KF_MAJOR}}")
if(_USE_KF)
    message(STATUS "  KDE Frameworks ${_KF_MAJOR} enabled (HAVE_KF=1)")
else()
    message(STATUS "  KDE Frameworks disabled (HAVE_KF=0) — pure Qt5 build")
endif()

# ── ECM (only when KF is used) ────────────────────────────────────────────────
if(_USE_KF)
    find_package(ECM REQUIRED NO_MODULE)
    set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
    include(KDEInstallDirs)
    include(KDECMakeSettings)
    include(KDECompilerSettings NO_POLICY_SCOPE)
    include(ECMPoQmTools)
endif()

include(FeatureSummary)

# ── Find Qt ───────────────────────────────────────────────────────────────────
find_package(Qt${_QT_MAJOR} REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Xml
    Charts
    DBus
)

# ── Find KDE Frameworks (only when enabled) ───────────────────────────────────
if(_USE_KF)
    find_package(KF${_KF_MAJOR} REQUIRED COMPONENTS
        CoreAddons
        WidgetsAddons
        XmlGui
        I18n
        Notifications
        Wallet
    )
endif()

# ── Find libsecret (only when KF is NOT used) ─────────────────────────────────
# libsecret provides the freedesktop Secret Service API (GNOME keyring / KWallet
# in Secret Service mode).  It is used as a secure password backend when KDE
# Frameworks (and thus KWallet) are unavailable.
if(NOT _USE_KF)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
    endif()
    if(LIBSECRET_FOUND)
        message(STATUS "libsecret found — passwords stored in Secret Service keyring")
    else()
        message(STATUS "libsecret NOT found — falling back to QSettings (plaintext)")
    endif()
endif()

# ── Source files ──────────────────────────────────────────────────────────────
set(fritzhome_SRCS
    src/main.cpp
    src/mainwindow.cpp
    src/loginwindow.cpp
    src/fritzapi.cpp
    src/devicemodel.cpp
    src/devicewidget.cpp
    src/switchwidget.cpp
    src/thermostatwidget.cpp
    src/energywidget.cpp
    src/dimmerwidget.cpp
    src/blindwidget.cpp
    src/colorwidget.cpp
    src/humiditysensorwidget.cpp
    src/alarmwidget.cpp
    src/chartwidget.cpp
    src/chartutils.cpp
    src/temperaturechartbuilder.cpp
    src/powerchartbuilder.cpp
    src/energygaugebuilder.cpp
    src/energyhistorybuilder.cpp
    src/secretstore.cpp
    src/resources.qrc
)

set(fritzhome_HDRS
    src/mainwindow.h
    src/loginwindow.h
    src/fritzapi.h
    src/devicemodel.h
    src/devicewidget.h
    src/switchwidget.h
    src/thermostatwidget.h
    src/energywidget.h
    src/dimmerwidget.h
    src/blindwidget.h
    src/colorwidget.h
    src/humiditysensorwidget.h
    src/alarmwidget.h
    src/chartwidget.h
    src/chartutils.h
    src/temperaturechartbuilder.h
    src/powerchartbuilder.h
    src/energygaugebuilder.h
    src/energyhistorybuilder.h
    src/fritzdevice.h
    src/secretstore.h
)

add_executable(fritzhome
    ${fritzhome_SRCS}
    ${fritzhome_HDRS}
)

# ── Silence GCC 15 false-positive in chartwidget.cpp ─────────────────────────
# GCC 15 pessimistically infers a possibly-negative range for barsToShow even
# after qMax(1, ...), and fires -Wstringop-overflow deep inside QVector's
# template machinery.  The flag is per-file so it does not hide real issues
# elsewhere.  Clang does not emit this warning, so the guard is GCC-only.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/chartwidget.cpp src/energyhistorybuilder.cpp PROPERTIES
        COMPILE_OPTIONS "-Wno-stringop-overflow"
    )
endif()

# ── Compile-time KF feature flag ──────────────────────────────────────────────
if(_USE_KF)
    target_compile_definitions(fritzhome PRIVATE HAVE_KF=1)
else()
    target_compile_definitions(fritzhome PRIVATE HAVE_KF=0)
endif()

# ── Compile-time libsecret feature flag ───────────────────────────────────────
if(LIBSECRET_FOUND)
    target_compile_definitions(fritzhome PRIVATE HAVE_LIBSECRET=1)
else()
    target_compile_definitions(fritzhome PRIVATE HAVE_LIBSECRET=0)
endif()

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(fritzhome
    Qt${_QT_MAJOR}::Core
    Qt${_QT_MAJOR}::Widgets
    Qt${_QT_MAJOR}::Network
    Qt${_QT_MAJOR}::Xml
    Qt${_QT_MAJOR}::Charts
    Qt${_QT_MAJOR}::DBus
)

if(LIBSECRET_FOUND)
    target_link_libraries(fritzhome PkgConfig::LIBSECRET)
endif()

if(_USE_KF)
    target_link_libraries(fritzhome
        KF${_KF_MAJOR}::CoreAddons
        KF${_KF_MAJOR}::WidgetsAddons
        KF${_KF_MAJOR}::XmlGui
        KF${_KF_MAJOR}::I18n
        KF${_KF_MAJOR}::Notifications
        KF${_KF_MAJOR}::Wallet
    )
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)

if(_USE_KF)
    install(TARGETS fritzhome ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
    # KXmlGuiWindow reads the UI description from the app's data directory.
    install(FILES src/fritzhomeui.rc
        DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/fritzhome)
else()
    install(TARGETS fritzhome RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# Desktop entry, icon, and AppStream metainfo are installed unconditionally
# (same standard paths regardless of KF build mode).
install(FILES data/io.github.flunaras.fritzhome.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# ── Translations ──────────────────────────────────────────────────────────────
if(_USE_KF)
    # KF build: install the .po catalogue so KI18n can find it at runtime.
    # ki18n_install() copies po/<lang>/fritzhome.po → share/locale/<lang>/LC_MESSAGES/fritzhome.mo
    ki18n_install(po)
else()
    # No-KF build: compile the Qt .ts file to a .qm binary and embed it as a
    # Qt resource so the binary is fully self-contained.
    find_package(Qt${_QT_MAJOR} REQUIRED COMPONENTS LinguistTools)
    set(TS_FILES translations/fritzhome_de.ts)
    if(_QT_MAJOR EQUAL 6)
        qt_add_translation(QM_FILES ${TS_FILES})
    else()
        qt5_add_translation(QM_FILES ${TS_FILES})
    endif()
    # Embed the generated .qm files as a Qt resource so the binary is
    # self-contained.  Qt6's qt_add_resources() accepts an inline file list;
    # Qt5's version only accepts a pre-written .qrc file path, so we use
    # file(GENERATE) to produce one at configure time for Qt5.
    set(_QM_ALIAS "fritzhome_de.qm")
    if(_QT_MAJOR EQUAL 6)
        qt_add_resources(fritzhome "translations"
            PREFIX "/translations"
            BASE "${CMAKE_CURRENT_BINARY_DIR}"
            FILES ${QM_FILES}
        )
    else()
        # Qt5: write a .qrc file at configure time, then compile it explicitly
        # with qt5_add_resources() so CMake tracks the .qm → .qrc → .cpp
        # dependency chain correctly.  We must NOT rely on CMAKE_AUTORCC here
        # because AUTORCC runs before the .qm file is generated.
        set(_QM_QRC "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
        file(WRITE "${_QM_QRC}"
            "<RCC><qresource prefix=\"/translations\"><file alias=\"${_QM_ALIAS}\">${CMAKE_CURRENT_BINARY_DIR}/fritzhome_de.qm</file></qresource></RCC>"
        )
        qt5_add_resources(QRC_SOURCES "${_QM_QRC}")
        # Make the compiled resource depend on the .qm being built first.
        set_source_files_properties(${QRC_SOURCES} PROPERTIES
            OBJECT_DEPENDS "${QM_FILES}"
        )
        target_sources(fritzhome PRIVATE ${QRC_SOURCES})
    endif()
endif()

install(FILES data/fritzhome.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

install(FILES data/io.github.flunaras.fritzhome.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)

# ── CPack — RPM packaging ─────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME        "fritzhome")
set(CPACK_PACKAGE_VERSION     "${PROJECT_VERSION}")
set(CPACK_PACKAGE_RELEASE     "1")
set(CPACK_PACKAGE_VENDOR      "Ulf Saran")
set(CPACK_PACKAGE_CONTACT     "Ulf Saran <ulf.saran@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_DESCRIPTION
"Fritz!Box Smart Home is a native Linux desktop application for monitoring
and controlling AVM Fritz!Box Smart Home devices over the local network.
It uses AVM's documented AHA HTTP interface — no cloud account required.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

# Default generator is RPM; may be overridden via -DCPACK_GENERATOR=DEB on the
# cmake command line (used by build.sh for Ubuntu targets).
if(NOT CPACK_GENERATOR)
    set(CPACK_GENERATOR "RPM")
endif()

# ── RPM-specific settings ─────────────────────────────────────────────────────
set(CPACK_RPM_PACKAGE_LICENSE       "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP         "Productivity/Networking/Other")
set(CPACK_RPM_PACKAGE_URL           "https://github.com/flunaras/fritzhome")
set(CPACK_RPM_PACKAGE_RELEASE       "${CPACK_PACKAGE_RELEASE}")
# When cross-compiling the host CPU differs from the target; force the correct arch.
set(CPACK_RPM_PACKAGE_ARCHITECTURE  "${CMAKE_SYSTEM_PROCESSOR}")

# Architecture-specific file name: fritzhome-1.0.0-1.<arch>.rpm
set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_PACKAGE_RELEASE}.${CMAKE_SYSTEM_PROCESSOR}")

# RPM runtime dependencies — vary by build flavour
# Uses openSUSE package names (not Fedora/RHEL names).
# Soname-based deps (libQt6Core.so.6 etc.) are auto-added by rpmbuild;
# we only list named package deps that aren't covered by soname scanning.
if(_USE_KF)
    if(_KF_MAJOR EQUAL 6)
        # libQt6Core6 covers the Qt6 base runtime (openSUSE package name)
        # kf6-knotifications provides the KF6 notifications plugin
        # libKF6Wallet6 is the openSUSE package name
        set(CPACK_RPM_PACKAGE_REQUIRES
            "libQt6Core6, libQt6Charts6, libglvnd, kf6-knotifications, libKF6Wallet6")
    else()
        set(CPACK_RPM_PACKAGE_REQUIRES
            "libQt5Core5 >= 5.15, libkwalletbackend5-5, libKF5Notifications5")
    endif()
else()
    # Qt5-only build; libsecret is optional (graceful fallback), but list it
    # so the keyring is available on a fresh install.
    set(CPACK_RPM_PACKAGE_REQUIRES
        "libQt5Core5 >= 5.15, libQt5Charts5, libsecret-1-0")
endif()

# Post-install scriptlets to update icon/desktop caches
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE   "")   # defined inline below
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "")

set(CPACK_RPM_SPEC_MORE_DEFINE "
%post
/bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null || :
%postun
if [ $1 -eq 0 ] ; then
    /bin/touch --no-create %{_datadir}/icons/hicolor &>/dev/null
    /usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
fi
%posttrans
/usr/bin/gtk-update-icon-cache %{_datadir}/icons/hicolor &>/dev/null || :
")

# ── DEB-specific settings ─────────────────────────────────────────────────────
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Ulf Saran <ulf.saran@users.noreply.github.com>")
set(CPACK_DEBIAN_PACKAGE_SECTION    "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE   "https://github.com/flunaras/fritzhome")

# DEB runtime dependencies — Ubuntu package names.
# dpkg-shlibdeps auto-detects soname deps during cpack; we list only extras
# that aren't covered by soname scanning.
if(_USE_KF)
    if(_KF_MAJOR EQUAL 6)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libqt6core6t64, libqt6charts6, libqt6gui6, libqt6widgets6")
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libqt5core5t64 (>= 5.15), libqt5charts5")
    endif()
elseif(_QT_MAJOR EQUAL 6)
    # Qt6, no KF (Ubuntu 24.04)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6t64, libqt6charts6, libqt6gui6, libqt6widgets6, libsecret-1-0")
else()
    # Qt5-only
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt5core5t64 (>= 5.15), libqt5charts5, libsecret-1-0")
endif()

# Post-install / post-remove scripts to update icon cache
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)

# Inline postinst / postrm via CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA is file-based;
# use the simpler CPACK_DEBIAN_PACKAGE_SHLIBDEPS approach and skip custom scripts
# (gtk-update-icon-cache is triggered by the desktop file on modern Ubuntu via dh_icons).

include(CPack)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
