#ifndef SYSTEMATICS_HH
#define SYSTEMATICS_HH

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "EventListIO.hh"

class TH1D;

namespace plot_utils
{
    struct HistogramSpec
    {
        std::string branch_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
        std::string selection_expr;
    };

    struct Envelope
    {
        std::vector<double> down;
        std::vector<double> up;

        bool empty() const
        {
            return down.empty() || up.empty();
        }
    };

    struct UniverseFamilyResult
    {
        std::string branch_name;
        std::size_t n_universes = 0;
        Envelope envelope;
        std::vector<double> sigma;
        std::vector<double> covariance;
        std::vector<std::vector<double>> universe_histograms;
    };

    struct SystematicsOptions
    {
        bool enable_cache = true;

        bool enable_detector = false;
        std::vector<std::string> detector_sample_keys;

        bool enable_genie = false;
        bool enable_flux = false;
        bool enable_reint = false;

        bool build_full_covariance = false;
        bool retain_universe_histograms = false;
    };

    struct SystematicsResult
    {
        std::vector<double> nominal;
        Envelope detector;

        std::optional<UniverseFamilyResult> genie;
        std::optional<UniverseFamilyResult> flux;
        std::optional<UniverseFamilyResult> reint;

        std::vector<double> total_down;
        std::vector<double> total_up;
    };

    class SystematicsEngine
    {
    public:
        static SystematicsResult evaluate(const EventListIO &eventlist,
                                          const std::string &sample_key,
                                          const HistogramSpec &spec,
                                          const SystematicsOptions &options = SystematicsOptions{});

        static void clear_cache();

        static std::unique_ptr<TH1D> make_histogram(const HistogramSpec &spec,
                                                    const std::vector<double> &bins,
                                                    const char *hist_name = "h_systematics",
                                                    const char *title = "");
    };
}

#endif // SYSTEMATICS_HH
