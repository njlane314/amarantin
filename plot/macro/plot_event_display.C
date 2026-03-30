#include <stdexcept>

#include "EventDisplay.hh"

void plot_event_display(const char *read_path = nullptr,
                        const char *sample_key = nullptr,
                        Long64_t entry = 0,
                        const char *plane = "U",
                        const char *mode = "detector",
                        int grid_w = 0,
                        int grid_h = 0)
{
    macro_utils::run_macro("plot_event_display", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("read_path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("sample_key is required");
        if (!plane || !*plane)
            throw std::runtime_error("plane is required");

        plot_utils::draw_event_display(read_path,
                                       sample_key,
                                       entry,
                                       plane,
                                       mode,
                                       grid_w,
                                       grid_h);
    });
}
