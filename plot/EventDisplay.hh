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
#include "TTreeReader.h"
#include "TTreeReaderValue.h"

class TH2F;

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
        std::unique_ptr<TH2F> build_histogram() const;
        void style_axes(TH2F &hist) const;
        void draw_detector(TCanvas &canvas, TH2F &hist) const;
        void draw_semantic(TCanvas &canvas, TH2F &hist) const;
        void draw_semantic_legend() const;

        Spec spec_;
        Options opt_;
        DetectorData detector_;
        SemanticData semantic_;
        SparseIndex indices_;
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

            const std::string context = "plot_utils::event_display::draw_one";
            TTreeReader reader(tree);
            TTreeReaderValue<int> run_reader(reader, branches.run.c_str());
            TTreeReaderValue<int> sub_reader(reader, branches.sub.c_str());
            TTreeReaderValue<int> evt_reader(reader, branches.evt.c_str());
            TTreeReaderValue<std::vector<std::uint32_t>> index_reader(reader,
                                                                      index_branch_for_plane(branches, plane).c_str());

            if (mode == EventDisplay::Mode::kDetector)
            {
                TTreeReaderValue<std::vector<float>> data_reader(reader,
                                                                 branch_for_plane(branches, plane, mode).c_str());
                if (reader.SetEntry(entry) != TTreeReader::kEntryValid)
                    throw std::runtime_error(context + ": failed to read requested entry");

                const int run = *run_reader;
                const int sub = *sub_reader;
                const int evt = *evt_reader;

                const std::string id = plane + "_" + std::to_string(run) + "_" +
                                       std::to_string(sub) + "_" + std::to_string(evt);
                const std::string title =
                    "Detector Image, Plane " + plane +
                    " - Run " + std::to_string(run) +
                    ", Subrun " + std::to_string(sub) +
                    ", Event " + std::to_string(evt);

                EventDisplay::Spec spec{id, title, mode, grid_w, grid_h};
                EventDisplay display(spec, options, *data_reader, *index_reader);
                TCanvas *canvas = new TCanvas(id.c_str(), title.c_str(),
                                              options.canvas_size, options.canvas_size);
                display.draw(*canvas);
                return canvas;
            }

            TTreeReaderValue<std::vector<unsigned char>> raw_reader(reader,
                                                                    branch_for_plane(branches, plane, mode).c_str());
            if (reader.SetEntry(entry) != TTreeReader::kEntryValid)
                throw std::runtime_error(context + ": failed to read requested entry");

            const int run = *run_reader;
            const int sub = *sub_reader;
            const int evt = *evt_reader;
            const std::string id = plane + "_" + std::to_string(run) + "_" +
                                   std::to_string(sub) + "_" + std::to_string(evt);
            const std::string title =
                "Semantic Image, Plane " + plane +
                " - Run " + std::to_string(run) +
                ", Subrun " + std::to_string(sub) +
                ", Event " + std::to_string(evt);

            EventDisplay::SemanticData data;
            data.reserve(raw_reader->size());
            for (unsigned char value : *raw_reader)
                data.push_back(static_cast<int>(value));
            EventDisplay::Spec spec{id, title, mode, grid_w, grid_h};
            EventDisplay display(spec, options, std::move(data), *index_reader);
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
