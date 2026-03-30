#!/usr/bin/env bash
set -euo pipefail

dir=""
out=""
pat=""
source_list=""
samdef=""

usage() {
    cat >&2 <<'EOF'
Usage:
    mklist.sh --dir DIR --pat "p1,p2,..." --out OUT.list
    mklist.sh --list INPUT.list --out OUT.list
    mklist.sh --samdef DEFNAME --out OUT.list
EOF
}

write_if_changed() {
    local tmp="$1"
    local target="$2"

    if [[ -f "${target}" ]] && cmp -s "${tmp}" "${target}"; then
        rm -f "${tmp}"
    else
        mv "${tmp}" "${target}"
    fi
}

require_samweb() {
    if ! command -v samweb >/dev/null 2>&1; then
        echo "mklist: samweb is not available in PATH" >&2
        exit 1
    fi
}

write_dir_list() {
    if [[ -z "${pat}" ]]; then
        echo "mklist: --pat is required with --dir" >&2
        exit 2
    fi

    if [[ ! -d "${dir}" ]]; then
        echo "mklist: missing directory: ${dir}" >&2
        exit 1
    fi

    local tmp="${out}.tmp"
    local -a find_args=()

    IFS=',' read -r -a pats <<< "${pat}"
    for p in "${pats[@]}"; do
        [[ -z "${p}" ]] && continue
        [[ ${#find_args[@]} -gt 0 ]] && find_args+=(-o)
        find_args+=(-name "${p}")
    done

    if [[ ${#find_args[@]} -eq 0 ]]; then
        echo "mklist: no usable patterns in --pat='${pat}'" >&2
        exit 2
    fi

    find "${dir}" -type f \( "${find_args[@]}" \) | LC_ALL=C sort -u > "${tmp}"

    if [[ ! -s "${tmp}" ]]; then
        rm -f "${tmp}"
        echo "mklist: no matching files in ${dir} (pat: ${pat})" >&2
        exit 1
    fi

    write_if_changed "${tmp}" "${out}"
}

write_existing_list() {
    if [[ ! -f "${source_list}" ]]; then
        echo "mklist: missing list file: ${source_list}" >&2
        exit 1
    fi

    local tmp="${out}.tmp"
    awk 'NF != 0 && $1 !~ /^#/' "${source_list}" | LC_ALL=C sort -u > "${tmp}"

    if [[ ! -s "${tmp}" ]]; then
        rm -f "${tmp}"
        echo "mklist: list file is empty after filtering: ${source_list}" >&2
        exit 1
    fi

    write_if_changed "${tmp}" "${out}"
}

resolve_sam_path() {
    local file_name="$1"
    local locate_output=""
    local path=""

    locate_output="$(samweb locate-file "${file_name}" 2>/dev/null || true)"
    path="$(printf '%s\n' "${locate_output}" | grep -o '/pnfs/[^ ]*' | head -n 1 || true)"
    path="${path%%)*}"

    if [[ -z "${path}" ]]; then
        echo "mklist: failed to locate file from samweb: ${file_name}" >&2
        exit 1
    fi

    case "${path}" in
        */"${file_name}")
            printf '%s\n' "${path}"
            ;;
        *)
            printf '%s/%s\n' "${path%/}" "${file_name}"
            ;;
    esac
}

write_samdef_list() {
    require_samweb

    local tmp="${out}.tmp"
    : > "${tmp}"

    samweb list-files "defname: ${samdef}" | LC_ALL=C sort -u | while IFS= read -r file_name; do
        [[ -n "${file_name}" ]] || continue
        resolve_sam_path "${file_name}"
    done > "${tmp}"

    LC_ALL=C sort -u "${tmp}" -o "${tmp}"

    if [[ ! -s "${tmp}" ]]; then
        rm -f "${tmp}"
        echo "mklist: no files returned for sam definition: ${samdef}" >&2
        exit 1
    fi

    write_if_changed "${tmp}" "${out}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir)
            dir="$2"
            shift 2
            ;;
        --pat)
            pat="$2"
            shift 2
            ;;
        --out)
            out="$2"
            shift 2
            ;;
        --list)
            source_list="$2"
            shift 2
            ;;
        --samdef)
            samdef="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "mklist: unknown arg: $1" >&2
            usage
            exit 2
            ;;
    esac
done

if [[ -z "${out}" ]]; then
    echo "mklist: --out is required" >&2
    exit 2
fi

mode_count=0
[[ -n "${dir}" ]] && mode_count=$((mode_count + 1))
[[ -n "${source_list}" ]] && mode_count=$((mode_count + 1))
[[ -n "${samdef}" ]] && mode_count=$((mode_count + 1))

if [[ ${mode_count} -ne 1 ]]; then
    echo "mklist: choose exactly one of --dir, --list, or --samdef" >&2
    exit 2
fi

if [[ -n "${pat}" && -z "${dir}" ]]; then
    echo "mklist: --pat is only valid with --dir" >&2
    exit 2
fi

mkdir -p "$(dirname "${out}")"

if [[ -n "${dir}" ]]; then
    write_dir_list
elif [[ -n "${source_list}" ]]; then
    write_existing_list
else
    write_samdef_list
fi
