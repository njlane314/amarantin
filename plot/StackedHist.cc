#include "StackedHist.hh"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "TCanvas.h"
#include "TLegend.h"
#include "TLine.h"
#include "TMatrixDSym.h"
#include "TPad.h"
#include "THStack.h"

#include "PlotChannels.hh"
#include "Plotter.hh"
#include "PlottingHelper.hh"
#include "bits/DataMcHistogramUtils.hh"

namespace plot_utils
{
    namespace
    {
        constexpr int k_uncertainty_fill_colour = kGray + 2;
        constexpr int k_uncertainty_fill_style = 3345;
    }

    StackedHist::StackedHist(TH1DModel spec,
                             Options opt,
                             std::vector<const Entry *> mc,
                             std::vector<const Entry *> data)
        : spec_(std::move(spec)),
          opt_(std::move(opt)),
          mc_(std::move(mc)),
          data_(std::move(data)),
          plot_name_(Plotter::sanitise(!spec_.id.empty() ? spec_.id : spec_.variable())),
          output_directory_(opt_.out_dir)
    {
    }

    StackedHist::StackedHist(TH1DModel spec, Options opt, const EventListIO &event_list)
        : spec_(std::move(spec)),
          opt_(std::move(opt)),
          plot_name_(Plotter::sanitise(!spec_.id.empty() ? spec_.id : spec_.variable())),
          output_directory_(opt_.out_dir)
    {
        owned_entries_ = make_entries(event_list);
        split_entries(owned_entries_, mc_, data_);
    }

    void StackedHist::setup_pads(TCanvas &canvas, TPad *&p_main, TPad *&p_ratio, TPad *&p_legend) const
    {
        canvas.cd();
        p_main = nullptr;
        p_ratio = nullptr;
        p_legend = nullptr;

        if (opt_.legend_on_top)
        {
            if (want_ratio())
            {
                p_ratio = new TPad((plot_name_ + "_ratio").c_str(), "", 0.0, 0.0, 1.0, 0.28);
                p_main = new TPad((plot_name_ + "_main").c_str(), "", 0.0, 0.28, 1.0, 0.82);
                p_legend = new TPad((plot_name_ + "_legend").c_str(), "", 0.0, 0.82, 1.0, 1.0);

                p_ratio->SetTopMargin(0.04);
                p_ratio->SetBottomMargin(0.35);
                p_ratio->SetLeftMargin(0.12);
                p_ratio->SetRightMargin(0.05);

                p_main->SetTopMargin(0.02);
                p_main->SetBottomMargin(0.03);
                p_main->SetLeftMargin(0.12);
                p_main->SetRightMargin(0.05);
            }
            else
            {
                p_main = new TPad((plot_name_ + "_main").c_str(), "", 0.0, 0.0, 1.0, 0.82);
                p_legend = new TPad((plot_name_ + "_legend").c_str(), "", 0.0, 0.82, 1.0, 1.0);
                p_main->SetTopMargin(0.02);
                p_main->SetBottomMargin(0.12);
                p_main->SetLeftMargin(0.12);
                p_main->SetRightMargin(0.05);
            }

            p_legend->SetTopMargin(0.05);
            p_legend->SetBottomMargin(0.01);
            p_legend->SetLeftMargin(0.02);
            p_legend->SetRightMargin(0.02);
        }
        else
        {
            if (want_ratio())
            {
                p_main = new TPad((plot_name_ + "_main").c_str(), "", 0.0, 0.30, 1.0, 1.0);
                p_ratio = new TPad((plot_name_ + "_ratio").c_str(), "", 0.0, 0.0, 1.0, 0.30);

                p_main->SetTopMargin(0.06);
                p_main->SetBottomMargin(0.02);
                p_main->SetLeftMargin(0.12);
                p_main->SetRightMargin(0.05);

                p_ratio->SetTopMargin(0.04);
                p_ratio->SetBottomMargin(0.35);
                p_ratio->SetLeftMargin(0.12);
                p_ratio->SetRightMargin(0.05);
            }
            else
            {
                p_main = new TPad((plot_name_ + "_main").c_str(), "", 0.0, 0.0, 1.0, 1.0);
                p_main->SetTopMargin(0.06);
                p_main->SetBottomMargin(0.12);
                p_main->SetLeftMargin(0.12);
                p_main->SetRightMargin(0.05);
            }
        }

        if (p_main && opt_.use_log_y)
            p_main->SetLogy();
        if (p_main && opt_.use_log_x)
            p_main->SetLogx();
        if (p_ratio && opt_.use_log_x)
            p_ratio->SetLogx();

        if (p_ratio)
            p_ratio->Draw();
        if (p_main)
            p_main->Draw();
        if (p_legend)
            p_legend->Draw();
    }

