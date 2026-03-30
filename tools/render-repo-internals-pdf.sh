#!/usr/bin/env bash
set -euo pipefail

readonly SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
readonly ROOT_DIR=$(cd "${SCRIPT_DIR}/.." && pwd)
readonly IMAGE_TAG=amarantin-plantuml-pdf
readonly INPUT_PUML=docs/repo-internals.puml
readonly OUTPUT_PDF=docs/repo-internals.pdf

cd "${ROOT_DIR}"

docker build -q -t "${IMAGE_TAG}" -f "${SCRIPT_DIR}/Dockerfile.plantuml-pdf" "${SCRIPT_DIR}" >/dev/null

docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "${ROOT_DIR}:/work" \
  -w /work \
  "${IMAGE_TAG}" \
  -tpdf \
  "${INPUT_PUML}"

rm -f docs/repo-internals.png docs/repo-internals.svg

if [[ ! -f "${OUTPUT_PDF}" ]]; then
  echo "tools/render-repo-internals-pdf.sh: expected ${OUTPUT_PDF} was not generated" >&2
  exit 1
fi
