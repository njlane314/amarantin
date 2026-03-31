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

    using Family = DistributionIO::Family;

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
        std::vector<std::string> detector_sample_keys;

        std::vector<double> nominal;
        std::vector<double> sumw2;

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
        std::vector<double> data;
        std::vector<std::string> data_source_keys;
        std::vector<Process> processes;

        const Process *find_process(const std::string &name) const;
        Process *find_process(const std::string &name);
    };

    enum class SourceKind
    {
        kGenieMode,
        kFluxMode,
        kReintMode,
        kDetectorTemplate,
        kDetectorEnvelope,
        kStatBin,
        kTotalEnvelope
    };

    struct ShiftTerm
    {
        std::string process_name;
        SourceKind source = SourceKind::kGenieMode;
        int index = 0;
        double coefficient = 1.0;
    };

    struct Nuisance
    {
        std::string name;
        double start_value = 0.0;
        double step = 0.1;
        double lower = -5.0;
        double upper = 5.0;
        bool constrained = true;
        double prior_center = 0.0;
        double prior_sigma = 1.0;
        bool fixed = false;
        std::vector<ShiftTerm> terms;
    };

    struct Problem
    {
        const Channel *channel = nullptr;
        std::string signal_process;
        double mu_start = 1.0;
        double mu_lower = 0.0;
        double mu_upper = 5.0;
        std::vector<Nuisance> nuisances;
    };

    struct FitOptions
    {
        int max_iterations = 10000;
        int max_function_calls = 100000;
        int scan_points = 48;
        int strategy = 1;
        int print_level = -1;
        double tolerance = 1e-4;
        bool compute_stat_only_interval = true;
        bool run_hesse = true;
    };

    struct Result
    {
        bool converged = false;
        int minimizer_status = -1;
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
    };

    // Build the default signal-strength problem from the persisted mode,
    // detector, and statistical payloads on one in-memory fit channel
    // assembled directly from cached DistributionIO entries.
    Problem make_independent_problem(const Channel &channel,
                                     const std::string &signal_process,
                                     double mu_start = 1.0,
                                     double mu_upper = 5.0);

    const char *source_kind_name(SourceKind source);

    std::vector<double> predict_bins(const Problem &problem,
                                     double mu,
                                     const std::vector<double> &nuisance_values);

    double objective(const Problem &problem,
                     double mu,
                     const std::vector<double> &nuisance_values);

    Result profile_signal_strength(const Problem &problem,
                                   const FitOptions &options = FitOptions{});
}

#endif // SIGNAL_STRENGTH_FIT_HH