    void StackedHist::build_histograms()
    {
        stack_ = std::make_unique<THStack>((plot_name_ + "_stack").c_str(), spec_.axis_title().c_str());
        mc_ch_hists_.clear();
        mc_total_.reset();
        data_hist_.reset();
        mc_unc_hist_.reset();
        sig_hist_.reset();
        ratio_hist_.reset();
        ratio_band_.reset();
        legend_.reset();
        chan_order_.clear();
        chan_event_yields_.clear();
        density_mode_ = false;

        std::map<int, std::unique_ptr<TH1D>> sum_by_channel;
        for (const Entry *entry : mc_)
        {
            if (!entry)
                continue;
            TTree *tree = entry->selected_tree();
            if (!tree)
                continue;

            for (int ch : Channels::mc_keys())
            {
                const std::string hist_name =
                    plot_name_ + "_mc_" + std::to_string(ch) + "_" + TH1DModel::sanitise(entry->sample_key);
                auto part = book_histogram(spec_, hist_name);
                fill_histogram(tree,
                               *part,
                               spec_,
                               combine_selection({spec_.selection, equality_selection(opt_.channel_column, ch)}),
                               true);

                auto &sum = sum_by_channel[ch];
                if (!sum)
                {
                    sum.reset(static_cast<TH1D *>(part->Clone((plot_name_ + "_sum_" + std::to_string(ch)).c_str())));
                    sum->SetDirectory(nullptr);
                }
                else
                {
                    sum->Add(part.get());
                }
            }
        }

        std::vector<std::pair<int, double>> yields;
        for (auto &kv : sum_by_channel)
        {
            if (kv.second)
                yields.emplace_back(kv.first, kv.second->Integral());
        }
        std::stable_sort(yields.begin(), yields.end(), [](const auto &a, const auto &b) {
            if (a.second == b.second)
                return a.first < b.first;
            return a.second > b.second;
        });

        for (const auto &[ch, yield] : yields)
        {
            auto it = sum_by_channel.find(ch);
            if (it == sum_by_channel.end() || !it->second)
                continue;

            it->second->SetFillColor(Channels::colour(ch));
            it->second->SetFillStyle(Channels::fill_style(ch));
            it->second->SetLineColor(kBlack);
            it->second->SetLineWidth(1);

            stack_->Add(it->second.get(), "HIST");
            mc_ch_hists_.push_back(std::move(it->second));
            chan_order_.push_back(ch);
            chan_event_yields_.push_back(yield);
        }

        for (const auto &hist : mc_ch_hists_)
        {
            if (!hist)
                continue;
            if (!mc_total_)
            {
                mc_total_.reset(static_cast<TH1D *>(hist->Clone((plot_name_ + "_mc_total").c_str())));
                mc_total_->SetDirectory(nullptr);
            }
            else
            {
                mc_total_->Add(hist.get());
            }
        }

        for (const Entry *entry : data_)
        {
            if (!entry)
                continue;
            TTree *tree = entry->selected_tree();
            if (!tree)
                continue;

            const std::string hist_name = plot_name_ + "_data_" + TH1DModel::sanitise(entry->sample_key);
            auto part = book_histogram(spec_, hist_name);
            fill_histogram(tree, *part, spec_, spec_.selection, false);

            if (!data_hist_)
            {
                data_hist_.reset(static_cast<TH1D *>(part->Clone((plot_name_ + "_data").c_str())));
                data_hist_->SetDirectory(nullptr);
            }
            else
            {
                data_hist_->Add(part.get());
            }
        }

        if (data_hist_)
        {
            data_hist_->SetMarkerStyle(kFullCircle);
            data_hist_->SetMarkerSize(0.9);
            data_hist_->SetLineColor(kBlack);
            data_hist_->SetFillStyle(0);
        }

        if (opt_.overlay_signal && !opt_.signal_channels.empty() && mc_total_ && !mc_ch_hists_.empty())
        {
            auto sig = std::unique_ptr<TH1D>(static_cast<TH1D *>(mc_ch_hists_.front()->Clone((plot_name_ + "_sig").c_str())));
            sig->Reset();
            for (std::size_t i = 0; i < mc_ch_hists_.size(); ++i)
            {
                const int ch = chan_order_.at(i);
                if (std::find(opt_.signal_channels.begin(), opt_.signal_channels.end(), ch) != opt_.signal_channels.end())
                    sig->Add(mc_ch_hists_[i].get());
            }

            const double signal_events = bits::integral_in_visible_range(*sig, spec_.xmin, spec_.xmax);
            const double visible_total = bits::integral_in_visible_range(*mc_total_, spec_.xmin, spec_.xmax);
            if (signal_events > 0.0 && visible_total > 0.0)
            {
                const double signal_scale = visible_total / signal_events;
                sig->Scale(signal_scale);
            }
            sig->SetLineColor(kGreen + 2);
            sig->SetLineStyle(kDashed);
            sig->SetLineWidth(3);
            sig->SetFillStyle(0);
            sig_hist_ = std::move(sig);
        }

        if (opt_.normalise_by_bin_width)
        {
            auto scale_width = [](std::unique_ptr<TH1D> &hist) {
                if (hist)
                    hist->Scale(1.0, "width");
            };

            for (auto &hist : mc_ch_hists_)
                scale_width(hist);
            scale_width(mc_total_);
            scale_width(data_hist_);
            scale_width(sig_hist_);
            density_mode_ = true;
        }
    }

