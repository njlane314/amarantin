#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "EventListIO.hh"

#include "TTree.h"

namespace
{
    std::string format_double(double value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        return out.str();
    }

    struct WeightSummary
    {
        Long64_t entries = 0;
        double sum_w_norm = 0.0;
        double sum_w_cv = 0.0;
        double sum_w = 0.0;
        double sum_w2 = 0.0;
        double min_w = std::numeric_limits<double>::infinity();
        double max_w = -std::numeric_limits<double>::infinity();
        int negative_w = 0;
        int nonfinite_w = 0;
        int inconsistent_w = 0;
    };

    WeightSummary summarise_tree(TTree *tree)
    {
        if (!tree)
            throw std::runtime_error("inspect_weights: null selected tree");
        if (!tree->GetBranch(EventListIO::event_weight_normalisation_branch_name()))
            throw std::runtime_error("inspect_weights: missing __w_norm__ branch");
        if (!tree->GetBranch(EventListIO::event_weight_central_value_branch_name()))
            throw std::runtime_error("inspect_weights: missing __w_cv__ branch");
        if (!tree->GetBranch(EventListIO::event_weight_branch_name()))
            throw std::runtime_error("inspect_weights: missing __w__ branch");
        if (!tree->GetBranch(EventListIO::event_weight_squared_branch_name()))
            throw std::runtime_error("inspect_weights: missing __w2__ branch");

        double w_norm = 0.0;
        double w_cv = 0.0;
        double w = 0.0;
        double w2 = 0.0;
        tree->SetBranchAddress(EventListIO::event_weight_normalisation_branch_name(), &w_norm);
        tree->SetBranchAddress(EventListIO::event_weight_central_value_branch_name(), &w_cv);
        tree->SetBranchAddress(EventListIO::event_weight_branch_name(), &w);
        tree->SetBranchAddress(EventListIO::event_weight_squared_branch_name(), &w2);

        WeightSummary summary;
        summary.entries = tree->GetEntries();
        for (Long64_t i = 0; i < summary.entries; ++i)
        {
            tree->GetEntry(i);
            summary.sum_w_norm += w_norm;
            summary.sum_w_cv += w_cv;
            summary.sum_w += w;
            summary.sum_w2 += w2;
            summary.min_w = std::min(summary.min_w, w);
            summary.max_w = std::max(summary.max_w, w);
            if (w < 0.0)
                ++summary.negative_w;
            if (!std::isfinite(w) || !std::isfinite(w_norm) || !std::isfinite(w_cv) || !std::isfinite(w2))
                ++summary.nonfinite_w;

            const double expected = w_norm * w_cv;
            const double scale = std::max(1.0, std::fabs(w));
            if (std::fabs(expected - w) > 1e-9 * scale)
                ++summary.inconsistent_w;
        }

        if (summary.entries == 0)
        {
            summary.min_w = 0.0;
            summary.max_w = 0.0;
        }
        return summary;
    }

    void print_summary(const std::string &sample_key,
                       const DatasetIO::Sample &sample,
                       const WeightSummary &summary)
    {
        const double mean_w =
            summary.entries > 0 ? summary.sum_w / static_cast<double>(summary.entries) : 0.0;

        std::cout << "sample=" << sample_key
                  << " origin=" << DatasetIO::Sample::origin_name(sample.origin)
                  << " variation=" << DatasetIO::Sample::variation_name(sample.variation)
                  << " nominal=" << (sample.nominal.empty() ? "-" : sample.nominal)
                  << " entries=" << summary.entries
                  << " sum_w_norm=" << format_double(summary.sum_w_norm)
                  << " sum_w_cv=" << format_double(summary.sum_w_cv)
                  << " sum_w=" << format_double(summary.sum_w)
                  << " sum_w2=" << format_double(summary.sum_w2)
                  << " mean_w=" << format_double(mean_w)
                  << " min_w=" << format_double(summary.min_w)
                  << " max_w=" << format_double(summary.max_w)
                  << " negative_w=" << summary.negative_w
                  << " nonfinite_w=" << summary.nonfinite_w
                  << " inconsistent_w=" << summary.inconsistent_w
                  << "\n";
    }
}

void inspect_weights(const char *read_path = nullptr,
                     const char *sample_key = nullptr)
{
    macro_utils::run_macro("inspect_weights", [&]() {
        if (!read_path || !*read_path)
            throw std::runtime_error("inspect_weights: read_path is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        const std::vector<std::string> keys =
            (sample_key && *sample_key) ? std::vector<std::string>{sample_key}
                                        : eventlist.sample_keys();
        if (keys.empty())
            throw std::runtime_error("inspect_weights: no samples found");

        for (const auto &key : keys)
        {
            TTree *tree = eventlist.selected_tree(key);
            if (!tree)
                throw std::runtime_error("inspect_weights: selected tree not found for sample_key: " + key);
            const auto sample = eventlist.sample(key);
            print_summary(key, sample, summarise_tree(tree));
        }
    });
}
