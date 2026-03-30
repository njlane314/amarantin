#ifndef STACKED_HIST_HH
#define STACKED_HIST_HH

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
    class StackedHist
    {
    public:
        StackedHist(TH1DModel spec,
                    Options opt,
                    std::vector<const Entry *> mc,
                    std::vector<const Entry *> data);
        StackedHist(TH1DModel spec, Options opt, const EventListIO &event_list);
        ~StackedHist() = default;

        void draw(TCanvas &canvas);
        void draw_and_save(const std::string &image_format);

    private:
        bool has_data() const { return data_hist_ && data_hist_->GetEntries() > 0.0; }
        bool want_ratio() const { return opt_.show_ratio && has_data() && mc_total_; }

        void build_histograms();
        void setup_pads(TCanvas &canvas, TPad *&p_main, TPad *&p_ratio, TPad *&p_legend) const;
        void draw_stack_and_unc(TPad *p_main, double &max_y);
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
        std::unique_ptr<THStack> stack_;
        std::vector<std::unique_ptr<TH1D>> mc_ch_hists_;
        std::unique_ptr<TH1D> mc_total_;
        std::unique_ptr<TH1D> data_hist_;
        std::unique_ptr<TH1D> mc_unc_hist_;
        std::unique_ptr<TH1D> sig_hist_;
        std::unique_ptr<TH1D> ratio_hist_;
        std::unique_ptr<TH1D> ratio_band_;
        std::unique_ptr<TLegend> legend_;
        std::vector<int> chan_order_;
        std::vector<double> chan_event_yields_;
        bool density_mode_ = false;
    };
}

#endif // STACKED_HIST_HH
