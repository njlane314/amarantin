#ifndef XSEC_FIT_HH
#define XSEC_FIT_HH

#include "SignalStrengthFit.hh"

namespace fit
{
    using Model = Problem;

    inline Problem make_independent_model(const ChannelIO::Channel &channel,
                                          const std::string &signal_process,
                                          double mu_start = 1.0,
                                          double mu_upper = 5.0)
    {
        return make_independent_problem(channel, signal_process, mu_start, mu_upper);
    }

    inline Result profile_xsec(const Problem &problem,
                               const FitOptions &options = FitOptions{})
    {
        return profile_signal_strength(problem, options);
    }
}

#endif // XSEC_FIT_HH
