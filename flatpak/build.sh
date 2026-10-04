#!/usr/bin/env bash
# flatpak/build.sh — Build fritzhome as a Flatpak bundle, directly on the host.
#
# For a build that needs nothing but Docker on the host (no flatpak/
# flatpak-builder/KDE-SDK install required), see
# `docker/build.sh --distro flatpak-x86_64` instead — it runs this same
# manifest inside a container with the KDE SDK pre-baked in. Use this script
# if you'd rather not pull in that ~1.7 GB Docker image, or want faster
# incremental rebuilds via flatpak-builder's own on-disk build cache.
#
# Usage:
#   ./flatpak/build.sh [--install] [--no-bundle]
#
#   --install     Also install the result into the user's local Flatpak
#                 installation (flatpak run io.github.flunaras.fritzhome)
#   --no-bundle   Skip producing the single-file .flatpak bundle in out/
#                 (only populate the local repo under flatpak/repo/)
#
# One-time host setup (not part of this script — run once per machine):
#   flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
#   flatpak install -y flathub org.kde.Sdk//6.11 org.kde.Platform//6.11
#
# Prerequisites:
#   - flatpak
#   - flatpak-builder   (e.g. `zypper install flatpak-builder` /
#                        `apt install flatpak-builder`)
#   - org.kde.Sdk//6.11 + org.kde.Platform//6.11 installed from Flathub (one-time)
#
# Output:
#   out/flatpak/x86_64/io.github.flunaras.fritzhome.flatpak
#     — single-file bundle, installable offline with:
#       flatpak install ./io.github.flunaras.fritzhome.flatpak
#
# No publishing / Flathub submission is required for any of this — the
# bundle is self-contained and works entirely offline once the KDE runtime
# is present locally.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

APP_ID="io.github.flunaras.fritzhome"
MANIFEST="${SCRIPT_DIR}/${APP_ID}.yml"
BUILD_DIR="${PROJECT_ROOT}/build/flatpak"
REPO_DIR="${SCRIPT_DIR}/repo"
OUT_DIR="${PROJECT_ROOT}/out/flatpak/x86_64"
BUNDLE_PATH="${OUT_DIR}/${APP_ID}.flatpak"

DO_INSTALL=0
DO_BUNDLE=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)   DO_INSTALL=1; shift ;;
        --no-bundle) DO_BUNDLE=0; shift ;;
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

# ── Preflight checks ──────────────────────────────────────────────────────────
command -v flatpak >/dev/null 2>&1 || {
    echo "ERROR: 'flatpak' not found. Install it first (e.g. zypper/apt install flatpak)." >&2
    exit 1
}
command -v flatpak-builder >/dev/null 2>&1 || {
    echo "ERROR: 'flatpak-builder' not found. Install it first (e.g. zypper/apt install flatpak-builder)." >&2
    exit 1
}

if ! flatpak info org.kde.Sdk//6.11 >/dev/null 2>&1; then
    echo "ERROR: org.kde.Sdk//6.11 is not installed." >&2
    echo "  Run once:" >&2
    echo "    flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo" >&2
    echo "    flatpak install -y flathub org.kde.Sdk//6.11 org.kde.Platform//6.11" >&2
    exit 1
fi

echo "[1/3] Cleaning previous build state ..."
STATE_DIR="${SCRIPT_DIR}/.flatpak-builder"
rm -rf "${BUILD_DIR}" "${STATE_DIR}"

echo "[2/3] Running flatpak-builder ..."
mkdir -p "${REPO_DIR}"
FLATPAK_BUILDER_ARGS=(--force-clean --repo="${REPO_DIR}" --state-dir="${STATE_DIR}")
if [[ "${DO_INSTALL}" -eq 1 ]]; then
    FLATPAK_BUILDER_ARGS+=(--install --user)
fi
flatpak-builder "${FLATPAK_BUILDER_ARGS[@]}" "${BUILD_DIR}" "${MANIFEST}"

if [[ "${DO_BUNDLE}" -eq 1 ]]; then
    echo "[3/3] Creating single-file bundle ..."
    mkdir -p "${OUT_DIR}"
    flatpak build-bundle "${REPO_DIR}" "${BUNDLE_PATH}" "${APP_ID}"
    echo ""
    echo "Bundle written to: ${BUNDLE_PATH}"
    echo "Install offline with:"
    echo "  flatpak install ${BUNDLE_PATH}"
else
    echo "[3/3] Skipping bundle creation (--no-bundle)"
fi

if [[ "${DO_INSTALL}" -eq 1 ]]; then
    echo ""
    echo "Installed locally. Run with:"
    echo "  flatpak run ${APP_ID}"
fi
