#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/amarantin-sbnfit-export.XXXXXX")"
trap 'rm -rf "${TMP_DIR}"' EXIT

require_library() {
  local stem=$1
  if [[ ! -e "${ROOT_DIR}/build/lib/lib${stem}.so" && ! -e "${ROOT_DIR}/build/lib/lib${stem}.dylib" ]]; then
    printf 'missing build/lib/lib%s.{so,dylib}; build the target first\n' "${stem}" >&2
    exit 1
  fi
}

require_binary() {
  local path=$1
  if [[ ! -x "${path}" ]]; then
    printf 'missing executable %s; build the target first\n' "${path}" >&2
    exit 1
  fi
}

if ! command -v root-config >/dev/null 2>&1; then
  printf 'root-config not found on PATH\n' >&2
  exit 1
fi

require_library "IO"
require_binary "${ROOT_DIR}/build/bin/mk_sbnfit_cov"

SOURCE="${TMP_DIR}/sbnfit_export_smoke.cc"
BINARY="${TMP_DIR}/sbnfit_export_smoke"
DIST_PATH="${TMP_DIR}/stacked-smoke.dists.root"
GOOD_MANIFEST="${TMP_DIR}/stacked-good.manifest"
BAD_MANIFEST="${TMP_DIR}/stacked-bad.manifest"
GOOD_OUTPUT="${TMP_DIR}/stacked-good.root"
BAD_OUTPUT="${TMP_DIR}/stacked-bad.root"
FAIL_LOG="${TMP_DIR}/stacked-bad.log"

cat >"${SOURCE}" <<'EOF'
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

#include <TFile.h>
#include <TMatrixT.h>
#include <TTree.h>

namespace
{
    bool approx(double lhs, double rhs)
    {
        return std::fabs(lhs - rhs) < 1e-9;
    }

    DistributionIO::Spectrum make_signal_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "signal";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.selection_expr = "selected != 0";
        spectrum.spec.cache_key = "smoke";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;
        spectrum.nominal = {10.0, 20.0};
        spectrum.sumw2 = {10.0, 20.0};

        spectrum.detector_source_labels = {"sce", "wiremod"};
        spectrum.detector_shift_vectors = {
            1.0, 0.0,
            0.0, 1.0
        };
        spectrum.detector_source_count = 2;

        spectrum.genie_knob_source_labels = {"shared_knob", "signal_only_knob"};
        spectrum.genie_knob_shift_vectors = {
            0.5, 0.0,
            0.0, 1.0
        };
        spectrum.genie_knob_source_count = 2;

        spectrum.genie.branch_name = "weightsGenie";
        spectrum.genie.n_variations = 2;
        spectrum.genie.universe_histograms = {
            11.0, 9.0,
            20.0, 22.0
        };

