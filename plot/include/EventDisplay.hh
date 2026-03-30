#ifndef EVENT_DISPLAY_HH
#define EVENT_DISPLAY_HH

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "RtypesCore.h"
#include "EventListIO.hh"

class TCanvas;
class TH1F;
class TH2F;
class TLegend;

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

        EventDisplay(Spec spec, Options opt, DetectorData data);
        EventDisplay(Spec spec, Options opt, SemanticData data);

        void draw(TCanvas &canvas);

    private:
        static std::pair<int, int> deduce_grid(int requested_w,
                                               int requested_h,
                                               std::size_t flat_size);
        void setup_canvas(TCanvas &canvas) const;
        void build_histogram();
        void style_axes() const;
        void draw_detector(TCanvas &canvas);
        void draw_semantic(TCanvas &canvas);
        void draw_semantic_legend();

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

        const std::string &branch_for_plane(const Branches &branches,
                                            const std::string &plane,
                                            EventDisplay::Mode mode);

        TCanvas *draw_one(const EventListIO &eventlist,
                          const std::string &sample_key,
                          Long64_t entry,
                          const std::string &plane,
                          EventDisplay::Mode mode,
                          const Branches &branches = Branches{},
                          const EventDisplay::Options &options = EventDisplay::Options{});
    }
}

#endif // EVENT_DISPLAY_HH
