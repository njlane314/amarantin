#ifndef XSEC_FIT_HH
#define XSEC_FIT_HH

#include <string>
#include <vector>

#include "ChannelIO.hh"

namespace fit
{
    enum class FamilyKind
    {
        kGenie,
        kFlux,
        kReint
    };

    struct NuisanceTerm
    {
        std::string process_name;
        FamilyKind family = FamilyKind::kGenie;
        int mode_index = 0;
        double coefficient = 1.0;
    };

    struct NuisanceSpec
    {
        std::string name;
        double start_value = 0.0;
        double lower = -5.0;
        double upper = 5.0;
        bool constrained = true;
        double prior_center = 0.0;
        double prior_sigma = 1.0;
        bool fixed = false;
        std::vector<NuisanceTerm> terms;
    };

    struct Model
    {
        const ChannelIO::Channel *channel = nullptr;
        std::string signal_process;
        double mu_start = 1.0;
        double mu_lower = 0.0;
        double mu_upper = 5.0;
        std::vector<NuisanceSpec> nuisances;
    };

    struct FitOptions
    {
        int max_iterations = 10;
        int nuisance_passes = 8;
        int scan_points = 48;
        double tolerance = 1e-4;
        bool compute_stat_only_interval = true;
    };

    struct Result
    {
        bool converged = false;
        double objective = 0.0;
        double mu_hat = 1.0;
        double mu_err_total_up = 0.0;
        double mu_err_total_down = 0.0;
        double mu_err_stat_up = 0.0;
        double mu_err_stat_down = 0.0;
        std::vector<std::string> nuisance_names;
        std::vector<double> nuisance_values;
        std::vector<double> predicted_signal;
        std::vector<double> predicted_background;
        std::vector<double> predicted_total;
    };

    // Build the smallest useful fit model: one nuisance per persisted mode.
    Model make_independent_model(const ChannelIO::Channel &channel,
                                 const std::string &signal_process,
                                 double mu_start = 1.0,
                                 double mu_upper = 5.0);

    const char *family_kind_name(FamilyKind family);

    std::vector<double> predict_bins(const Model &model,
                                     double mu,
                                     const std::vector<double> &nuisance_values);

    double objective(const Model &model,
                     double mu,
                     const std::vector<double> &nuisance_values);

    Result profile_xsec(const Model &model,
                        const FitOptions &options = FitOptions{});
}

#endif // XSEC_FIT_HH
