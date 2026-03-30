#ifndef ANALYSIS_CHANNELS_HH
#define ANALYSIS_CHANNELS_HH

#include <cmath>

class AnalysisChannels
{
public:
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

    static int to_int(Channel channel)
    {
        return static_cast<int>(channel);
    }

    static bool is_signal(bool is_nu_mu_cc,
                          int ccnc,
                          bool in_fiducial,
                          int lambda_pdg,
                          float mu_p,
                          float proton_p,
                          float pion_p,
                          float lambda_decay_sep)
    {
        constexpr float kMinMuonMomentum = 0.10f;
        constexpr float kMinProtonMomentum = 0.30f;
        constexpr float kMinPionMomentum = 0.10f;
        constexpr float kMinLambdaDecaySeparation = 0.50f;

        if (!is_nu_mu_cc || ccnc != 0 || !in_fiducial)
            return false;
        if (lambda_pdg != 3122)
            return false;
        if (!std::isfinite(mu_p) || !std::isfinite(proton_p) ||
            !std::isfinite(pion_p) || !std::isfinite(lambda_decay_sep))
            return false;
        if (mu_p < kMinMuonMomentum ||
            proton_p < kMinProtonMomentum ||
            pion_p < kMinPionMomentum)
            return false;
        return lambda_decay_sep >= kMinLambdaDecaySeparation;
    }

    static Channel classify(bool in_fiducial,
                            int nu_pdg,
                            int ccnc,
                            int n_protons,
                            int n_pi_minus,
                            int n_pi_plus,
                            int n_pi0,
                            int n_gamma,
                            int n_k0,
                            int n_sigma0,
                            bool is_nu_mu_cc,
                            int lambda_pdg,
                            float mu_p,
                            float proton_p,
                            float pion_p,
                            float lambda_decay_sep)
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

        if (is_signal(is_nu_mu_cc, ccnc, in_fiducial,
                      lambda_pdg, mu_p, proton_p, pion_p, lambda_decay_sep))
            return Channel::kSignalLambda;

        if (std::abs(nu_pdg) == 12 && ccnc == 0)
            return Channel::kECCC;

        if (std::abs(nu_pdg) == 14 && ccnc == 0)
        {
            if (n_sigma0 > 0)
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
};

#endif // ANALYSIS_CHANNELS_HH
