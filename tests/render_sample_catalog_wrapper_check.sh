#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
readonly TMP_DIR=$(mktemp -d "${TMPDIR:-/tmp}/amarantin-render-catalog.XXXXXX")
readonly OUT_DIR_NAME="rendered"
readonly OUT_DIR_PATH="${TMP_DIR}/${OUT_DIR_NAME}"

cleanup() {
  if [[ -d "${TMP_DIR}" ]]; then
    find "${TMP_DIR}" -type f -exec rm {} +
    find "${TMP_DIR}" -type d -depth -mindepth 1 -exec rmdir {} + 2>/dev/null || true
    rmdir "${TMP_DIR}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

(
  cd "${TMP_DIR}"
  bash "${ROOT_DIR}/tools/render-sample-catalog.sh" "" "" "${OUT_DIR_NAME}"
)

[[ -f "${OUT_DIR_PATH}/datasets.mk" ]]
[[ -f "${OUT_DIR_PATH}/run1_fhc.sample.defs" ]]
[[ -f "${OUT_DIR_PATH}/run1_fhc.beam.sample.manifest" ]]
[[ -f "${OUT_DIR_PATH}/run1_fhc.dataset.manifest" ]]

grep -Fx "dataset_defs.run1_fhc := ${OUT_DIR_NAME}/run1_fhc.sample.defs" "${OUT_DIR_PATH}/datasets.mk" >/dev/null
grep -Fx "dataset_manifest.run1_fhc := ${OUT_DIR_NAME}/run1_fhc.dataset.manifest" "${OUT_DIR_PATH}/datasets.mk" >/dev/null
grep -Fx $'beam\tbuild/samples/run1_fhc/beam.root' "${OUT_DIR_PATH}/run1_fhc.dataset.manifest" >/dev/null

printf 'render_sample_catalog_wrapper_check=ok\n'
