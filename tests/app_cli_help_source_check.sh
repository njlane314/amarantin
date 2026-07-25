#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)

require_help_handler() {
  local path=$1
  if ! grep -F 'arg == "-h" || arg == "--help"' "${path}" >/dev/null; then
    printf 'app_cli_help_source_check: missing -h/--help handler in %s\n' "${path}" >&2
    exit 1
  fi
}

require_help_success_exit() {
  local path=$1
  if ! grep -F 'struct HelpRequested final {};' "${path}" >/dev/null; then
    printf 'app_cli_help_source_check: missing explicit HelpRequested marker in %s\n' "${path}" >&2
    exit 1
  fi
  if ! grep -F 'throw HelpRequested{};' "${path}" >/dev/null; then
    printf 'app_cli_help_source_check: missing explicit help throw in %s\n' "${path}" >&2
    exit 1
  fi
  if ! grep -F 'catch (const HelpRequested &)' "${path}" >/dev/null; then
    printf 'app_cli_help_source_check: missing HelpRequested success catch in %s\n' "${path}" >&2
    exit 1
  fi
  if grep -F 'throw std::runtime_error("")' "${path}" >/dev/null; then
    printf 'app_cli_help_source_check: stale empty runtime_error help path in %s\n' "${path}" >&2
    exit 1
  fi
}

require_help_handler "${ROOT_DIR}/app/mk_sample.cc"
require_help_handler "${ROOT_DIR}/app/mk_dataset.cc"
require_help_handler "${ROOT_DIR}/app/mk_eventlist.cc"
require_help_handler "${ROOT_DIR}/app/mk_dist.cc"
require_help_handler "${ROOT_DIR}/app/mk_cov.cc"

require_help_success_exit "${ROOT_DIR}/app/mk_sample.cc"
require_help_success_exit "${ROOT_DIR}/app/mk_dataset.cc"
require_help_success_exit "${ROOT_DIR}/app/mk_eventlist.cc"
require_help_success_exit "${ROOT_DIR}/app/mk_dist.cc"
require_help_success_exit "${ROOT_DIR}/app/mk_cov.cc"

printf 'app_cli_help_source_check=ok\n'
