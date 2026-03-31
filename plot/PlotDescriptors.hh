#ifndef PLOT_DESCRIPTORS_HH
#define PLOT_DESCRIPTORS_HH

#include <cctype>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "TMatrixDSym.h"

#include "DatasetIO.hh"
#include "EventListIO.hh"

class TTree;

namespace plot_utils
{
    enum class CutDir
    {
        kLessThan,
        kGreaterThan
    };

    struct CutSpec
    {
        double x = 0.0;
        CutDir dir = CutDir::kGreaterThan;
    };

    struct Entry
    {
        const EventListIO *event_list = nullptr;
        std::string sample_key;
        DatasetIO::Sample sample;

        TTree *selected_tree() const
        {
            return event_list ? event_list->selected_tree(sample_key) : nullptr;
        }

        TTree *subrun_tree() const
        {
            return event_list ? event_list->subrun_tree(sample_key) : nullptr;
        }
    };

    struct Options
    {
        std::string out_dir = ".";
        std::string image_format = "png";

        bool show_ratio = true;
        bool show_ratio_band = true;
        bool normalise_by_bin_width = true;
        bool annotate_numbers = true;
        bool overlay_signal = false;
        bool show_legend = true;
        bool legend_on_top = true;
        bool use_log_x = false;
        bool use_log_y = false;
        bool show_cuts = false;

        double y_min = 0.0;
        double y_max = -1.0;

        std::string x_title;
        std::string y_title;

        std::vector<int> signal_event_categories;
        std::shared_ptr<TMatrixDSym> total_cov;
        std::vector<double> syst_bin;
        std::vector<CutSpec> cuts;

        std::string event_category_column = EventListIO::event_category_branch_name();
        std::vector<int> unstack_event_category_keys;
        std::map<int, std::string> unstack_event_category_labels;
        std::map<int, int> unstack_event_category_colours;
    };

    struct TH1DModel
    {
        std::string id;
        std::string name;
        std::string title;
        std::string expr;
        std::string selection = "1";
        std::string weight = "__w__";
        int nbins = 1;
        double xmin = 0.0;
        double xmax = 1.0;
        std::vector<double> bin_edges;

        bool has_custom_bins() const noexcept
        {
            return bin_edges.size() >= 2;
        }

        std::string variable() const
        {
            if (!expr.empty())
                return expr;
            if (!id.empty())
                return id;
            return name;
        }

        std::string axis_title() const
        {
            if (!title.empty())
                return title;

            const std::string base = !name.empty() ? name : variable();
            if (base.empty())
                return ";x;Events";
            return ";" + base + ";Events";
        }

        static std::string sanitise(const std::string &raw)
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
            return out.empty() ? std::string("plot") : out;
        }
    };
}

#endif // PLOT_DESCRIPTORS_HH