        return spectrum;
    }

    DistributionIO::Spectrum make_beam_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam-bkg";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.selection_expr = "selected != 0";
        spectrum.spec.cache_key = "smoke";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;
        spectrum.nominal = {5.0, 7.0};
        spectrum.sumw2 = {5.0, 7.0};

        spectrum.detector_source_labels = {"sce", "wiremodx"};
        spectrum.detector_shift_vectors = {
            2.0, 0.0,
            0.0, 3.0
        };
        spectrum.detector_source_count = 2;

        spectrum.genie_knob_source_labels = {"shared_knob", "beam_only_knob"};
        spectrum.genie_knob_shift_vectors = {
            1.0, 0.0,
            0.0, 2.0
        };
        spectrum.genie_knob_source_count = 2;

        spectrum.genie.branch_name = "weightsGenie";
        spectrum.genie.n_variations = 2;
        spectrum.genie.universe_histograms = {
            6.0, 4.0,
            7.0, 8.0
        };

        return spectrum;
    }

    DistributionIO::Spectrum make_bad_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "bad-genie";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.selection_expr = "selected != 0";
        spectrum.spec.cache_key = "smoke";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;
        spectrum.nominal = {3.0, 4.0};
        spectrum.sumw2 = {3.0, 4.0};

        spectrum.genie.branch_name = "weightsGenieMismatch";
        spectrum.genie.n_variations = 2;
        spectrum.genie.universe_histograms = {
            4.0, 2.0,
            4.0, 5.0
        };

        return spectrum;
    }

    void write_fixture(const std::string &path)
    {
        DistributionIO dist(path, DistributionIO::Mode::kWrite);

        DistributionIO::Metadata metadata;
        metadata.eventlist_path = "synthetic.eventlist.root";
        metadata.build_version = 1;
        dist.write_metadata(metadata);

        dist.write("signal", "smoke", make_signal_spectrum());
        dist.write("beam-bkg", "smoke", make_beam_spectrum());
        dist.write("bad-genie", "smoke", make_bad_spectrum());
        dist.flush();
    }

    template <class TObjectType>
    TObjectType *must_get(TFile &input, const std::string &name)
    {
        TObject *object = input.Get(name.c_str());
        if (!object)
            throw std::runtime_error("missing object: " + name);
        auto *typed = dynamic_cast<TObjectType *>(object);
        if (!typed)
            throw std::runtime_error("unexpected object type: " + name);
        return typed;
    }

    void expect_matrix_value(const TMatrixT<double> &matrix,
                             int row,
                             int col,
                             double expected,
                             const std::string &label)
    {
        if (!approx(matrix(row, col), expected))
        {
            throw std::runtime_error(
                label + " mismatch at (" + std::to_string(row) + "," +
                std::to_string(col) + ")");
        }
    }

    void check_output(const std::string &path)
    {
        TFile input(path.c_str(), "READ");
        if (input.IsZombie())
            throw std::runtime_error("failed to open export file");

        auto *fractional = must_get<TMatrixT<float>>(input, "frac_covariance");
        auto *absolute = must_get<TMatrixT<double>>(input, "abs_covariance");
        auto *detector = must_get<TMatrixT<double>>(input, "detector_covariance");
        auto *genie_knobs = must_get<TMatrixT<double>>(input, "genie_knobs_covariance");
        auto *genie = must_get<TMatrixT<double>>(input, "genie_covariance");
        auto *manifest = must_get<TTree>(input, "stack_manifest");

        if (fractional->GetNrows() != 4 || fractional->GetNcols() != 4)
            throw std::runtime_error("unexpected fractional covariance dimensions");
        if (absolute->GetNrows() != 4 || absolute->GetNcols() != 4)
            throw std::runtime_error("unexpected absolute covariance dimensions");

        expect_matrix_value(*detector, 0, 2, 2.0, "detector covariance");
        expect_matrix_value(*genie_knobs, 0, 2, 0.5, "genie_knob covariance");
        expect_matrix_value(*genie, 0, 2, 1.0, "genie covariance");
        expect_matrix_value(*absolute, 0, 2, 3.5, "total absolute covariance");
        expect_matrix_value(*absolute, 1, 3, 1.0, "total absolute covariance");

        if (manifest->GetEntries() != 2)
            throw std::runtime_error("unexpected stack_manifest entry count");

        Int_t bin_offset = -1;
        Int_t nbins = -1;
        manifest->SetBranchAddress("bin_offset", &bin_offset);
        manifest->SetBranchAddress("nbins", &nbins);

        manifest->GetEntry(0);
        if (bin_offset != 0 || nbins != 2)
            throw std::runtime_error("unexpected first stack_manifest row");

        manifest->GetEntry(1);
        if (bin_offset != 2 || nbins != 2)
            throw std::runtime_error("unexpected second stack_manifest row");
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("expected <write|check> <path>");

        const std::string mode = argv[1] ? argv[1] : "";
        const std::string path = argv[2] ? argv[2] : "";
        if (mode == "write")
        {
            write_fixture(path);
            return 0;
        }
        if (mode == "check")
        {
            check_output(path);
            return 0;
        }

        throw std::runtime_error("unknown mode: " + mode);
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
EOF

read -r -a ROOT_CFLAGS <<<"$(root-config --cflags)"
read -r -a ROOT_LIBS <<<"$(root-config --libs)"

"${CXX:-c++}" \
  -std=c++17 \
  -I"${ROOT_DIR}/io" \
  "${ROOT_CFLAGS[@]}" \
  "${SOURCE}" \
  -L"${ROOT_DIR}/build/lib" \
  -Wl,-rpath,"${ROOT_DIR}/build/lib" \
  -lIO \
  "${ROOT_LIBS[@]}" \
  -o "${BINARY}"

export DYLD_LIBRARY_PATH="${ROOT_DIR}/build/lib${DYLD_LIBRARY_PATH:+:${DYLD_LIBRARY_PATH}}"
export LD_LIBRARY_PATH="${ROOT_DIR}/build/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

"${BINARY}" write "${DIST_PATH}"

cat >"${GOOD_MANIFEST}" <<'EOF'
signal signal smoke
beam_bkg beam-bkg smoke
EOF

cat >"${BAD_MANIFEST}" <<'EOF'
signal signal smoke
bad bad-genie smoke
EOF

"${ROOT_DIR}/build/bin/mk_sbnfit_cov" \
  --manifest "${GOOD_MANIFEST}" \
  "${DIST_PATH}" \
  "${GOOD_OUTPUT}"

if "${ROOT_DIR}/build/bin/mk_sbnfit_cov" \
  --manifest "${BAD_MANIFEST}" \
  "${DIST_PATH}" \
  "${BAD_OUTPUT}" >"${FAIL_LOG}" 2>&1; then
  printf 'expected stacked export with mismatched family metadata to fail\n' >&2
  exit 1
fi

if ! grep -q "matching family branch names" "${FAIL_LOG}"; then
  printf 'missing expected rejection message in %s\n' "${FAIL_LOG}" >&2
  exit 1
fi

"${BINARY}" check "${GOOD_OUTPUT}"

printf 'systematics_sbnfit_export_smoke=ok\n'
