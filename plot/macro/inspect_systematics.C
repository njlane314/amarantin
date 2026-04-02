#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

namespace
{
    std::string join_csv(const std::vector<std::string> &values)
    {
        if (values.empty())
            return "-";

        std::string out;
        for (std::size_t i = 0; i < values.size(); ++i)
        {
            if (i != 0)
                out += ",";
            out += values[i];
        }
        return out;
    }

    std::string format_double(double value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        return out.str();
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const char *cache_key)
    {
        if (cache_key && *cache_key)
        {
            if (!dist.has(sample_key, cache_key))
                throw std::runtime_error("inspect_systematics: cache_key not found for sample_key");
            return cache_key;
        }

        const auto keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("inspect_systematics: no cached distributions found for sample_key");
        return keys.front();
    }

    void print_family(const char *label,
                      const DistributionIO::UniverseFamily &family)
    {
        std::cout << label
                  << " branch=" << (family.branch_name.empty() ? "-" : family.branch_name)
                  << " universes=" << family.n_variations
                  << " eigen_rank=" << family.eigen_rank
                  << " sigma_bins=" << family.sigma.size()
                  << " covariance_bins=" << family.covariance.size()
                  << " eigenvalues=" << family.eigenvalues.size()
                  << " eigenmodes=" << family.eigenmodes.size()
                  << " universe_hist_bins=" << family.universe_histograms.size()
                  << "\n";
    }
}

void inspect_systematics(const char *path = "output.dists.root",
                         const char *sample_key = "beam-s0",
                         const char *cache_key = nullptr)
{
    macro_utils::run_macro("inspect_systematics", [&]() {
        if (!path || !*path)
            throw std::runtime_error("inspect_systematics: path is required");
        if (!sample_key || !*sample_key)
            throw std::runtime_error("inspect_systematics: sample_key is required");

        DistributionIO dist(path, DistributionIO::Mode::kRead);
        const std::string selected_cache_key = pick_cache_key(dist, sample_key, cache_key);
        const auto spectrum = dist.read(sample_key, selected_cache_key);
        const double yield =
            std::accumulate(spectrum.nominal.begin(), spectrum.nominal.end(), 0.0);

        std::cout << "sample=" << sample_key
                  << " cache_key=" << selected_cache_key
                  << " nbins=" << spectrum.spec.nbins
                  << " branch=" << spectrum.spec.branch_expr
                  << " selection=" << spectrum.spec.selection_expr
                  << " yield=" << format_double(yield)
                  << "\n";
        std::cout << "detector_source_count=" << spectrum.detector_source_count
                  << " detector_template_count=" << spectrum.detector_template_count
                  << " detector_covariance_bins=" << spectrum.detector_covariance.size()
                  << " detector_envelope="
                  << ((!spectrum.detector_down.empty() && !spectrum.detector_up.empty()) ? "present" : "absent")
                  << " detector_source_labels=" << join_csv(spectrum.detector_source_labels)
                  << " detector_sample_keys=" << join_csv(spectrum.detector_sample_keys)
                  << "\n";
        std::cout << "genie_knob_source_count=" << spectrum.genie_knob_source_count
                  << " genie_knob_covariance_bins=" << spectrum.genie_knob_covariance.size()
                  << " genie_knob_source_labels=" << join_csv(spectrum.genie_knob_source_labels)
                  << "\n";
        print_family("genie", spectrum.genie);
        print_family("flux", spectrum.flux);
        print_family("reint", spectrum.reint);
        std::cout << "total_envelope="
                  << ((!spectrum.total_down.empty() && !spectrum.total_up.empty()) ? "present" : "absent")
                  << "\n";
    });
}
