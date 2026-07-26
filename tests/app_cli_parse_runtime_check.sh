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

require_unchanged() {
  local expected_path=$1
  local actual_path=$2
  local label=$3

  if ! cmp -s "${expected_path}" "${actual_path}"; then
    printf 'app_cli_parse_runtime_check: %s changed after rejected command\n' "${label}" >&2
    exit 1
  fi
}

require_no_temporary_output() {
  local output_path=$1
  local label=$2

  if compgen -G "${output_path}.tmp.*" >/dev/null; then
    printf 'app_cli_parse_runtime_check: %s left a temporary output\n' "${label}" >&2
    exit 1
  fi
}

write_minimal_input_file() {
  local input_path=$1
  local include_event_tree=${2:-true}
  local macro_path="${TMP_DIR}/write_minimal_input.C"

  cat > "${macro_path}" <<'EOF'
#include <stdexcept>

#include "TFile.h"
#include "TTree.h"

void write_minimal_input(const char *path, bool include_event_tree)
{
  TFile file(path, "RECREATE");
  if (file.IsZombie())
    throw std::runtime_error("failed to create minimal input file");

  Int_t run = 1;
  Int_t subRun = 2;
  Int_t software_trigger = 1;
  Int_t num_slices = 1;
  Float_t topological_score = 0.5f;
  Bool_t in_reco_fiducial = true;
  Int_t selection_pass = 1;

  if (include_event_tree)
  {
    TTree events("EventSelectionFilter", "");
    events.Branch("run", &run, "run/I");
    events.Branch("subRun", &subRun, "subRun/I");
    events.Branch("software_trigger", &software_trigger, "software_trigger/I");
    events.Branch("num_slices", &num_slices, "num_slices/I");
    events.Branch("topological_score", &topological_score, "topological_score/F");
    events.Branch("in_reco_fiducial", &in_reco_fiducial, "in_reco_fiducial/O");
    events.Branch("selection_pass", &selection_pass, "selection_pass/I");
    events.Fill();
    events.Write();
  }

  TTree subruns("SubRun", "");
  Double_t pot = 1.0;
  subruns.Branch("run", &run, "run/I");
  subruns.Branch("subRun", &subRun, "subRun/I");
  subruns.Branch("pot", &pot, "pot/D");
  subruns.Fill();
  subruns.Write();
  file.Close();
}
EOF

  root -n -l -b -q "${macro_path}(\"${input_path}\",${include_event_tree})"
}

write_run_db() {
  local run_db_path=$1

  python3 - "${run_db_path}" <<'PY'
import sqlite3
import sys

connection = sqlite3.connect(sys.argv[1])
connection.execute("CREATE TABLE runinfo(run INTEGER, subrun INTEGER, tortgt REAL)")
connection.execute("INSERT INTO runinfo(run, subrun, tortgt) VALUES (1, 2, 1.0)")
connection.commit()
connection.close()
PY
}

write_multi_cache_distribution_file() {
  local dist_path=$1
  local macro_path="${TMP_DIR}/write_multi_cache_distribution.C"

  cat > "${macro_path}" <<EOF
#include <string>
#include <vector>

#include "${ROOT_DIR}/io/DistributionIO.hh"

R__LOAD_LIBRARY(${BUILD_DIR}/lib/libIO.so)

namespace {
DistributionIO::Spectrum make_spectrum(const std::string &cache_key,
                                       double offset)
{
  DistributionIO::Spectrum spectrum;
  spectrum.spec.sample_key = "beam";
  spectrum.spec.branch_expr = "score";
  spectrum.spec.selection_expr = "1";
  spectrum.spec.cache_key = cache_key;
  spectrum.spec.nbins = 2;
  spectrum.spec.xmin = 0.0;
  spectrum.spec.xmax = 2.0;
  spectrum.nominal = {1.0 + offset, 2.0 + offset};
  spectrum.sumw2 = {1.0 + offset, 4.0 + offset};
  spectrum.total_down = {0.8 + offset, 1.8 + offset};
  spectrum.total_up = {1.2 + offset, 2.2 + offset};
  return spectrum;
}
}

void write_multi_cache_distribution(const char *path)
{
  DistributionIO dist(path, DistributionIO::Mode::kUpdate);
  dist.write("beam", "alpha", make_spectrum("alpha", 0.0));
  dist.write("beam", "beta", make_spectrum("beta", 10.0));
  dist.flush();
}
EOF

  root -n -l -b -q "${macro_path}(\"${dist_path}\")"
}

