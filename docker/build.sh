#!/usr/bin/env bash
# build.sh — Build fritzhome inside a Docker container for openSUSE Leap / Ubuntu.
#
# Usage:
#   ./docker/build.sh [--distro <alias>|all]
#
# Available aliases:
#   opensuse-leap-15.6-x86_64    openSUSE Leap 15.6,  x86_64,  Qt5-only
#   opensuse-leap-16.0-x86_64    openSUSE Leap 16.0,  x86_64,  Qt6 + KF6
#   opensuse-tumbleweed-x86_64   openSUSE Tumbleweed, x86_64,  Qt6 + KF6
#   opensuse-tumbleweed-aarch64  openSUSE Tumbleweed, aarch64, Qt6 + KF6 (cross-compiled)
#   ubuntu-24.04-x86_64          Ubuntu 24.04,         x86_64,  Qt6-only
#   all                          Build all of the above (default)
#
# The script:
#   1. Builds (or reuses) a Docker image from docker/Dockerfile.<dockerfile-name>
#   2. Runs cmake + ninja + cpack inside the container as the current host user
#      (--user $(id -u):$(id -g)) so output files are owned by you
#   3. Copies the resulting binary and package into a hierarchical directory tree:
#
#        build/<family>/<distro>/<arch>/   ← cmake / ninja / cpack work tree
#        out/<family>/<distro>/<arch>/     ← final binary and package
#
#      where <family> is "opensuse" or "ubuntu", <distro> is the short distro name
#      (e.g. "leap15.6", "tumbleweed", "24.04"), and <arch> is "x86_64" or "aarch64"
#      for openSUSE targets, and "amd64" or "arm64" for Ubuntu targets.
#      Package filenames follow distribution standards:
#        RPM (openSUSE):  fritzhome-<version>-<release>.<arch>.rpm
#        DEB (Ubuntu):    fritzhome_<version>-<release>_<deb_arch>.deb

set -euo pipefail

# ── Resolve project root (directory that contains CMakeLists.txt) ─────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Defaults ──────────────────────────────────────────────────────────────────
DISTRO="all"

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --distro)
            DISTRO="$2"
            shift 2
            ;;
        --distro=*)
            DISTRO="${1#*=}"
            shift
            ;;
        -h|--help)
            sed -n '/^# Usage:/,/^[^#]/{ /^[^#]/d; s/^# \{0,2\}//; p }' "$0"
            exit 0
            ;;
        *)
            echo "ERROR: Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

# Validate distro choice
case "$DISTRO" in
    opensuse-leap-15.6-x86_64|\
    opensuse-leap-16.0-x86_64|\
    opensuse-tumbleweed-x86_64|\
    opensuse-tumbleweed-aarch64|\
    ubuntu-24.04-x86_64|\
    all) ;;
    *)
        echo "ERROR: --distro must be one of:" >&2
        echo "  opensuse-leap-15.6-x86_64, opensuse-leap-16.0-x86_64," >&2
        echo "  opensuse-tumbleweed-x86_64, opensuse-tumbleweed-aarch64," >&2
        echo "  ubuntu-24.04-x86_64, all" >&2
        exit 1
        ;;
esac

# ── Map alias → distribution family (top-level directory) ────────────────────
family_for_distro() {
    case "$1" in
        ubuntu-*) echo "ubuntu"   ;;
        *)        echo "opensuse" ;;
    esac
}

# ── Map alias → Dockerfile suffix (matches docker/Dockerfile.<suffix>) ───────
dockerfile_for_distro() {
    case "$1" in
        opensuse-leap-15.6-x86_64)   echo "leap15.6"           ;;
        opensuse-leap-16.0-x86_64)   echo "leap16.0"           ;;
        opensuse-tumbleweed-x86_64)  echo "tumbleweed"         ;;
        opensuse-tumbleweed-aarch64) echo "tumbleweed-aarch64" ;;
        ubuntu-24.04-x86_64)         echo "ubuntu24.04"        ;;
    esac
}

