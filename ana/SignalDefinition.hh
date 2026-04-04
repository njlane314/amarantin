#ifndef SIGNAL_DEFINITION_HH
#define SIGNAL_DEFINITION_HH

#include "DatasetIO.hh"

#include <limits>
#include <string>

namespace ana
{
    struct SampleSelectionRule
    {
        const char *expression = nullptr;
        const char *required_branch = nullptr;
    };

    class SignalDefinition
    {
    public:
        enum class MeasurementTruthCategory
        {
            kUnknown = 0,
            kMeasurementSignal = 1,
            kNeutralHyperonOutOfPhaseSpace = 2,
            kOtherStrangeBackground = 3,
            kDetectorSecondaryHyperonBackground = 4,
            kNonstrangeOverlay = 5
        };

        struct FiducialBox
        {
            float active_min_x = 0.0f;
            float active_max_x = 256.35f;
            float active_min_y = -116.5f;
            float active_max_y = 116.5f;
            float active_min_z = 0.0f;
            float active_max_z = 1036.8f;
            float x_start = 5.0f;
            float x_end = 5.35f;
            float y_start = 6.5f;
            float y_end = 6.5f;
            float z_start = 20.0f;
            float z_end = 50.8f;
            float excluded_z_min = 675.0f;
            float excluded_z_max = 775.0f;
        };

        struct MeasurementTruthContract
        {
            bool require_cc_interaction = true;
            bool require_muon_flavour_cc = true;
            bool include_antineutrino = true;
            bool flux_averaged = true;
            bool require_true_vertex_in_fv = true;
            float min_lambda0_p = 0.42f;
            float min_sigma0_p = 0.80f;
        };

        struct TruthInput
        {
            bool is_nu_mu_cc = false;
            int ccnc = -1;
            bool truth_in_fiducial = false;
            float truth_vtx_x = std::numeric_limits<float>::quiet_NaN();
            float truth_vtx_y = std::numeric_limits<float>::quiet_NaN();
            float truth_vtx_z = std::numeric_limits<float>::quiet_NaN();
            float mu_p = std::numeric_limits<float>::quiet_NaN();
            float contained_fraction = std::numeric_limits<float>::quiet_NaN();
        };

        struct StrangeTruthSummary
        {
            bool has_strange_final_state = false;
            bool has_exit_lambda0 = false;
            bool has_exit_sigma0 = false;
            bool has_detector_secondary_lambda0 = false;
            bool has_detector_secondary_lambda0_from_sigma0 = false;
            int qualifying_exit_lambda0_count = 0;
            int qualifying_exit_sigma0_count = 0;
        };

        struct LambdaTruthCandidate
        {
            bool valid = false;
            bool has_ppi_decay = false;
            bool has_sigma0_ancestor = false;
            int lambda_pdg = 0;
            float lambda_p = std::numeric_limits<float>::quiet_NaN();
            float proton_p = std::numeric_limits<float>::quiet_NaN();
            float pion_p = std::numeric_limits<float>::quiet_NaN();
            float decay_sep = std::numeric_limits<float>::quiet_NaN();
            float decay_x = std::numeric_limits<float>::quiet_NaN();
            float decay_y = std::numeric_limits<float>::quiet_NaN();
            float decay_z = std::numeric_limits<float>::quiet_NaN();
            float proton_end_x = std::numeric_limits<float>::quiet_NaN();
            float proton_end_y = std::numeric_limits<float>::quiet_NaN();
            float proton_end_z = std::numeric_limits<float>::quiet_NaN();
            float pion_end_x = std::numeric_limits<float>::quiet_NaN();
            float pion_end_y = std::numeric_limits<float>::quiet_NaN();
            float pion_end_z = std::numeric_limits<float>::quiet_NaN();
        };

    public:
        static const SignalDefinition &canonical();
        static const FiducialBox &canonical_fiducial_box();
        static MeasurementTruthContract canonical_contract();
        static const char *measurement_truth_category_name(MeasurementTruthCategory category);
        static bool in_fiducial(float x,
                                float y,
                                float z,
                                const FiducialBox &box);

        const MeasurementTruthContract &contract() const { return contract_; }
        bool passes(const TruthInput &truth,
                    const LambdaTruthCandidate &candidate) const;
        bool passes_measurement_signal(const TruthInput &truth,
                                      const StrangeTruthSummary &summary) const;
        MeasurementTruthCategory classify_measurement_truth(
            const TruthInput &truth,
            const StrangeTruthSummary &summary) const;
        bool truth_vertex_in_fv(const TruthInput &truth) const;
        std::string describe() const;

    private:
        SignalDefinition() = default;

    private:
        FiducialBox truth_vertex_fv_;
        MeasurementTruthContract contract_;
    };

    SampleSelectionRule sample_selection_rule(const DatasetIO::Sample &sample);
}

#endif // SIGNAL_DEFINITION_HH
