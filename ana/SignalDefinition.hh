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
        struct FiducialBox
        {
            float active_min_x = 0.0f;
            float active_max_x = 256.35f;
            float active_min_y = -116.5f;
            float active_max_y = 116.5f;
            float active_min_z = 0.0f;
            float active_max_z = 1036.8f;
            float x_start = 10.0f;
            float x_end = 10.0f;
            float y_start = 15.0f;
            float y_end = 15.0f;
            float z_start = 10.0f;
            float z_end = 50.0f;
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

        bool passes(const TruthInput &truth,
                    const LambdaTruthCandidate &candidate) const;
        std::string describe() const;

    private:
        SignalDefinition() = default;

        static bool in_fiducial(float x,
                                float y,
                                float z,
                                const FiducialBox &box);
        bool truth_vertex_in_fv(const TruthInput &truth) const;

    private:
        FiducialBox truth_vertex_fv_;
        FiducialBox lambda_decay_fv_;
        FiducialBox daughter_end_fv_;
        bool require_cc_interaction_ = true;
        bool require_numu_cc_ = true;
        bool require_truth_vertex_in_fv_ = true;
        bool require_ppi_decay_ = true;
        bool require_lambda_decay_in_fv_ = false;
        bool require_ppi_endpoints_in_fv_ = false;
        bool require_sigma0_ancestor_ = false;
        int required_lambda_pdg_ = 3122;
        float min_muon_p_ = 0.10f;
        float min_lambda_p_ = -1.0f;
        float max_lambda_p_ = -1.0f;
        float min_proton_p_ = 0.30f;
        float max_proton_p_ = -1.0f;
        float min_pion_p_ = 0.10f;
        float max_pion_p_ = -1.0f;
        float min_lambda_decay_sep_ = 0.50f;
        float min_reco_contained_fraction_ = -1.0f;
    };

    SampleSelectionRule sample_selection_rule(const DatasetIO::Sample &sample);
}

#endif // SIGNAL_DEFINITION_HH
