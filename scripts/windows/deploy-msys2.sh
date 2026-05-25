#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: deploy-msys2.sh [options]

Builds a Windows deployment staging directory from an MSYS2 UCRT64 Qt build.

Options:
  --app-name NAME     Executable target name without .exe (default: uil)
  --build-dir DIR     CMake build directory (default: build-windows)
  --stage-dir DIR     Deployment staging directory (default: dist/uil-windows-x64)
  --help              Show this help text
EOF
}

log() {
    printf '==> %s\n' "$*"
}

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

trim() {
    local value="$1"
    value="${value#"${value%%[![:space:]]*}"}"
    value="${value%"${value##*[![:space:]]}"}"
    printf '%s' "$value"
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

canonical_file_path() {
    local path="$1"
    if [[ -e "$path" ]]; then
        printf '%s/%s\n' "$(cd "$(dirname "$path")" && pwd -P)" "$(basename "$path")"
    else
        printf '%s\n' "$path"
    fi
}

to_posix_path() {
    local path="$1"
    path="${path%$'\r'}"
    if [[ "$path" =~ ^[A-Za-z]:\\ ]]; then
        cygpath -u "$path"
    else
        printf '%s\n' "$path"
    fi
}

find_tool() {
    local name
    for name in "$@"; do
        if command -v "$name" >/dev/null 2>&1; then
            command -v "$name"
            return 0
        fi
    done

    local prefix
    for prefix in "${MINGW_PREFIX:-/ucrt64}" /ucrt64; do
        for name in "$@"; do
            if [[ -x "$prefix/bin/$name" ]]; then
                printf '%s/bin/%s\n' "$prefix" "$name"
                return 0
            fi
            if [[ -x "$prefix/bin/$name.exe" ]]; then
                printf '%s/bin/%s.exe\n' "$prefix" "$name"
                return 0
            fi
        done
    done

    return 1
}

parse_ldd_paths() {
    local binary="$1"
    local line
    local dep

    while IFS= read -r line; do
        line="${line%$'\r'}"
        dep=""

        if [[ "$line" == *"=>"* ]]; then
            dep="${line#*=>}"
        else
            dep="$line"
        fi

        dep="${dep%% (0x*}"
        dep="$(trim "$dep")"

        [[ -n "$dep" ]] || continue
        [[ "${dep,,}" == *".dll" ]] || continue
        [[ "${dep,,}" == "not found" ]] && continue

        if [[ "$dep" == /* || "$dep" =~ ^[A-Za-z]:\\ ]]; then
            to_posix_path "$dep"
        fi
    done < <(ldd "$binary" 2>&1 || true)
}

is_windows_system_path() {
    local path_l="${1,,}"
    [[ "$path_l" == /c/windows/* || "$path_l" =~ ^/[a-z]/windows/ ]]
}

is_stage_path() {
    local path_l="${1,,}"
    [[ "$path_l" == "$STAGE_DIR_L/"* ]]
}

is_mingw_path() {
    local path_l="${1,,}"
    [[ "$path_l" == "$MINGW_PREFIX_L/"* ]]
}

is_msys_runtime_path() {
    local path_l="${1,,}"
    [[ "$path_l" == /usr/bin/* ]]
}

should_copy_dependency() {
    local dep="$1"
    [[ -f "$dep" ]] || return 1
    is_stage_path "$dep" && return 1
    is_windows_system_path "$dep" && return 1
    is_mingw_path "$dep" && [[ "${dep,,}" == *.dll ]]
}

find_stage_binaries() {
    find "$STAGE_DIR" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print | sort
}

copy_runtime_dependency_closure() {
    local iteration=0
    local added
    local binary
    local dep
    local dep_base
    local dest

    while :; do
        iteration=$((iteration + 1))
        added=0

        while IFS= read -r binary; do
            while IFS= read -r dep; do
                dep="$(canonical_file_path "$dep")"
                if should_copy_dependency "$dep"; then
                    dep_base="$(basename "$dep")"
                    dest="$STAGE_DIR/$dep_base"
                    if [[ ! -f "$dest" ]]; then
                        log "Bundling $dep_base"
                        cp -f "$dep" "$dest"
                        chmod u+w "$dest"
                        added=$((added + 1))
                    fi
                fi
            done < <(parse_ldd_paths "$binary")
        done < <(find_stage_binaries)

        if (( added == 0 )); then
            break
        fi
        if (( iteration > 50 )); then
            die "dependency copy did not converge after $iteration iterations"
        fi
    done
}

verify_dependency_closure() {
    local problems=0
    local binary
    local dep
    local dep_base
    local ldd_output
    local rel
    local imports_log="$STAGE_DIR/deployment-imports.txt"
    local ldd_log="$STAGE_DIR/deployment-ldd.txt"

    : > "$ldd_log"
    : > "$imports_log"

    while IFS= read -r binary; do
        rel="${binary#"$STAGE_DIR"/}"
        {
            printf '### %s\n' "$rel"
            ldd "$binary" 2>&1 || true
            printf '\n'
        } >> "$ldd_log"

        ldd_output="$(ldd "$binary" 2>&1 || true)"
        if grep -qi 'not found' <<<"$ldd_output"; then
            printf 'unresolved dependency in %s:\n%s\n' "$rel" "$ldd_output" >&2
            problems=$((problems + 1))
        fi

        while IFS= read -r dep; do
            dep="$(canonical_file_path "$dep")"
            dep_base="$(basename "$dep")"

            if is_stage_path "$dep" || is_windows_system_path "$dep"; then
                continue
            fi

            if is_mingw_path "$dep"; then
                if [[ ! -f "$STAGE_DIR/$dep_base" ]]; then
                    printf 'dependency was resolved from MSYS2 but is not staged: %s -> %s\n' "$rel" "$dep" >&2
                    problems=$((problems + 1))
                fi
                continue
            fi

            if is_msys_runtime_path "$dep"; then
                printf 'native Windows deployment unexpectedly depends on MSYS runtime: %s -> %s\n' "$rel" "$dep" >&2
                problems=$((problems + 1))
                continue
            fi

            printf 'dependency is outside the staged app, Windows system directories, and %s: %s -> %s\n' \
                "$MINGW_PREFIX" "$rel" "$dep" >&2
            problems=$((problems + 1))
        done < <(parse_ldd_paths "$binary")

        if command -v objdump >/dev/null 2>&1; then
            {
                printf '### %s\n' "$rel"
                objdump -p "$binary" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: /  /p' || true
                printf '\n'
            } >> "$imports_log"
        fi
    done < <(find_stage_binaries)

    if [[ ! -f "$STAGE_DIR/platforms/qwindows.dll" ]]; then
        printf 'Qt platform plugin is missing: platforms/qwindows.dll\n' >&2
        problems=$((problems + 1))
    fi

    if [[ -f "$STAGE_DIR/Qt6Svg.dll" && ! -f "$STAGE_DIR/iconengines/qsvgicon.dll" ]]; then
        printf 'Qt SVG icon plugin is missing: iconengines/qsvgicon.dll\n' >&2
        problems=$((problems + 1))
    fi

    if (( problems > 0 )); then
        die "deployment verification failed with $problems problem(s)"
    fi
}

write_manifest() {
    local manifest="$STAGE_DIR/deployment-manifest.txt"
    local summary="$STAGE_DIR/deployment-summary.md"
    local file_count
    local dll_count

    file_count="$(find "$STAGE_DIR" -type f | wc -l | tr -d '[:space:]')"
    dll_count="$(find "$STAGE_DIR" -type f -iname '*.dll' | wc -l | tr -d '[:space:]')"

    {
        printf 'uil Windows deployment manifest\n'
        printf 'Generated UTC: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'MSYSTEM: %s\n' "${MSYSTEM:-}"
        printf 'MINGW_PREFIX: %s\n' "$MINGW_PREFIX"
        printf 'windeployqt: %s\n' "$WINDEPLOYQT"
        printf 'Executable: %s\n' "$APP_NAME.exe"
        printf 'File count: %s\n' "$file_count"
        printf 'DLL count: %s\n' "$dll_count"
        printf '\nFiles:\n'
        (cd "$STAGE_DIR" && find . -type f | sed 's#^\./##' | sort)
    } > "$manifest"

    {
        printf '# Windows Deployment Summary\n\n'
        printf '- App: `%s.exe`\n' "$APP_NAME"
        printf '- MSYS2 environment: `%s`\n' "${MSYSTEM:-}"
        printf '- Toolchain prefix: `%s`\n' "$MINGW_PREFIX"
        printf '- Qt deploy tool: `%s`\n' "$WINDEPLOYQT"
        printf '- Files staged: `%s`\n' "$file_count"
        printf '- DLLs staged: `%s`\n' "$dll_count"
        printf '\nThe staging directory was populated with `windeployqt`, then completed by recursively copying non-system DLL dependencies resolved by `ldd` from the active MSYS2 toolchain.\n'
    } > "$summary"
}

APP_NAME="uil"
BUILD_DIR="build-windows"
STAGE_DIR="dist/uil-windows-x64"

while (($#)); do
    case "$1" in
        --app-name)
            APP_NAME="${2:-}"
            shift 2
            ;;
        --build-dir)
            BUILD_DIR="${2:-}"
            shift 2
            ;;
        --stage-dir)
            STAGE_DIR="${2:-}"
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
[[ -n "$BUILD_DIR" ]] || die "--build-dir must not be empty"
[[ -n "$STAGE_DIR" ]] || die "--stage-dir must not be empty"

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    die "run this script from an MSYS2 UCRT64 shell; current MSYSTEM is '${MSYSTEM:-unset}'"
fi

MINGW_PREFIX="${MINGW_PREFIX:-/ucrt64}"

command -v cygpath >/dev/null 2>&1 || die "cygpath is required"
command -v ldd >/dev/null 2>&1 || die "ldd is required"

BUILD_DIR="$(absolute_path "$BUILD_DIR")"
STAGE_DIR="$(absolute_path "$STAGE_DIR")"
MINGW_PREFIX="$(absolute_path "$MINGW_PREFIX")"
STAGE_DIR_L="${STAGE_DIR,,}"
MINGW_PREFIX_L="${MINGW_PREFIX,,}"

mapfile -t EXE_CANDIDATES < <(find "$BUILD_DIR" -maxdepth 4 -type f -iname "$APP_NAME.exe" | sort)
if ((${#EXE_CANDIDATES[@]} == 0)); then
    die "could not find $APP_NAME.exe under $BUILD_DIR"
fi

EXE_PATH="${EXE_CANDIDATES[0]}"
for candidate in "${EXE_CANDIDATES[@]}"; do
    if [[ "$candidate" == "$BUILD_DIR/$APP_NAME.exe" ]]; then
        EXE_PATH="$candidate"
        break
    fi
done

WINDEPLOYQT="$(find_tool windeployqt6 windeployqt-qt6 windeployqt)" || die "could not find windeployqt for Qt 6"

case "$STAGE_DIR" in
    ""|"/"|"/tmp"|"$HOME"|"$PWD")
        die "refusing to clean unsafe stage directory: $STAGE_DIR"
        ;;
esac

log "Preparing staging directory $STAGE_DIR"
rm -rf -- "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -f "$EXE_PATH" "$STAGE_DIR/$APP_NAME.exe"
chmod u+w "$STAGE_DIR/$APP_NAME.exe"

log "Running Qt deployment tool"
"$WINDEPLOYQT" \
    --release \
    --compiler-runtime \
    --force \
    --verbose 1 \
    "$STAGE_DIR/$APP_NAME.exe"

log "Completing non-Qt runtime DLL dependency closure"
copy_runtime_dependency_closure

log "Verifying staged runtime dependency closure"
verify_dependency_closure

log "Writing deployment manifest"
write_manifest

log "Deployment staging complete: $STAGE_DIR"
