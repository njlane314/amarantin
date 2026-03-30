#ifndef EVENT_DISPLAY_HH
#define EVENT_DISPLAY_HH

#include <memory>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>

#include "RtypesCore.h"
#include "EventListIO.hh"

#include "TCanvas.h"
#include "TTree.h"

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
        using SparseIndex = std::vector<std::uint32_t>;

        EventDisplay(Spec spec, Options opt, DetectorData data, SparseIndex indices = {});
        EventDisplay(Spec spec, Options opt, SemanticData data, SparseIndex indices = {});
        ~EventDisplay();

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
        SparseIndex indices_;
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
            std::string idx_u = "detector_image_u_index";
            std::string idx_v = "detector_image_v_index";
            std::string idx_w = "detector_image_w_index";
            std::string det_u = "detector_image_u_adc";
            std::string det_v = "detector_image_v_adc";
            std::string det_w = "detector_image_w_adc";
            std::string sem_u = "semantic_image_u_label";
            std::string sem_v = "semantic_image_v_label";
            std::string sem_w = "semantic_image_w_label";
        };

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

        inline const std::string &index_branch_for_plane(const Branches &branches,
                                                         const std::string &plane)
        {
            if (plane == "U") return branches.idx_u;
            if (plane == "V") return branches.idx_v;
            return branches.idx_w;
        }

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

        inline TCanvas *draw_one(const EventListIO &eventlist,
                                 const std::string &sample_key,
                                 Long64_t entry,
                                 const std::string &plane,
                                 EventDisplay::Mode mode,
                                 const Branches &branches = Branches{},
                                 const EventDisplay::Options &options = EventDisplay::Options{},
                                 int grid_w = 0,
                                 int grid_h = 0)
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

            EventDisplay::Spec spec{id, title, mode, grid_w, grid_h};
            std::vector<std::uint32_t> indices =
                read_vector_branch<std::uint32_t>(tree, index_branch_for_plane(branches, plane), entry);

            if (mode == EventDisplay::Mode::kDetector)
            {
                EventDisplay::DetectorData data =
                    read_vector_branch<float>(tree, branch_for_plane(branches, plane, mode), entry);
                EventDisplay display(spec, options, std::move(data), std::move(indices));
                TCanvas *canvas = new TCanvas(id.c_str(), title.c_str(),
                                              options.canvas_size, options.canvas_size);
                display.draw(*canvas);
                return canvas;
            }

            std::vector<unsigned char> raw =
                read_vector_branch<unsigned char>(tree, branch_for_plane(branches, plane, mode), entry);
            EventDisplay::SemanticData data;
            data.reserve(raw.size());
            for (unsigned char value : raw)
                data.push_back(static_cast<int>(value));
            EventDisplay display(spec, options, std::move(data), std::move(indices));
            TCanvas *canvas = new TCanvas(id.c_str(), title.c_str(),
                                          options.canvas_size, options.canvas_size);
            display.draw(*canvas);
            return canvas;
        }
    }

    inline TCanvas *draw_event_display(const EventListIO &eventlist,
                                       const char *sample_key,
                                       Long64_t entry,
                                       const char *plane,
                                       const char *mode_name = "detector",
                                       int grid_w = 0,
                                       int grid_h = 0)
    {
        if (!sample_key || !*sample_key)
            throw std::runtime_error("plot_utils::draw_event_display: sample_key is required");
        if (!plane || !*plane)
            throw std::runtime_error("plot_utils::draw_event_display: plane is required");

        const std::string mode_text = (mode_name && *mode_name) ? std::string(mode_name) : std::string("detector");
        const EventDisplay::Mode mode =
            (mode_text == "semantic")
                ? EventDisplay::Mode::kSemantic
                : EventDisplay::Mode::kDetector;

        return event_display::draw_one(eventlist,
                                       sample_key,
                                       entry,
                                       plane,
                                       mode,
                                       event_display::Branches{},
                                       EventDisplay::Options{},
                                       grid_w,
                                       grid_h);
    }

    inline TCanvas *draw_event_display(const char *read_path,
                                       const char *sample_key,
                                       Long64_t entry,
                                       const char *plane,
                                       const char *mode_name = "detector",
                                       int grid_w = 0,
                                       int grid_h = 0)
    {
        if (!read_path || !*read_path)
            throw std::runtime_error("plot_utils::draw_event_display: read_path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("plot_utils::draw_event_display: sample_key is required");
        if (!plane || !*plane)
            throw std::runtime_error("plot_utils::draw_event_display: plane is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        return draw_event_display(eventlist,
                                  sample_key,
                                  entry,
                                  plane,
                                  mode_name,
                                  grid_w,
                                  grid_h);
    }
}

#endif // EVENT_DISPLAY_HH
