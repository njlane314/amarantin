#include "EfficiencyPlot.hh"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "EventListPlotting.hh"

#include "TCanvas.h"
#include "TColor.h"
#include "TGraphAsymmErrors.h"
#include "TGaxis.h"
#include "TH1D.h"
#include "TLatex.h"
#include "TLegend.h"
#include "TPad.h"
#include "TStyle.h"
#include "TTree.h"

namespace
{
    std::string combine_selection(const std::vector<std::string> &parts)
    {
        std::string out;
        for (const auto &part : parts)
        {
            if (part.empty() || part == "true" || part == "1")
                continue;
            if (!out.empty())
                out += " && ";
            out += "(" + part + ")";
        }
        return out.empty() ? "1" : out;
    }

    std::string weighted_selection(const std::string &selection,
                                   const plot_utils::EfficiencyPlot::Config &cfg)
    {
        if (!cfg.use_weighted_events)
            return selection == "1" ? std::string{} : selection;
        if (cfg.weight_expr.empty())
            throw std::runtime_error("plot_utils::EfficiencyPlot: weight_expr is required when weighted events are enabled");

        const std::string base = selection.empty() ? std::string("1") : selection;
        return "(" + base + ")*(" + cfg.weight_expr + ")";
    }

    std::uint64_t count_selected_entries(TTree *tree,
                                         const std::string &selection)
    {
        if (!tree)
            return 0;
        const std::string expr = selection == "1" ? std::string{} : selection;
        return static_cast<std::uint64_t>(tree->Draw("1", expr.c_str(), "goff"));
    }

    double histogram_yield(const TH1D &hist)
    {
        return hist.Integral(0, hist.GetNbinsX() + 1);
    }

    std::unique_ptr<TGraphAsymmErrors> make_efficiency_graph(const TH1D &passed,
                                                             const TH1D &total,
                                                             const plot_utils::EfficiencyPlot::Config &cfg)
    {
        const char *opt = cfg.use_weighted_events ? "w" : "";
        if (!TEfficiency::CheckConsistency(passed, total, opt))
            throw std::runtime_error("plot_utils::EfficiencyPlot: inconsistent numerator/denominator histograms");

        TEfficiency eff(passed, total);
        eff.SetStatisticOption(cfg.stat);
        eff.SetConfidenceLevel(cfg.conf_level);
        if (cfg.use_weighted_events)
            eff.SetUseWeightedEvents();

        std::unique_ptr<TGraphAsymmErrors> graph(eff.CreateGraph());
        if (!graph)
            throw std::runtime_error("plot_utils::EfficiencyPlot: failed to create efficiency graph");
        return graph;
    }

    double positive_minimum(const TH1D &hist)
    {
        double min_value = 0.0;
        for (int bin = 1; bin <= hist.GetNbinsX(); ++bin)
        {
            const double value = hist.GetBinContent(bin);
            if (value <= 0.0)
                continue;
            if (min_value <= 0.0 || value < min_value)
                min_value = value;
        }
        return min_value;
    }

    std::pair<double, double> auto_x_range_limits(const TH1D &total,
                                                  const TH1D &passed,
                                                  const plot_utils::EfficiencyPlot::Config &cfg)
    {
        if (!cfg.auto_x_range)
            return {0.0, 0.0};

        int first = 0;
        int last = 0;
        for (int bin = 1; bin <= total.GetNbinsX(); ++bin)
        {
            if (total.GetBinContent(bin) <= 0.0 && passed.GetBinContent(bin) <= 0.0)
                continue;
            if (first == 0)
                first = bin;
            last = bin;
        }

        if (first == 0 || last == 0)
            return {0.0, 0.0};

        const double width = total.GetXaxis()->GetBinUpEdge(last) -
                             total.GetXaxis()->GetBinLowEdge(first);
        const double pad = std::max(0.0, cfg.x_pad_fraction) * width;
        return {
            total.GetXaxis()->GetBinLowEdge(first) - pad,
            total.GetXaxis()->GetBinUpEdge(last) + pad
        };
    }

    void apply_global_plot_style()
    {
        gStyle->SetOptStat(0);
        gStyle->SetOptTitle(0);
        gStyle->SetPadTickX(1);
        gStyle->SetPadTickY(1);
        gStyle->SetErrorX(0.0);
        TGaxis::SetMaxDigits(4);
    }
}

namespace plot_utils
{
    EfficiencyPlot::EfficiencyPlot(Spec spec)
        : EfficiencyPlot(std::move(spec), Config{})
    {
    }

    EfficiencyPlot::EfficiencyPlot(Spec spec, Config cfg)
        : spec_(std::move(spec)), cfg_(std::move(cfg))
    {
    }

    EfficiencyPlot::~EfficiencyPlot() = default;