require_command root
require_command python3
require_binary "${BUILD_DIR}/bin/mk_sample"
require_binary "${BUILD_DIR}/bin/mk_dataset"
require_binary "${BUILD_DIR}/bin/mk_eventlist"
require_binary "${BUILD_DIR}/bin/mk_dist"
require_binary "${BUILD_DIR}/bin/mk_cov"

sample_log="${TMP_DIR}/mk_sample.log"
capture_failure "${sample_log}" "${BUILD_DIR}/bin/mk_sample" --run-db
grep -Fx "mk_sample: --run-db requires a path" "${sample_log}" >/dev/null
[[ "$(grep -Fc 'mk_sample: --run-db requires a path' "${sample_log}")" == "1" ]]

sample_list_collision="${TMP_DIR}/sample-list-collision.list"
sample_list_collision_copy="${TMP_DIR}/sample-list-collision.list.copy"
sample_list_collision_log="${TMP_DIR}/sample-list-collision.log"
printf '/tmp/missing-input.root\n' > "${sample_list_collision}"
cp "${sample_list_collision}" "${sample_list_collision_copy}"
capture_failure "${sample_list_collision_log}" \
  "${BUILD_DIR}/bin/mk_sample" \
  "${sample_list_collision}" "${sample_list_collision}" data nominal numi fhc
grep -Fx "mk_sample: sample list and output paths must differ" \
  "${sample_list_collision_log}" >/dev/null
require_unchanged "${sample_list_collision_copy}" "${sample_list_collision}" \
  "mk_sample input list"

sample_root_collision="${TMP_DIR}/sample-root-collision.root"
sample_root_collision_copy="${TMP_DIR}/sample-root-collision.root.copy"
sample_root_collision_list="${TMP_DIR}/sample-root-collision.list"
sample_root_collision_run_db="${TMP_DIR}/sample-root-collision.run.db"
sample_root_collision_log="${TMP_DIR}/sample-root-collision.log"
write_minimal_input_file "${sample_root_collision}"
write_run_db "${sample_root_collision_run_db}"
printf '%s\n' "${sample_root_collision}" > "${sample_root_collision_list}"
cp "${sample_root_collision}" "${sample_root_collision_copy}"
capture_failure "${sample_root_collision_log}" \
  "${BUILD_DIR}/bin/mk_sample" --run-db "${sample_root_collision_run_db}" \
  "${sample_root_collision}" "${sample_root_collision_list}" data nominal numi fhc
grep -Fx "mk_sample: input ROOT and output paths must differ" \
  "${sample_root_collision_log}" >/dev/null
require_unchanged "${sample_root_collision_copy}" "${sample_root_collision}" \
  "mk_sample ROOT input"

dataset_log="${TMP_DIR}/mk_dataset.log"
capture_failure "${dataset_log}" "${BUILD_DIR}/bin/mk_dataset" --run
grep -F "usage: mk_dataset " "${dataset_log}" >/dev/null
grep -Fx "mk_dataset: --run requires a run" "${dataset_log}" >/dev/null

sample_path="${TMP_DIR}/beam=sample.root"
sample_manifest="${TMP_DIR}/beam.sample.manifest"
sample_build_log="${TMP_DIR}/beam.sample.log"
dataset_manifest="${TMP_DIR}/dataset.manifest"
native_dataset_path="${TMP_DIR}/native.dataset.root"
legacy_dataset_path="${TMP_DIR}/legacy.dataset.root"
printf 'beam-shard %s\n' "${sample_root_collision_list}" > "${sample_manifest}"
capture_success "${sample_build_log}" \
  "${BUILD_DIR}/bin/mk_sample" --run-db "${sample_root_collision_run_db}" \
  --sample beam --manifest "${sample_manifest}" \
  "${sample_path}" data nominal numi fhc
printf 'beam %s\n' "${sample_path}" > "${dataset_manifest}"

dataset_manifest_collision="${TMP_DIR}/dataset-manifest-collision.manifest"
dataset_manifest_collision_copy="${TMP_DIR}/dataset-manifest-collision.manifest.copy"
dataset_manifest_collision_log="${TMP_DIR}/dataset-manifest-collision.log"
printf 'beam %s\n' "${sample_path}" > "${dataset_manifest_collision}"
cp "${dataset_manifest_collision}" "${dataset_manifest_collision_copy}"
capture_failure "${dataset_manifest_collision_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --manifest "${dataset_manifest_collision}" \
  "${dataset_manifest_collision}" context
