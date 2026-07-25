#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
readonly BUILD_DIR_INPUT="${AMARANTIN_BUILD_DIR:-build}"
if [[ "${BUILD_DIR_INPUT}" = /* ]]; then
  readonly BUILD_DIR="${BUILD_DIR_INPUT}"
else
  readonly BUILD_DIR="${ROOT_DIR}/${BUILD_DIR_INPUT}"
fi
readonly TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amarantin-app-cli-help.XXXXXX")

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

require_binary() {
  local path=$1
  if [[ ! -x "${path}" ]]; then
    printf 'app_cli_help_runtime_check: missing executable %s\n' "${path}" >&2
    exit 1
  fi
}

capture_success() {
  local log_path=$1
  shift

  set +e
  "$@" >"${log_path}" 2>&1
  local status=$?
  set -e

  if [[ ${status} -ne 0 ]]; then
    printf 'app_cli_help_runtime_check: expected success from:' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    cat "${log_path}" >&2
    exit 1
  fi
}

require_binary "${BUILD_DIR}/bin/mk_sample"
require_binary "${BUILD_DIR}/bin/mk_dataset"
require_binary "${BUILD_DIR}/bin/mk_eventlist"
require_binary "${BUILD_DIR}/bin/mk_dist"
require_binary "${BUILD_DIR}/bin/mk_cov"

sample_log="${TMP_DIR}/mk_sample.log"
capture_success "${sample_log}" "${BUILD_DIR}/bin/mk_sample" --help
grep -F "usage: mk_sample " "${sample_log}" >/dev/null

dataset_log="${TMP_DIR}/mk_dataset.log"
capture_success "${dataset_log}" "${BUILD_DIR}/bin/mk_dataset" --help
grep -F "usage: mk_dataset " "${dataset_log}" >/dev/null

eventlist_log="${TMP_DIR}/mk_eventlist.log"
capture_success "${eventlist_log}" "${BUILD_DIR}/bin/mk_eventlist" --help
grep -F "usage: mk_eventlist " "${eventlist_log}" >/dev/null

dist_log="${TMP_DIR}/mk_dist.log"
capture_success "${dist_log}" "${BUILD_DIR}/bin/mk_dist" --help
grep -F "usage: mk_dist " "${dist_log}" >/dev/null

cov_log="${TMP_DIR}/mk_cov.log"
capture_success "${cov_log}" "${BUILD_DIR}/bin/mk_cov" --help
grep -F "usage: mk_cov " "${cov_log}" >/dev/null

printf 'app_cli_help_runtime_check=ok\n'
