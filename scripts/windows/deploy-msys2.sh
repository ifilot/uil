#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: deploy-msys2.sh [options]

Builds a Windows deployment staging directory from an MSYS2 UCRT64 Qt build.

Options:
  --app-name NAME     Executable target name without .exe (default: uil)
  --viewer-name NAME  Qt viewer name without .exe (default: uil-viewer)
  --build-dir DIR     CMake build directory (default: build-windows)
  --stage-dir DIR     Deployment staging directory (default: dist/uil-windows-x64)
  --finalize-existing Reuse an already verified stage and only write final metadata
  --third-party-notices
                      Generate the exhaustive MSYS2 license inventory (slow)
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

find_stage_binaries() {
    find "$STAGE_DIR" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print | sort
}

import_names_for_binary() {
    objdump -p "$1" 2>/dev/null \
        | sed -n 's/^[[:space:]]*DLL Name: //p' \
        | sort -fu
}

index_dependency_directory() {
    local directory="$1"
    local index_name="$2"
    local file
    local -n index="$index_name"

    while IFS= read -r file; do
        index["${file##*/}"]="$file"
    done < <(find "$directory" -maxdepth 1 -type f -iname '*.dll' -print 2>/dev/null \
        | awk '{print tolower($0)}')
}

refresh_stage_dependency_index() {
    local file
    STAGE_FILE_BY_NAME=()
    while IFS= read -r file; do
        STAGE_FILE_BY_NAME["${file##*/}"]="$file"
    done < <(find "$STAGE_DIR" -maxdepth 1 -type f -iname '*.dll' -print 2>/dev/null \
        | awk '{print tolower($0)}')
}

initialize_external_dependency_indexes() {
    index_dependency_directory "$WINDOWS_SYSTEM_DIR" SYSTEM_FILE_BY_NAME
    index_dependency_directory "$MINGW_PREFIX/bin" MINGW_FILE_BY_NAME
}

resolve_import_source() {
    local name="$1"
    local name_l="${name,,}"

    if [[ -n "${STAGE_FILE_BY_NAME[$name_l]+x}" ]]; then
        printf 'stage\t%s\n' "${STAGE_FILE_BY_NAME[$name_l]}"
        return 0
    fi

    if [[ "$name_l" == api-ms-win-*.dll || "$name_l" == ext-ms-*.dll ]]; then
        printf 'system\t%s\n' "$name"
        return 0
    fi

    if [[ -n "${SYSTEM_FILE_BY_NAME[$name_l]+x}" ]]; then
        printf 'system\t%s\n' "${SYSTEM_FILE_BY_NAME[$name_l]}"
        return 0
    fi

    if [[ -n "${MINGW_FILE_BY_NAME[$name_l]+x}" ]]; then
        printf 'mingw\t%s\n' "${MINGW_FILE_BY_NAME[$name_l]}"
        return 0
    fi

    return 1
}

seed_lazy_ffmpeg_libraries() {
    local pattern
    local library
    local copied=0
    local -a patterns=(
        'avformat-*.dll'
        'avcodec-*.dll'
        'avutil-*.dll'
        'swscale-*.dll'
    )

    for pattern in "${patterns[@]}"; do
        while IFS= read -r library; do
            [[ -f "$library" ]] || continue
            cp -f "$library" "$STAGE_DIR/$(basename "$library")"
            chmod u+w "$STAGE_DIR/$(basename "$library")"
            copied=$((copied + 1))
        done < <(find "$MINGW_PREFIX/bin" -maxdepth 1 -type f -iname "$pattern" -print | sort)
    done

    if (( copied > 0 )); then
        log "Seeded $copied lazy-loaded FFmpeg DLL(s)"
    else
        log "No FFmpeg runtime DLLs found; video decoding will remain unavailable"
    fi
}

