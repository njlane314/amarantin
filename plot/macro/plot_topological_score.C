void plot_topological_score(const char *read_path = nullptr,
                            const char *sample_key = nullptr)
{
    macro_utils::run_macro("plot_topological_score", [&]() {
        plot_utils::draw_distribution(read_path,
                                      "topological_score",
                                      50,
                                      0.0,
                                      1.0,
                                      "c_topological_score",
                                      sample_key);
    });
}
