#ifndef SIGNAL_STRENGTH_FIT_HH
#define SIGNAL_STRENGTH_FIT_HH

#include <limits>
#include <string>
#include <vector>

#include "DistributionIO.hh"

namespace fit
{
    enum class ProcessKind
    {
        kData,
        kSignal,
        kBackground
    };

    using Family = DistributionIO::UniverseFamily;

    struct Spec
    {
        std::string channel_key;
        std::string branch_expr;
        std::string selection_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
    };

    struct Process
    {
        std::string name;
        ProcessKind kind = ProcessKind::kBackground;
        std::vector<std::string> source_keys;
        std::vector<std::string> detector_source_labels;
        std::vector<std::string> detector_sample_keys;
        std::vector<std::string> genie_knob_source_labels;

        std::vector<double> nominal;
        std::vector<double> sumw2;

        std::vector<double> detector_shift_vectors;
        int detector_source_count = 0;
        std::vector<double> genie_knob_shift_vectors;
        int genie_knob_source_count = 0;
        std::vector<double> detector_down;
        std::vector<double> detector_up;
        std::vector<double> detector_templates;
        int detector_template_count = 0;

        Family genie;
        Family flux;
        Family reint;

        std::vector<double> total_down;
        std::vector<double> total_up;
    };

    struct Channel
    {
        Spec spec;
        double stat_rel_threshold = 0.0;
        std::string stat_constraint = "Poisson";
        std::vector<double> data;
        std::vector<std::string> data_source_keys;
        std::vector<Process> processes;

        const Process *find_process(const std::string &name) const;
        Process *find_process(const std::string &name);
    };

    struct Problem
    {
        std::string measurement_name = "measurement";
        std::vector<Channel> channels;
        std::string poi_name = "mu";
        double mu_start = 1.0;
        double mu_lower = 0.0;
        double mu_upper = 5.0;
        double lumi = 1.0;
        double lumi_rel_error = 0.0;
        std::vector<std::string> constant_params;
    };

    struct FitOptions
    {
        int max_iterations = 10000;
        int max_function_calls = 100000;
        int strategy = 1;
        int print_level = -1;
        double tolerance = 1e-4;
        bool compute_stat_only_interval = true;
        bool run_hesse = true;
    };

    struct ChannelResult
    {
        std::string channel_key;
        std::string branch_expr;
        std::string selection_expr;
        std::vector<std::string> observed_source_keys;
        std::vector<double> observed;
        std::vector<double> predicted_signal;
        std::vector<double> predicted_background;
        std::vector<double> predicted_total;
    };

    struct Result
    {
        bool converged = false;
        int minimizer_status = -1;
        int minimizer_status_stat = -1;
        double edm = 0.0;
        double objective = 0.0;
        double mu_hat = 1.0;
        double mu_err_total_up = std::numeric_limits<double>::quiet_NaN();
        double mu_err_total_down = std::numeric_limits<double>::quiet_NaN();
        double mu_err_stat_up = std::numeric_limits<double>::quiet_NaN();
        double mu_err_stat_down = std::numeric_limits<double>::quiet_NaN();
        bool mu_err_total_up_found = false;
        bool mu_err_total_down_found = false;
        bool mu_err_stat_up_found = false;
        bool mu_err_stat_down_found = false;
        std::vector<std::string> nuisance_names;
        std::vector<double> nuisance_values;
        std::vector<std::string> parameter_names;
        std::vector<double> parameter_values;
        std::vector<double> covariance;
        std::vector<double> predicted_signal;
        std::vector<double> predicted_background;
        std::vector<double> predicted_total;
        std::vector<ChannelResult> channels;
    };

    // Build the default signal-strength problem from one or more
    // in-memory channels assembled directly from cached DistributionIO
    // entries. Every process tagged kSignal shares the same POI.
    Problem make_independent_problem(const Channel &channel,
                                     double mu_start = 1.0,
                                     double mu_upper = 5.0);
    Problem make_independent_problem(const std::vector<Channel> &channels,
                                     double mu_start = 1.0,
                                     double mu_upper = 5.0);

    Result profile_signal_strength(const Problem &problem,
                                   const FitOptions &options = FitOptions{});
}

#endif // SIGNAL_STRENGTH_FIT_HH