    void StackedHist::draw_stack_and_unc(TPad *p_main, double &max_y)
    {
        if (!p_main)
            return;

        p_main->cd();
        stack_->Draw("HIST");

        TH1 *frame = stack_->GetHistogram();
        if (frame)
        {
            const std::string default_x = !spec_.name.empty() ? spec_.name : spec_.variable();
            const std::string default_y = density_mode_ ? "Events / bin width" : "Events";
            frame->SetTitle((";" +
                             (opt_.x_title.empty() ? default_x : opt_.x_title) +
                             ";" +
                             (opt_.y_title.empty() ? default_y : opt_.y_title))
                                .c_str());
            if (spec_.xmin < spec_.xmax)
                frame->GetXaxis()->SetRangeUser(spec_.xmin, spec_.xmax);

            if (want_ratio())
            {
                frame->GetXaxis()->SetLabelSize(0.0);
                frame->GetXaxis()->SetTitleSize(0.0);
            }
            else
            {
                frame->GetXaxis()->SetLabelSize(0.040);
                frame->GetXaxis()->SetTitleSize(0.045);
            }

            frame->GetYaxis()->SetLabelSize(0.040);
            frame->GetYaxis()->SetTitleSize(0.045);
            frame->GetYaxis()->SetTitleOffset(1.1);
        }

        if (mc_total_ && (opt_.total_cov || !opt_.syst_bin.empty()))
        {
            bits::apply_total_errors(*mc_total_,
                                     opt_.total_cov.get(),
                                     opt_.syst_bin.empty() ? nullptr : &opt_.syst_bin,
                                     density_mode_);
        }

        max_y = 0.0;
        if (mc_total_)
            max_y = std::max(max_y, bits::maximum_in_visible_range(*mc_total_, spec_.xmin, spec_.xmax, true));
        if (sig_hist_)
            max_y = std::max(max_y, bits::maximum_in_visible_range(*sig_hist_, spec_.xmin, spec_.xmax, false));
        if (data_hist_)
            max_y = std::max(max_y, bits::maximum_in_visible_range(*data_hist_, spec_.xmin, spec_.xmax, true));
        if (opt_.y_max > 0.0)
            max_y = opt_.y_max;
        if (max_y <= 0.0)
            max_y = 1.0;

        stack_->SetMaximum(max_y * (opt_.use_log_y ? 10.0 : 1.3));
        stack_->SetMinimum(opt_.use_log_y ? 0.1 : opt_.y_min);

        if (mc_total_)
        {
            mc_unc_hist_.reset(static_cast<TH1D *>(mc_total_->Clone((plot_name_ + "_unc").c_str())));
            mc_unc_hist_->SetDirectory(nullptr);
            mc_unc_hist_->SetFillColor(k_uncertainty_fill_colour);
            mc_unc_hist_->SetFillStyle(k_uncertainty_fill_style);
            mc_unc_hist_->SetLineColor(k_uncertainty_fill_colour);
            mc_unc_hist_->SetMarkerSize(0);
            mc_unc_hist_->Draw("E2 SAME");
        }

        if (sig_hist_)
            sig_hist_->Draw("HIST SAME");
        if (has_data())
            data_hist_->Draw("E1 SAME");
    }

