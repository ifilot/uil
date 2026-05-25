#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build-inno-installer.sh [options]

Compiles the Inno Setup installer for a prepared Windows staging directory.

Options:
  --app-name NAME       App/executable name without .exe (default: uil)
  --display-name NAME   Display name shown by the installer (default: uil)
  --stage-dir DIR       Deployment staging directory (default: dist/uil-windows-x64)
  --output-dir DIR      Installer output directory (default: dist)
  --output-base NAME    Output filename without .exe
  --version VERSION     App version. Defaults to the CMake project version.
  --script PATH         Inno Setup script path (default: packaging/windows/uil.iss)
  --help                Show this help text
EOF
}

log() {
    printf '==> %s\n' "$*"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

absolute_path() {
    local path="$1"
    if [[ -d "$path" ]]; then
        (cd "$path" && pwd -P)
    else
        local dir
        local base
        dir="$(dirname "$path")"
        base="$(basename "$path")"
        mkdir -p "$dir"
        printf '%s/%s\n' "$(cd "$dir" && pwd -P)" "$base"
    fi
}

project_version() {
    sed -nE 's/^[[:space:]]*project\([^)]*VERSION[[:space:]]+([^[:space:])]+).*/\1/p' CMakeLists.txt | head -n 1
}

find_iscc() {
    if [[ -n "${ISCC:-}" ]]; then
        local configured_iscc="$ISCC"
        if [[ "$configured_iscc" =~ ^[A-Za-z]:\\ ]]; then
            configured_iscc="$(cygpath -u "$configured_iscc")"
        fi
        if [[ -x "$configured_iscc" ]]; then
            printf '%s\n' "$configured_iscc"
            return 0
        fi
    fi

    local name
    for name in iscc ISCC ISCC.exe; do
        if command -v "$name" >/dev/null 2>&1; then
            command -v "$name"
            return 0
        fi
    done

    local candidate
    for candidate in \
        "/c/ProgramData/chocolatey/bin/ISCC.exe" \
        "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
        "/c/Program Files/Inno Setup 6/ISCC.exe" \
        "/c/tools/innosetup/tools/ISCC.exe"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

APP_NAME="uil"
DISPLAY_NAME="uil"
STAGE_DIR="dist/uil-windows-x64"
OUTPUT_DIR="dist"
OUTPUT_BASE=""
VERSION=""
INNO_SCRIPT="packaging/windows/uil.iss"

while (($#)); do
    case "$1" in
        --app-name)
            APP_NAME="${2:-}"
            shift 2
            ;;
        --display-name)
            DISPLAY_NAME="${2:-}"
            shift 2
            ;;
        --stage-dir)
            STAGE_DIR="${2:-}"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="${2:-}"
            shift 2
            ;;
        --output-base)
            OUTPUT_BASE="${2:-}"
            shift 2
            ;;
        --version)
            VERSION="${2:-}"
            shift 2
            ;;
        --script)
            INNO_SCRIPT="${2:-}"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[[ -n "$APP_NAME" ]] || die "--app-name must not be empty"
[[ -n "$DISPLAY_NAME" ]] || die "--display-name must not be empty"
[[ -n "$STAGE_DIR" ]] || die "--stage-dir must not be empty"
[[ -n "$OUTPUT_DIR" ]] || die "--output-dir must not be empty"
[[ -n "$INNO_SCRIPT" ]] || die "--script must not be empty"

if [[ -z "$VERSION" ]]; then
    VERSION="$(project_version)"
fi
VERSION="${VERSION#v}"
[[ -n "$VERSION" ]] || die "could not determine app version"

if [[ -z "$OUTPUT_BASE" ]]; then
    OUTPUT_BASE="$APP_NAME-$VERSION-windows-x64-setup"
fi

command -v cygpath >/dev/null 2>&1 || die "cygpath is required"

STAGE_DIR="$(absolute_path "$STAGE_DIR")"
OUTPUT_DIR="$(absolute_path "$OUTPUT_DIR")"
INNO_SCRIPT="$(absolute_path "$INNO_SCRIPT")"

[[ -d "$STAGE_DIR" ]] || die "stage directory does not exist: $STAGE_DIR"
[[ -f "$STAGE_DIR/$APP_NAME.exe" ]] || die "stage directory does not contain $APP_NAME.exe"
[[ -f "$INNO_SCRIPT" ]] || die "Inno Setup script does not exist: $INNO_SCRIPT"
mkdir -p "$OUTPUT_DIR"

ISCC_PATH="$(find_iscc)" || die "could not find Inno Setup compiler (ISCC.exe). Install Inno Setup 6 or set ISCC."

export UIL_APP_NAME="$APP_NAME"
export UIL_APP_DISPLAY_NAME="$DISPLAY_NAME"
export UIL_APP_PUBLISHER="${UIL_APP_PUBLISHER:-uil}"
export UIL_EXE_NAME="$APP_NAME.exe"
export UIL_VERSION="$VERSION"
export UIL_STAGE_DIR="$(cygpath -w "$STAGE_DIR")"
export UIL_OUTPUT_DIR="$(cygpath -w "$OUTPUT_DIR")"
export UIL_OUTPUT_BASE="$OUTPUT_BASE"

log "Compiling Inno Setup installer"
MSYS2_ARG_CONV_EXCL='*' "$ISCC_PATH" "$(cygpath -w "$INNO_SCRIPT")"

INSTALLER_PATH="$OUTPUT_DIR/$OUTPUT_BASE.exe"
[[ -f "$INSTALLER_PATH" ]] || die "installer was not created: $INSTALLER_PATH"

sha256sum "$INSTALLER_PATH" > "$INSTALLER_PATH.sha256"
log "Installer created: $INSTALLER_PATH"
