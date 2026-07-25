#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

run_missing_value_check() {
  local flag=$1
  shift
  local output=
  local status=0

  if output=$(bash "${ROOT_DIR}/tools/mklist.sh" "${flag}" "$@" 2>&1); then
    printf 'mklist_argument_check: %s unexpectedly succeeded\n' "${flag}" >&2
    exit 1
  else
    status=$?
  fi

  if [[ ${status} -ne 2 ]]; then
    printf 'mklist_argument_check: %s exited %d, expected 2\n' "${flag}" "${status}" >&2
    exit 1
  fi

  grep -F "mklist: missing value for ${flag}" <<<"${output}" >/dev/null
  grep -F "Usage:" <<<"${output}" >/dev/null
  if grep -Fq "unbound variable" <<<"${output}"; then
    printf 'mklist_argument_check: %s still hit an unbound-variable shell abort\n' "${flag}" >&2
    exit 1
  fi
}

run_missing_value_check --dir
run_missing_value_check --pat
run_missing_value_check --out
run_missing_value_check --list
run_missing_value_check --samdef
run_missing_value_check --dir --out foo.list
run_missing_value_check --list --samdef beam
run_missing_value_check --samdef --out foo.list

printf 'mklist_argument_check=ok\n'