    void StackedHist::draw_ratio(TPad *p_ratio)
    {
        if (!p_ratio || !has_data() || !mc_total_)
            return;

        p_ratio->cd();
        ratio_hist_ = bits::make_ratio_histogram(*data_hist_, *mc_total_, plot_name_ + "_ratio");
        ratio_hist_->SetTitle("");
        ratio_hist_->SetMarkerStyle(kFullCircle);
        ratio_hist_->SetMarkerSize(0.8);
        ratio_hist_->SetLineColor(kBlack);
        ratio_hist_->SetMinimum(0.5);
        ratio_hist_->SetMaximum(1.5);
        ratio_hist_->GetYaxis()->SetTitle("Data / MC");
        ratio_hist_->GetYaxis()->SetNdivisions(505);
        ratio_hist_->GetYaxis()->SetTitleSize(0.11);
        ratio_hist_->GetYaxis()->SetLabelSize(0.10);
        ratio_hist_->GetYaxis()->SetTitleOffset(0.45);
        ratio_hist_->GetXaxis()->SetTitle((opt_.x_title.empty() ? (!spec_.name.empty() ? spec_.name : spec_.variable()) : opt_.x_title).c_str());
        ratio_hist_->GetXaxis()->SetTitleSize(0.12);
        ratio_hist_->GetXaxis()->SetLabelSize(0.10);
        ratio_hist_->GetXaxis()->SetTitleOffset(1.0);
        ratio_hist_->Draw("E1");

        if (opt_.show_ratio_band)
        {
            ratio_band_ = bits::make_ratio_band_histogram(*mc_total_, plot_name_ + "_ratio_band");
            ratio_band_->SetFillColor(k_uncertainty_fill_colour);
            ratio_band_->SetFillStyle(k_uncertainty_fill_style);
            ratio_band_->SetLineColor(k_uncertainty_fill_colour);
            ratio_band_->SetMarkerSize(0);
            ratio_band_->Draw("E2 SAME");
        }

        TLine *unity = new TLine(ratio_hist_->GetXaxis()->GetXmin(), 1.0,
                                 ratio_hist_->GetXaxis()->GetXmax(), 1.0);
        unity->SetLineColor(kBlack);
        unity->SetLineStyle(kDashed);
        unity->Draw("SAME");

        ratio_hist_->Draw("E1 SAME");
    }

    void StackedHist::draw_legend(TPad *pad)
    {
        if (!pad || !opt_.show_legend)
            return;

        pad->cd();

        if (opt_.legend_on_top)
            legend_ = std::make_unique<TLegend>(0.02, 0.10, 0.98, 0.90);
        else
            legend_ = std::make_unique<TLegend>(0.58, 0.55, 0.95, 0.88);

        legend_->SetBorderSize(0);
        legend_->SetFillStyle(0);
        legend_->SetTextFont(42);
        if (opt_.legend_on_top)
            legend_->SetNColumns(mc_ch_hists_.size() > 4 ? 3 : 2);

        if (has_data())
            legend_->AddEntry(data_hist_.get(), "Data", "lep");
        if (mc_unc_hist_)
            legend_->AddEntry(mc_unc_hist_.get(), "MC unc.", "f");
        if (sig_hist_)
            legend_->AddEntry(sig_hist_.get(), "Signal overlay", "l");

        for (std::size_t i = 0; i < mc_ch_hists_.size(); ++i)
        {
            std::string label = Channels::label(chan_order_.at(i));
            if (opt_.annotate_numbers && i < chan_event_yields_.size())
                label += " : " + Plotter::fmt_commas(chan_event_yields_[i], 2);
            legend_->AddEntry(mc_ch_hists_[i].get(), label.c_str(), "f");
        }

        legend_->Draw();
    }

    void StackedHist::draw_cuts(TPad *pad, double max_y)
    {
        if (!pad || !opt_.show_cuts || opt_.cuts.empty())
            return;

        pad->cd();
        const double y0 = opt_.use_log_y ? std::max(0.1, opt_.y_min) : opt_.y_min;
        for (const auto &cut : opt_.cuts)
        {
            TLine *line = new TLine(cut.x, y0, cut.x, max_y);
            line->SetLineColor(kBlack);
            line->SetLineStyle(kDashed);
            line->Draw("SAME");
        }
    }

    void StackedHist::draw(TCanvas &canvas)
    {
        build_histograms();

        TPad *p_main = nullptr;
        TPad *p_ratio = nullptr;
        TPad *p_legend = nullptr;
        setup_pads(canvas, p_main, p_ratio, p_legend);

        double max_y = 0.0;
        draw_stack_and_unc(p_main, max_y);
        if (opt_.legend_on_top)
            draw_legend(p_legend);
        else
            draw_legend(p_main);
        draw_ratio(p_ratio);
        draw_cuts(p_main, max_y);
        canvas.cd();
        canvas.Update();
    }

    void StackedHist::draw_and_save(const std::string &image_format)
    {
        std::filesystem::create_directories(output_directory_);
        TCanvas canvas((plot_name_ + "_canvas").c_str(), plot_name_.c_str(), 900, want_ratio() ? 800 : 700);
        draw(canvas);

        std::string format = image_format.empty() ? opt_.image_format : image_format;
        if (format.empty())
            format = "png";

        const std::filesystem::path out_path =
            std::filesystem::path(output_directory_) / (plot_name_ + "." + format);
        canvas.SaveAs(out_path.string().c_str());
    }
}
