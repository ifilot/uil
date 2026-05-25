#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: write-release-notes.sh --version VERSION --installer PATH --output PATH [--manifest PATH]
EOF
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

VERSION=""
INSTALLER=""
OUTPUT=""
MANIFEST=""

while (($#)); do
    case "$1" in
        --version)
            VERSION="${2:-}"
            shift 2
            ;;
        --installer)
            INSTALLER="${2:-}"
            shift 2
            ;;
        --output)
            OUTPUT="${2:-}"
            shift 2
            ;;
        --manifest)
            MANIFEST="${2:-}"
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

[[ -n "$VERSION" ]] || die "--version is required"
[[ -n "$INSTALLER" ]] || die "--installer is required"
[[ -n "$OUTPUT" ]] || die "--output is required"
[[ -f "$INSTALLER" ]] || die "installer does not exist: $INSTALLER"

installer_name="$(basename "$INSTALLER")"
installer_size="$(du -h "$INSTALLER" | awk '{print $1}')"
installer_sha="$(sha256sum "$INSTALLER" | awk '{print $1}')"
short_sha="${GITHUB_SHA:-}"
short_sha="${short_sha:0:7}"

{
    printf '# uil %s\n\n' "$VERSION"
    printf 'Windows installer for the `%s` release.\n\n' "$VERSION"
    printf '## Download\n\n'
    printf -- '- `%s`\n' "$installer_name"
    printf -- '- Size: `%s`\n' "$installer_size"
    printf -- '- SHA-256: `%s`\n' "$installer_sha"
    if [[ -n "$short_sha" ]]; then
        printf -- '- Build commit: `%s`\n' "$short_sha"
    fi
    printf '\n## Packaging\n\n'
    printf -- '- Built on GitHub Actions with MSYS2 UCRT64 and Qt 6.\n'
    printf -- '- Qt runtime libraries and plugins are deployed with `windeployqt`.\n'
    printf -- '- Non-Qt runtime DLLs are collected recursively with `ldd` and verified before installer creation.\n'
    if [[ -n "$MANIFEST" && -f "$MANIFEST" ]]; then
        printf -- '- The deployment manifest is attached as a release asset for auditing.\n'
    fi
} > "$OUTPUT"
