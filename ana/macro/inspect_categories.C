#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "EventCategory.hh"

namespace
{
    struct CategorySummary
    {
        long long entries = 0;
        double weighted = 0.0;
    };

    std::string format_double(double value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        return out.str();
    }

    const char *category_label(int code)
    {
        switch (static_cast<event_category::EventCategory>(code))
        {
            case event_category::EventCategory::kExternal: return "external";
            case event_category::EventCategory::kOutFV: return "out_fv";
            case event_category::EventCategory::kMuCC0PiGe1P: return "mu_cc_0pi_ge1p";
            case event_category::EventCategory::kMuCC1Pi: return "mu_cc_1pi";
            case event_category::EventCategory::kMuCCPi0OrGamma: return "mu_cc_pi0_or_gamma";
            case event_category::EventCategory::kMuCCNPi: return "mu_cc_npi";
            case event_category::EventCategory::kNC: return "nc";
            case event_category::EventCategory::kSignalLambda: return "signal_lambda";
            case event_category::EventCategory::kMuCCSigma0: return "mu_cc_sigma0";
            case event_category::EventCategory::kMuCCK0: return "mu_cc_k0";
            case event_category::EventCategory::kECCC: return "eccc";
            case event_category::EventCategory::kMuCCOther: return "mu_cc_other";
            case event_category::EventCategory::kDataInclusive: return "data_inclusive";
            case event_category::EventCategory::kUnknown:
            default: return "unknown";
        }
    }

    std::vector<std::string> selected_sample_keys(const EventListIO &eventlist,
                                                  const char *sample_key)
    {
        if (sample_key && *sample_key)
            return {sample_key};
        return eventlist.sample_keys();
    }

    void inspect_sample_categories(EventListIO &eventlist,
                                   const std::string &sample_key)
    {
        const DatasetIO::Sample sample = eventlist.sample(sample_key);
        TTree *tree = eventlist.selected_tree(sample_key);
        if (!tree)
            throw std::runtime_error("inspect_categories: missing selected tree for sample_key");
        if (!tree->GetBranch(EventListIO::event_category_branch_name()))
            throw std::runtime_error("inspect_categories: selected tree is missing the event category branch");

        double weight = 1.0;
        Int_t category = 0;
        Bool_t passes_signal_definition = kFALSE;

        const bool has_weight = tree->GetBranch(EventListIO::event_weight_branch_name()) != nullptr;
        const bool has_signal_definition =
            tree->GetBranch(EventListIO::passes_signal_definition_branch_name()) != nullptr;

        if (has_weight)
            tree->SetBranchAddress(EventListIO::event_weight_branch_name(), &weight);
        tree->SetBranchAddress(EventListIO::event_category_branch_name(), &category);
        if (has_signal_definition)
        {
            tree->SetBranchAddress(EventListIO::passes_signal_definition_branch_name(),
                                   &passes_signal_definition);
        }

        std::map<int, CategorySummary> categories;
        const Long64_t entries = tree->GetEntries();
        double weighted_entries = 0.0;
        long long signal_definition_entries = 0;
        double weighted_signal_definition = 0.0;
        for (Long64_t i = 0; i < entries; ++i)
        {
            tree->GetEntry(i);
            const double current_weight = has_weight ? weight : 1.0;
            weighted_entries += current_weight;

            auto &summary = categories[category];
            ++summary.entries;
            summary.weighted += current_weight;

            if (has_signal_definition && passes_signal_definition != kFALSE)
            {
                ++signal_definition_entries;
                weighted_signal_definition += current_weight;
            }
        }

        std::cout << "sample=" << sample_key
                  << " origin=" << DatasetIO::Sample::origin_name(sample.origin)
                  << " variation=" << DatasetIO::Sample::variation_name(sample.variation)
                  << " entries=" << entries
                  << " weighted_entries=" << format_double(weighted_entries)
                  << "\n";
        if (has_signal_definition)
        {
            std::cout << "passes_signal_definition=" << signal_definition_entries
                      << " weighted_passes_signal_definition=" << format_double(weighted_signal_definition)
                      << "\n";
        }

        for (const auto &entry : categories)
        {
            std::cout << "category=" << entry.first
                      << " label=" << category_label(entry.first)
                      << " entries=" << entry.second.entries
                      << " weighted=" << format_double(entry.second.weighted)
                      << "\n";
        }
    }
}

void inspect_categories(const char *read_path = nullptr,
                        const char *sample_key = nullptr)
{
    macro_utils::run_macro("inspect_categories", [&]() {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("inspect_categories: read_path is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        const auto sample_keys = selected_sample_keys(eventlist, sample_key);
        if (sample_keys.empty())
            throw std::runtime_error("inspect_categories: no samples found");

        for (const auto &key : sample_keys)
            inspect_sample_categories(eventlist, key);
    });
}
