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
readonly TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amarantin-app-cli.XXXXXX")

cleanup() {
  rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

require_binary() {
  local path=$1
  if [[ ! -x "${path}" ]]; then
    printf 'app_cli_parse_runtime_check: missing executable %s\n' "${path}" >&2
    exit 1
  fi
}

capture_failure() {
  local log_path=$1
  shift

  set +e
  "$@" >"${log_path}" 2>&1
  local status=$?
  set -e

  if [[ ${status} -eq 0 ]]; then
    printf 'app_cli_parse_runtime_check: expected failure from:' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    cat "${log_path}" >&2
    exit 1
  fi
  if [[ ${status} -ne 1 ]]; then
    printf 'app_cli_parse_runtime_check: unexpected exit %d from:' "${status}" >&2
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
capture_failure "${sample_log}" "${BUILD_DIR}/bin/mk_sample" --run-db
grep -Fx "mk_sample: --run-db requires a path" "${sample_log}" >/dev/null
[[ "$(grep -Fc 'mk_sample: --run-db requires a path' "${sample_log}")" == "1" ]]

dataset_log="${TMP_DIR}/mk_dataset.log"
capture_failure "${dataset_log}" "${BUILD_DIR}/bin/mk_dataset" --run
grep -F "usage: mk_dataset " "${dataset_log}" >/dev/null
grep -Fx "mk_dataset: --run requires a run" "${dataset_log}" >/dev/null

eventlist_log="${TMP_DIR}/mk_eventlist.log"
capture_failure "${eventlist_log}" "${BUILD_DIR}/bin/mk_eventlist" --selection
grep -F "usage: mk_eventlist " "${eventlist_log}" >/dev/null
grep -Fx "mk_eventlist: --selection requires an expression" "${eventlist_log}" >/dev/null

dist_log="${TMP_DIR}/mk_dist.log"
capture_failure "${dist_log}" \
  "${BUILD_DIR}/bin/mk_dist" --fine-nbins nope out.root in.root beam x 1 0 1
grep -Fx "mk_dist: invalid integer for --fine-nbins: nope" "${dist_log}" >/dev/null

cov_log="${TMP_DIR}/mk_cov.log"
capture_failure "${cov_log}" "${BUILD_DIR}/bin/mk_cov" --matrix-name
grep -F "usage: mk_cov " "${cov_log}" >/dev/null
grep -Fx "mk_cov: --matrix-name requires a name" "${cov_log}" >/dev/null

printf 'app_cli_parse_runtime_check=ok\n'