# ── Map alias → KF version ────────────────────────────────────────────────────
kf_version_for_distro() {
    case "$1" in
        opensuse-leap-15.6-x86_64)   echo "0"    ;;  # no KDE Frameworks — Qt5 only
        opensuse-leap-16.0-x86_64)   echo "6"    ;;
        opensuse-tumbleweed-x86_64)  echo "6"    ;;
        opensuse-tumbleweed-aarch64) echo "6"    ;;
        ubuntu-24.04-x86_64)         echo "qt6"  ;;  # Qt6, no KDE Frameworks
        *)                           echo ""     ;;  # empty = auto-detect
    esac
}

# ── Map alias → CPU architecture (as reported by the build) ──────────────────
arch_for_distro() {
    case "$1" in
        opensuse-tumbleweed-aarch64) echo "aarch64" ;;
        *)                           echo "x86_64"  ;;
    esac
}

# ── Map alias → distro directory name used in build/ and out/ paths ──────────
# tumbleweed-aarch64 shares the "tumbleweed" directory; arch distinguishes it.
dir_name_for_distro() {
    case "$1" in
        opensuse-leap-15.6-x86_64)   echo "leap15.6"   ;;
        opensuse-leap-16.0-x86_64)   echo "leap16.0"   ;;
        opensuse-tumbleweed-x86_64)  echo "tumbleweed" ;;
        opensuse-tumbleweed-aarch64) echo "tumbleweed" ;;
        ubuntu-24.04-x86_64)         echo "24.04"      ;;
    esac
}

# ── Map RPM/ELF arch → Debian arch name ──────────────────────────────────────
deb_arch_for_arch() {
    case "$1" in
        x86_64)  echo "amd64"  ;;
        aarch64) echo "arm64"  ;;
        *)       echo "$1"     ;;
    esac
}

