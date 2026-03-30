#include "EventListPlotting.hh"

#include <memory>
#include <stdexcept>
#include <string>

#include "TCanvas.h"
#include "TH1D.h"
#include "TTree.h"

namespace plot_utils
{
    std::vector<std::string> selected_sample_keys(const EventListIO &eventlist,
                                                  const char *sample_key)
    {
        if (sample_key && std::string(sample_key).size() > 0)
            return {sample_key};
        return eventlist.sample_keys();
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

        std::unique_ptr<TH1D> hist(new TH1D(hist_name, branch_expr, nbins, xmin, xmax));
        hist->SetDirectory(nullptr);
        hist->Sumw2();

        for (const auto &key : selected_sample_keys(eventlist, sample_key))
        {
            TTree *tree = eventlist.selected_tree(key);
            if (!tree)
                continue;

            const std::string draw_expr = std::string(branch_expr) + ">>+" + hist_name;
            tree->Draw(draw_expr.c_str(), "", "goff");
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
