#ifndef EFFICIENCY_PLOT_HH
#define EFFICIENCY_PLOT_HH

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "EventListIO.hh"

#include "TEfficiency.h"

class TCanvas;
class TGraphAsymmErrors;
class TH1D;

namespace plot_utils
{
    class EfficiencyPlot
    {
    public:
        struct Spec
        {
            std::string branch_expr;
            int nbins = 0;
            double xmin = 0.0;
            double xmax = 0.0;
            std::string hist_name = "h_efficiency";
            std::string title;
        };

        struct Config
        {
            std::string x_title;
            std::string y_counts_title = "Events";
            std::string y_eff_title = "Efficiency";

            std::string legend_total = "All events";
            std::string legend_passed = "Selected";
            std::string legend_eff = "Efficiency";

            bool draw_distributions = true;
            bool draw_total_hist = true;
            bool draw_passed_hist = true;
            bool logy = false;

            double eff_ymin = 0.0;
            double eff_ymax = 1.05;

            bool auto_x_range = false;
            double x_pad_fraction = 0.05;

            double conf_level = 0.68;
            TEfficiency::EStatOption stat = TEfficiency::kFCP;
            bool use_weighted_events = false;
            std::string weight_expr = "__w__";

            bool draw_stats_text = false;
            bool print_stats = true;
            bool no_exponent_y = false;

            std::vector<std::string> extra_text_lines;
        };

        explicit EfficiencyPlot(Spec spec);
        EfficiencyPlot(Spec spec, Config cfg);
        ~EfficiencyPlot();

        const Spec &spec() const noexcept { return spec_; }
        Spec &spec() noexcept { return spec_; }

        const Config &config() const noexcept { return cfg_; }
        Config &config() noexcept { return cfg_; }

        int compute(const EventListIO &eventlist,
                    const std::string &denom_sel,
                    const std::string &pass_sel,
                    const std::string &extra_sel = "true",
                    const char *sample_key = nullptr);

        int compute(const char *read_path,
                    const std::string &denom_sel,
                    const std::string &pass_sel,
                    const std::string &extra_sel = "true",
                    const char *sample_key = nullptr);

        TCanvas *draw(const char *canvas_name = "c_efficiency") const;

        int draw_and_save(const std::string &file_stem,
                          const std::string &format = "") const;

        const TH1D *total_hist() const noexcept { return h_total_.get(); }
        const TH1D *passed_hist() const noexcept { return h_passed_.get(); }
        const TGraphAsymmErrors *eff_graph() const noexcept { return g_eff_.get(); }

        std::uint64_t denom_entries() const noexcept { return n_denom_; }
        std::uint64_t pass_entries() const noexcept { return n_pass_; }

        bool ready() const noexcept { return ready_; }

    private:
        Spec spec_;
        Config cfg_;

        std::unique_ptr<TH1D> h_total_;
        std::unique_ptr<TH1D> h_passed_;
        std::unique_ptr<TGraphAsymmErrors> g_eff_;

        std::uint64_t n_denom_ = 0;
        std::uint64_t n_pass_ = 0;
        bool ready_ = false;

        static std::string sanitise_(const std::string &s);
    };
}

#endif // EFFICIENCY_PLOT_HH
