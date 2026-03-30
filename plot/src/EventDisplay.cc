#include "EventDisplay.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "TCanvas.h"
#include "TColor.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TLegend.h"
#include "TStyle.h"
#include "TTree.h"

namespace plot_utils
{
    EventDisplay::EventDisplay(Spec spec, Options opt, DetectorData data, SparseIndex indices)
        : spec_(std::move(spec)), opt_(std::move(opt)), detector_(std::move(data)), indices_(std::move(indices))
    {
    }

    EventDisplay::EventDisplay(Spec spec, Options opt, SemanticData data, SparseIndex indices)
        : spec_(std::move(spec)), opt_(std::move(opt)), semantic_(std::move(data)), indices_(std::move(indices))
    {
    }

    EventDisplay::~EventDisplay() = default;

    void EventDisplay::draw(TCanvas &canvas)
    {
        setup_canvas(canvas);
        build_histogram();

        if (spec_.mode == Mode::kDetector)
        {
            draw_detector(canvas);
        }
        else
        {
            draw_semantic(canvas);
            if (opt_.show_legend)
                draw_semantic_legend();
        }

        canvas.Update();
    }

    std::pair<int, int> EventDisplay::deduce_grid(int requested_w,
                                                  int requested_h,
                                                  std::size_t flat_size)
    {
        const int cells = std::max<int>(1, static_cast<int>(flat_size));
        if (requested_w > 0 && requested_h > 0)
            return {requested_w, requested_h};
        if (requested_w > 0 && requested_h == 0)
            return {requested_w, std::max(1, static_cast<int>((cells + requested_w - 1) / requested_w))};
        if (requested_h > 0 && requested_w == 0)
            return {std::max(1, static_cast<int>((cells + requested_h - 1) / requested_h)), requested_h};

        const int width = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(cells)))));
        const int height = std::max(1, static_cast<int>((cells + width - 1) / width));
        return {width, height};
    }

    void EventDisplay::setup_canvas(TCanvas &canvas) const
    {
        canvas.SetCanvasSize(opt_.canvas_size, opt_.canvas_size);
        canvas.SetBorderMode(0);
        canvas.SetFrameBorderMode(0);
        canvas.SetFrameLineColor(0);
        canvas.SetFrameLineWidth(0);
        canvas.SetFixedAspectRatio();

        const double m = std::clamp(opt_.margin, 0.02, 0.25);
        canvas.SetTopMargin(m);
        canvas.SetBottomMargin(m);
        canvas.SetLeftMargin(m);
        canvas.SetRightMargin(m);

        gStyle->SetTitleAlign(23);
        gStyle->SetTitleX(0.5);
        gStyle->SetTitleY(1 - m / 3.0);
    }

    void EventDisplay::build_histogram()
    {
        const std::size_t flat_size =
            !indices_.empty()
                ? (static_cast<std::size_t>(*std::max_element(indices_.begin(), indices_.end())) + 1)
                : (spec_.mode == Mode::kDetector ? detector_.size() : semantic_.size());

        const auto [w, h] = deduce_grid(spec_.grid_w, spec_.grid_h, flat_size);

        hist_.reset(new TH2F(spec_.id.c_str(),
                             spec_.title.c_str(),
                             w,
                             0,
                             w,
                             h,
                             0,
                             h));
        hist_->SetDirectory(nullptr);

        const int bin_offset = 1;
        if (spec_.mode == Mode::kDetector)
        {
            const int n = static_cast<int>(detector_.size());
            if (!indices_.empty())
            {
                const int nidx = std::min<int>(n, static_cast<int>(indices_.size()));
                for (int i = 0; i < nidx; ++i)
                {
                    const std::uint32_t idx = indices_[static_cast<std::size_t>(i)];
                    const int r = static_cast<int>(idx / static_cast<std::uint32_t>(w));
                    const int c = static_cast<int>(idx % static_cast<std::uint32_t>(w));
                    if (r >= h || c >= w)
                        continue;
                    float x = detector_[static_cast<std::size_t>(i)];
                    if (opt_.use_log_z && x <= opt_.det_min)
                        x = static_cast<float>(opt_.det_min);
                    hist_->SetBinContent(c + bin_offset, r + bin_offset, x);
                }
            }
            else
            {
                for (int r = 0; r < h; ++r)
                {
                    for (int c = 0; c < w; ++c)
                    {
                        const int idx = r * w + c;
                        if (idx >= n)
                            break;
                        float x = detector_[static_cast<std::size_t>(idx)];
                        if (opt_.use_log_z && x <= opt_.det_min)
                            x = static_cast<float>(opt_.det_min);
                        hist_->SetBinContent(c + bin_offset, r + bin_offset, x);
                    }
                }
            }
        }
        else
        {
            const int n = static_cast<int>(semantic_.size());
            if (!indices_.empty())
            {
                const int nidx = std::min<int>(n, static_cast<int>(indices_.size()));
                for (int i = 0; i < nidx; ++i)
                {
                    const std::uint32_t idx = indices_[static_cast<std::size_t>(i)];
                    const int r = static_cast<int>(idx / static_cast<std::uint32_t>(w));
                    const int c = static_cast<int>(idx % static_cast<std::uint32_t>(w));
                    if (r >= h || c >= w)
                        continue;
                    hist_->SetBinContent(c + bin_offset, r + bin_offset,
                                         semantic_[static_cast<std::size_t>(i)]);
                }
            }
            else
            {
                for (int r = 0; r < h; ++r)
                {
                    for (int c = 0; c < w; ++c)
                    {
                        const int idx = r * w + c;
                        if (idx >= n)
                            break;
                        hist_->SetBinContent(c + bin_offset, r + bin_offset,
                                             semantic_[static_cast<std::size_t>(idx)]);
                    }
                }
            }
        }
    }

    void EventDisplay::style_axes() const
    {
        hist_->GetXaxis()->SetTitle("Local Wire Coordinate");
        hist_->GetYaxis()->SetTitle("Local Drift Coordinate");
        hist_->GetXaxis()->CenterTitle(true);
        hist_->GetYaxis()->CenterTitle(true);
        hist_->GetXaxis()->SetTitleOffset(0.80);
        hist_->GetYaxis()->SetTitleOffset(0.80);
        hist_->GetXaxis()->SetTickLength(0);
        hist_->GetYaxis()->SetTickLength(0);
        hist_->GetXaxis()->SetLabelSize(0);
        hist_->GetYaxis()->SetLabelSize(0);
        hist_->GetXaxis()->SetAxisColor(0);
        hist_->GetYaxis()->SetAxisColor(0);
    }

    void EventDisplay::draw_detector(TCanvas &canvas)
    {
        canvas.SetFillColor(kWhite);
        canvas.SetTicks(0, 0);
        canvas.SetLogz(opt_.use_log_z ? 1 : 0);

        hist_->SetStats(false);
        hist_->SetMinimum(opt_.det_min);
        hist_->SetMaximum(opt_.det_max);
        style_axes();
        hist_->Draw("COL");
    }

    void EventDisplay::draw_semantic(TCanvas &canvas)
    {
        constexpr int palette_size = 15;
        const int background = TColor::GetColor(230, 230, 230);
        std::array<int, palette_size> palette = {
            background,
            TColor::GetColor("#666666"),
            TColor::GetColor("#e41a1c"),
            TColor::GetColor("#377eb8"),
            TColor::GetColor("#4daf4a"),
            TColor::GetColor("#ff7f00"),
            TColor::GetColor("#984ea3"),
            TColor::GetColor("#ffff33"),
            TColor::GetColor("#1b9e77"),
            TColor::GetColor("#f781bf"),
            TColor::GetColor("#a65628"),
            TColor::GetColor("#66a61e"),
            TColor::GetColor("#e6ab02"),
            TColor::GetColor("#a6cee3"),
            TColor::GetColor("#b15928")};
        gStyle->SetPalette(palette_size, palette.data());

        canvas.SetFillColor(kWhite);
        canvas.SetFrameFillColor(background);
        canvas.SetTicks(0, 0);

        hist_->SetStats(false);
        hist_->GetZaxis()->SetRangeUser(-0.5, palette_size - 0.5);
        style_axes();
        hist_->Draw("COL");
    }

    void EventDisplay::draw_semantic_legend()
    {
        constexpr int palette_size = 15;
        const int background = TColor::GetColor(230, 230, 230);
        const double margin = std::clamp(opt_.margin, 0.02, 0.25);

        std::array<int, palette_size> counts{};
        for (int v : semantic_)
        {
            if (v >= 0 && v < palette_size)
                counts[static_cast<std::size_t>(v)]++;
        }

        std::vector<int> order(palette_size - 1);
        std::iota(order.begin(), order.end(), 1);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return counts[a] > counts[b]; });

        const double legend_y_max = 1.0 - margin - 0.01;
        const double legend_y_min = std::max(0.80, legend_y_max - 0.10);
        legend_.reset(new TLegend(0.12, legend_y_min, 0.95, legend_y_max, "", "brNDC"));
        legend_->SetNColumns(std::max(1, opt_.legend_cols));
        legend_->SetFillColor(background);
        legend_->SetFillStyle(1001);
        legend_->SetBorderSize(0);
        legend_->SetTextFont(42);
        legend_->SetTextSize(0.025);

        const std::array<const char *, palette_size> labels = {
            "#emptyset",
            "Cosmic",
            "#mu",
            "e^{-}",
            "#gamma",
            "#pi^{#pm}",
            "#pi^{0}",
            "n",
            "p",
            "K^{#pm}",
            "K^{0}",
            "#Lambda",
            "#Sigma^{#pm}",
            "#Sigma^{0}",
            "Other"};

        std::array<int, palette_size> palette = {
            background,
            TColor::GetColor("#666666"),
            TColor::GetColor("#e41a1c"),
            TColor::GetColor("#377eb8"),
            TColor::GetColor("#4daf4a"),
            TColor::GetColor("#ff7f00"),
            TColor::GetColor("#984ea3"),
            TColor::GetColor("#ffff33"),
            TColor::GetColor("#1b9e77"),
            TColor::GetColor("#f781bf"),
            TColor::GetColor("#a65628"),
            TColor::GetColor("#66a61e"),
            TColor::GetColor("#e6ab02"),
            TColor::GetColor("#a6cee3"),
            TColor::GetColor("#b15928")};

        legend_entries_.clear();
        for (int idx : order)
        {
            auto h = std::make_unique<TH1F>((spec_.id + "_leg_" + std::to_string(idx)).c_str(), "", 1, 0, 1);
            if (counts[idx] > 0)
            {
                h->SetFillColor(palette[static_cast<std::size_t>(idx)]);
                h->SetLineColor(palette[static_cast<std::size_t>(idx)]);
                h->SetLineWidth(1);
                h->SetFillStyle(1001);
                legend_->AddEntry(h.get(), labels[static_cast<std::size_t>(idx)], "f");
            }
            else
            {
                h->SetFillColor(background);
                h->SetLineColor(background);
                h->SetLineWidth(0);
                h->SetFillStyle(1001);
                legend_->AddEntry(h.get(), "", "f");
            }
            legend_entries_.push_back(std::move(h));
        }

        legend_->Draw();
    }

}
