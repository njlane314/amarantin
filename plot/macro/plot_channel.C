#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelIO.hh"

#include "TCanvas.h"
#include "TH1D.h"
#include "THStack.h"
#include "TLegend.h"

void plot_channel(const char *path = "output.channels.root",
                  const char *channel_key = "muon_region",
                  const char *output_path = "channel_stack.png")
{
    macro_utils::run_macro("plot_channel", [&]() {
        if (!path || !*path)
            throw std::runtime_error("path is required");
        if (!channel_key || !*channel_key)
            throw std::runtime_error("channel_key is required");
        if (!output_path || !*output_path)
            throw std::runtime_error("output_path is required");

        ChannelIO chio(path, ChannelIO::Mode::kRead);
        const auto channel = chio.read(channel_key);

        THStack stack("stack", (";" + channel.spec.branch_expr + ";Events").c_str());
        std::vector<std::unique_ptr<TH1D>> owned;

        const int fill_colours[] = {kGreen + 1, kAzure + 1, kOrange + 7, kMagenta + 1};

        for (std::size_t process_index = 0; process_index < channel.processes.size(); ++process_index)
        {
            const auto &proc = channel.processes[process_index];
            auto hist = std::make_unique<TH1D>(proc.name.c_str(),
                                               proc.name.c_str(),
                                               channel.spec.nbins,
                                               channel.spec.xmin,
                                               channel.spec.xmax);
            hist->SetDirectory(nullptr);
            hist->SetLineColor(kBlack);
            hist->SetFillColor(fill_colours[process_index % (sizeof(fill_colours) / sizeof(fill_colours[0]))]);
            hist->SetFillStyle(1001);

            for (int i = 1; i <= channel.spec.nbins; ++i)
            {
                const std::size_t j = static_cast<std::size_t>(i - 1);
                hist->SetBinContent(i, j < proc.nominal.size() ? proc.nominal[j] : 0.0);
                hist->SetBinError(i, j < proc.sumw2.size() ? std::sqrt(std::max(0.0, proc.sumw2[j])) : 0.0);
            }

            stack.Add(hist.get(), "hist");
            owned.push_back(std::move(hist));
        }

        auto data = std::make_unique<TH1D>("data",
                                           "data",
                                           channel.spec.nbins,
                                           channel.spec.xmin,
                                           channel.spec.xmax);
        data->SetDirectory(nullptr);
        data->SetLineColor(kBlack);
        data->SetMarkerStyle(kFullCircle);
        data->SetMarkerSize(0.9);
        for (int i = 1; i <= channel.spec.nbins; ++i)
        {
            const std::size_t j = static_cast<std::size_t>(i - 1);
            data->SetBinContent(i, j < channel.data.size() ? channel.data[j] : 0.0);
        }

        TCanvas canvas("c_channel", "c_channel", 900, 600);
        stack.Draw("hist");
        data->Draw("E1 same");

        TLegend legend(0.60, 0.68, 0.88, 0.88);
        legend.SetBorderSize(0);
        legend.SetFillStyle(0);
        for (const auto &hist : owned)
            legend.AddEntry(hist.get(), hist->GetName(), "f");
        legend.AddEntry(data.get(), "data", "lep");
        legend.Draw();

        canvas.SaveAs(output_path);
    });
}
