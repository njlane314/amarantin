#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

require_unknown_option_guard() {
  local path=$1
  local message_prefix=$2

  grep -E '[[:alnum:]_]+\.rfind\("--", 0\) == 0' "${path}" >/dev/null
  grep -F "${message_prefix}: unknown option: " "${path}" >/dev/null
}

require_unknown_option_guard "${ROOT_DIR}/app/mk_sample.cc" "mk_sample"
require_unknown_option_guard "${ROOT_DIR}/app/mk_dataset.cc" "mk_dataset"
require_unknown_option_guard "${ROOT_DIR}/app/mk_eventlist.cc" "mk_eventlist"
require_unknown_option_guard "${ROOT_DIR}/app/mk_dist.cc" "mk_dist"
require_unknown_option_guard "${ROOT_DIR}/app/mk_cov.cc" "mk_cov"
require_unknown_option_guard "${ROOT_DIR}/app/mk_collie.cc" "mk_collie"

printf 'app_cli_unknown_option_source_check=ok\n'
