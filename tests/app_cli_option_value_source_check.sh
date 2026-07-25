#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

require_guard_count() {
  local path=$1
  local expected=$2
  local actual=0

  actual=$(grep -Fc 'looks_like_option_token(argv[i])' "${path}")
  if [[ "${actual}" != "${expected}" ]]; then
    printf 'app_cli_option_value_source_check: %s has %s guarded option-value checks, expected %s\n' \
      "${path}" "${actual}" "${expected}" >&2
    exit 1
  fi
}

require_guard_count "${ROOT_DIR}/app/mk_sample.cc" 3
require_guard_count "${ROOT_DIR}/app/mk_dataset.cc" 6
require_guard_count "${ROOT_DIR}/app/mk_eventlist.cc" 4
require_guard_count "${ROOT_DIR}/app/mk_dist.cc" 4
require_guard_count "${ROOT_DIR}/app/mk_cov.cc" 4

printf 'app_cli_option_value_source_check=ok\n'
