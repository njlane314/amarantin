#include <cctype>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"
#include "EventListPlotting.hh"

#include "TCanvas.h"
#include "TH1D.h"
#include "TLegend.h"

namespace
{
    std::string sanitise_name(const std::string &raw)
    {
        std::string out;
        out.reserve(raw.size());
        for (unsigned char c : raw)
        {
            if (std::isalnum(c) || c == '_' || c == '-')
                out.push_back(static_cast<char>(c));
            else
                out.push_back('_');
        }
        return out.empty() ? std::string("dist") : out;
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const char *cache_key)
    {
        if (cache_key && *cache_key)
        {
            if (!dist.has(sample_key, cache_key))
                throw std::runtime_error("inspect_dist: cache_key not found for sample_key");
            return cache_key;
        }

        const auto keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("inspect_dist: no cached distributions found for sample_key");
        return keys.front();
    }

    std::unique_ptr<TH1D> hist_from_bins(const DistributionIO::Spectrum &spectrum,
                                         const std::vector<double> &bins,
                                         const char *name)
    {
        auto hist = plot_utils::make_histogram(spectrum, name);
        for (int i = 1; i <= hist->GetNbinsX(); ++i)
        {
            const std::size_t j = static_cast<std::size_t>(i - 1);
            hist->SetBinContent(i, j < bins.size() ? bins[j] : 0.0);
            hist->SetBinError(i, 0.0);
        }
        return hist;
    }
}

void inspect_dist(const char *path = "output.dists.root",
                  const char *sample_key = "beam-s0",
                  const char *cache_key = nullptr)
{
    macro_utils::run_macro("inspect_dist", [&]() {
        if (!path || !*path)
            throw std::runtime_error("path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("sample_key is required");

        DistributionIO dist(path, DistributionIO::Mode::kRead);
        const std::string selected_cache_key = pick_cache_key(dist, sample_key, cache_key);
        const auto entry = dist.read(sample_key, selected_cache_key);

        const double yield =
            std::accumulate(entry.nominal.begin(), entry.nominal.end(), 0.0);

        std::cout << "cache_key=" << selected_cache_key << "\n";
        std::cout << "branch=" << entry.spec.branch_expr << "\n";
        std::cout << "selection=" << entry.spec.selection_expr << "\n";
        std::cout << "yield=" << yield << "\n";
        std::cout << "genie eigen_rank=" << entry.genie.eigen_rank << "\n";

        auto h_nom = plot_utils::make_histogram(entry, "h_nom");
        auto h_up = hist_from_bins(entry, entry.total_up, "h_up");
        auto h_dn = hist_from_bins(entry, entry.total_down, "h_dn");

        h_nom->SetLineColor(kBlack);
        h_nom->SetLineWidth(2);
        h_nom->SetMarkerStyle(kFullCircle);
        h_nom->SetMarkerSize(0.8);
        h_up->SetLineColor(kRed + 1);
        h_up->SetLineWidth(2);
        h_dn->SetLineColor(kBlue + 1);
        h_dn->SetLineWidth(2);

        const std::string canvas_name = "c_" + sanitise_name(sample_key) + "_" + sanitise_name(selected_cache_key);
        TCanvas canvas(canvas_name.c_str(), canvas_name.c_str(), 900, 600);
        h_nom->Draw("E1");
        h_up->Draw("hist same");
        h_dn->Draw("hist same");

        TLegend legend(0.58, 0.70, 0.88, 0.88);
        legend.SetBorderSize(0);
        legend.SetFillStyle(0);
        legend.AddEntry(h_nom.get(), "Nominal", "lep");
        legend.AddEntry(h_up.get(), "Total up envelope", "l");
        legend.AddEntry(h_dn.get(), "Total down envelope", "l");
        legend.Draw();

        const std::string out_name =
            "dist_" + sanitise_name(sample_key) + "_" + sanitise_name(selected_cache_key) + ".png";
        canvas.SaveAs(out_name.c_str());
    });
}
