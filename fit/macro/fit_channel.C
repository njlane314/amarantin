#include <iostream>
#include <vector>

#include "ChannelIO.hh"
#include "XsecFit.hh"

void fit_channel(const char *path = "output.channels.root",
                 const char *channel_key = "muon_region",
                 const char *signal_process = "signal")
{
    macro_utils::run_macro("fit_channel", [&]() {
        ChannelIO chio(path, ChannelIO::Mode::kRead);
        ChannelIO::Channel channel = chio.read(channel_key);

        fit::Model model = fit::make_independent_model(channel, signal_process);

        fit::FitOptions options;
        options.max_iterations = 10;
        options.nuisance_passes = 8;
        options.scan_points = 48;
        options.tolerance = 1e-4;
        options.compute_stat_only_interval = true;

        fit::Result result = fit::profile_xsec(model, options);

        std::cout << "converged = " << result.converged << "\n";
        std::cout << "mu_hat = " << result.mu_hat << "\n";
        std::cout << "mu_err_total_down = " << result.mu_err_total_down << "\n";
        std::cout << "mu_err_total_up = " << result.mu_err_total_up << "\n";
        std::cout << "mu_err_stat_down = " << result.mu_err_stat_down << "\n";
        std::cout << "mu_err_stat_up = " << result.mu_err_stat_up << "\n";

        for (std::size_t i = 0; i < result.predicted_total.size(); ++i)
            std::cout << "bin " << i << " total = " << result.predicted_total[i] << "\n";
    });
}
