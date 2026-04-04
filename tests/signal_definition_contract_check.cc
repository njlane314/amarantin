#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "DatasetIO.hh"
#include "SampleIO.hh"
#include "SignalDefinition.hh"

namespace
{
    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("signal_definition_contract_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    void require_close(double lhs, double rhs, double tolerance, const std::string &message)
    {
        if (std::fabs(lhs - rhs) > tolerance)
            fail(message);
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
            fail("expected <measurement_contract.json>");

        std::ifstream input(argv[1] ? argv[1] : "");
        require(input.good(), "failed to open measurement contract JSON");

        nlohmann::json contract_json;
        input >> contract_json;

        const auto contract = ana::SignalDefinition::canonical_contract();
        require(contract_json.contains("observable") &&
                    contract_json["observable"]["kind"].is_string(),
                "observable contract should parse");
        require(contract.require_cc_interaction,
                "canonical contract should require charged-current interactions");
        require(contract.require_muon_flavour_cc,
                "canonical contract should require muon-flavour CC");
        require(contract.include_antineutrino,
                "canonical contract should include antineutrino events");
        require(contract.flux_averaged,
                "canonical contract should be flux-averaged over the included beam modes");
        require(contract.require_true_vertex_in_fv,
                "canonical contract should require the true vertex fiducial cut");
        require_close(contract.min_lambda0_p,
                      contract_json["phase_space"]["lambda0_min_p_GeV_c"].get<double>(),
                      1.0e-6,
                      "lambda threshold does not match the JSON contract");
        require_close(contract.min_sigma0_p,
                      contract_json["phase_space"]["sigma0_min_p_GeV_c"].get<double>(),
                      1.0e-6,
                      "sigma threshold does not match the JSON contract");

        require(DatasetIO::Sample::origin_from("strange") ==
                    DatasetIO::Sample::Origin::kSignal,
                "DatasetIO should accept strange as the logical all-strange alias");
        require(SampleIO::origin_from("strange_mc") == SampleIO::Origin::kSignal,
                "SampleIO should accept strange_mc as the logical all-strange alias");

        const ana::SignalDefinition &signal_definition =
            ana::SignalDefinition::canonical();

        ana::SignalDefinition::TruthInput truth;
        truth.is_nu_mu_cc = true;
        truth.ccnc = 0;
        truth.truth_in_fiducial = true;

        ana::SignalDefinition::StrangeTruthSummary measurement_signal;
        measurement_signal.has_strange_final_state = true;
        measurement_signal.has_exit_lambda0 = true;
        measurement_signal.qualifying_exit_lambda0_count = 2;
        require(signal_definition.passes_measurement_signal(truth, measurement_signal),
                "qualifying exit-state Lambda0 events should be signal");
        require(signal_definition.classify_measurement_truth(truth, measurement_signal) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kMeasurementSignal,
                "qualifying exit-state Lambda0 events should map to measurement_signal");

        ana::SignalDefinition::StrangeTruthSummary sigma0_signal;
        sigma0_signal.has_strange_final_state = true;
        sigma0_signal.has_exit_sigma0 = true;
        sigma0_signal.has_detector_secondary_lambda0_from_sigma0 = true;
        sigma0_signal.qualifying_exit_sigma0_count = 1;
        require(signal_definition.passes_measurement_signal(truth, sigma0_signal),
                "qualifying exit-state Sigma0 events should be signal");

        ana::SignalDefinition::StrangeTruthSummary out_of_phase_space;
        out_of_phase_space.has_strange_final_state = true;
        out_of_phase_space.has_exit_lambda0 = true;
        require(!signal_definition.passes_measurement_signal(truth, out_of_phase_space),
                "out-of-phase-space neutral hyperons should not pass the measurement signal");
        require(signal_definition.classify_measurement_truth(truth, out_of_phase_space) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kNeutralHyperonOutOfPhaseSpace,
                "non-qualifying exit-state neutral hyperons should remain explicit background");

        ana::SignalDefinition::StrangeTruthSummary detector_secondary;
        detector_secondary.has_detector_secondary_lambda0 = true;
        require(signal_definition.classify_measurement_truth(truth, detector_secondary) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kDetectorSecondaryHyperonBackground,
                "pure detector-secondary Lambda0 events should be background");

        ana::SignalDefinition::StrangeTruthSummary other_strange;
        other_strange.has_strange_final_state = true;
        require(signal_definition.classify_measurement_truth(truth, other_strange) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kOtherStrangeBackground,
                "non-hyperon strange events should stay in the strange background category");

        ana::SignalDefinition::StrangeTruthSummary nonstrange;
        require(signal_definition.classify_measurement_truth(truth, nonstrange) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kNonstrangeOverlay,
                "nonstrange events should map to nonstrange_overlay");

        ana::SignalDefinition::TruthInput wrong_flavour = truth;
        wrong_flavour.is_nu_mu_cc = false;
        require(!signal_definition.passes_measurement_signal(wrong_flavour, measurement_signal),
                "wrong-flavour events should not pass the measurement signal");
        require(signal_definition.classify_measurement_truth(wrong_flavour, measurement_signal) ==
                    ana::SignalDefinition::MeasurementTruthCategory::kNeutralHyperonOutOfPhaseSpace,
                "wrong-flavour hyperon events should stay explicit background");

        require(std::string(ana::SignalDefinition::measurement_truth_category_name(
                    ana::SignalDefinition::MeasurementTruthCategory::kMeasurementSignal)) ==
                    contract_json["truth_categories"][0].get<std::string>(),
                "measurement truth category labels should stay aligned with the JSON contract");
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