copy_runtime_dependency_closure() {
    local -a pending
    local index=0
    local binary
    local import_name
    local source
    local source_kind
    local source_path
    local dep_base
    local dest
    local key
    local copied=0
    local scanned=0
    declare -A processed=()

    refresh_stage_dependency_index
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

        AUDIT_BINARIES+=("$binary")

        while IFS= read -r import_name; do
            [[ -n "$import_name" ]] || continue
            source="$(resolve_import_source "$import_name")" \
                || die "could not resolve imported DLL $import_name required by $binary"
            source_kind="${source%%$'\t'*}"
            source_path="${source#*$'\t'}"
            if [[ "$source_kind" == "mingw" ]]; then
                dep_base="$(basename "$source_path")"
                dest="$STAGE_DIR/$dep_base"
                log "Bundling $dep_base"
                cp -f "$source_path" "$dest"
                chmod u+w "$dest"
                STAGE_FILE_BY_NAME["${dep_base,,}"]="$dest"
                pending+=("$dest")
                copied=$((copied + 1))
            fi
        done < <(import_names_for_binary "$binary")
    done

    log "Bundled $copied dependency DLL(s) after statically scanning $scanned PE file(s)"
}

verify_dependency_closure() {
    local problems=0
    local binary
    local import_name
    local source
    local rel
    local imports_log="$STAGE_DIR/deployment-imports.txt"

    refresh_stage_dependency_index
    : > "$imports_log"

    for binary in "${AUDIT_BINARIES[@]}"; do
        rel="${binary#"$STAGE_DIR"/}"
        {
            printf '### %s\n' "$rel"
            while IFS= read -r import_name; do
                [[ -n "$import_name" ]] || continue
                if source="$(resolve_import_source "$import_name")"; then
                    printf '  %s -> %s\n' "$import_name" "$source"
                else
                    printf 'unresolved static import in %s: %s\n' "$rel" "$import_name" >&2
                    printf '  %s -> unresolved\n' "$import_name"
                    problems=$((problems + 1))
                fi
            done < <(import_names_for_binary "$binary")
            printf '\n'
        } >> "$imports_log"
    done

    if objdump -p "$STAGE_DIR/$APP_NAME.exe" 2>/dev/null \
        | sed -n 's/^[[:space:]]*DLL Name: //p' \
        | grep -Eqi '^(Qt6|libgcc|libstdc\+\+|libwinpthread|zlib)'; then
        printf 'native launcher unexpectedly imports Qt or an MSYS2 runtime library\n' >&2
        problems=$((problems + 1))
    fi

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

    verify_primary_binary_dependencies
}

validate_existing_dependency_audit() {
    local audit_count
    local binary_count
    local audit_log="$STAGE_DIR/deployment-imports.txt"

    [[ -s "$audit_log" ]] || die "cannot finalize stage without a completed dependency audit"
    grep -qi 'unresolved' "$audit_log" \
        && die "cannot finalize stage because its dependency audit contains unresolved entries"

    audit_count="$(grep -c '^### ' "$audit_log" || true)"
    binary_count="$(find_stage_binaries | wc -l | tr -d '[:space:]')"
    [[ "$audit_count" == "$binary_count" ]] \
        || die "cannot finalize stage: audit covers $audit_count of $binary_count binaries"

    verify_primary_binary_dependencies
}

