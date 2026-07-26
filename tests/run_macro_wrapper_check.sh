#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
readonly TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amarantin-run-macro.XXXXXX")
readonly CALLER_DIR="${TMP_DIR}/caller"

cleanup() {
  if [[ -d "${TMP_DIR}" ]]; then
    find "${TMP_DIR}" -type f -exec rm {} +
    find "${TMP_DIR}" -type d -depth -mindepth 1 -exec rmdir {} + 2>/dev/null || true
    rmdir "${TMP_DIR}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

mkdir -p "${TMP_DIR}/bin" "${CALLER_DIR}"
cat >"${TMP_DIR}/bin/root" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf 'pwd=%s\n' "${PWD}"
index=0
for arg in "$@"; do
  printf 'arg[%d]=%s\n' "${index}" "${arg}"
  index=$((index + 1))
done
EOF
chmod +x "${TMP_DIR}/bin/root"
: >"${CALLER_DIR}/fixture.root"

output="$(
  cd "${CALLER_DIR}"
  PATH="${TMP_DIR}/bin:${PATH}" bash "${ROOT_DIR}/tools/run-macro" inspect_covariance ./fixture.root
)"

grep -Fx "pwd=$(cd "${CALLER_DIR}" && pwd)" <<<"${output}" >/dev/null
grep -Fx "arg[0]=-n" <<<"${output}" >/dev/null
grep -Fx "arg[1]=-l" <<<"${output}" >/dev/null
grep -Fx "arg[2]=-q" <<<"${output}" >/dev/null
grep -Fx "arg[3]=${ROOT_DIR}/.rootlogon.C" <<<"${output}" >/dev/null
grep -Fx "arg[4]=${ROOT_DIR}/plot/macro/inspect_covariance.C(\"./fixture.root\")" <<<"${output}" >/dev/null

typed_output="$(
  cd "${CALLER_DIR}"
  PATH="${TMP_DIR}/bin:${PATH}" bash "${ROOT_DIR}/tools/run-macro" plot_event_display ./fixture.root beam 1. .5 1e-3 true n:null s:0123
)"

grep -Fx "arg[4]=${ROOT_DIR}/plot/macro/plot_event_display.C(\"./fixture.root\", \"beam\", 1., .5, 1e-3, true, nullptr, \"0123\")" <<<"${typed_output}" >/dev/null

set +e
invalid_output="$(
  cd "${CALLER_DIR}"
  PATH="${TMP_DIR}/bin:${PATH}" bash "${ROOT_DIR}/tools/run-macro" inspect_covariance i:1.5 2>&1
)"
invalid_status=$?
set -e

[[ ${invalid_status} -ne 0 ]]
grep -Fx "tools/run-macro: invalid int literal: i:1.5" <<<"${invalid_output}" >/dev/null

printf 'run_macro_wrapper_check=ok\n'
