#!/usr/bin/env bash

dir=""
out=""
pat=""

usage() {
    cat >&2 <<EOF
Usage:
    mklist.sh --dir DIR --pat "p1,p2,..." --out OUT.list
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir) dir="$2"; shift 2 ;;
        --pat) pat="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "mklist: unknown arg: $1" >&2; usage; exit 2 ;;
    esac
done

if [[ -z "${dir}" || -z "${out}" || -z "${pat}" ]]; then
    echo "mklist: --dir, --pat, and --out are required" >&2
    exit 2
fi

if [[ ! -d "${dir}" ]]; then
    echo "mklist: missing directory: ${dir}" >&2
    exit 1
fi

mkdir -p "$(dirname "$out")"
tmp="${out}.tmp"

write_or_keep() {
    if [[ -f "$2" ]] && cmp -s "$1" "$2"; then
        rm -f "$1"
    else
        mv "$1" "$2"
    fi
}

find_args=()
build_find_args() {
    local csv="$1"
    local -a pats=()
    local p=""

    IFS=',' read -r -a pats <<< "$csv"
    for p in "${pats[@]}"; do
        [[ -z "$p" ]] && continue
        [[ ${#find_args[@]} -gt 0 ]] && find_args+=(-o)
        find_args+=(-name "$p")
    done

    [[ ${#find_args[@]} -gt 0 ]]
}

if ! build_find_args "$pat"; then
    echo "mklist: no usable patterns in --pat='${pat}'" >&2
    exit 2
fi

find "${dir}" -type f \( "${find_args[@]}" \) | LC_ALL=C sort -u > "${tmp}"

if [[ ! -s "${tmp}" ]]; then
    rm -f "${tmp}"
    echo "mklist: no matching files in ${dir} (pat: ${pat})" >&2
    exit 1
fi

write_or_keep "${tmp}" "${out}"