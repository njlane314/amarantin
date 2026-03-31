#ifndef UNSTACKED_HIST_HH
#define UNSTACKED_HIST_HH

#include <memory>
#include <string>
#include <vector>

#include "TLegend.h"
#include "TH1D.h"
#include "THStack.h"

#include "PlotDescriptors.hh"

class TCanvas;
class TPad;

namespace plot_utils
{
    class UnstackedHist
    {
    public:
        UnstackedHist(TH1DModel spec,
                      Options opt,
                      std::vector<const Entry *> mc,
                      std::vector<const Entry *> data);
        UnstackedHist(TH1DModel spec, Options opt, const EventListIO &event_list);
        ~UnstackedHist() = default;

        void draw(TCanvas &canvas);
        void draw_and_save(const std::string &image_format);

    private:
        bool has_data() const { return data_hist_ && data_hist_->GetEntries() > 0.0; }
        bool want_ratio() const { return opt_.show_ratio && has_data() && mc_total_; }

        void setup_pads(TCanvas &canvas, TPad *&p_main, TPad *&p_ratio, TPad *&p_legend) const;
        void build_histograms();
        void draw_overlay_and_unc(TPad *p_main, double &max_y);
        void draw_ratio(TPad *p_ratio);
        void draw_legend(TPad *pad);
        void draw_cuts(TPad *pad, double max_y);

    private:
        TH1DModel spec_;
        Options opt_;
        std::vector<const Entry *> mc_;
        std::vector<const Entry *> data_;
        std::vector<Entry> owned_entries_;
        std::string plot_name_;
        std::string output_directory_;
        std::unique_ptr<THStack> overlay_;
        std::vector<std::unique_ptr<TH1D>> mc_event_category_hists_;
        std::vector<int> event_category_order_;
        std::vector<double> event_category_yields_;
        std::unique_ptr<TH1D> mc_total_;
        std::unique_ptr<TH1D> mc_unc_band_;
        std::unique_ptr<TH1D> data_hist_;
        std::unique_ptr<TH1D> sig_hist_;
        std::unique_ptr<TH1D> ratio_hist_;
        std::unique_ptr<TH1D> ratio_band_;
        std::unique_ptr<TLegend> legend_;
        bool density_mode_ = false;
    };
}

#endif // UNSTACKED_HIST_HH
