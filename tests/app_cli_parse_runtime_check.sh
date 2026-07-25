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

require_command() {
  local name=$1
  if ! command -v "${name}" >/dev/null 2>&1; then
    printf 'app_cli_parse_runtime_check: missing command %s\n' "${name}" >&2
    exit 1
  fi
}

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

capture_success() {
  local log_path=$1
  shift

  set +e
  "$@" >"${log_path}" 2>&1
  local status=$?
  set -e

  if [[ ${status} -ne 0 ]]; then
    printf 'app_cli_parse_runtime_check: expected success from:' >&2
    printf ' %q' "$@" >&2
    printf '\n' >&2
    cat "${log_path}" >&2
    exit 1
  fi
}

write_minimal_sample_file() {
  local sample_path=$1
  local macro_path="${TMP_DIR}/write_minimal_sample.C"

  cat > "${macro_path}" <<'EOF'
#include <stdexcept>
#include <string>

#include "TDirectory.h"
#include "TFile.h"
#include "TNamed.h"
#include "TParameter.h"
#include "TTree.h"

void write_minimal_sample(const char *path)
{
  TFile file(path, "RECREATE");
  if (file.IsZombie())
    throw std::runtime_error("failed to create minimal sample file");

  TDirectory *meta_dir = file.mkdir("meta");
  if (!meta_dir)
    throw std::runtime_error("failed to create meta directory");
  meta_dir->cd();
  TNamed("output_path", path).Write("output_path", TObject::kOverwrite);

  TTree input_paths("input_paths", "");
  std::string input_path = "/tmp/input.root";
  input_paths.Branch("input_path", &input_path);
  input_paths.Fill();
  input_paths.Write("input_paths", TObject::kOverwrite);

  TDirectory *sample_dir = file.mkdir("sample");
  if (!sample_dir)
    throw std::runtime_error("failed to create sample directory");
  sample_dir->cd();
  TNamed("sample", "beam").Write("sample", TObject::kOverwrite);
  TNamed("origin", "data").Write("origin", TObject::kOverwrite);
  TNamed("variation", "nominal").Write("variation", TObject::kOverwrite);
  TNamed("beam", "numi").Write("beam", TObject::kOverwrite);
  TNamed("polarity", "fhc").Write("polarity", TObject::kOverwrite);
  TNamed("normalisation_mode", "unit").Write("normalisation_mode", TObject::kOverwrite);
  TParameter<double>("subrun_pot_sum", 1.0).Write("subrun_pot_sum", TObject::kOverwrite);
  TParameter<double>("db_tortgt_pot_sum", 0.0).Write("db_tortgt_pot_sum", TObject::kOverwrite);
  TParameter<double>("normalisation", 1.0).Write("normalisation", TObject::kOverwrite);
  TParameter<double>("normalised_pot_sum", 1.0).Write("normalised_pot_sum", TObject::kOverwrite);

  TTree normalisation_tree("run_subrun_normalisation", "");
  Int_t run = 1;
  Int_t subrun = 0;
  Double_t generated_exposure = 1.0;
  Double_t target_exposure = 0.0;
  Double_t normalisation = 1.0;
  normalisation_tree.Branch("run", &run, "run/I");
  normalisation_tree.Branch("subrun", &subrun, "subrun/I");
  normalisation_tree.Branch("generated_exposure", &generated_exposure, "generated_exposure/D");
  normalisation_tree.Branch("target_exposure", &target_exposure, "target_exposure/D");
  normalisation_tree.Branch("normalisation", &normalisation, "normalisation/D");
  normalisation_tree.Fill();
  normalisation_tree.Write("run_subrun_normalisation", TObject::kOverwrite);

  file.Write();
  file.Close();
}
EOF

  root -n -l -b -q "${macro_path}(\"${sample_path}\")"
}

require_command root
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

sample_path="${TMP_DIR}/beam=sample.root"
dataset_manifest="${TMP_DIR}/dataset.manifest"
native_dataset_path="${TMP_DIR}/native.dataset.root"
legacy_dataset_path="${TMP_DIR}/legacy.dataset.root"
write_minimal_sample_file "${sample_path}"
printf 'beam %s\n' "${sample_path}" > "${dataset_manifest}"

