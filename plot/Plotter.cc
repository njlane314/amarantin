#include "Plotter.hh"

#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "TMatrixDSym.h"
#include "TGaxis.h"
#include "TROOT.h"
#include "TStyle.h"

#include "PlottingHelper.hh"
#include "StackedHist.hh"
#include "UnstackedHist.hh"

namespace
{
    template <typename PlotType>
    void draw_plot(const plot_utils::TH1DModel &spec,
                   const plot_utils::Options &opt,
                   const std::vector<const plot_utils::Entry *> &mc,
                   const std::vector<const plot_utils::Entry *> &data)
    {
        PlotType plot(spec, opt, mc, data);
        plot.draw_and_save(opt.image_format);
    }

    template <typename PlotType>
    void draw_plot_cov(const plot_utils::TH1DModel &spec,
                       const plot_utils::Options &opt,
                       const std::vector<const plot_utils::Entry *> &mc,
                       const std::vector<const plot_utils::Entry *> &data,
                       const TMatrixDSym &total_cov)
    {
        auto cov_opt = opt;
        cov_opt.total_cov = std::make_shared<TMatrixDSym>(total_cov);
        PlotType plot(spec, std::move(cov_opt), mc, data);
        plot.draw_and_save(opt.image_format);
    }

    void apply_env_defaults(plot_utils::Options &opt)
    {
        if (opt.out_dir.empty())
            opt.out_dir = ".";
        if (opt.image_format.empty())
            opt.image_format = "png";
    }
}

namespace plot_utils
{
    Plotter::Plotter()
    {
        apply_env_defaults(opt_);
    }

    Plotter::Plotter(Options opt)
        : opt_(std::move(opt))
    {
        apply_env_defaults(opt_);
    }

    const Options &Plotter::options() const noexcept
    {
        return opt_;
    }

    Options &Plotter::options() noexcept
    {
        return opt_;
    }

    void Plotter::set_options(Options opt)
    {
        opt_ = std::move(opt);
        apply_env_defaults(opt_);
    }

    void Plotter::draw_stack(const TH1DModel &spec, const EventListIO &event_list) const
    {
        set_global_style();
        StackedHist plot(spec, opt_, event_list);
        plot.draw_and_save(opt_.image_format);
    }

    void Plotter::draw_stack(const TH1DModel &spec, const std::vector<const Entry *> &mc) const
    {
        static const std::vector<const Entry *> empty_data;
        draw_stack(spec, mc, empty_data);
    }

    void Plotter::draw_stack(const TH1DModel &spec,
                             const std::vector<const Entry *> &mc,
                             const std::vector<const Entry *> &data) const
    {
        set_global_style();
        draw_plot<StackedHist>(spec, opt_, mc, data);
    }

    void Plotter::draw_stack_cov(const TH1DModel &spec,
                                 const std::vector<const Entry *> &mc,
                                 const std::vector<const Entry *> &data,
                                 const TMatrixDSym &total_cov) const
    {
        set_global_style();
        draw_plot_cov<StackedHist>(spec, opt_, mc, data, total_cov);
    }

    void Plotter::draw_unstack(const TH1DModel &spec, const EventListIO &event_list) const
    {
        set_global_style();
        UnstackedHist plot(spec, opt_, event_list);
        plot.draw_and_save(opt_.image_format);
    }

    void Plotter::draw_unstack(const TH1DModel &spec, const std::vector<const Entry *> &mc) const
    {
        static const std::vector<const Entry *> empty_data;
        draw_unstack(spec, mc, empty_data);
    }

    void Plotter::draw_unstack(const TH1DModel &spec,
                               const std::vector<const Entry *> &mc,
                               const std::vector<const Entry *> &data) const
    {
        set_global_style();
        draw_plot<UnstackedHist>(spec, opt_, mc, data);
    }

    void Plotter::draw_unstack_cov(const TH1DModel &spec,
                                   const std::vector<const Entry *> &mc,
                                   const std::vector<const Entry *> &data,
                                   const TMatrixDSym &total_cov) const
    {
        set_global_style();
        draw_plot_cov<UnstackedHist>(spec, opt_, mc, data, total_cov);
    }

    std::string Plotter::sanitise(const std::string &name)
    {
        return TH1DModel::sanitise(name);
    }

    std::string Plotter::fmt_commas(double value, int precision)
    {
        std::ostringstream ss;
        if (precision >= 0)
            ss << std::fixed << std::setprecision(precision);
        ss << value;

        std::string text = ss.str();
        const auto pos = text.find('.');
        std::string integer = pos == std::string::npos ? text : text.substr(0, pos);
        const std::string fraction = pos == std::string::npos ? std::string{} : text.substr(pos);

        bool negative = false;
        if (!integer.empty() && integer.front() == '-')
        {
            negative = true;
            integer.erase(integer.begin());
        }

        std::string out;
        out.reserve(integer.size() + integer.size() / 3 + fraction.size() + 1);
        for (std::size_t i = 0; i < integer.size(); ++i)
        {
            if (i != 0 && (integer.size() - i) % 3 == 0)
                out.push_back(',');
            out.push_back(integer[i]);
        }
        if (negative)
            out.insert(out.begin(), '-');
        return out + fraction;
    }

    void Plotter::set_global_style() const
    {
        const int font_style = 42;
        TStyle *style = gROOT->GetStyle("PlotterStyle");
        if (!style)
            style = new TStyle("PlotterStyle", "Plotter Style");

        style->SetTitleFont(font_style, "X");
        style->SetTitleFont(font_style, "Y");
        style->SetTitleFont(font_style, "Z");
        style->SetTitleSize(0.055, "X");
        style->SetTitleSize(0.055, "Y");
        style->SetTitleSize(0.05, "Z");
        style->SetLabelFont(font_style, "X");
        style->SetLabelFont(font_style, "Y");
        style->SetLabelFont(font_style, "Z");
        style->SetLabelSize(0.045, "X");
        style->SetLabelSize(0.045, "Y");
        style->SetLabelSize(0.045, "Z");
        style->SetLabelOffset(0.005, "X");
        style->SetLabelOffset(0.005, "Y");
        style->SetLabelOffset(0.005, "Z");
        style->SetTitleOffset(1.00, "X");
        style->SetTitleOffset(1.05, "Y");
        style->SetOptStat(0);
        style->SetOptTitle(0);
        style->SetPadTickX(1);
        style->SetPadTickY(1);
        style->SetErrorX(0.0);
        style->SetLineWidth(1);
        style->SetFrameLineWidth(1);
        style->SetHistLineWidth(2);
        style->SetGridColor(17);
        TGaxis::SetMaxDigits(4);
        style->SetPadLeftMargin(0.15);
        style->SetPadRightMargin(0.05);
        style->SetPadTopMargin(0.07);
        style->SetPadBottomMargin(0.12);
        style->SetMarkerSize(1.0);
        style->SetCanvasColor(0);
        style->SetPadColor(0);
        style->SetFrameFillColor(0);
        style->SetCanvasBorderMode(0);
        style->SetPadBorderMode(0);
        style->SetStatColor(0);
        style->SetFrameBorderMode(0);
        style->SetTitleFillColor(0);
        style->SetTitleBorderSize(0);

        gROOT->SetStyle("PlotterStyle");
        gROOT->ForceStyle();
    }
}
