#ifndef CHANNELS_HH
#define CHANNELS_HH

#include <cmath>
#include <limits>

namespace channels
{
    enum class Channel
    {
        kUnknown = 0,
        kExternal = 1,
        kOutFV = 2,
        kMuCC0PiGe1P = 10,
        kMuCC1Pi = 11,
        kMuCCPi0OrGamma = 12,
        kMuCCNPi = 13,
        kNC = 14,
        kSignalLambda = 15,
        kMuCCSigma0 = 16,
        kMuCCK0 = 17,
        kECCC = 19,
        kMuCCOther = 20,
        kDataInclusive = 99
    };

    inline int to_int(Channel channel)
    {
        return static_cast<int>(channel);
    }

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

    struct SignalDefinition
    {
        FiducialBox lambda_decay_fv;
        FiducialBox daughter_end_fv;
        bool require_numu_cc = true;
        bool require_truth_vertex_in_fv = true;
        bool require_ppi_decay = true;
        bool require_lambda_decay_in_fv = false;
        bool require_ppi_endpoints_in_fv = false;
        bool require_sigma0_ancestor = false;
        int required_lambda_pdg = 3122;
        float min_muon_p = 0.10f;
        float min_lambda_p = -1.0f;
        float max_lambda_p = -1.0f;
        float min_proton_p = 0.30f;
        float max_proton_p = -1.0f;
        float min_pion_p = 0.10f;
        float max_pion_p = -1.0f;
        float min_lambda_decay_sep = 0.50f;
        float min_reco_contained_fraction = -1.0f;
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

    inline bool in_fiducial(float x, float y, float z, const FiducialBox &box)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;
        return x > (box.active_min_x + box.x_start) &&
               x < (box.active_max_x - box.x_end) &&
               y > (box.active_min_y + box.y_start) &&
               y < (box.active_max_y - box.y_end) &&
               z > (box.active_min_z + box.z_start) &&
               z < (box.active_max_z - box.z_end);
    }

    inline bool passes_signal_definition(bool is_nu_mu_cc,
                                         int ccnc,
                                         bool truth_in_fiducial,
                                         float mu_p,
                                         float contained_fraction,
                                         const LambdaTruthCandidate &cand,
                                         const SignalDefinition &cfg)
    {
        if (!cand.valid)
            return false;
        if (cfg.require_numu_cc && !is_nu_mu_cc)
            return false;
        if (ccnc != 0)
            return false;
        if (cfg.require_truth_vertex_in_fv && !truth_in_fiducial)
            return false;
        if (cfg.required_lambda_pdg != 0 && cand.lambda_pdg != cfg.required_lambda_pdg)
            return false;
        if (cfg.require_ppi_decay && !cand.has_ppi_decay)
            return false;
        if (cfg.require_sigma0_ancestor && !cand.has_sigma0_ancestor)
            return false;
        if (!std::isfinite(mu_p) || mu_p < cfg.min_muon_p)
            return false;
        if (!std::isfinite(cand.lambda_p))
            return false;
        if (cfg.min_lambda_p >= 0.0f && cand.lambda_p < cfg.min_lambda_p)
            return false;
        if (cfg.max_lambda_p >= 0.0f && cand.lambda_p > cfg.max_lambda_p)
            return false;
        if (!std::isfinite(cand.proton_p) || !std::isfinite(cand.pion_p))
            return false;
        if (cfg.min_proton_p >= 0.0f && cand.proton_p < cfg.min_proton_p)
            return false;
        if (cfg.max_proton_p >= 0.0f && cand.proton_p > cfg.max_proton_p)
            return false;
        if (cfg.min_pion_p >= 0.0f && cand.pion_p < cfg.min_pion_p)
            return false;
        if (cfg.max_pion_p >= 0.0f && cand.pion_p > cfg.max_pion_p)
            return false;
        if (!std::isfinite(cand.decay_sep) || cand.decay_sep < cfg.min_lambda_decay_sep)
            return false;
        if (cfg.require_lambda_decay_in_fv &&
            !in_fiducial(cand.decay_x, cand.decay_y, cand.decay_z, cfg.lambda_decay_fv))
            return false;
        if (cfg.require_ppi_endpoints_in_fv &&
            (!in_fiducial(cand.proton_end_x, cand.proton_end_y, cand.proton_end_z, cfg.daughter_end_fv) ||
             !in_fiducial(cand.pion_end_x, cand.pion_end_y, cand.pion_end_z, cfg.daughter_end_fv)))
            return false;
        if (cfg.min_reco_contained_fraction >= 0.0f &&
            (!std::isfinite(contained_fraction) ||
             contained_fraction < cfg.min_reco_contained_fraction))
            return false;
        return true;
    }

    inline bool is_signal(bool is_nu_mu_cc,
                          int ccnc,
                          bool truth_in_fiducial,
                          float mu_p,
                          float contained_fraction,
                          const LambdaTruthCandidate &cand,
                          const SignalDefinition &cfg)
    {
        return passes_signal_definition(is_nu_mu_cc, ccnc, truth_in_fiducial,
                                        mu_p, contained_fraction, cand, cfg);
    }

    inline Channel classify(bool in_fiducial,
                            int nu_pdg,
                            int ccnc,
                            int n_protons,
                            int n_pi_minus,
                            int n_pi_plus,
                            int n_pi0,
                            int n_gamma,
                            int n_k0,
                            int n_sigma0,
                            bool has_sigma0_lambda_ancestor,
                            bool signal_like_lambda)
    {
        const int n_charged_pions = n_pi_minus + n_pi_plus;

        if (!in_fiducial)
        {
            if (nu_pdg == 0)
                return Channel::kExternal;
            return Channel::kOutFV;
        }

        if (ccnc == 1)
            return Channel::kNC;

        if (signal_like_lambda)
            return Channel::kSignalLambda;

        if (std::abs(nu_pdg) == 12 && ccnc == 0)
            return Channel::kECCC;

        if (std::abs(nu_pdg) == 14 && ccnc == 0)
        {
            if (has_sigma0_lambda_ancestor || n_sigma0 > 0)
                return Channel::kMuCCSigma0;
            if (n_k0 > 0)
                return Channel::kMuCCK0;
            if (n_charged_pions == 0 && n_protons > 0)
                return Channel::kMuCC0PiGe1P;
            if (n_charged_pions == 1 && n_pi0 == 0)
                return Channel::kMuCC1Pi;
            if (n_pi0 > 0 || n_gamma >= 2)
                return Channel::kMuCCPi0OrGamma;
            if (n_charged_pions > 1)
                return Channel::kMuCCNPi;
            return Channel::kMuCCOther;
        }

        return Channel::kUnknown;
    }
}

#endif // CHANNELS_HH