verify_primary_binary_dependencies() {
    local binary
    local dep
    local dep_base
    local ldd_output
    local ldd_log="$STAGE_DIR/deployment-ldd.txt"
    local problems=0

    : > "$ldd_log"
    for binary in "$STAGE_DIR/$APP_NAME.exe" "$STAGE_DIR/$VIEWER_NAME.exe"; do
        if ! ldd_output="$(timeout 20s ldd "$binary" 2>&1)"; then
            printf 'runtime dependency check failed or timed out for %s:\n%s\n' \
                "$binary" "$ldd_output" >&2
            problems=$((problems + 1))
        fi
        {
            printf '### %s\n' "$(basename "$binary")"
            printf '%s\n\n' "$ldd_output"
        } >> "$ldd_log"

        if grep -qi 'not found' <<<"$ldd_output"; then
            printf 'updated primary binary has an unresolved dependency: %s\n%s\n' \
                "$binary" "$ldd_output" >&2
            problems=$((problems + 1))
        fi

        while IFS= read -r dep; do
            dep_base="${dep##*/}"
            if is_stage_path "$dep" || is_windows_system_path "$dep"; then
                continue
            fi
            if is_mingw_path "$dep" && [[ -f "$STAGE_DIR/$dep_base" ]]; then
                continue
            fi
            printf 'updated primary binary resolves outside the verified deployment: %s -> %s\n' \
                "$binary" "$dep" >&2
            problems=$((problems + 1))
        done < <(parse_ldd_output_paths <<<"$ldd_output")
    done

    (( problems == 0 )) \
        || die "updated primary-binary verification failed with $problems problem(s)"
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

write_third_party_notices() {
    local notices="$STAGE_DIR/THIRD_PARTY_NOTICES.txt"
    local third_party_dir="$STAGE_DIR/third-party"
    local stage_inventory="$third_party_dir/staged-file-inventory.tsv"
    local package_inventory="$third_party_dir/package-inventory.tsv"
    local review="$third_party_dir/license-review.md"
    local licenses_dir="$third_party_dir/licenses"
    local package_file_index="$third_party_dir/.package-files.tsv"
    local ownership_requests="$third_party_dir/.ownership-requests.txt"
    local ownership_index="$third_party_dir/.ownership-index.tsv"
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
    local current_package
    local line
    local index
    local phase_started
    local review_required=0
    local -a packages=()
    local -a unresolved=()
    local -a staged_rels=()
    local -a staged_sources=()
    local -a staged_packages=()
    local -a owned_indices=()
    local -a ownership_sources=()
    local -a installed_mingw_packages=()
    declare -A package_by_name=()
    declare -A owner_by_source=()
    declare -A staged_by_package=()
    declare -A unresolved_files=()
    declare -A package_versions=()
    declare -A package_licenses=()
    declare -A package_urls=()
    declare -A package_descriptions=()
    declare -A copied_license_count=()

    command -v pacman >/dev/null 2>&1 || die "pacman is required to write third-party notices"

    rm -rf -- "$third_party_dir"
    mkdir -p "$licenses_dir"
    generated_utc="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    mapfile -t STAGED_NOTICE_FILES < <(find "$STAGE_DIR" -type f | sort)

    if [[ -f resources/bluecurve/LICENSE.txt ]]; then
        mkdir -p "$licenses_dir/bluecurve"
        cp -f resources/bluecurve/LICENSE.txt "$licenses_dir/bluecurve/LICENSE.txt"
        cp -f resources/bluecurve/README.md "$licenses_dir/bluecurve/README.md"
    fi

    phase_started=$SECONDS
    for staged_file in "${STAGED_NOTICE_FILES[@]}"; do
        rel="${staged_file#"$STAGE_DIR"/}"

        if [[ "$rel" == "$APP_NAME.exe" || "$rel" == "$VIEWER_NAME.exe" || "$rel" == "LICENSE.txt" ]]; then
            continue
        fi

        source_path=""
        index="${#staged_rels[@]}"
        staged_rels+=("$rel")
        staged_sources+=("")
        staged_packages+=("")
        if source_path="$(resolve_staged_source_path "$staged_file")"; then
            staged_sources[$index]="$source_path"
            owned_indices+=("$index")
            ownership_sources+=("$source_path")
        else
            unresolved_files["$rel"]="could not map staged file back to an MSYS2 source path"
        fi
    done
    log "Resolved ${#staged_rels[@]} staged source path(s) in $((SECONDS - phase_started))s"

    if ((${#ownership_sources[@]} > 0)); then
        phase_started=$SECONDS
        mapfile -t installed_mingw_packages < <(
            pacman -Qq 2>/dev/null | grep -E "^${MINGW_PACKAGE_PREFIX}-"
        )
        ((${#installed_mingw_packages[@]} > 0)) \
            || die "no installed packages found for prefix $MINGW_PACKAGE_PREFIX"
        pacman -Ql -- "${installed_mingw_packages[@]}" > "$package_file_index"
        printf '%s\n' "${ownership_sources[@]}" | sort -u > "$ownership_requests"
        awk '
            NR == FNR { requested[$0] = 1; next }
            {
                package = $1
                sub(/^[^ ]+ /, "")
                if ($0 in requested) {
                    print package "\t" $0
                }
            }
        ' "$ownership_requests" "$package_file_index" > "$ownership_index"
        while IFS=$'\t' read -r package source_path; do
            [[ -n "$package" && -n "$source_path" ]] || continue
            owner_by_source["$source_path"]="$package"
        done < "$ownership_index"

        for index in "${owned_indices[@]}"; do
            source_path="${staged_sources[$index]}"
            rel="${staged_rels[$index]}"
            package="${owner_by_source[$source_path]:-}"
            if [[ -z "$package" ]]; then
                unresolved_files["$rel"]="no owning package for resolved source $source_path"
                continue
            fi
            staged_packages[$index]="$package"
            package_by_name["$package"]=1
            staged_by_package["$package"]+="${rel}"$'\n'
        done
        log "Indexed package ownership in $((SECONDS - phase_started))s"
    fi

    mapfile -t packages < <(printf '%s\n' "${!package_by_name[@]}" | sed '/^$/d' | sort)
    mapfile -t unresolved < <(printf '%s\n' "${!unresolved_files[@]}" | sed '/^$/d' | sort)

    for package in "${packages[@]}"; do
        package_slug="${package//[^A-Za-z0-9_.+-]/_}"
        mkdir -p "$licenses_dir/$package_slug/files"
        : > "$licenses_dir/$package_slug/PACMAN_INFO.txt"
        copied_license_count["$package"]=0
    done

    phase_started=$SECONDS
    current_package=""
    while IFS= read -r line; do
        line="${line%$'\r'}"
        if [[ "$line" =~ ^Name[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            current_package="${BASH_REMATCH[1]}"
        fi
        if [[ -n "$current_package" ]]; then
            package_slug="${current_package//[^A-Za-z0-9_.+-]/_}"
            printf '%s\n' "$line" >> "$licenses_dir/$package_slug/PACMAN_INFO.txt"
        fi
        if [[ "$line" =~ ^Version[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            package_versions["$current_package"]="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^Licenses[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            package_licenses["$current_package"]="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^URL[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            package_urls["$current_package"]="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^Description[[:space:]]*:[[:space:]]*(.*)$ ]]; then
            package_descriptions["$current_package"]="${BASH_REMATCH[1]}"
        fi
    done < <(pacman -Qi -- "${packages[@]}" 2>/dev/null)
    log "Loaded metadata for ${#packages[@]} package(s) in $((SECONDS - phase_started))s"

    phase_started=$SECONDS
    while IFS=' ' read -r package source_path; do
        [[ -n "$package" && -n "$source_path" ]] || continue
        [[ -n "${package_by_name[$package]:-}" ]] || continue
        [[ "$source_path" =~ (^|/)([Cc][Oo][Pp][Yy][Ii][Nn][Gg]|[Cc][Oo][Pp][Yy][Rr][Ii][Gg][Hh][Tt]|[Ll][Ii][Cc][Ee][Nn][CcSs][Ee]|[Nn][Oo][Tt][Ii][Cc][Ee]|[Aa][Uu][Tt][Hh][Oo][Rr][Ss])([._\ -]?[^/]*)?$ ]] || continue
        [[ -f "$source_path" ]] || continue
        package_slug="${package//[^A-Za-z0-9_.+-]/_}"
        rel="${source_path#/}"
        mkdir -p "$licenses_dir/$package_slug/files/$(dirname "$rel")"
        cp -f "$source_path" "$licenses_dir/$package_slug/files/$rel"
        copied_license_count["$package"]=$((copied_license_count["$package"] + 1))
    done < "$package_file_index"

    for package in "${packages[@]}"; do
        if ((copied_license_count["$package"] == 0)); then
            package_slug="${package//[^A-Za-z0-9_.+-]/_}"
            printf 'No installed license-like files were found for %s by pacman -Ql.\n' "$package" \
                > "$licenses_dir/$package_slug/NO_INSTALLED_LICENSE_FILES_FOUND.txt"
        fi
    done
    log "Copied package license files in $((SECONDS - phase_started))s"
    rm -f -- "$package_file_index" "$ownership_requests" "$ownership_index"

    {
        printf 'staged_path\tsource_path\tpackage\tversion\tlicenses\n'
        for ((index = 0; index < ${#staged_rels[@]}; ++index)); do
            rel="${staged_rels[$index]}"
            source_path="${staged_sources[$index]}"
            package="${staged_packages[$index]}"
            version=""
            licenses=""
            if [[ -n "$package" ]]; then
                version="${package_versions[$package]:-}"
                licenses="${package_licenses[$package]:-}"
            fi
            printf '%s\t%s\t%s\t%s\t%s\n' "$rel" "$source_path" "$package" "$version" "$licenses"
        done
    } > "$stage_inventory"

    {
        printf 'package\tversion\tlicenses\turl\tdescription\tmsys2_package_page\n'
    } > "$package_inventory"

    for package in "${packages[@]}"; do
        version="${package_versions[$package]:-}"
        licenses="${package_licenses[$package]:-}"
        url="${package_urls[$package]:-}"
        description="${package_descriptions[$package]:-}"
        printf '%s\t%s\t%s\t%s\t%s\thttps://packages.msys2.org/package/%s\n' \
            "$package" "$version" "$licenses" "$url" "$description" "$package" \
            >> "$package_inventory"
    done

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
        printf 'Red Hat Bluecurve icon artwork:\n'
        printf '  Source: https://github.com/neeeeow/Bluecurve\n'
        printf '  License: GNU General Public License v3.0\n'
        printf '  Installed license text: third-party/licenses/bluecurve/LICENSE.txt\n\n'
        printf 'Important compliance note:\n'
        printf '  MSYS2 packages are independent upstream projects with their own licenses.\n'
        printf '  The package license strings below are generated from the local pacman database.\n'
        printf '  Installed license and notice files, when present in the packages, are copied under third-party/licenses/.\n\n'
        printf 'Package inventory:\n'
        for package in "${packages[@]}"; do
            version="${package_versions[$package]:-}"
            licenses="${package_licenses[$package]:-}"
            url="${package_urls[$package]:-}"
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
        printf -- '- `Bluecurve icon artwork`: `GPL-3.0`\n'
    } > "$review"
    review_required=1

    for package in "${packages[@]}"; do
        licenses="${package_licenses[$package]:-}"
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

copy_app_license_files() {
    if [[ -f LICENSE ]]; then
        cp -f LICENSE "$STAGE_DIR/LICENSE.txt"
    fi
    if [[ -d LICENSES ]] && compgen -G "LICENSES/*" >/dev/null; then
        mkdir -p "$STAGE_DIR/LICENSES"
        cp -f LICENSES/* "$STAGE_DIR/LICENSES/"
    fi
}

copy_example_presentations() {
    local source_dir="examples/bundled"
    local destination_dir="$STAGE_DIR/examples"
    local -a example_files=(
        getting-started.pdf
        pointer-and-annotations.pdf
    )
    local example_file

    mkdir -p "$destination_dir"
    for example_file in "${example_files[@]}"; do
        [[ -f "$source_dir/$example_file" ]] \
            || die "missing bundled example presentation: $source_dir/$example_file"
        cp -f "$source_dir/$example_file" "$destination_dir/$example_file"
    done
}

write_manifest() {
    local manifest="$STAGE_DIR/deployment-manifest.txt"
    local summary="$STAGE_DIR/deployment-summary.md"
    local checksums="$STAGE_DIR/deployment-checksums.sha256"
    local file_count
    local dll_count

    : > "$checksums"
    file_count="$(find "$STAGE_DIR" -type f | wc -l | tr -d '[:space:]')"
    dll_count="$(find "$STAGE_DIR" -type f -iname '*.dll' | wc -l | tr -d '[:space:]')"

    {
        printf 'uil Windows deployment manifest\n'
        printf 'Generated UTC: %s\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
        printf 'MSYSTEM: %s\n' "${MSYSTEM:-}"
        printf 'MINGW_PREFIX: %s\n' "$MINGW_PREFIX"
        printf 'windeployqt: %s\n' "$WINDEPLOYQT"
        printf 'Launcher: %s\n' "$APP_NAME.exe"
        printf 'Viewer: %s\n' "$VIEWER_NAME.exe"
        printf 'File count: %s\n' "$file_count"
        printf 'DLL count: %s\n' "$dll_count"
        printf '\nFiles:\n'
        (cd "$STAGE_DIR" && find . -type f | sed 's#^\./##' | sort)
    } > "$manifest"

    {
        printf '# Windows Deployment Summary\n\n'
        printf -- '- Launcher: `%s.exe`\n' "$APP_NAME"
        printf -- '- Qt viewer: `%s.exe`\n' "$VIEWER_NAME"
        printf -- '- MSYS2 environment: `%s`\n' "${MSYSTEM:-}"
        printf -- '- Toolchain prefix: `%s`\n' "$MINGW_PREFIX"
        printf -- '- Qt deploy tool: `%s`\n' "$WINDEPLOYQT"
        printf -- '- Files staged: `%s`\n' "$file_count"
        printf -- '- DLLs staged: `%s`\n' "$dll_count"
        printf '\nThe staging directory was populated with `windeployqt`, then completed by recursively copying non-system DLL imports found with `objdump`. `ldd` validates the final launcher and viewer with a strict timeout.\n'
    } > "$summary"

    (
        cd "$STAGE_DIR"
        find . -type f ! -name 'deployment-checksums.sha256' -print0 \
            | sort -z \
            | xargs -0 sha256sum
    ) > "$checksums"
}

APP_NAME="uil"
VIEWER_NAME="uil-viewer"
BUILD_DIR="build-windows"
STAGE_DIR="dist/uil-windows-x64"
GENERATE_THIRD_PARTY_NOTICES=0
FINALIZE_EXISTING=0
declare -a AUDIT_BINARIES=()
declare -A STAGE_FILE_BY_NAME=()
declare -A SYSTEM_FILE_BY_NAME=()
declare -A MINGW_FILE_BY_NAME=()

while (($#)); do
    case "$1" in
        --app-name)
            APP_NAME="${2:-}"
            shift 2
            ;;
        --viewer-name)
            VIEWER_NAME="${2:-}"
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
        --third-party-notices)
            GENERATE_THIRD_PARTY_NOTICES=1
            shift
            ;;
        --finalize-existing)
            FINALIZE_EXISTING=1
            shift
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
[[ -n "$VIEWER_NAME" ]] || die "--viewer-name must not be empty"
[[ -n "$BUILD_DIR" ]] || die "--build-dir must not be empty"
[[ -n "$STAGE_DIR" ]] || die "--stage-dir must not be empty"

if [[ "${MSYSTEM:-}" != "UCRT64" ]]; then
    die "run this script from an MSYS2 UCRT64 shell; current MSYSTEM is '${MSYSTEM:-unset}'"
fi

MINGW_PREFIX="${MINGW_PREFIX:-/ucrt64}"
MINGW_PACKAGE_PREFIX="${MINGW_PACKAGE_PREFIX:-mingw-w64-ucrt-x86_64}"

command -v cygpath >/dev/null 2>&1 || die "cygpath is required"
command -v ldd >/dev/null 2>&1 || die "ldd is required"
command -v objdump >/dev/null 2>&1 || die "objdump is required"
command -v timeout >/dev/null 2>&1 || die "timeout is required"

BUILD_DIR="$(absolute_path "$BUILD_DIR")"
STAGE_DIR="$(absolute_path "$STAGE_DIR")"
MINGW_PREFIX="$(absolute_path "$MINGW_PREFIX")"
WINDOWS_ROOT="$(cygpath -u "${WINDIR:-C:\\Windows}")"
WINDOWS_SYSTEM_DIR="$WINDOWS_ROOT/System32"
STAGE_DIR_L="${STAGE_DIR,,}"
MINGW_PREFIX_L="${MINGW_PREFIX,,}"
initialize_external_dependency_indexes

find_built_executable() {
    local executable_name="$1"
    local candidate
    local -a candidates=()

    mapfile -t candidates < <(find "$BUILD_DIR" -maxdepth 4 -type f -iname "$executable_name.exe" | sort)
    ((${#candidates[@]} > 0)) || return 1
    for candidate in "${candidates[@]}"; do
        if [[ "$candidate" == "$BUILD_DIR/$executable_name.exe" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    printf '%s\n' "${candidates[0]}"
}

EXE_PATH="$(find_built_executable "$APP_NAME")" \
    || die "could not find $APP_NAME.exe under $BUILD_DIR"
VIEWER_EXE_PATH="$(find_built_executable "$VIEWER_NAME")" \
    || die "could not find $VIEWER_NAME.exe under $BUILD_DIR"

WINDEPLOYQT="$(find_tool windeployqt6 windeployqt-qt6 windeployqt)" || die "could not find windeployqt for Qt 6"

case "$STAGE_DIR" in
    ""|"/"|"/tmp"|"$HOME"|"$PWD")
        die "refusing to clean unsafe stage directory: $STAGE_DIR"
        ;;
esac

if (( FINALIZE_EXISTING )); then
    [[ -f "$STAGE_DIR/$APP_NAME.exe" ]] \
        || die "cannot finalize stage without $STAGE_DIR/$APP_NAME.exe"
    [[ -f "$STAGE_DIR/$VIEWER_NAME.exe" ]] \
        || die "cannot finalize stage without $STAGE_DIR/$VIEWER_NAME.exe"
    validate_existing_dependency_audit
    log "Reusing existing verified staging directory $STAGE_DIR"
else
    log "Preparing staging directory $STAGE_DIR"
    rm -rf -- "$STAGE_DIR"
    mkdir -p "$STAGE_DIR"
    cp -f "$EXE_PATH" "$STAGE_DIR/$APP_NAME.exe"
    cp -f "$VIEWER_EXE_PATH" "$STAGE_DIR/$VIEWER_NAME.exe"
    chmod u+w "$STAGE_DIR/$APP_NAME.exe"
    chmod u+w "$STAGE_DIR/$VIEWER_NAME.exe"

    log "Running Qt deployment tool"
    "$WINDEPLOYQT" \
        --release \
        --compiler-runtime \
        --force \
        --verbose 1 \
        "$STAGE_DIR/$VIEWER_NAME.exe"

    log "Completing non-Qt runtime DLL dependency closure"
    seed_lazy_ffmpeg_libraries
    copy_runtime_dependency_closure

    log "Verifying staged runtime dependency closure"
    verify_dependency_closure
fi

log "Copying application license files"
copy_app_license_files

log "Copying example presentations"
copy_example_presentations

if (( GENERATE_THIRD_PARTY_NOTICES )); then
    log "Writing exhaustive third-party notices"
    write_third_party_notices
else
    log "Skipping exhaustive third-party notice inventory (use --third-party-notices to enable)"
fi

log "Writing deployment manifest"
write_manifest

log "Deployment staging complete: $STAGE_DIR"
