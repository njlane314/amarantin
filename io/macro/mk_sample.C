void mk_sample(const char *output_path = "build/sample/beam-s0.sample.root",
               const char *list_path = "samplelists/numi_fhc_run1/beam-s0.list",
               const char *origin = "data",
               const char *variation = "nominal",
               const char *beam = "numi",
               const char *polarity = "fhc",
               const char *run_db_path = nullptr)
{
    macro_utils::run_macro("mk_sample", [&]() {
        if (!output_path || std::string(output_path).empty())
            throw std::runtime_error("mk_sample: output_path is required");
        if (!list_path || std::string(list_path).empty())
            throw std::runtime_error("mk_sample: list_path is required");

        SampleIO sample;
        sample.build(list_path,
                     origin ? origin : "",
                     variation ? variation : "",
                     beam ? beam : "",
                     polarity ? polarity : "",
                     run_db_path ? run_db_path : "");
        sample.write(output_path);

        std::cout << "mk_sample: wrote " << output_path << " from " << list_path << "\n";
    });
}