dataset_native_log="${TMP_DIR}/mk_dataset_native.log"
capture_success "${dataset_native_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --run run1 --beam numi --polarity fhc \
  --manifest "${dataset_manifest}" "${native_dataset_path}"
grep -F "mk_dataset: wrote ${native_dataset_path} with 1 logical samples for scope numi_fhc_run1 from manifest ${dataset_manifest}" \
  "${dataset_native_log}" >/dev/null

dataset_legacy_log="${TMP_DIR}/mk_dataset_legacy.log"
capture_success "${dataset_legacy_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --manifest "${dataset_manifest}" "${legacy_dataset_path}" context
grep -F "mk_dataset: wrote ${legacy_dataset_path} with 1 logical samples from manifest ${dataset_manifest}" \
  "${dataset_legacy_log}" >/dev/null

eventlist_log="${TMP_DIR}/mk_eventlist.log"
capture_failure "${eventlist_log}" "${BUILD_DIR}/bin/mk_eventlist" --selection
grep -F "usage: mk_eventlist " "${eventlist_log}" >/dev/null
grep -Fx "mk_eventlist: --selection requires an expression" "${eventlist_log}" >/dev/null

eventlist_conflict_log="${TMP_DIR}/mk_eventlist_conflict.log"
capture_failure "${eventlist_conflict_log}" \
  "${BUILD_DIR}/bin/mk_eventlist" --preset muon --selection "x != 0" out.root in.root
grep -F "usage: mk_eventlist " "${eventlist_conflict_log}" >/dev/null
grep -Fx "mk_eventlist: --preset and --selection are mutually exclusive" \
  "${eventlist_conflict_log}" >/dev/null

dist_log="${TMP_DIR}/mk_dist.log"
capture_failure "${dist_log}" \
  "${BUILD_DIR}/bin/mk_dist" --fine-nbins nope out.root in.root beam x 1 0 1
grep -Fx "mk_dist: invalid integer for --fine-nbins: nope" "${dist_log}" >/dev/null

dist_manifest="${TMP_DIR}/bad.requests.manifest"
cat > "${dist_manifest}" <<'EOF'
beam score 10junk 0 1
EOF
dist_manifest_log="${TMP_DIR}/mk_dist_manifest.log"
capture_failure "${dist_manifest_log}" \
  "${BUILD_DIR}/bin/mk_dist" --manifest "${dist_manifest}" out.root in.root
grep -Fx "mk_dist: invalid integer for nbins at line 1 in ${dist_manifest}: 10junk" \
  "${dist_manifest_log}" >/dev/null

dist_manifest_conflict="${TMP_DIR}/ok.requests.manifest"
cat > "${dist_manifest_conflict}" <<'EOF'
beam score 10 0 1
EOF
dist_manifest_conflict_log="${TMP_DIR}/mk_dist_manifest_conflict.log"
capture_failure "${dist_manifest_conflict_log}" \
  "${BUILD_DIR}/bin/mk_dist" --manifest "${dist_manifest_conflict}" --selection "sel != 0" out.root in.root
grep -F "usage: mk_dist " "${dist_manifest_conflict_log}" >/dev/null
grep -Fx "mk_dist: --selection is not supported with --manifest" \
  "${dist_manifest_conflict_log}" >/dev/null

cov_log="${TMP_DIR}/mk_cov.log"
capture_failure "${cov_log}" "${BUILD_DIR}/bin/mk_cov" --matrix-name
grep -F "usage: mk_cov " "${cov_log}" >/dev/null
grep -Fx "mk_cov: --matrix-name requires a name" "${cov_log}" >/dev/null

cov_manifest="${TMP_DIR}/export.manifest"
cat > "${cov_manifest}" <<'EOF'
beam beam
EOF
cov_manifest_log="${TMP_DIR}/mk_cov_manifest.log"
capture_failure "${cov_manifest_log}" \
  "${BUILD_DIR}/bin/mk_cov" --manifest "${cov_manifest}" --cache-key abc in.root out.root
grep -F "usage: mk_cov " "${cov_manifest_log}" >/dev/null
grep -Fx "mk_cov: --cache-key is not supported with --manifest" \
  "${cov_manifest_log}" >/dev/null

printf 'app_cli_parse_runtime_check=ok\n'
