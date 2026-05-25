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

parse_ldd_output_paths() {
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
    done
}

parse_ldd_paths() {
    local binary="$1"
    { ldd "$binary" 2>&1 || true; } | parse_ldd_output_paths
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
    local -a pending
    local index=0
    local binary
    local dep
    local dep_base
    local dest
    local key
    local copied=0
    local scanned=0
    declare -A processed=()

    mapfile -t pending < <(find_stage_binaries)

    while (( index < ${#pending[@]} )); do
        binary="$(canonical_file_path "${pending[$index]}")"
        index=$((index + 1))

        [[ -f "$binary" ]] || continue

        key="${binary,,}"
        if [[ -n "${processed[$key]+x}" ]]; then
            continue
        fi
        processed[$key]=1
        scanned=$((scanned + 1))

        while IFS= read -r dep; do
            dep="$(canonical_file_path "$dep")"
            if should_copy_dependency "$dep"; then
                dep_base="$(basename "$dep")"
                dest="$STAGE_DIR/$dep_base"
                if [[ ! -f "$dest" ]]; then
                    log "Bundling $dep_base"
                    cp -f "$dep" "$dest"
                    chmod u+w "$dest"
                    pending+=("$dest")
                    copied=$((copied + 1))
                fi
            fi
        done < <(parse_ldd_paths "$binary")
    done

    log "Bundled $copied dependency DLL(s) after scanning $scanned binary file(s)"
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
        ldd_output="$(ldd "$binary" 2>&1 || true)"

        {
            printf '### %s\n' "$rel"
            printf '%s\n' "$ldd_output"
            printf '\n'
        } >> "$ldd_log"

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
        done < <(parse_ldd_output_paths <<<"$ldd_output")

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

resolve_staged_source_path() {
    local staged_file="$1"
    local rel="${staged_file#"$STAGE_DIR"/}"
    local base
    local candidate
    base="$(basename "$staged_file")"

    local -a candidates=(
        "$MINGW_PREFIX/bin/$base"
        "$MINGW_PREFIX/share/qt6/plugins/$rel"
        "$MINGW_PREFIX/lib/qt6/plugins/$rel"
        "$MINGW_PREFIX/qt6/plugins/$rel"
        "$MINGW_PREFIX/share/qt6/translations/$base"
        "$MINGW_PREFIX/share/qt6/resources/$base"
    )

    for candidate in "${candidates[@]}"; do
        if [[ -f "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

package_field() {
    local package="$1"
    local field="$2"

    pacman -Qi "$package" 2>/dev/null \
        | sed -nE "s/^${field}[[:space:]]*: //p" \
        | head -n 1
}

package_version() {
    local package="$1"
    pacman -Q "$package" 2>/dev/null | awk '{print $2}'
}

copy_package_license_files() {
    local package="$1"
    local destination="$2"
    local file
    local rel
    local copied=0

    mkdir -p "$destination/files"
    pacman -Qi "$package" > "$destination/PACMAN_INFO.txt" 2>/dev/null || true

    while IFS= read -r file; do
        [[ -f "$file" ]] || continue
        rel="${file#/}"
        mkdir -p "$destination/files/$(dirname "$rel")"
        cp -f "$file" "$destination/files/$rel"
        copied=$((copied + 1))
    done < <(pacman -Qlq "$package" 2>/dev/null \
        | grep -Ei '(^|/)(copying|copyright|licen[cs]e|notice|authors)([._ -]?[^/]*)?$' \
        | sort -u)

    if (( copied == 0 )); then
        printf 'No installed license-like files were found for %s by pacman -Qlq.\n' "$package" \
            > "$destination/NO_INSTALLED_LICENSE_FILES_FOUND.txt"
    fi
}

write_third_party_notices() {
    local notices="$STAGE_DIR/THIRD_PARTY_NOTICES.txt"
    local third_party_dir="$STAGE_DIR/third-party"
    local stage_inventory="$third_party_dir/staged-file-inventory.tsv"
    local package_inventory="$third_party_dir/package-inventory.tsv"
    local review="$third_party_dir/license-review.md"
    local licenses_dir="$third_party_dir/licenses"
    local generated_utc
    local staged_file
    local rel
    local source_path
    local package
    local version
    local licenses
    local url
    local description
    local license_l
    local package_slug
    local review_required=0
    local -a packages=()
    local -a unresolved=()
    declare -A package_by_name=()
    declare -A staged_by_package=()
    declare -A unresolved_files=()

    command -v pacman >/dev/null 2>&1 || die "pacman is required to write third-party notices"

    generated_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    mapfile -t STAGED_NOTICE_FILES < <(find "$STAGE_DIR" -type f | sort)

    rm -rf -- "$third_party_dir"
    mkdir -p "$licenses_dir"

    {
        printf 'staged_path\tsource_path\tpackage\tversion\tlicenses\n'
    } > "$stage_inventory"

    for staged_file in "${STAGED_NOTICE_FILES[@]}"; do
        rel="${staged_file#"$STAGE_DIR"/}"

        if [[ "$rel" == "$APP_NAME.exe" || "$rel" == "LICENSE.txt" ]]; then
            continue
        fi

        source_path=""
        package=""
        version=""
        licenses=""

        if source_path="$(resolve_staged_source_path "$staged_file")"; then
            if package="$(pacman -Qoq "$source_path" 2>/dev/null | head -n 1)"; then
                version="$(package_version "$package")"
                licenses="$(package_field "$package" "Licenses")"
                package_by_name["$package"]=1
                staged_by_package["$package"]+="${rel}"$'\n'
            else
                unresolved_files["$rel"]="no owning package for resolved source $source_path"
            fi
        else
            unresolved_files["$rel"]="could not map staged file back to an MSYS2 source path"
        fi

        printf '%s\t%s\t%s\t%s\t%s\n' "$rel" "$source_path" "$package" "$version" "$licenses" \
            >> "$stage_inventory"
    done

    {
        printf 'package\tversion\tlicenses\turl\tdescription\tmsys2_package_page\n'
    } > "$package_inventory"

    mapfile -t packages < <(printf '%s\n' "${!package_by_name[@]}" | sed '/^$/d' | sort)
    mapfile -t unresolved < <(printf '%s\n' "${!unresolved_files[@]}" | sed '/^$/d' | sort)

    for package in "${packages[@]}"; do
        version="$(package_version "$package")"
        licenses="$(package_field "$package" "Licenses")"
        url="$(package_field "$package" "URL")"
        description="$(package_field "$package" "Description")"
        printf '%s\t%s\t%s\t%s\t%s\thttps://packages.msys2.org/package/%s\n' \
            "$package" "$version" "$licenses" "$url" "$description" "$package" \
            >> "$package_inventory"

        package_slug="${package//[^A-Za-z0-9_.+-]/_}"
        copy_package_license_files "$package" "$licenses_dir/$package_slug"
    done

    if [[ -f LICENSE ]]; then
        cp -f LICENSE "$STAGE_DIR/LICENSE.txt"
    fi
    if [[ -d LICENSES ]] && compgen -G "LICENSES/*" >/dev/null; then
        mkdir -p "$STAGE_DIR/LICENSES"
        cp -f LICENSES/* "$STAGE_DIR/LICENSES/"
    fi

    {
        printf 'Third-party notices for uil\n'
        printf 'Generated UTC: %s\n\n' "$generated_utc"
        printf 'This file describes third-party software redistributed with the Windows build of uil.\n'
        printf 'The detailed machine-readable inventories are installed next to this file under third-party/.\n\n'
        printf 'uil application code:\n'
        printf '  Copyright (C) 2026 Ivo Filot\n'
        printf '  License: GNU Lesser General Public License v3.0 only\n'
        printf '  Repository: https://github.com/ifilot/uil\n'
        printf '  Installed license texts: LICENSE.txt and LICENSES/GPL-3.0-only.txt\n\n'
        printf 'Important compliance note:\n'
        printf '  MSYS2 packages are independent upstream projects with their own licenses.\n'
        printf '  The package license strings below are generated from the local pacman database.\n'
        printf '  Installed license and notice files, when present in the packages, are copied under third-party/licenses/.\n\n'
        printf 'Package inventory:\n'
        for package in "${packages[@]}"; do
            version="$(package_version "$package")"
            licenses="$(package_field "$package" "Licenses")"
            url="$(package_field "$package" "URL")"
            printf '\n%s %s\n' "$package" "$version"
            printf '  Licenses: %s\n' "${licenses:-unknown}"
            printf '  Upstream: %s\n' "${url:-unknown}"
            printf '  MSYS2 package: https://packages.msys2.org/package/%s\n' "$package"
            printf '  Staged files:\n'
            while IFS= read -r rel; do
                [[ -n "$rel" ]] || continue
                printf '    - %s\n' "$rel"
            done <<<"${staged_by_package[$package]}"
        done

        if ((${#unresolved[@]} > 0)); then
            printf '\nFiles without pacman package attribution:\n'
            for rel in "${unresolved[@]}"; do
                printf '  - %s: %s\n' "$rel" "${unresolved_files[$rel]}"
            done
        fi
    } > "$notices"

    {
        printf '# Third-party License Review\n\n'
        printf 'Generated UTC: `%s`\n\n' "$generated_utc"
        printf 'This review is generated from staged files and MSYS2 pacman metadata. It is an audit aid, not legal advice.\n\n'
        printf '## Copyleft Attention Items\n\n'
    } > "$review"

    for package in "${packages[@]}"; do
        licenses="$(package_field "$package" "Licenses")"
        license_l="${licenses,,}"
        if [[ "$license_l" == *gpl* ]]; then
            review_required=1
            {
                printf -- '- `%s`: `%s`\n' "$package" "${licenses:-unknown}"
            } >> "$review"
        fi
    done

    if (( review_required == 0 )); then
        printf 'No GPL/LGPL-family license strings were detected in package metadata.\n' >> "$review"
    else
        {
            printf '\nReview these packages before release, especially FFmpeg-related packages, because GPL-enabled codec libraries can affect redistribution obligations.\n'
            printf 'See FFmpeg legal guidance: https://www.ffmpeg.org/legal.html\n'
        } >> "$review"
        log "Third-party license review contains copyleft attention items"
    fi

    if ((${#unresolved[@]} > 0)); then
        {
            printf '\n## Files Without Package Attribution\n\n'
            for rel in "${unresolved[@]}"; do
                printf -- '- `%s`: %s\n' "$rel" "${unresolved_files[$rel]}"
            done
        } >> "$review"
        log "Third-party notice generation could not attribute ${#unresolved[@]} staged file(s)"
    fi

    log "Wrote third-party notices for ${#packages[@]} package(s)"
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
        printf -- '- App: `%s.exe`\n' "$APP_NAME"
        printf -- '- MSYS2 environment: `%s`\n' "${MSYSTEM:-}"
        printf -- '- Toolchain prefix: `%s`\n' "$MINGW_PREFIX"
        printf -- '- Qt deploy tool: `%s`\n' "$WINDEPLOYQT"
        printf -- '- Files staged: `%s`\n' "$file_count"
        printf -- '- DLLs staged: `%s`\n' "$dll_count"
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

log "Writing third-party notices"
write_third_party_notices

log "Writing deployment manifest"
write_manifest

log "Deployment staging complete: $STAGE_DIR"
