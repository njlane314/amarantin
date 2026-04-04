#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "SignalStrengthFit.hh"

namespace
{
    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error(message);
    }

    fit::Process make_signal_process()
    {
        fit::Process signal;
        signal.name = "signal";
        signal.kind = fit::ProcessKind::kSignal;
        signal.nominal = {5.0, 4.0};
        signal.sumw2 = {1.0, 1.0};
        return signal;
    }

    fit::Process make_background_process(const std::vector<double> &nominal,
                                         const std::vector<double> &sumw2)
    {
        fit::Process background;
        background.name = "background";
        background.kind = fit::ProcessKind::kBackground;
        background.nominal = nominal;
        background.sumw2 = sumw2;
        background.detector_source_labels = {"shared_shape"};
        background.detector_source_count = 1;
        background.detector_shift_vectors = {
            1.0, -1.0,
        };
        return background;
    }

    fit::Channel make_sr_channel()
    {
        fit::Channel channel;
        channel.spec.channel_key = "SR";
        channel.spec.branch_expr = "score";
        channel.spec.selection_expr = "region == 0";
        channel.spec.nbins = 2;
        channel.spec.xmin = 0.0;
        channel.spec.xmax = 2.0;
        channel.data = {15.0, 12.0};
        channel.data_source_keys = {"data_sr"};
        channel.processes.push_back(make_signal_process());
        channel.processes.push_back(make_background_process({10.0, 8.0},
                                                           {2.0, 2.0}));
        return channel;
    }

    fit::Channel make_cr_channel()
    {
        fit::Channel channel;
        channel.spec.channel_key = "CR";
        channel.spec.branch_expr = "score";
        channel.spec.selection_expr = "region == 1";
        channel.spec.nbins = 2;
        channel.spec.xmin = 0.0;
        channel.spec.xmax = 2.0;
        channel.data = {30.0, 28.0};
        channel.data_source_keys = {"data_cr"};
        channel.processes.push_back(make_background_process({30.0, 28.0},
                                                           {4.0, 4.0}));
        return channel;
    }
}

int main()
{
    try
    {
        fit::Problem problem =
            fit::make_independent_problem({make_sr_channel(), make_cr_channel()},
                                          1.0,
                                          5.0);
        problem.measurement_name = "fit_histfactory_smoke";
        problem.mu_lower = 0.0;
        problem.mu_upper = 5.0;

        fit::Result result = fit::profile_signal_strength(problem);

        if (!result.converged)
            fail("combined HistFactory fit did not converge");
        if (result.minimizer_status != 0)
            fail("combined HistFactory fit returned a non-zero status");
        if (result.channels.size() != 2)
            fail("combined HistFactory fit did not keep both channels");
        if (std::fabs(result.mu_hat - 1.0) > 0.25)
            fail("combined HistFactory fit returned an unexpected mu_hat");
        if (result.predicted_total.size() != 0)
            fail("top-level predicted_total should stay empty for multi-channel fits");
        if (result.channels[0].predicted_total.size() != 2 ||
            result.channels[1].predicted_total.size() != 2)
        {
            fail("combined HistFactory fit did not return per-channel predictions");
        }
        if (result.nuisance_names.empty())
            fail("combined HistFactory fit should expose nuisance parameters");

        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "fit_histfactory_smoke: " << e.what() << "\n";
        return 1;
    }
}
