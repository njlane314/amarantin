#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
readonly TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amarantin-overnight-pass.XXXXXX")
readonly PROMPT_PATH="${TMP_DIR}/custom-prompt.md"
readonly STUB_DIR="${TMP_DIR}/bin"
readonly LOG_DIR="${ROOT_DIR}/.codex-run-logs"
created_log_path=

cleanup() {
  if [[ -n "${created_log_path}" && -f "${created_log_path}" ]]; then
    rm -f "${created_log_path}"
  fi
  if [[ -d "${TMP_DIR}" ]]; then
    find "${TMP_DIR}" -type f -exec rm {} +
    find "${TMP_DIR}" -type d -depth -mindepth 1 -exec rmdir {} + 2>/dev/null || true
    rmdir "${TMP_DIR}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

mkdir -p "${STUB_DIR}"
printf 'wrapper prompt\n' >"${PROMPT_PATH}"
cat >"${STUB_DIR}/codex" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf 'codex-stub pwd=%s\n' "${PWD}"
printf 'codex-stub argv:'
for arg in "$@"; do
  printf ' [%s]' "${arg}"
done
printf '\n'
sleep 1
EOF
chmod +x "${STUB_DIR}/codex"

output="$(
  cd "${TMP_DIR}"
  PATH="${STUB_DIR}:${PATH}" \
    AMARANTIN_MINIMALITY_TIME_BUDGET_SECS=1 \
    AMARANTIN_MINIMALITY_PAUSE_SECS=0 \
    bash "${ROOT_DIR}/tools/overnight-minimality-pass.sh" ./custom-prompt.md
)"

grep -F "codex-stub pwd=${ROOT_DIR}" <<<"${output}" >/dev/null
grep -F "codex-stub argv: [exec] [--full-auto] [--json] [wrapper prompt]" <<<"${output}" >/dev/null

created_log_path="$(find "${LOG_DIR}" -maxdepth 1 -type f -name 'amarantin-minimality-*.jsonl' -print | LC_ALL=C sort | tail -n 1)"
[[ -n "${created_log_path}" ]]
grep -F "codex-stub argv: [exec] [--full-auto] [--json] [wrapper prompt]" "${created_log_path}" >/dev/null

printf 'overnight_minimality_pass_wrapper_check=ok\n'