    double EfficiencyPlot::denom_total() const noexcept
    {
        return h_total_ ? histogram_yield(*h_total_) : 0.0;
    }

    double EfficiencyPlot::pass_total() const noexcept
    {
        return h_passed_ ? histogram_yield(*h_passed_) : 0.0;
    }

    double EfficiencyPlot::overall_efficiency() const noexcept
    {
        const double denom = denom_total();
        return denom > 0.0 ? pass_total() / denom : 0.0;
    }

    int EfficiencyPlot::compute(const EventListIO &eventlist,
                                const std::string &denom_sel,
                                const std::string &pass_sel,
                                const std::string &extra_sel,
                                const char *sample_key)
    {
        if (spec_.branch_expr.empty())
            throw std::runtime_error("plot_utils::EfficiencyPlot: branch_expr is required");
        if (spec_.nbins <= 0)
            throw std::runtime_error("plot_utils::EfficiencyPlot: nbins must be positive");
        if (!(spec_.xmax > spec_.xmin))
            throw std::runtime_error("plot_utils::EfficiencyPlot: invalid histogram range");

        const std::string total_name = sanitise_(spec_.hist_name + "_total");
        const std::string passed_name = sanitise_(spec_.hist_name + "_passed");
        const std::string hist_title = spec_.title.empty() ? spec_.branch_expr : spec_.title;

        h_total_.reset(new TH1D(total_name.c_str(), hist_title.c_str(),
                                spec_.nbins, spec_.xmin, spec_.xmax));
        h_passed_.reset(new TH1D(passed_name.c_str(), hist_title.c_str(),
                                 spec_.nbins, spec_.xmin, spec_.xmax));
        h_total_->SetDirectory(nullptr);
        h_passed_->SetDirectory(nullptr);
        h_total_->Sumw2();
        h_passed_->Sumw2();

        n_denom_ = 0;
        n_pass_ = 0;
        ready_ = false;

        const std::string denom_selection = combine_selection({extra_sel, denom_sel});
        const std::string pass_selection = combine_selection({extra_sel, denom_sel, pass_sel});
        const std::string total_weight = weighted_selection(denom_selection, cfg_);
        const std::string pass_weight = weighted_selection(pass_selection, cfg_);

        for (const auto &key : selected_sample_keys(eventlist, sample_key))
        {
            TTree *tree = eventlist.selected_tree(key);
            if (!tree)
                continue;

            const std::string total_draw = spec_.branch_expr + ">>+" + total_name;
            const std::string passed_draw = spec_.branch_expr + ">>+" + passed_name;

            tree->Draw(total_draw.c_str(), total_weight.c_str(), "goff");
            tree->Draw(passed_draw.c_str(), pass_weight.c_str(), "goff");

            n_denom_ += count_selected_entries(tree, denom_selection);
            n_pass_ += count_selected_entries(tree, pass_selection);
        }

        g_eff_ = make_efficiency_graph(*h_passed_, *h_total_, cfg_);
        ready_ = true;
        return 0;
    }