grep -Fx "mk_dataset: manifest and output paths must differ" \
  "${dataset_manifest_collision_log}" >/dev/null
require_unchanged "${dataset_manifest_collision_copy}" "${dataset_manifest_collision}" \
  "mk_dataset manifest"

dataset_sample_collision="${TMP_DIR}/dataset-sample-collision.root"
dataset_sample_collision_copy="${TMP_DIR}/dataset-sample-collision.root.copy"
dataset_sample_collision_output="${TMP_DIR}/dataset-sample-collision-output.root"
dataset_sample_collision_manifest="${TMP_DIR}/dataset-sample-collision.manifest"
dataset_sample_collision_log="${TMP_DIR}/dataset-sample-collision.log"
cp "${sample_path}" "${dataset_sample_collision}"
cp "${dataset_sample_collision}" "${dataset_sample_collision_copy}"
ln -s "${dataset_sample_collision}" "${dataset_sample_collision_output}"
printf 'beam %s\n' "${dataset_sample_collision}" > "${dataset_sample_collision_manifest}"
capture_failure "${dataset_sample_collision_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --manifest "${dataset_sample_collision_manifest}" \
  "${dataset_sample_collision_output}" context
grep -Fx "mk_dataset: sample and output paths must differ" \
  "${dataset_sample_collision_log}" >/dev/null
require_unchanged "${dataset_sample_collision_copy}" "${dataset_sample_collision}" \
  "mk_dataset sample"
