#include <stdexcept>

#include "EfficiencyPlot.hh"

void plot_efficiency(const char *read_path = nullptr,
                     const char *branch_expr = nullptr,
                     const char *denom_sel = "true",
                     const char *pass_sel = "true",
                     int nbins = 50,
                     double xmin = 0.0,
                     double xmax = 1.0,
                     const char *sample_key = nullptr,
                     const char *extra_sel = "true",
                     bool use_weighted_events = false)
{
    macro_utils::run_macro("plot_efficiency", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("read_path is required");
        if (!branch_expr || !*branch_expr)
            throw std::runtime_error("branch_expr is required");
        if (nbins <= 0)
            throw std::runtime_error("nbins must be positive");
        if (!(xmax > xmin))
            throw std::runtime_error("xmax must be greater than xmin");

        plot_utils::EfficiencyPlot::Spec spec;
        spec.branch_expr = branch_expr;
        spec.nbins = nbins;
        spec.xmin = xmin;
        spec.xmax = xmax;
        spec.hist_name = "h_efficiency";
        spec.title = branch_expr;

        plot_utils::EfficiencyPlot::Config cfg;
        cfg.x_title = branch_expr;
        cfg.use_weighted_events = use_weighted_events;

        plot_utils::EfficiencyPlot plot(spec, cfg);
        plot.compute(read_path,
                     denom_sel ? denom_sel : "true",
                     pass_sel ? pass_sel : "true",
                     extra_sel ? extra_sel : "true",
                     sample_key);
        plot.draw("c_efficiency");
    });
}
