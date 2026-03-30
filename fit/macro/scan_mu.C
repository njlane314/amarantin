#include <vector>

#include "ChannelIO.hh"
#include "XsecFit.hh"

#include "TCanvas.h"
#include "TGraph.h"

void scan_mu(const char *path = "output.channels.root",
             const char *channel_key = "muon_region",
             const char *signal_process = "signal")
{
    macro_utils::run_macro("scan_mu", [&]() {
        ChannelIO chio(path, ChannelIO::Mode::kRead);
        ChannelIO::Channel channel = chio.read(channel_key);

        fit::Model model = fit::make_independent_model(channel, signal_process);
        std::vector<double> theta(model.nuisances.size(), 0.0);

        const int n = 41;
        double x[n];
        double y[n];
        for (int i = 0; i < n; ++i)
        {
            x[i] = 0.05 * i;
            y[i] = fit::objective(model, x[i], theta);
        }

        TCanvas c("c_scan", "c_scan", 800, 600);
        TGraph g(n, x, y);
        g.SetTitle(";#mu;q(#mu) with #theta = 0");
        g.Draw("AL");
        c.SaveAs("mu_scan.png");
    });
}
