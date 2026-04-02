#include "EventListPlotting.hh"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include "PlottingHelper.hh"

#include "TCanvas.h"
#include "TH1D.h"
#include "TTree.h"

namespace plot_utils
{
    namespace
    {
        bool uses_event_weights(const EventListIO &eventlist, const std::string &sample_key)
        {
            return eventlist.sample(sample_key).origin != DatasetIO::Sample::Origin::kData;
        }

        bool include_default_sample(const DatasetIO::Sample &sample)
        {
            return sample.variation != DatasetIO::Sample::Variation::kDetector;
        }
    }

    std::vector<std::string> selected_sample_keys(const EventListIO &eventlist,
                                                  const char *sample_key)
    {
        if (sample_key && std::string(sample_key).size() > 0)
            return {sample_key};

        std::vector<std::string> out;
        for (const auto &key : eventlist.sample_keys())
        {
            if (!include_default_sample(eventlist.sample(key)))
                continue;
            out.push_back(key);
        }
        return out;
    }

    std::unique_ptr<TH1D> make_histogram(const EventListIO &eventlist,
                                         const char *branch_expr,
                                         int nbins,
                                         double xmin,
                                         double xmax,
                                         const char *hist_name,
                                         const char *sample_key)
    {
        if (!branch_expr || std::string(branch_expr).empty())
            throw std::runtime_error("plot_utils::make_histogram: branch_expr is required");
        if (nbins <= 0)
            throw std::runtime_error("plot_utils::make_histogram: nbins must be positive");
        if (!(xmax > xmin))
            throw std::runtime_error("plot_utils::make_histogram: invalid histogram range");

        TH1DModel spec;
        spec.expr = branch_expr;
        spec.name = branch_expr;
        spec.weight = "__w__";
        spec.nbins = nbins;
        spec.xmin = xmin;
        spec.xmax = xmax;

        std::unique_ptr<TH1D> hist = book_histogram(spec, hist_name, branch_expr);

        for (const auto &key : selected_sample_keys(eventlist, sample_key))
        {
            TTree *tree = eventlist.selected_tree(key);
            if (!tree)
                continue;

            fill_histogram(tree, *hist, spec, "1", uses_event_weights(eventlist, key));
        }

        return hist;
    }

    std::unique_ptr<TH1D> make_histogram(const DistributionIO::Spectrum &spectrum,
                                         const char *hist_name)
    {
        const auto &spec = spectrum.spec;
        if (spec.nbins <= 0)
            throw std::runtime_error("plot_utils::make_histogram: nbins must be positive");
        if (!(spec.xmax > spec.xmin))
            throw std::runtime_error("plot_utils::make_histogram: invalid histogram range");
        if (spectrum.nominal.size() != static_cast<std::size_t>(spec.nbins))
            throw std::runtime_error("plot_utils::make_histogram: nominal size does not match histogram bins");
        if (!spectrum.sumw2.empty() &&
            spectrum.sumw2.size() != static_cast<std::size_t>(spec.nbins))
        {
            throw std::runtime_error("plot_utils::make_histogram: sumw2 size does not match histogram bins");
        }

        std::unique_ptr<TH1D> hist(new TH1D(hist_name,
                                            spec.branch_expr.c_str(),
                                            spec.nbins,
                                            spec.xmin,
                                            spec.xmax));
        hist->SetDirectory(nullptr);
        hist->Sumw2();

        const std::size_t nbins = static_cast<std::size_t>(spec.nbins);
        for (std::size_t bin = 0; bin < nbins; ++bin)
        {
            hist->SetBinContent(static_cast<int>(bin + 1), spectrum.nominal[bin]);
            if (!spectrum.sumw2.empty())
                hist->SetBinError(static_cast<int>(bin + 1), std::sqrt(std::max(0.0, spectrum.sumw2[bin])));
        }

        return hist;
    }

    TCanvas *draw_distribution(const EventListIO &eventlist,
                               const char *branch_expr,
                               int nbins,
                               double xmin,
                               double xmax,
                               const char *canvas_name,
                               const char *sample_key)
    {
        std::unique_ptr<TH1D> hist = make_histogram(eventlist,
                                                    branch_expr,
                                                    nbins,
                                                    xmin,
                                                    xmax,
                                                    "h_eventlist",
                                                    sample_key);

        TCanvas *canvas = new TCanvas(canvas_name, branch_expr, 900, 600);
        hist->SetLineWidth(2);
        hist->SetStats(false);
        hist->Draw("hist");
        canvas->Update();

        hist.release();
        return canvas;
    }

    TCanvas *draw_distribution(const char *read_path,
                               const char *branch_expr,
                               int nbins,
                               double xmin,
                               double xmax,
                               const char *canvas_name,
                               const char *sample_key)
    {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("plot_utils::draw_distribution: read_path is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        return draw_distribution(eventlist,
                                 branch_expr,
                                 nbins,
                                 xmin,
                                 xmax,
                                 canvas_name,
                                 sample_key);
    }
}
