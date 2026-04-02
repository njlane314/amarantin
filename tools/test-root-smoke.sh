#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR_INPUT="${1:-${ROOT_DIR}/build}"
if [[ "${BUILD_DIR_INPUT}" = /* ]]; then
  BUILD_DIR="${BUILD_DIR_INPUT}"
else
  BUILD_DIR="${ROOT_DIR}/${BUILD_DIR_INPUT}"
fi
FIXTURE_PATH="${2:-${ROOT_DIR}/test.root}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amarantin-test-root.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

require_binary() {
  local path=$1
  if [[ ! -x "${path}" ]]; then
    printf 'missing executable %s; build the target first\n' "${path}" >&2
    exit 1
  fi
}

require_file() {
  local path=$1
  if [[ ! -f "${path}" ]]; then
    printf 'missing input file %s\n' "${path}" >&2
    exit 1
  fi
}

require_command() {
  local name=$1
  if ! command -v "${name}" >/dev/null 2>&1; then
    printf 'missing command %s\n' "${name}" >&2
    exit 1
  fi
}

write_fixture_runinfo_rows() {
  local fixture_path=$1
  local rows_path=$2
  local macro_path="${TMP_DIR}/write_fixture_runinfo.C"

  cat > "${macro_path}" <<'EOF'
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

#include <TFile.h>
#include <TTree.h>

void write_fixture_runinfo(const char *fixture_path, const char *rows_path)
{
  TFile input(fixture_path, "READ");
  if (input.IsZombie())
    throw std::runtime_error("failed to open fixture ROOT file");

  auto *tree = dynamic_cast<TTree *>(input.Get("nuselection/SubRun"));
  if (!tree)
    tree = dynamic_cast<TTree *>(input.Get("SubRun"));
  if (!tree)
    throw std::runtime_error("fixture is missing a SubRun tree");

  Int_t run = 0;
  Int_t subRun = 0;
  Double_t pot = 0.0;
  Double_t pot_per_gate = 0.0;
  Long64_t n_beam_gates = 0;

  const bool has_pot = tree->GetBranch("pot") != nullptr;
  const bool has_pot_per_gate = tree->GetBranch("pot_per_gate") != nullptr;
  const bool has_n_beam_gates = tree->GetBranch("n_beam_gates") != nullptr;
  if (!has_pot && !(has_pot_per_gate && has_n_beam_gates))
  {
    throw std::runtime_error(
      "fixture SubRun tree is missing pot and pot_per_gate+n_beam_gates");
  }

  tree->SetBranchAddress("run", &run);
  tree->SetBranchAddress("subRun", &subRun);
  if (has_pot)
    tree->SetBranchAddress("pot", &pot);
  else
  {
    tree->SetBranchAddress("pot_per_gate", &pot_per_gate);
    tree->SetBranchAddress("n_beam_gates", &n_beam_gates);
  }

  std::map<std::pair<int, int>, double> tortgt_by_pair;
  const Long64_t n_entries = tree->GetEntries();
  for (Long64_t i = 0; i < n_entries; ++i)
  {
    tree->GetEntry(i);
    const double generated_exposure =
      has_pot ? static_cast<double>(pot)
              : static_cast<double>(pot_per_gate) * static_cast<double>(n_beam_gates);
    tortgt_by_pair[std::make_pair(static_cast<int>(run), static_cast<int>(subRun))] +=
      generated_exposure / 1.0e12;
  }

  std::ofstream out(rows_path);
  if (!out)
    throw std::runtime_error("failed to create temporary runinfo rows file");

  for (const auto &entry : tortgt_by_pair)
  {
    out << entry.first.first << '\t'
        << entry.first.second << '\t'
        << entry.second << '\n';
  }
}
EOF

  root -n -l -b -q "${macro_path}(\"${fixture_path}\",\"${rows_path}\")"
}

write_fixture_run_db() {
  local fixture_path=$1
  local run_db_path=$2
  local rows_path="${TMP_DIR}/fixture.runinfo.tsv"

  require_command root
  write_fixture_runinfo_rows "${fixture_path}" "${rows_path}"

  if command -v sqlite3 >/dev/null 2>&1; then
    sqlite3 "${run_db_path}" <<SQL
CREATE TABLE runinfo(run INTEGER, subrun INTEGER, tortgt REAL);
.mode tabs
.import '${rows_path}' runinfo
SQL
    return
  fi

  if command -v python3 >/dev/null 2>&1; then
    python3 - "${rows_path}" "${run_db_path}" <<'PY'
import sqlite3
import sys

rows_path, run_db_path = sys.argv[1], sys.argv[2]
connection = sqlite3.connect(run_db_path)
connection.execute("CREATE TABLE runinfo(run INTEGER, subrun INTEGER, tortgt REAL)")
with open(rows_path, "r", encoding="utf-8") as handle:
    rows = [line.rstrip("\n").split("\t") for line in handle if line.strip()]
connection.executemany(
    "INSERT INTO runinfo(run, subrun, tortgt) VALUES (?, ?, ?)",
    rows,
)
connection.commit()
connection.close()
PY
    return
  fi

  printf 'need sqlite3 or python3 to build a temporary run DB\n' >&2
  exit 1
}

run_macro_capture() {
  local log_path=$1
  shift
  AMARANTIN_BUILD_DIR="${BUILD_DIR}" bash "${ROOT_DIR}/tools/run-macro" "$@" >"${log_path}" 2>&1
}

check_truth_has_strange_split() {
  local fixture_path=$1
  local overlay_eventlist_path=$2
  local signal_eventlist_path=$3
  local macro_path="${TMP_DIR}/check_truth_has_strange_split.C"

  cat > "${macro_path}" <<'EOF'
#include <stdexcept>
#include <string>

#include <TFile.h>
#include <TTree.h>

namespace
{
  TTree *require_tree(TFile &file, const char *primary, const char *fallback)
  {
    auto *tree = dynamic_cast<TTree *>(file.Get(primary));
    if (!tree && fallback)
      tree = dynamic_cast<TTree *>(file.Get(fallback));
    if (!tree)
      throw std::runtime_error(std::string("missing tree ") + primary);
    return tree;
  }

  void require_exact_split(const char *label,
                           TTree *tree,
                           Long64_t expected_entries,
                           const char *unexpected_expr)
  {
    if (!tree->GetBranch("truth_has_strange_fs"))
      throw std::runtime_error(std::string(label) + " selected tree is missing truth_has_strange_fs");

    const Long64_t entries = tree->GetEntries();
    if (entries != expected_entries)
    {
      throw std::runtime_error(std::string(label) + " selected tree entry mismatch: got " +
                               std::to_string(entries) + ", expected " +
                               std::to_string(expected_entries));
    }

    const Long64_t unexpected_entries = tree->GetEntries(unexpected_expr);
    if (unexpected_entries != 0)
    {
      throw std::runtime_error(std::string(label) + " selected tree contains " +
                               std::to_string(unexpected_entries) +
                               " events from the wrong strange-truth side");
    }
  }
}

void check_truth_has_strange_split(const char *fixture_path,
                                   const char *overlay_eventlist_path,
                                   const char *signal_eventlist_path)
{
  TFile fixture(fixture_path, "READ");
  if (fixture.IsZombie())
    throw std::runtime_error("failed to open fixture ROOT file");

  TTree *source = require_tree(fixture,
                               "nuselection/EventSelectionFilter",
                               "EventSelectionFilter");
  if (!source->GetBranch("truth_has_strange_fs"))
    throw std::runtime_error("fixture event tree is missing truth_has_strange_fs");

  const Long64_t fixture_entries = source->GetEntries();
  const Long64_t expected_overlay = source->GetEntries("truth_has_strange_fs == 0");
  const Long64_t expected_signal = source->GetEntries("truth_has_strange_fs != 0");
  if (expected_overlay + expected_signal != fixture_entries)
    throw std::runtime_error("fixture strange split does not partition the event tree");
  if (expected_overlay <= 0 || expected_signal <= 0)
    throw std::runtime_error("fixture strange split is not informative");

  TFile overlay_file(overlay_eventlist_path, "READ");
  if (overlay_file.IsZombie())
    throw std::runtime_error("failed to open overlay eventlist");

  TFile signal_file(signal_eventlist_path, "READ");
  if (signal_file.IsZombie())
    throw std::runtime_error("failed to open signal eventlist");

  TTree *overlay = require_tree(overlay_file,
                                "samples/beam/events/selected",
                                "samples/beam/selected");
  TTree *signal = require_tree(signal_file,
                               "samples/beam/events/selected",
                               "samples/beam/selected");

  require_exact_split("overlay", overlay, expected_overlay, "truth_has_strange_fs != 0");
  require_exact_split("signal", signal, expected_signal, "truth_has_strange_fs == 0");
}
EOF

  root -n -l -b -q "${macro_path}(\"${fixture_path}\",\"${overlay_eventlist_path}\",\"${signal_eventlist_path}\")"
}

check_snapshot_output() {
  local snapshot_path=$1
  local macro_path="${TMP_DIR}/check_snapshot.C"

  cat > "${macro_path}" <<'EOF'
#include <stdexcept>

#include <TFile.h>
#include <TTree.h>

void check_snapshot_output(const char *snapshot_path)
{
  TFile input(snapshot_path, "READ");
  if (input.IsZombie())
    throw std::runtime_error("failed to open snapshot output");

  auto *tree = dynamic_cast<TTree *>(input.Get("train"));
  if (!tree)
    throw std::runtime_error("snapshot output is missing train");
  if (tree->GetEntries() <= 0)
    throw std::runtime_error("snapshot output is empty");
  if (!tree->GetBranch("topological_score"))
    throw std::runtime_error("snapshot output is missing topological_score");
  if (!tree->GetBranch("__w__"))
    throw std::runtime_error("snapshot output is missing __w__");
}
EOF

  root -n -l -b -q "${macro_path}(\"${snapshot_path}\")"
}

require_file "${FIXTURE_PATH}"
require_binary "${BUILD_DIR}/bin/mk_sample"
require_binary "${BUILD_DIR}/bin/mk_dataset"
require_binary "${BUILD_DIR}/bin/mk_eventlist"
require_binary "${BUILD_DIR}/bin/mk_dist"
require_binary "${BUILD_DIR}/bin/mk_fit"
require_binary "${BUILD_DIR}/bin/mk_cov"
require_binary "${BUILD_DIR}/bin/testroot_pipeline_check"
require_file "${ROOT_DIR}/tools/run-macro"

LIST_PATH="${TMP_DIR}/fixture.list"
SAMPLE_MANIFEST="${TMP_DIR}/fixture.sample.manifest"
DATASET_MANIFEST="${TMP_DIR}/fixture.dataset.manifest"
FIT_MANIFEST="${TMP_DIR}/fixture.fit.manifest"
SAMPLE_PATH="${TMP_DIR}/fixture.sample.root"
DATASET_PATH="${TMP_DIR}/fixture.dataset.root"
EVENTLIST_PATH="${TMP_DIR}/fixture.eventlist.root"
PRESET_EVENTLIST_PATH="${TMP_DIR}/fixture.eventlist.muon.root"
OVERLAY_SAMPLE_PATH="${TMP_DIR}/fixture.overlay.sample.root"
OVERLAY_DATASET_MANIFEST="${TMP_DIR}/fixture.overlay.dataset.manifest"
OVERLAY_DATASET_PATH="${TMP_DIR}/fixture.overlay.dataset.root"
OVERLAY_EVENTLIST_PATH="${TMP_DIR}/fixture.overlay.eventlist.root"
SIGNAL_SAMPLE_PATH="${TMP_DIR}/fixture.signal.sample.root"
SIGNAL_DATASET_MANIFEST="${TMP_DIR}/fixture.signal.dataset.manifest"
SIGNAL_DATASET_PATH="${TMP_DIR}/fixture.signal.dataset.root"
SIGNAL_EVENTLIST_PATH="${TMP_DIR}/fixture.signal.eventlist.root"
DIST_PATH="${TMP_DIR}/fixture.dists.root"
FIT_PATH="${TMP_DIR}/fixture.fit.txt"
COV_PATH="${TMP_DIR}/fixture.cov.root"
EFFICIENCY_PLOT_PATH="${TMP_DIR}/fixture.efficiency.png"
SNAPSHOT_PATH="${TMP_DIR}/fixture.snapshot.root"
ROWPLOT_PATH="${TMP_DIR}/fixture.rowplot.png"
MACRO_SNAPSHOT_PATH="${TMP_DIR}/fixture.macro.snapshot.root"
RUN_DB_PATH="${TMP_DIR}/fixture.run.db"

PRINT_SAMPLE_LOG="${TMP_DIR}/print_sample.log"
PRINT_DATASET_LOG="${TMP_DIR}/print_dataset.log"
PRINT_EVENTLIST_LOG="${TMP_DIR}/print_eventlist.log"
INSPECT_WEIGHTS_LOG="${TMP_DIR}/inspect_weights.log"
INSPECT_CUTFLOW_LOG="${TMP_DIR}/inspect_cutflow.log"
INSPECT_CATEGORIES_LOG="${TMP_DIR}/inspect_categories.log"
INSPECT_DIST_LOG="${TMP_DIR}/inspect_dist.log"
INSPECT_SYSTEMATICS_LOG="${TMP_DIR}/inspect_systematics.log"
INSPECT_COVARIANCE_LOG="${TMP_DIR}/inspect_covariance.log"
SNAPSHOT_MACRO_LOG="${TMP_DIR}/mk_snapshot.log"

printf '%s\n' "${FIXTURE_PATH}" > "${LIST_PATH}"
printf 'beam-s0 %s\n' "${LIST_PATH}" > "${SAMPLE_MANIFEST}"
printf 'beam %s\n' "${SAMPLE_PATH}" > "${DATASET_MANIFEST}"
cat > "${FIT_MANIFEST}" <<'EOF'
signal signal beam
observed data beam
EOF
write_fixture_run_db "${FIXTURE_PATH}" "${RUN_DB_PATH}"

"${BUILD_DIR}/bin/mk_sample" \
  --run-db "${RUN_DB_PATH}" \
  --sample beam \
  --manifest "${SAMPLE_MANIFEST}" \
  "${SAMPLE_PATH}" \
  external nominal numi fhc

"${BUILD_DIR}/bin/mk_dataset" \
  --run run1 \
  --beam numi \
  --polarity fhc \
  --manifest "${DATASET_MANIFEST}" \
  "${DATASET_PATH}"

"${BUILD_DIR}/bin/mk_eventlist" \
  --event-tree nuselection/EventSelectionFilter \
  --subrun-tree nuselection/SubRun \
  --selection 1 \
  "${EVENTLIST_PATH}" \
  "${DATASET_PATH}"

"${BUILD_DIR}/bin/mk_eventlist" \
  --event-tree nuselection/EventSelectionFilter \
  --subrun-tree nuselection/SubRun \
  --preset muon \
  "${PRESET_EVENTLIST_PATH}" \
  "${DATASET_PATH}"

printf 'beam %s\n' "${OVERLAY_SAMPLE_PATH}" > "${OVERLAY_DATASET_MANIFEST}"
"${BUILD_DIR}/bin/mk_sample" \
  --run-db "${RUN_DB_PATH}" \
  --sample beam \
  --manifest "${SAMPLE_MANIFEST}" \
  "${OVERLAY_SAMPLE_PATH}" \
  overlay nominal numi fhc

"${BUILD_DIR}/bin/mk_dataset" \
  --run run1 \
  --beam numi \
  --polarity fhc \
  --manifest "${OVERLAY_DATASET_MANIFEST}" \
  "${OVERLAY_DATASET_PATH}"

"${BUILD_DIR}/bin/mk_eventlist" \
  --event-tree nuselection/EventSelectionFilter \
  --subrun-tree nuselection/SubRun \
  --selection 1 \
  "${OVERLAY_EVENTLIST_PATH}" \
  "${OVERLAY_DATASET_PATH}"

printf 'beam %s\n' "${SIGNAL_SAMPLE_PATH}" > "${SIGNAL_DATASET_MANIFEST}"
"${BUILD_DIR}/bin/mk_sample" \
  --run-db "${RUN_DB_PATH}" \
  --sample beam \
  --manifest "${SAMPLE_MANIFEST}" \
  "${SIGNAL_SAMPLE_PATH}" \
  signal nominal numi fhc

"${BUILD_DIR}/bin/mk_dataset" \
  --run run1 \
  --beam numi \
  --polarity fhc \
  --manifest "${SIGNAL_DATASET_MANIFEST}" \
  "${SIGNAL_DATASET_PATH}"

"${BUILD_DIR}/bin/mk_eventlist" \
  --event-tree nuselection/EventSelectionFilter \
  --subrun-tree nuselection/SubRun \
  --selection 1 \
  "${SIGNAL_EVENTLIST_PATH}" \
  "${SIGNAL_DATASET_PATH}"

check_truth_has_strange_split \
  "${FIXTURE_PATH}" \
  "${OVERLAY_EVENTLIST_PATH}" \
  "${SIGNAL_EVENTLIST_PATH}"

"${BUILD_DIR}/bin/mk_dist" \
  --selection "selection_pass != 0" \
  --genie \
  --genie-knobs \
  --flux \
  --reint \
  "${DIST_PATH}" \
  "${EVENTLIST_PATH}" \
  beam \
  topological_score \
  10 0 1

"${BUILD_DIR}/bin/mk_fit" \
  --manifest "${FIT_MANIFEST}" \
  --output "${FIT_PATH}" \
  "${DIST_PATH}" \
  beam_fit

"${BUILD_DIR}/bin/mk_cov" \
  "${DIST_PATH}" \
  beam \
  "${COV_PATH}"

"${BUILD_DIR}/bin/testroot_pipeline_check" \
  "${SAMPLE_PATH}" \
  "${DATASET_PATH}" \
  "${EVENTLIST_PATH}" \
  "${PRESET_EVENTLIST_PATH}" \
  "${DIST_PATH}" \
  "${COV_PATH}" \
  "${FIT_PATH}" \
  "${EFFICIENCY_PLOT_PATH}" \
  "${SNAPSHOT_PATH}" \
  "${ROWPLOT_PATH}" \
  "${FIXTURE_PATH}"

run_macro_capture "${PRINT_SAMPLE_LOG}" print_sample "${SAMPLE_PATH}"
grep -F "origin=external beam=numi polarity=fhc shards=1" "${PRINT_SAMPLE_LOG}" >/dev/null

run_macro_capture "${PRINT_DATASET_LOG}" print_dataset "${DATASET_PATH}"
grep -F "samples: 1" "${PRINT_DATASET_LOG}" >/dev/null
grep -F "sample=beamorigin=external" "${PRINT_DATASET_LOG}" >/dev/null

run_macro_capture "${PRINT_EVENTLIST_LOG}" print_eventlist "${EVENTLIST_PATH}"
grep -F "Sample: beam" "${PRINT_EVENTLIST_LOG}" >/dev/null
grep -F "selection_pass=" "${PRINT_EVENTLIST_LOG}" >/dev/null

run_macro_capture "${INSPECT_WEIGHTS_LOG}" inspect_weights "${EVENTLIST_PATH}" beam
grep -F "sample=beam origin=external variation=nominal nominal=- entries=" "${INSPECT_WEIGHTS_LOG}" >/dev/null
grep -F "nonfinite_w=0 inconsistent_w=0" "${INSPECT_WEIGHTS_LOG}" >/dev/null

run_macro_capture "${INSPECT_CUTFLOW_LOG}" inspect_cutflow "${EVENTLIST_PATH}" beam
grep -F "sample=beam origin=external variation=nominal selection_name=raw" "${INSPECT_CUTFLOW_LOG}" >/dev/null
grep -F "stage=trigger passed=" "${INSPECT_CUTFLOW_LOG}" >/dev/null
grep -F "stage=selection_pass passed=" "${INSPECT_CUTFLOW_LOG}" >/dev/null

run_macro_capture "${INSPECT_CATEGORIES_LOG}" inspect_categories "${EVENTLIST_PATH}" beam
grep -F "sample=beam origin=external variation=nominal" "${INSPECT_CATEGORIES_LOG}" >/dev/null
grep -F "category=1 label=external" "${INSPECT_CATEGORIES_LOG}" >/dev/null

run_macro_capture "${INSPECT_DIST_LOG}" inspect_dist "${DIST_PATH}" beam
grep -F "branch=topological_score" "${INSPECT_DIST_LOG}" >/dev/null
grep -F "selection=selection_pass != 0" "${INSPECT_DIST_LOG}" >/dev/null

CACHE_KEY="$(sed -n 's/^cache_key=//p' "${INSPECT_DIST_LOG}" | head -n 1)"
if [[ -z "${CACHE_KEY}" ]]; then
  printf 'failed to recover cache_key from inspect_dist output\n' >&2
  exit 1
fi

DIST_PLOT_PATH="${ROOT_DIR}/dist_beam_${CACHE_KEY}.png"
trap 'rm -rf "${TMP_DIR}"; rm -f "${DIST_PLOT_PATH}"' EXIT
require_file "${DIST_PLOT_PATH}"

run_macro_capture "${INSPECT_SYSTEMATICS_LOG}" inspect_systematics "${DIST_PATH}" beam "${CACHE_KEY}"
grep -F "sample=beam cache_key=${CACHE_KEY} nbins=10 branch=topological_score selection=selection_pass != 0" "${INSPECT_SYSTEMATICS_LOG}" >/dev/null
grep -F "genie branch=weightsGenie" "${INSPECT_SYSTEMATICS_LOG}" >/dev/null
grep -F "flux branch=weightsPPFX" "${INSPECT_SYSTEMATICS_LOG}" >/dev/null
grep -F "reint branch=weightsReint" "${INSPECT_SYSTEMATICS_LOG}" >/dev/null
grep -F "genie_knob_source_count=0" "${INSPECT_SYSTEMATICS_LOG}" >/dev/null

run_macro_capture "${INSPECT_COVARIANCE_LOG}" inspect_covariance "${COV_PATH}"
grep -F "matrix=abs_covariance type=double rows=10 cols=10" "${INSPECT_COVARIANCE_LOG}" >/dev/null
grep -F "matrix=frac_covariance type=float rows=10 cols=10" "${INSPECT_COVARIANCE_LOG}" >/dev/null

run_macro_capture "${SNAPSHOT_MACRO_LOG}" \
  mk_snapshot \
  "${EVENTLIST_PATH}" \
  "${MACRO_SNAPSHOT_PATH}" \
  "topological_score,__w__" \
  "selection_pass != 0"
grep -F "snapshot entries=" "${SNAPSHOT_MACRO_LOG}" >/dev/null
check_snapshot_output "${MACRO_SNAPSHOT_PATH}"

printf 'test_root_smoke=ok\n'
