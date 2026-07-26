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
declare -a BACKGROUND_PIDS=()

cleanup() {
  local process_id
  for process_id in "${BACKGROUND_PIDS[@]}"; do
    if kill -0 "${process_id}" 2>/dev/null; then
      kill -CONT "${process_id}" 2>/dev/null || true
      kill "${process_id}" 2>/dev/null || true
    fi
  done
  for process_id in "${BACKGROUND_PIDS[@]}"; do
    wait "${process_id}" 2>/dev/null || true
  done
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

run_with_file_size_limit() {
  local block_limit=$1
  shift

  (
    trap '' XFSZ
    ulimit -f "${block_limit}"
    "$@"
  ) 2>&1 | cat
}

# Bash expresses RLIMIT_FSIZE in 1024-byte blocks.
file_size_limit_blocks_for() {
  local path=$1
  local byte_count
  byte_count=$(wc -c < "${path}")
  printf '%d\n' "$(( (byte_count + 1023) / 1024 ))"
}

file_size_limit_blocks_below() {
  local path=$1
  local byte_count
  local block_count
  byte_count=$(wc -c < "${path}")
  block_count=$(( (byte_count - 1) / 1024 ))
  printf '%d\n' "$(( block_count > 0 ? block_count : 1 ))"
}

wait_for_temporary_output() {
  local temporary_path=$1
  local process_id=$2
  local log_path=$3
  local attempts=0

  while (( attempts < 500 )); do
    if [[ -e "${temporary_path}" ]]; then
      return
    fi
    if ! kill -0 "${process_id}" 2>/dev/null; then
      wait "${process_id}" 2>/dev/null || true
      printf 'app_cli_parse_runtime_check: writer exited before creating %s\n' \
        "${temporary_path}" >&2
      cat "${log_path}" >&2
      exit 1
    fi
    sleep 0.01
    attempts=$((attempts + 1))
  done

  printf 'app_cli_parse_runtime_check: timed out waiting for %s\n' \
    "${temporary_path}" >&2
  exit 1
}

wait_for_successful_process() {
  local process_id=$1
  local log_path=$2
  local label=$3
  local status=0

  wait "${process_id}" || status=$?
  if [[ ${status} -ne 0 ]]; then
    printf 'app_cli_parse_runtime_check: %s exited with status %d\n' \
      "${label}" "${status}" >&2
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

require_sample_output_path() {
  local sample_path=$1
  local expected_output_path=$2
  local macro_path="${TMP_DIR}/require_sample_output_path.C"

  cat > "${macro_path}" <<'EOF'
#include <stdexcept>
#include <string>

#include "TFile.h"
#include "TNamed.h"

void require_sample_output_path(const char *sample_path,
                                const char *expected_output_path)
{
  TFile file(sample_path, "READ");
  if (file.IsZombie())
    throw std::runtime_error("failed to open sample file");

  const auto *recorded_output =
      dynamic_cast<TNamed *>(file.Get("meta/output_path"));
  if (!recorded_output)
    throw std::runtime_error("sample file has no meta/output_path");
  if (std::string(recorded_output->GetTitle()) != expected_output_path)
    throw std::runtime_error("sample file records the wrong output path");
}
EOF

  root -n -l -b -q \
    "${macro_path}(\"${sample_path}\",\"${expected_output_path}\")"
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

require_distribution_branch() {
  local dist_path=$1
  local sample_key=$2
  local branch_expr=$3
  local macro_path="${TMP_DIR}/require_distribution_branch.C"

  cat > "${macro_path}" <<EOF
#include <stdexcept>
#include <string>

#include "${ROOT_DIR}/io/DistributionIO.hh"

R__LOAD_LIBRARY(${BUILD_DIR}/lib/libIO.so)

void require_distribution_branch(const char *dist_path,
                                 const char *sample_key,
                                 const char *branch_expr)
{
  DistributionIO distributions(dist_path, DistributionIO::Mode::kRead);
  for (const auto &cache_key : distributions.dist_keys(sample_key))
  {
    if (distributions.read(sample_key, cache_key).spec.branch_expr == branch_expr)
      return;
  }
  throw std::runtime_error("distribution branch is missing");
}
EOF

  root -n -l -b -q \
    "${macro_path}(\"${dist_path}\",\"${sample_key}\",\"${branch_expr}\")"
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
require_sample_output_path "${sample_path}" "${sample_path}"

sample_write_failure_output="${TMP_DIR}/sample-write-failure.root"
sample_write_failure_expected="${TMP_DIR}/sample-write-failure.root.expected"
sample_write_failure_log="${TMP_DIR}/sample-write-failure.log"
cp "${sample_path}" "${sample_write_failure_output}"
cp "${sample_write_failure_output}" "${sample_write_failure_expected}"
capture_failure "${sample_write_failure_log}" run_with_file_size_limit 1 \
  "${BUILD_DIR}/bin/mk_sample" --run-db "${sample_root_collision_run_db}" \
  --sample beam --manifest "${sample_manifest}" \
  "${sample_write_failure_output}" data nominal numi fhc
grep -Fx "mk_sample: SampleIO: failed to write: ${sample_write_failure_output}" \
  "${sample_write_failure_log}" >/dev/null
require_unchanged "${sample_write_failure_expected}" \
  "${sample_write_failure_output}" \
  "mk_sample existing output after write failure"
require_no_temporary_output "${sample_write_failure_output}" \
  "mk_sample existing output write failure"

sample_failed_new_output="${TMP_DIR}/sample-failed-new-output.root"
sample_failed_new_output_log="${TMP_DIR}/sample-failed-new-output.log"
capture_failure "${sample_failed_new_output_log}" run_with_file_size_limit 1 \
  "${BUILD_DIR}/bin/mk_sample" --run-db "${sample_root_collision_run_db}" \
  --sample beam --manifest "${sample_manifest}" \
  "${sample_failed_new_output}" data nominal numi fhc
grep -Fx "mk_sample: SampleIO: failed to write: ${sample_failed_new_output}" \
  "${sample_failed_new_output_log}" >/dev/null
if [[ -e "${sample_failed_new_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_sample left a partial output after write failure\n' >&2
  exit 1
fi
require_no_temporary_output "${sample_failed_new_output}" \
  "mk_sample new output write failure"

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

dataset_write_failure_output="${TMP_DIR}/dataset-write-failure.root"
dataset_write_failure_expected="${TMP_DIR}/dataset-write-failure.root.expected"
dataset_write_failure_log="${TMP_DIR}/dataset-write-failure.log"
cp "${native_dataset_path}" "${dataset_write_failure_output}"
cp "${dataset_write_failure_output}" "${dataset_write_failure_expected}"
capture_failure "${dataset_write_failure_log}" run_with_file_size_limit 1 \
  "${BUILD_DIR}/bin/mk_dataset" --run run1 --beam numi --polarity fhc \
  --manifest "${dataset_manifest}" "${dataset_write_failure_output}"
grep -Fx "mk_dataset: DatasetIO: failed to write output file" \
  "${dataset_write_failure_log}" >/dev/null
require_unchanged "${dataset_write_failure_expected}" \
  "${dataset_write_failure_output}" \
  "mk_dataset existing output after write failure"
require_no_temporary_output "${dataset_write_failure_output}" \
  "mk_dataset existing output write failure"

dataset_failed_write_output="${TMP_DIR}/dataset-failed-write-output.root"
dataset_failed_write_output_log="${TMP_DIR}/dataset-failed-write-output.log"
capture_failure "${dataset_failed_write_output_log}" run_with_file_size_limit 1 \
  "${BUILD_DIR}/bin/mk_dataset" --run run1 --beam numi --polarity fhc \
  --manifest "${dataset_manifest}" "${dataset_failed_write_output}"
grep -Fx "mk_dataset: DatasetIO: failed to write output file" \
  "${dataset_failed_write_output_log}" >/dev/null
if [[ -e "${dataset_failed_write_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_dataset left a partial output after write failure\n' >&2
  exit 1
fi
require_no_temporary_output "${dataset_failed_write_output}" \
  "mk_dataset new output write failure"

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

eventlist_write_failure_output="${TMP_DIR}/eventlist-write-failure.root"
eventlist_write_failure_expected="${TMP_DIR}/eventlist-write-failure.root.expected"
eventlist_write_failure_log="${TMP_DIR}/eventlist-write-failure.log"
cp "${eventlist_late_failure_output}" "${eventlist_write_failure_output}"
cp "${eventlist_write_failure_output}" "${eventlist_write_failure_expected}"
eventlist_write_failure_block_limit=$(file_size_limit_blocks_below \
  "${eventlist_write_failure_output}")
capture_failure "${eventlist_write_failure_log}" \
  run_with_file_size_limit "${eventlist_write_failure_block_limit}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_write_failure_output}" "${native_dataset_path}"
grep -Fx "mk_eventlist: EventListIO: failed to write output file" \
  "${eventlist_write_failure_log}" >/dev/null
require_unchanged "${eventlist_write_failure_expected}" \
  "${eventlist_write_failure_output}" \
  "mk_eventlist existing output after write failure"
require_no_temporary_output "${eventlist_write_failure_output}" \
  "mk_eventlist existing output write failure"

eventlist_failed_write_output="${TMP_DIR}/eventlist-failed-write-output.root"
eventlist_failed_write_output_log="${TMP_DIR}/eventlist-failed-write-output.log"
capture_failure "${eventlist_failed_write_output_log}" \
  run_with_file_size_limit "${eventlist_write_failure_block_limit}" \
  "${BUILD_DIR}/bin/mk_eventlist" --selection 1 \
  "${eventlist_failed_write_output}" "${native_dataset_path}"
grep -Fx "mk_eventlist: EventListIO: failed to write output file" \
  "${eventlist_failed_write_output_log}" >/dev/null
if [[ -e "${eventlist_failed_write_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_eventlist left a partial output after write failure\n' >&2
  exit 1
fi
require_no_temporary_output "${eventlist_failed_write_output}" \
  "mk_eventlist new output write failure"

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

dist_write_failure_output="${TMP_DIR}/dist-write-failure.root"
dist_write_failure_expected="${TMP_DIR}/dist-write-failure.root.expected"
dist_write_failure_log="${TMP_DIR}/dist-write-failure.log"
cp "${dist_late_failure_output}" "${dist_write_failure_output}"
cp "${dist_write_failure_output}" "${dist_write_failure_expected}"
dist_write_failure_block_limit=$(file_size_limit_blocks_for \
  "${dist_write_failure_output}")
capture_failure "${dist_write_failure_log}" \
  run_with_file_size_limit "${dist_write_failure_block_limit}" \
  "${BUILD_DIR}/bin/mk_dist" \
  "${dist_write_failure_output}" "${eventlist_late_failure_output}" \
  beam selection_pass 2 0 2
grep -Fx "mk_dist: DistributionIO: failed to write output file" \
  "${dist_write_failure_log}" >/dev/null
require_unchanged "${dist_write_failure_expected}" "${dist_write_failure_output}" \
  "mk_dist existing output after write failure"
require_no_temporary_output "${dist_write_failure_output}" \
  "mk_dist existing output write failure"

dist_failed_write_output="${TMP_DIR}/dist-failed-write-output.root"
dist_failed_write_output_log="${TMP_DIR}/dist-failed-write-output.log"
capture_failure "${dist_failed_write_output_log}" run_with_file_size_limit 1 \
  "${BUILD_DIR}/bin/mk_dist" \
  "${dist_failed_write_output}" "${eventlist_late_failure_output}" \
  beam selection_pass 2 0 2
grep -Fx "mk_dist: DistributionIO: failed to write output file" \
  "${dist_failed_write_output_log}" >/dev/null
if [[ -e "${dist_failed_write_output}" ]]; then
  printf 'app_cli_parse_runtime_check: mk_dist left a partial output after write failure\n' >&2
  exit 1
fi
require_no_temporary_output "${dist_failed_write_output}" \
  "mk_dist new output write failure"

dist_concurrent_output="${TMP_DIR}/dist-concurrent.root"
dist_concurrent_seed_log="${TMP_DIR}/dist-concurrent-seed.log"
dist_concurrent_slow_manifest="${TMP_DIR}/dist-concurrent-slow.manifest"
dist_concurrent_slow_log="${TMP_DIR}/dist-concurrent-slow.log"
dist_concurrent_fast_log="${TMP_DIR}/dist-concurrent-fast.log"
capture_success "${dist_concurrent_seed_log}" \
  "${BUILD_DIR}/bin/mk_dist" \
  "${dist_concurrent_output}" "${eventlist_late_failure_output}" \
  beam topological_score 2 0 1
: > "${dist_concurrent_slow_manifest}"
for ((nbins = 2; nbins <= 101; ++nbins)); do
  printf 'beam software_trigger %d 0 2 1\n' "${nbins}" \
    >> "${dist_concurrent_slow_manifest}"
done

"${BUILD_DIR}/bin/mk_dist" --manifest "${dist_concurrent_slow_manifest}" \
  "${dist_concurrent_output}" "${eventlist_late_failure_output}" \
  >"${dist_concurrent_slow_log}" 2>&1 &
dist_concurrent_slow_pid=$!
BACKGROUND_PIDS=("${dist_concurrent_slow_pid}")
dist_concurrent_slow_temporary="${dist_concurrent_output}.tmp.${dist_concurrent_slow_pid}"
wait_for_temporary_output "${dist_concurrent_slow_temporary}" \
  "${dist_concurrent_slow_pid}" "${dist_concurrent_slow_log}"
kill -STOP "${dist_concurrent_slow_pid}"

"${BUILD_DIR}/bin/mk_dist" \
  "${dist_concurrent_output}" "${eventlist_late_failure_output}" \
  beam selection_pass 2 0 2 >"${dist_concurrent_fast_log}" 2>&1 &
dist_concurrent_fast_pid=$!
BACKGROUND_PIDS+=("${dist_concurrent_fast_pid}")
dist_concurrent_fast_temporary="${dist_concurrent_output}.tmp.${dist_concurrent_fast_pid}"
dist_concurrent_fast_advanced=false
for ((attempt = 0; attempt < 200; ++attempt)); do
  if [[ -e "${dist_concurrent_fast_temporary}" ]] || \
     ! kill -0 "${dist_concurrent_fast_pid}" 2>/dev/null; then
    dist_concurrent_fast_advanced=true
    break
  fi
  sleep 0.01
done

if [[ "${dist_concurrent_fast_advanced}" == true ]]; then
  wait_for_successful_process "${dist_concurrent_fast_pid}" \
    "${dist_concurrent_fast_log}" "fast concurrent mk_dist writer"
fi
kill -CONT "${dist_concurrent_slow_pid}"
wait_for_successful_process "${dist_concurrent_slow_pid}" \
  "${dist_concurrent_slow_log}" "slow concurrent mk_dist writer"
if [[ "${dist_concurrent_fast_advanced}" != true ]]; then
  wait_for_successful_process "${dist_concurrent_fast_pid}" \
    "${dist_concurrent_fast_log}" "fast concurrent mk_dist writer"
fi
BACKGROUND_PIDS=()
require_distribution_branch "${dist_concurrent_output}" beam software_trigger
require_distribution_branch "${dist_concurrent_output}" beam selection_pass
require_no_temporary_output "${dist_concurrent_output}" \
  "concurrent mk_dist update"

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