    int EfficiencyPlot::compute(const char *read_path,
                                const std::string &denom_sel,
                                const std::string &pass_sel,
                                const std::string &extra_sel,
                                const char *sample_key)
    {
        if (!read_path || !*read_path)
            throw std::runtime_error("plot_utils::EfficiencyPlot: read_path is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        return compute(eventlist, denom_sel, pass_sel, extra_sel, sample_key);
    }

    TCanvas *EfficiencyPlot::draw(const char *canvas_name) const
    {
        if (!ready_ || !h_total_ || !h_passed_ || !g_eff_)
            throw std::runtime_error("plot_utils::EfficiencyPlot: compute() must be called before draw()");

        apply_global_plot_style();

        const std::string name = (canvas_name && *canvas_name) ? std::string(canvas_name)
                                                               : sanitise_(spec_.hist_name + "_canvas");
        TCanvas *canvas = new TCanvas(name.c_str(), name.c_str(), 900, 600);

        TPad *counts_pad = new TPad((name + "_counts").c_str(), "", 0.0, 0.0, 1.0, 1.0);
        counts_pad->SetFillStyle(0);
        counts_pad->SetLeftMargin(0.12);
        counts_pad->SetRightMargin(0.12);
        counts_pad->SetTopMargin(0.08);
        counts_pad->SetBottomMargin(0.12);
        counts_pad->Draw();
        counts_pad->cd();
        counts_pad->SetTicks(1, 1);
        if (cfg_.logy)
            counts_pad->SetLogy();

        TH1D *axis_hist = static_cast<TH1D *>(h_total_->Clone((name + "_axis").c_str()));
        axis_hist->SetDirectory(nullptr);
        axis_hist->Reset("ICES");
        axis_hist->SetLineColor(0);
        axis_hist->SetMarkerColor(0);
        axis_hist->SetMarkerSize(0.0);
        axis_hist->SetStats(false);
        axis_hist->GetXaxis()->SetTitle((cfg_.x_title.empty() ? spec_.branch_expr : cfg_.x_title).c_str());
        axis_hist->GetYaxis()->SetTitle(cfg_.y_counts_title.c_str());
        axis_hist->GetXaxis()->CenterTitle(true);
        axis_hist->GetYaxis()->CenterTitle(true);
        axis_hist->GetYaxis()->SetTitleOffset(1.1);
        if (cfg_.no_exponent_y)
            axis_hist->GetYaxis()->SetNoExponent(true);

        TH1D *total_hist = static_cast<TH1D *>(h_total_->Clone((name + "_total_draw").c_str()));
        TH1D *passed_hist = static_cast<TH1D *>(h_passed_->Clone((name + "_passed_draw").c_str()));
        total_hist->SetDirectory(nullptr);
        passed_hist->SetDirectory(nullptr);

        total_hist->SetStats(false);
        passed_hist->SetStats(false);
        total_hist->SetLineColor(kGray + 2);
        total_hist->SetLineWidth(2);
        total_hist->SetLineStyle(2);
        passed_hist->SetLineColor(kAzure + 2);
        passed_hist->SetLineWidth(3);

        const auto [xlow, xhigh] = auto_x_range_limits(*total_hist, *passed_hist, cfg_);
        if (xhigh > xlow)
        {
            axis_hist->GetXaxis()->SetRangeUser(xlow, xhigh);
            total_hist->GetXaxis()->SetRangeUser(xlow, xhigh);
            passed_hist->GetXaxis()->SetRangeUser(xlow, xhigh);
        }

        double max_value = 0.0;
        if (cfg_.draw_distributions && cfg_.draw_total_hist)
            max_value = std::max(max_value, total_hist->GetMaximum());
        if (cfg_.draw_distributions && cfg_.draw_passed_hist)
            max_value = std::max(max_value, passed_hist->GetMaximum());
        if (max_value <= 0.0)
            max_value = 1.0;

        if (cfg_.logy)
        {
            double min_value = 0.0;
            if (cfg_.draw_distributions && cfg_.draw_total_hist)
                min_value = positive_minimum(*total_hist);
            if (cfg_.draw_distributions && cfg_.draw_passed_hist)
            {
                const double passed_min = positive_minimum(*passed_hist);
                if (min_value <= 0.0 || (passed_min > 0.0 && passed_min < min_value))
                    min_value = passed_min;
            }
            if (min_value <= 0.0)
                min_value = 0.1;
            axis_hist->SetMinimum(min_value * 0.5);
            axis_hist->SetMaximum(max_value * 10.0);
        }
        else
        {
            axis_hist->SetMinimum(0.0);
            axis_hist->SetMaximum(max_value * 1.25);
        }

        axis_hist->Draw("hist");
        if (cfg_.draw_distributions && cfg_.draw_total_hist)
            total_hist->Draw("hist same");
        if (cfg_.draw_distributions && cfg_.draw_passed_hist)
            passed_hist->Draw("hist same");

        TLegend *legend = new TLegend(0.58, 0.70, 0.88, 0.88);
        legend->SetBorderSize(0);
        legend->SetFillStyle(0);
        legend->SetTextFont(42);
        if (cfg_.draw_distributions && cfg_.draw_total_hist)
            legend->AddEntry(total_hist, cfg_.legend_total.c_str(), "l");
        if (cfg_.draw_distributions && cfg_.draw_passed_hist)
            legend->AddEntry(passed_hist, cfg_.legend_passed.c_str(), "l");

        counts_pad->Update();

        canvas->cd();
        TPad *eff_pad = new TPad((name + "_eff").c_str(), "", 0.0, 0.0, 1.0, 1.0);
        eff_pad->SetFillStyle(4000);
        eff_pad->SetFrameFillStyle(0);
        eff_pad->SetFrameLineColor(0);
        eff_pad->SetLeftMargin(counts_pad->GetLeftMargin());
        eff_pad->SetRightMargin(counts_pad->GetRightMargin());
        eff_pad->SetTopMargin(counts_pad->GetTopMargin());
        eff_pad->SetBottomMargin(counts_pad->GetBottomMargin());
        eff_pad->Draw();
        eff_pad->cd();

        TH1D *eff_axis = static_cast<TH1D *>(axis_hist->Clone((name + "_eff_axis").c_str()));
        eff_axis->SetDirectory(nullptr);
        eff_axis->Reset("ICES");
        eff_axis->SetMinimum(cfg_.eff_ymin);
        eff_axis->SetMaximum(cfg_.eff_ymax);
        eff_axis->SetLineColor(0);
        eff_axis->SetMarkerSize(0.0);
        eff_axis->SetStats(false);
        eff_axis->GetXaxis()->SetLabelSize(0.0);
        eff_axis->GetXaxis()->SetTitle("");
        eff_axis->GetYaxis()->SetLabelSize(0.0);
        eff_axis->GetYaxis()->SetTickLength(0.0);
        eff_axis->GetYaxis()->SetTitle("");
        eff_axis->Draw("hist");

        TGraphAsymmErrors *eff_graph = static_cast<TGraphAsymmErrors *>(g_eff_->Clone((name + "_graph").c_str()));
        eff_graph->SetMarkerStyle(20);
        eff_graph->SetMarkerSize(1.0);
        eff_graph->SetMarkerColor(kBlack);
        eff_graph->SetLineColor(kBlack);
        eff_graph->SetLineWidth(2);
        eff_graph->Draw("P same");
        legend->AddEntry(eff_graph, cfg_.legend_eff.c_str(), "lp");

        const double x_right = eff_axis->GetXaxis()->GetBinUpEdge(eff_axis->GetXaxis()->GetLast());
        TGaxis *right_axis = new TGaxis(x_right,
                                        cfg_.eff_ymin,
                                        x_right,
                                        cfg_.eff_ymax,
                                        cfg_.eff_ymin,
                                        cfg_.eff_ymax,
                                        510,
                                        "+L");
        right_axis->SetTitle(cfg_.y_eff_title.c_str());
        right_axis->SetLabelFont(42);
        right_axis->SetTitleFont(42);
        right_axis->SetLabelSize(0.04);
        right_axis->SetTitleSize(0.045);
        right_axis->SetTitleOffset(1.1);
        right_axis->SetLineWidth(1);
        right_axis->Draw();

        counts_pad->cd();
        legend->Draw();

        TLatex text;
        text.SetNDC(true);
        text.SetTextFont(42);
        text.SetTextSize(0.035);

        const double reported_denom = cfg_.use_weighted_events
                                          ? denom_total()
                                          : static_cast<double>(n_denom_);
        const double reported_pass = cfg_.use_weighted_events
                                         ? pass_total()
                                         : static_cast<double>(n_pass_);
        const double reported_eff = cfg_.use_weighted_events
                                        ? overall_efficiency()
                                        : (reported_denom > 0.0 ? reported_pass / reported_denom : 0.0);

        double text_y = 0.88;
        if (cfg_.draw_stats_text)
        {
            std::ostringstream stats;
            stats << std::setprecision(6);
            if (cfg_.use_weighted_events)
            {
                stats << "denom_w=" << reported_denom
                      << " pass_w=" << reported_pass
                      << " eff_w=" << reported_eff;
            }
            else
            {
                stats << "denom=" << n_denom_
                      << " pass=" << n_pass_
                      << " eff=" << reported_eff;
            }
            text.DrawLatex(0.16, text_y, stats.str().c_str());
            text_y -= 0.05;
        }

        for (const auto &line : cfg_.extra_text_lines)
        {
            text.DrawLatex(0.16, text_y, line.c_str());
            text_y -= 0.05;
        }

        if (cfg_.print_stats)
        {
            std::ostringstream msg;
            msg << std::setprecision(6)
                << "efficiency_plot"
                << " branch=" << spec_.branch_expr;
            if (cfg_.use_weighted_events)
            {
                msg << " denom_w=" << reported_denom
                    << " pass_w=" << reported_pass
                    << " efficiency_w=" << reported_eff
                    << " denom_rows=" << n_denom_
                    << " pass_rows=" << n_pass_;
            }
            else
            {
                msg << " denom=" << n_denom_
                    << " pass=" << n_pass_
                    << " efficiency=" << reported_eff;
            }
            std::cout << msg.str() << "\n";
        }

        canvas->cd();
        canvas->Update();
        return canvas;
    }

    int EfficiencyPlot::draw_and_save(const std::string &file_stem,
                                      const std::string &format) const
    {
        std::unique_ptr<TCanvas> canvas(draw());
        std::string out_path = file_stem;
        if (out_path.empty())
            out_path = sanitise_(spec_.hist_name.empty() ? spec_.branch_expr : spec_.hist_name);

        if (!format.empty())
            out_path += "." + format;
        else if (out_path.find('.') == std::string::npos)
            out_path += ".png";

        canvas->SaveAs(out_path.c_str());
        return 0;
    }

    std::string EfficiencyPlot::sanitise_(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '_' || c == '-')
                out.push_back(static_cast<char>(c));
            else
                out.push_back('_');
        }
        return out.empty() ? std::string("efficiency_plot") : out;
    }
}
