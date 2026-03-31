#ifndef EVENT_CATEGORY_HH
#define EVENT_CATEGORY_HH

#include <cmath>

namespace event_category
{
    enum class EventCategory
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

    inline int to_int(EventCategory category)
    {
        return static_cast<int>(category);
    }

    inline EventCategory classify(bool in_fiducial,
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
                return EventCategory::kExternal;
            return EventCategory::kOutFV;
        }

        if (ccnc == 1)
            return EventCategory::kNC;

        if (signal_like_lambda)
            return EventCategory::kSignalLambda;

        if (std::abs(nu_pdg) == 12 && ccnc == 0)
            return EventCategory::kECCC;

        if (std::abs(nu_pdg) == 14 && ccnc == 0)
        {
            if (has_sigma0_lambda_ancestor || n_sigma0 > 0)
                return EventCategory::kMuCCSigma0;
            if (n_k0 > 0)
                return EventCategory::kMuCCK0;
            if (n_charged_pions == 0 && n_protons > 0)
                return EventCategory::kMuCC0PiGe1P;
            if (n_charged_pions == 1 && n_pi0 == 0)
                return EventCategory::kMuCC1Pi;
            if (n_pi0 > 0 || n_gamma >= 2)
                return EventCategory::kMuCCPi0OrGamma;
            if (n_charged_pions > 1)
                return EventCategory::kMuCCNPi;
            return EventCategory::kMuCCOther;
        }

        return EventCategory::kUnknown;
    }
}

#endif // EVENT_CATEGORY_HH
