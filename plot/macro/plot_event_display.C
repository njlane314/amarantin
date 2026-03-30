void plot_event_display(const char *read_path = nullptr,
                        const char *sample_key = nullptr,
                        Long64_t entry = 0,
                        const char *plane = "U",
                        const char *mode = "detector")
{
    macro_utils::run_macro("plot_event_display", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("read_path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("sample_key is required");
        if (!plane || !*plane)
            throw std::runtime_error("plane is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        const std::string mode_name = (mode && *mode) ? std::string(mode) : std::string("detector");
        const auto display_mode =
            (mode_name == "semantic")
                ? plot_utils::EventDisplay::Mode::kSemantic
                : plot_utils::EventDisplay::Mode::kDetector;

        plot_utils::event_display::draw_one(eventlist,
                                            sample_key,
                                            entry,
                                            plane,
                                            display_mode);
    });
}