# ── Build function ────────────────────────────────────────────────────────────
build_for_distro() {
    local distro="$1"          # e.g. opensuse-leap-15.6-x86_64
    local dockerfile_suffix
    dockerfile_suffix="$(dockerfile_for_distro "${distro}")"
    local image="fritzhome-builder-${dockerfile_suffix}"
    local dockerfile="${SCRIPT_DIR}/Dockerfile.${dockerfile_suffix}"
    local kf_ver
    kf_ver="$(kf_version_for_distro "${distro}")"
    local use_kf_flag=""
    if [[ -n "${kf_ver}" ]]; then
        use_kf_flag="-DUSE_KF=${kf_ver}"
    fi

    # Project version — read from the project() call in CMakeLists.txt
    local version
    version="$(grep -A3 '^project(fritzhome' "${PROJECT_ROOT}/CMakeLists.txt" \
                | grep -oP 'VERSION\s+\K\d+\.\d+\.\d+')"
    local release="1"

    # CPU architecture and Debian arch
    local arch
    arch="$(arch_for_distro "${distro}")"
    local deb_arch
    deb_arch="$(deb_arch_for_arch "${arch}")"

    # Distribution family (top-level directory) and distro directory name
    local family
    family="$(family_for_distro "${distro}")"
    local dir_name
    dir_name="$(dir_name_for_distro "${distro}")"

    # Directory-level arch tag: Debian families use their own arch vocabulary
    # (amd64, arm64) to match Ubuntu conventions; RPM families use x86_64/aarch64.
    local dir_arch
    case "${family}" in
        ubuntu) dir_arch="${deb_arch}" ;;
        *)      dir_arch="${arch}"     ;;
    esac

    # Hierarchical build and output directories
    local build_dir="${PROJECT_ROOT}/build/${family}/${dir_name}/${dir_arch}"
    local out_dir="${PROJECT_ROOT}/out/${family}/${dir_name}/${dir_arch}"

    # Cross-compilation toolchain flag (only for cross-build targets)
    local toolchain_flag=""
    local qt_host_path_flags=""
    case "${distro}" in
        opensuse-tumbleweed-aarch64)
            toolchain_flag="-DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchain-aarch64.cmake"
            qt_host_path_flags="-DQT_HOST_PATH=/usr -DQT_HOST_PATH_CMAKE_DIR=/usr/lib64/cmake"
            ;;
    esac

    # Determine package type for this distro
    local pkg_type="RPM"
    case "${distro}" in
        ubuntu-*) pkg_type="DEB" ;;
    esac

    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  Building for: ${distro}  (${version}  ${arch})"
    echo "════════════════════════════════════════════════════════════"

    # ── Step 1: Build (or update) the Docker image ────────────────────────────
    echo ""
    echo "[1/4] Building Docker image '${image}' ..."
    docker build \
        --file "${dockerfile}" \
        --tag  "${image}" \
        "${PROJECT_ROOT}"
    echo "      Docker image ready."

    # ── Step 2: Run cmake + ninja + cpack inside the container ────────────────
    mkdir -p "${build_dir}" "${out_dir}"

    echo ""
    echo "[2/4] Running cmake + ninja + cpack inside container ..."
    echo "      Source  : ${PROJECT_ROOT}  →  /src  (read-only)"
    echo "      Build   : ${build_dir}  →  /build  (writable)"
    echo "      User    : $(id -u):$(id -g)"

    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -v "${PROJECT_ROOT}:/src:ro" \
        -v "${build_dir}:/build" \
        -v "/etc/passwd:/etc/passwd:ro" \
        -v "/etc/group:/etc/group:ro" \
        "${image}" \
        bash -c "
            set -euo pipefail
            echo '  [cmake] Configuring ...'
            # Remove stale CMakeCache to prevent cached package paths from a previous
            # (possibly failed) configure from poisoning this run.
            rm -f /build/CMakeCache.txt
            cmake_args=(/src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPACK_GENERATOR=${pkg_type})
            [[ -n '${use_kf_flag}' ]] && cmake_args+=('${use_kf_flag}')
            [[ -n '${toolchain_flag}' ]] && cmake_args+=('${toolchain_flag}')
            [[ -n '${qt_host_path_flags}' ]] && cmake_args+=(${qt_host_path_flags})
            cmake \"\${cmake_args[@]}\"
            echo '  [ninja] Building ...'
            ninja -C /build
            echo '  [cpack] Packaging ...'
            cd /build && cpack --config CPackConfig.cmake
        "
    echo "      Build + package succeeded."

    # ── Step 3: Copy binary ───────────────────────────────────────────────────
    echo ""
    echo "[3/4] Copying binary to out/${family}/${dir_name}/${dir_arch}/ ..."
    if [[ ! -f "${build_dir}/fritzhome" ]]; then
        echo "ERROR: Expected binary '${build_dir}/fritzhome' not found." >&2
        exit 1
    fi
    cp "${build_dir}/fritzhome" "${out_dir}/fritzhome"
    echo "      Binary : ${out_dir}/fritzhome"

    # ── Step 4: Copy package (standard distro filename) ───────────────────────
    echo ""
    echo "[4/4] Copying ${pkg_type} package to out/${family}/${dir_name}/${dir_arch}/ ..."

    local pkg_file=""
    case "${pkg_type}" in
        RPM) pkg_file="$(find "${build_dir}" -maxdepth 1 -name "fritzhome-*.rpm" | sort | tail -1)" ;;
        DEB) pkg_file="$(find "${build_dir}" -maxdepth 1 -name "fritzhome-*.deb" | sort | tail -1)" ;;
    esac

    if [[ -z "${pkg_file}" ]]; then
        echo "WARNING: No ${pkg_type} file found in '${build_dir}' — cpack may have failed." >&2
    else
        local pkg_out=""
        case "${pkg_type}" in
            # RPM standard:  name-version-release.arch.rpm
            RPM) pkg_out="${out_dir}/fritzhome-${version}-${release}.${arch}.rpm" ;;
            # Debian policy: name_version-revision_arch.deb   (underscores)
            DEB) pkg_out="${out_dir}/fritzhome_${version}-${release}_${deb_arch}.deb" ;;
        esac
        cp "${pkg_file}" "${pkg_out}"
        echo "      Package: ${pkg_out}"
    fi
}

# ── Dispatch ──────────────────────────────────────────────────────────────────
if [[ "$DISTRO" == "all" ]]; then
    build_for_distro "opensuse-leap-15.6-x86_64"
    build_for_distro "opensuse-leap-16.0-x86_64"
    build_for_distro "opensuse-tumbleweed-x86_64"
    build_for_distro "opensuse-tumbleweed-aarch64"
    build_for_distro "ubuntu-24.04-x86_64"
else
    build_for_distro "$DISTRO"
fi

echo ""
echo "Done."