if [[ ! -L "${dataset_sample_collision_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_dataset replaced an aliased output symlink\n' >&2
  exit 1
fi

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

dataset_late_failure_manifest="${TMP_DIR}/dataset-late-failure.manifest"
dataset_late_failure_missing_sample="${TMP_DIR}/missing.sample.root"
dataset_late_failure_output="${TMP_DIR}/dataset-late-failure.root"
dataset_late_failure_expected="${TMP_DIR}/dataset-late-failure.root.expected"
dataset_late_failure_log="${TMP_DIR}/dataset-late-failure.log"
printf 'beam %s\nmissing %s\n' \
  "${sample_path}" "${dataset_late_failure_missing_sample}" \
  > "${dataset_late_failure_manifest}"
cp "${native_dataset_path}" "${dataset_late_failure_output}"
cp "${dataset_late_failure_output}" "${dataset_late_failure_expected}"
capture_failure "${dataset_late_failure_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --run run1 --beam numi --polarity fhc \
  --manifest "${dataset_late_failure_manifest}" "${dataset_late_failure_output}"
grep -Fx "mk_dataset: SampleIO: failed to open: ${dataset_late_failure_missing_sample}" \
  "${dataset_late_failure_log}" >/dev/null
require_unchanged "${dataset_late_failure_expected}" "${dataset_late_failure_output}" \
  "mk_dataset existing output after late sample failure"
require_no_temporary_output "${dataset_late_failure_output}" \
  "mk_dataset existing output failure"

dataset_failed_new_output="${TMP_DIR}/dataset-failed-new-output.root"
dataset_failed_new_output_log="${TMP_DIR}/dataset-failed-new-output.log"
capture_failure "${dataset_failed_new_output_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --manifest "${dataset_late_failure_manifest}" \
  "${dataset_failed_new_output}" context
grep -Fx "mk_dataset: SampleIO: failed to open: ${dataset_late_failure_missing_sample}" \
  "${dataset_failed_new_output_log}" >/dev/null
if [[ -e "${dataset_failed_new_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_dataset left a partial output after late sample failure\n' >&2
  exit 1
fi
require_no_temporary_output "${dataset_failed_new_output}" \
  "mk_dataset new output failure"

eventlist_dataset_collision="${TMP_DIR}/eventlist-dataset-collision.root"
eventlist_dataset_collision_copy="${TMP_DIR}/eventlist-dataset-collision.root.copy"
eventlist_dataset_collision_log="${TMP_DIR}/eventlist-dataset-collision.log"
cp "${native_dataset_path}" "${eventlist_dataset_collision}"
cp "${eventlist_dataset_collision}" "${eventlist_dataset_collision_copy}"
capture_failure "${eventlist_dataset_collision_log}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_dataset_collision}" "${eventlist_dataset_collision}"
grep -Fx "mk_eventlist: dataset and output paths must differ" \
  "${eventlist_dataset_collision_log}" >/dev/null
require_unchanged "${eventlist_dataset_collision_copy}" "${eventlist_dataset_collision}" \
  "mk_eventlist dataset"

eventlist_missing_tree_input="${TMP_DIR}/missing-tree-input.root"
eventlist_missing_tree_input_list="${TMP_DIR}/missing-tree-input.list"
eventlist_missing_tree_sample="${TMP_DIR}/missing-tree.sample.root"
eventlist_missing_tree_sample_manifest="${TMP_DIR}/missing-tree.sample.manifest"
eventlist_missing_tree_sample_log="${TMP_DIR}/missing-tree.sample.log"
eventlist_failure_manifest="${TMP_DIR}/eventlist-failure.manifest"
eventlist_failure_dataset="${TMP_DIR}/eventlist-failure.dataset.root"
eventlist_failure_dataset_log="${TMP_DIR}/eventlist-failure-dataset.log"
write_minimal_input_file "${eventlist_missing_tree_input}" false
printf '%s\n' "${eventlist_missing_tree_input}" > "${eventlist_missing_tree_input_list}"
printf 'bad-shard %s\n' "${eventlist_missing_tree_input_list}" \
  > "${eventlist_missing_tree_sample_manifest}"
capture_success "${eventlist_missing_tree_sample_log}" \
  "${BUILD_DIR}/bin/mk_sample" --run-db "${sample_root_collision_run_db}" \
  --sample z_bad --manifest "${eventlist_missing_tree_sample_manifest}" \
  "${eventlist_missing_tree_sample}" data nominal numi fhc
printf 'a_good %s\nz_bad %s\n' "${sample_path}" "${eventlist_missing_tree_sample}" \
  > "${eventlist_failure_manifest}"
capture_success "${eventlist_failure_dataset_log}" \
  "${BUILD_DIR}/bin/mk_dataset" --manifest "${eventlist_failure_manifest}" \
  "${eventlist_failure_dataset}" context

eventlist_late_failure_output="${TMP_DIR}/eventlist-late-failure.root"
eventlist_late_failure_expected="${TMP_DIR}/eventlist-late-failure.root.expected"
eventlist_seed_log="${TMP_DIR}/eventlist-seed.log"
eventlist_late_failure_log="${TMP_DIR}/eventlist-late-failure.log"
capture_success "${eventlist_seed_log}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_late_failure_output}" "${native_dataset_path}"
grep -Fx "mk_eventlist: wrote ${eventlist_late_failure_output} from dataset ${native_dataset_path}" \
  "${eventlist_seed_log}" >/dev/null
cp "${eventlist_late_failure_output}" "${eventlist_late_failure_expected}"
capture_failure "${eventlist_late_failure_log}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_late_failure_output}" "${eventlist_failure_dataset}"
grep -Fx "mk_eventlist: ana::build_event_list: failed to clone event tree structure" \
  "${eventlist_late_failure_log}" >/dev/null
require_unchanged "${eventlist_late_failure_expected}" "${eventlist_late_failure_output}" \
  "mk_eventlist existing output after late sample failure"
require_no_temporary_output "${eventlist_late_failure_output}" \
  "mk_eventlist existing output failure"

eventlist_failed_new_output="${TMP_DIR}/eventlist-failed-new-output.root"
eventlist_failed_new_output_log="${TMP_DIR}/eventlist-failed-new-output.log"
capture_failure "${eventlist_failed_new_output_log}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_failed_new_output}" "${eventlist_failure_dataset}"
grep -Fx "mk_eventlist: ana::build_event_list: failed to clone event tree structure" \
  "${eventlist_failed_new_output_log}" >/dev/null
if [[ -e "${eventlist_failed_new_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_eventlist left a partial output after late sample failure\n' >&2
  exit 1
fi
require_no_temporary_output "${eventlist_failed_new_output}" \
  "mk_eventlist new output failure"

dist_eventlist_collision="${TMP_DIR}/dist-eventlist-collision.root"
dist_eventlist_collision_copy="${TMP_DIR}/dist-eventlist-collision.root.copy"
dist_eventlist_collision_log="${TMP_DIR}/dist-eventlist-collision.log"
cp "${native_dataset_path}" "${dist_eventlist_collision}"
cp "${dist_eventlist_collision}" "${dist_eventlist_collision_copy}"
capture_failure "${dist_eventlist_collision_log}" \
  "${BUILD_DIR}/bin/mk_dist" \
  "${dist_eventlist_collision}" "${dist_eventlist_collision}" beam score 1 0 1
grep -Fx "mk_dist: event list and output paths must differ" \
  "${dist_eventlist_collision_log}" >/dev/null
require_unchanged "${dist_eventlist_collision_copy}" "${dist_eventlist_collision}" \
  "mk_dist event list"

dist_late_failure_output="${TMP_DIR}/dist-late-failure.root"
dist_late_failure_expected="${TMP_DIR}/dist-late-failure.root.expected"
dist_seed_log="${TMP_DIR}/dist-seed.log"
dist_late_failure_manifest="${TMP_DIR}/dist-late-failure.manifest"
dist_late_failure_log="${TMP_DIR}/dist-late-failure.log"
capture_success "${dist_seed_log}" \
  "${BUILD_DIR}/bin/mk_dist" \
  "${dist_late_failure_output}" "${eventlist_late_failure_output}" \
  beam topological_score 2 0 1
grep -Fx "mk_dist: wrote ${dist_late_failure_output} from event list ${eventlist_late_failure_output} for sample beam" \
  "${dist_seed_log}" >/dev/null
cp "${dist_late_failure_output}" "${dist_late_failure_expected}"
cat > "${dist_late_failure_manifest}" <<'EOF'
beam software_trigger 2 0 2 1
missing topological_score 2 0 1 1
EOF
capture_failure "${dist_late_failure_log}" \
  "${BUILD_DIR}/bin/mk_dist" --manifest "${dist_late_failure_manifest}" \
  "${dist_late_failure_output}" "${eventlist_late_failure_output}"
grep -Fx "mk_dist: RootUtils: missing samples/missing" \
  "${dist_late_failure_log}" >/dev/null
require_unchanged "${dist_late_failure_expected}" "${dist_late_failure_output}" \
  "mk_dist existing output after late request failure"
require_no_temporary_output "${dist_late_failure_output}" \
  "mk_dist existing output failure"

dist_failed_new_output="${TMP_DIR}/dist-failed-new-output.root"
dist_failed_new_output_log="${TMP_DIR}/dist-failed-new-output.log"
capture_failure "${dist_failed_new_output_log}" \
  "${BUILD_DIR}/bin/mk_dist" --manifest "${dist_late_failure_manifest}" \
  "${dist_failed_new_output}" "${eventlist_late_failure_output}"
grep -Fx "mk_dist: RootUtils: missing samples/missing" \
  "${dist_failed_new_output_log}" >/dev/null
if [[ -e "${dist_failed_new_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_dist left a partial output after late request failure\n' >&2
  exit 1
fi
require_no_temporary_output "${dist_failed_new_output}" \
  "mk_dist new output failure"

dist_successful_update_manifest="${TMP_DIR}/dist-successful-update.manifest"
dist_successful_update_log="${TMP_DIR}/dist-successful-update.log"
dist_successful_update_cov_log="${TMP_DIR}/dist-successful-update-cov.log"
printf 'beam software_trigger 2 0 2 1\n' > "${dist_successful_update_manifest}"
capture_success "${dist_successful_update_log}" \
  "${BUILD_DIR}/bin/mk_dist" --manifest "${dist_successful_update_manifest}" \
  "${dist_late_failure_output}" "${eventlist_late_failure_output}"
grep -Fx "mk_dist: wrote ${dist_late_failure_output} from event list ${eventlist_late_failure_output} using manifest ${dist_successful_update_manifest} (1 request(s))" \
  "${dist_successful_update_log}" >/dev/null
capture_failure "${dist_successful_update_cov_log}" \
  "${BUILD_DIR}/bin/mk_cov" \
  "${dist_late_failure_output}" beam "${TMP_DIR}/dist-successful-update.cov.root"
grep -Fx "mk_cov: sample has multiple cached distributions; pass --cache-key" \
  "${dist_successful_update_cov_log}" >/dev/null

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

multi_cache_dist="${TMP_DIR}/multi-cache.dists.root"
multi_cache_cov_log="${TMP_DIR}/mk_cov_multi_cache.log"
write_multi_cache_distribution_file "${multi_cache_dist}"
capture_failure "${multi_cache_cov_log}" \
  "${BUILD_DIR}/bin/mk_cov" "${multi_cache_dist}" beam "${TMP_DIR}/multi-cache.cov.root"
grep -Fx "mk_cov: sample has multiple cached distributions; pass --cache-key" \
  "${multi_cache_cov_log}" >/dev/null

printf 'app_cli_parse_runtime_check=ok\n'
