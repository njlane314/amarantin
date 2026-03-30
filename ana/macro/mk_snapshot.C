void mk_snapshot(const char *read_path = nullptr,
                 const char *out_path = nullptr,
                 const char *columns_csv = nullptr,
                 const char *selection = "true")
{
    macro_utils::run_macro("mk_snapshot", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("read_path is required");
        if (!out_path || !*out_path)
            throw std::runtime_error("out_path is required");
        if (!columns_csv || !*columns_csv)
            throw std::runtime_error("columns_csv is required");

        auto split_csv = [](const std::string &csv) {
            std::vector<std::string> columns;
            std::string current;
            for (char c : csv)
            {
                if (c == ',')
                {
                    if (!current.empty())
                    {
                        columns.push_back(current);
                        current.clear();
                    }
                    continue;
                }
                if (!std::isspace(static_cast<unsigned char>(c)))
                    current.push_back(c);
            }
            if (!current.empty())
                columns.push_back(current);
            return columns;
        };

        EventListIO event_list(read_path, EventListIO::Mode::kRead);
        snapshot::Spec spec;
        spec.columns = split_csv(columns_csv);
        spec.selection = (selection && *selection) ? selection : "true";
        spec.tree_name = "train";
        const auto count = snapshot::merged(event_list, out_path, spec);
        std::cout << "snapshot entries=" << count << "\n";
    });
}
