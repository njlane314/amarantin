#include "PlottingHelper.hh"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "TH1D.h"
#include "TTree.h"

namespace plot_utils
{
    std::vector<Entry> make_entries(const EventListIO &event_list)
    {
        std::vector<Entry> entries;
        const auto keys = event_list.sample_keys();
        entries.reserve(keys.size());
        for (const auto &key : keys)
            entries.push_back(Entry{&event_list, key, event_list.sample(key)});
        return entries;
    }

    void split_entries(const std::vector<Entry> &entries,
                       std::vector<const Entry *> &mc,
                       std::vector<const Entry *> &data)
    {
        mc.clear();
        data.clear();
        for (const auto &entry : entries)
        {
            if (is_data_origin(entry.sample.origin))
                data.push_back(&entry);
            else if (is_mc_like_origin(entry.sample.origin))
                mc.push_back(&entry);
        }
    }

    bool is_data_origin(DatasetIO::Sample::Origin origin)
    {
        return origin == DatasetIO::Sample::Origin::kData;
    }

    bool is_mc_like_origin(DatasetIO::Sample::Origin origin)
    {
        return origin == DatasetIO::Sample::Origin::kExternal ||
               origin == DatasetIO::Sample::Origin::kOverlay ||
               origin == DatasetIO::Sample::Origin::kDirt ||
               origin == DatasetIO::Sample::Origin::kEnriched;
    }

    TH1DModel make_spec(const std::string &expr,
                        int nbins,
                        double xmin,
                        double xmax,
                        const std::string &weight)
    {
        TH1DModel spec;
        spec.id = "h_" + TH1DModel::sanitise(expr);
        spec.name = expr;
        spec.expr = expr;
        spec.weight = weight;
        spec.nbins = nbins;
        spec.xmin = xmin;
        spec.xmax = xmax;
        return spec;
    }

    TH1DModel make_spec(const std::string &expr,
                        const std::vector<double> &bin_edges,
                        const std::string &weight)
    {
        TH1DModel spec;
        spec.id = "h_" + TH1DModel::sanitise(expr);
        spec.name = expr;
        spec.expr = expr;
        spec.weight = weight;
        spec.bin_edges = bin_edges;
        if (bin_edges.size() >= 2)
        {
            spec.nbins = static_cast<int>(bin_edges.size()) - 1;
            spec.xmin = bin_edges.front();
            spec.xmax = bin_edges.back();
        }
        return spec;
    }

    std::string combine_selection(const std::vector<std::string> &parts)
    {
        std::string out;
        for (const auto &part : parts)
        {
            if (part.empty() || part == "1" || part == "true")
                continue;
            if (!out.empty())
                out += " && ";
            out += "(" + part + ")";
        }
        return out.empty() ? std::string("1") : out;
    }

    std::string equality_selection(const std::string &column, int value)
    {
        if (column.empty())
            throw std::runtime_error("plot_utils::equality_selection: column is required");
        return column + " == " + std::to_string(value);
    }

    std::unique_ptr<TH1D> book_histogram(const TH1DModel &spec,
                                         const std::string &hist_name,
                                         const std::string &hist_title)
    {
        if (spec.nbins <= 0)
            throw std::runtime_error("plot_utils::book_histogram: nbins must be positive");

        const std::string title = hist_title.empty()
                                      ? (spec.title.empty() ? spec.variable() : spec.title)
                                      : hist_title;

        std::unique_ptr<TH1D> hist;
        if (spec.has_custom_bins())
        {
            hist.reset(new TH1D(hist_name.c_str(),
                                title.c_str(),
                                static_cast<int>(spec.bin_edges.size()) - 1,
                                spec.bin_edges.data()));
        }
        else
        {
            hist.reset(new TH1D(hist_name.c_str(),
                                title.c_str(),
                                spec.nbins,
                                spec.xmin,
                                spec.xmax));
        }

        hist->SetDirectory(nullptr);
        hist->Sumw2();
        hist->SetStats(false);
        return hist;
    }

    void fill_histogram(TTree *tree,
                        TH1D &hist,
                        const TH1DModel &spec,
                        const std::string &selection,
                        bool use_weights)
    {
        if (!tree)
            return;

        const std::string variable = spec.variable();
        if (variable.empty())
            throw std::runtime_error("plot_utils::fill_histogram: expr or id is required");

        const std::string draw_expr = variable + ">>+" + hist.GetName();
        std::string weight_expr;
        if (use_weights && !spec.weight.empty())
        {
            const std::string base = (selection.empty() || selection == "1" || selection == "true")
                                         ? std::string("1")
                                         : "(" + selection + ")";
            weight_expr = base + "*(" + spec.weight + ")";
        }
        else if (!selection.empty() && selection != "1")
            weight_expr = selection;

        tree->Draw(draw_expr.c_str(), weight_expr.c_str(), "goff");
    }
}
