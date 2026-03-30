#ifndef EVENT_DISPLAY_HH
#define EVENT_DISPLAY_HH

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "EventListIO.hh"
#include "TCanvas.h"
#include "TColor.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TLegend.h"
#include "TStyle.h"
#include "TTree.h"

namespace plot_utils
{
    class EventDisplay
    {
    public:
        enum class Mode
        {
            kDetector,
            kSemantic
        };

        struct Spec
        {
            std::string id;
            std::string title;
            Mode mode = Mode::kDetector;
            int grid_w = 0;
            int grid_h = 0;
        };

        struct Options
        {
            int canvas_size = 1800;
            double margin = 0.10;
            bool use_log_z = true;

            double det_min = 1.0;
            double det_max = 1000.0;

            bool show_legend = true;
            int legend_cols = 5;
        };

        using DetectorData = std::vector<float>;
        using SemanticData = std::vector<int>;

        EventDisplay(Spec spec, Options opt, DetectorData data)
            : spec_(std::move(spec)), opt_(std::move(opt)), detector_(std::move(data))
        {
        }

        EventDisplay(Spec spec, Options opt, SemanticData data)
            : spec_(std::move(spec)), opt_(std::move(opt)), semantic_(std::move(data))
        {
        }

        void draw(TCanvas &canvas)
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

    private:
        static std::pair<int, int> deduce_grid(int requested_w,
                                               int requested_h,
                                               std::size_t flat_size)
        {
            if (requested_w > 0 && requested_h > 0)
                return {requested_w, requested_h};
            if (requested_w > 0 && requested_h == 0)
                return {requested_w, std::max(1, static_cast<int>(flat_size / requested_w))};
            if (requested_h > 0 && requested_w == 0)
                return {std::max(1, static_cast<int>(flat_size / requested_h)), requested_h};

            int dim = static_cast<int>(std::sqrt(static_cast<double>(flat_size)));
            dim = std::max(1, dim);
            return {dim, dim};
        }

        void setup_canvas(TCanvas &canvas) const
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

        void build_histogram()
        {
            const std::size_t flat_size =
                spec_.mode == Mode::kDetector ? detector_.size() : semantic_.size();
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
            else
            {
                const int n = static_cast<int>(semantic_.size());
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

        void style_axes() const
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

        void draw_detector(TCanvas &canvas)
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

        void draw_semantic(TCanvas &canvas)
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

        void draw_semantic_legend()
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

        Spec spec_;
        Options opt_;
        DetectorData detector_;
        SemanticData semantic_;
        std::unique_ptr<TH2F> hist_;
        std::unique_ptr<TLegend> legend_;
        std::vector<std::unique_ptr<TH1F>> legend_entries_;
    };

    namespace event_display
    {
        struct Branches
        {
            std::string run = "run";
            std::string sub = "sub";
            std::string evt = "evt";
            std::string det_u = "detector_image_u_adc";
            std::string det_v = "detector_image_v_adc";
            std::string det_w = "detector_image_w_adc";
            std::string sem_u = "semantic_image_u_label";
            std::string sem_v = "semantic_image_v_label";
            std::string sem_w = "semantic_image_w_label";
        };

        template <class T>
        inline std::vector<T> read_vector_branch(TTree *tree,
                                                 const std::string &branch_name,
                                                 Long64_t entry)
        {
            std::vector<T> *ptr = nullptr;
            tree->SetBranchAddress(branch_name.c_str(), &ptr);
            tree->GetEntry(entry);
            return ptr ? *ptr : std::vector<T>{};
        }

        inline int read_int_branch(TTree *tree,
                                   const std::string &branch_name,
                                   Long64_t entry)
        {
            int value = 0;
            tree->SetBranchAddress(branch_name.c_str(), &value);
            tree->GetEntry(entry);
            return value;
        }

        inline const std::string &branch_for_plane(const Branches &branches,
                                                   const std::string &plane,
                                                   EventDisplay::Mode mode)
        {
            if (mode == EventDisplay::Mode::kDetector)
            {
                if (plane == "U") return branches.det_u;
                if (plane == "V") return branches.det_v;
                return branches.det_w;
            }

            if (plane == "U") return branches.sem_u;
            if (plane == "V") return branches.sem_v;
            return branches.sem_w;
        }

        inline TCanvas *draw_one(const EventListIO &eventlist,
                                 const std::string &sample_key,
                                 Long64_t entry,
                                 const std::string &plane,
                                 EventDisplay::Mode mode,
                                 const Branches &branches = Branches{},
                                 const EventDisplay::Options &options = EventDisplay::Options{})
        {
            TTree *tree = eventlist.selected_tree(sample_key);
            if (!tree)
                throw std::runtime_error("plot_utils::event_display::draw_one: missing selected tree");
            if (entry < 0 || entry >= tree->GetEntries())
                throw std::runtime_error("plot_utils::event_display::draw_one: entry out of range");

            const int run = read_int_branch(tree, branches.run, entry);
            const int sub = read_int_branch(tree, branches.sub, entry);
            const int evt = read_int_branch(tree, branches.evt, entry);

            const std::string id = plane + "_" + std::to_string(run) + "_" +
                                   std::to_string(sub) + "_" + std::to_string(evt);
            const std::string title =
                std::string(mode == EventDisplay::Mode::kDetector ? "Detector" : "Semantic") +
                " Image, Plane " + plane +
                " - Run " + std::to_string(run) +
                ", Subrun " + std::to_string(sub) +
                ", Event " + std::to_string(evt);

            if (mode == EventDisplay::Mode::kDetector)
            {
                EventDisplay::DetectorData data =
                    read_vector_branch<float>(tree, branch_for_plane(branches, plane, mode), entry);
                EventDisplay display({id, title, mode}, options, std::move(data));
                TCanvas *canvas = new TCanvas(id.c_str(), title.c_str(),
                                              options.canvas_size, options.canvas_size);
                display.draw(*canvas);
                return canvas;
            }

            {
                std::vector<unsigned char> raw =
                    read_vector_branch<unsigned char>(tree, branch_for_plane(branches, plane, mode), entry);
                EventDisplay::SemanticData data;
                data.reserve(raw.size());
                for (unsigned char value : raw)
                    data.push_back(static_cast<int>(value));
                EventDisplay display({id, title, mode}, options, std::move(data));
                TCanvas *canvas = new TCanvas(id.c_str(), title.c_str(),
                                              options.canvas_size, options.canvas_size);
                display.draw(*canvas);
                return canvas;
            }
        }
    }
}

#endif // EVENT_DISPLAY_HH
