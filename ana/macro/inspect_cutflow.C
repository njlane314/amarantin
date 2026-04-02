#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Cuts.hh"

namespace
{
    struct Stage
    {
        std::string name;
        long long passed = 0;
        double weighted_passed = 0.0;
    };

    std::string format_double(double value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        return out.str();
    }

    std::vector<std::string> selected_sample_keys(const EventListIO &eventlist,
                                                  const char *sample_key)
    {
        if (sample_key && *sample_key)
            return {sample_key};
        return eventlist.sample_keys();
    }

    void inspect_sample_cutflow(EventListIO &eventlist,
                                const std::string &sample_key)
    {
        const DatasetIO::Sample sample = eventlist.sample(sample_key);
        const EventListIO::Metadata metadata = eventlist.metadata();
        TTree *tree = eventlist.selected_tree(sample_key);
        if (!tree)
            throw std::runtime_error("inspect_cutflow: missing selected tree for sample_key");

        double weight = 1.0;
        Bool_t pass_trigger = kFALSE;
        Bool_t pass_slice = kFALSE;
        Bool_t pass_fiducial = kFALSE;
        Bool_t pass_muon = kFALSE;
        Int_t selection_pass = 0;

        const bool has_weight = tree->GetBranch(EventListIO::event_weight_branch_name()) != nullptr;
        const bool has_trigger = tree->GetBranch(cuts::trigger_branch()) != nullptr;
        const bool has_slice = tree->GetBranch(cuts::slice_branch()) != nullptr;
        const bool has_fiducial = tree->GetBranch(cuts::fiducial_branch()) != nullptr;
        const bool has_muon = tree->GetBranch(cuts::muon_branch()) != nullptr;
        const bool has_selection_pass = tree->GetBranch("selection_pass") != nullptr;

        if (has_weight)
            tree->SetBranchAddress(EventListIO::event_weight_branch_name(), &weight);
        if (has_trigger)
            tree->SetBranchAddress(cuts::trigger_branch(), &pass_trigger);
        if (has_slice)
            tree->SetBranchAddress(cuts::slice_branch(), &pass_slice);
        if (has_fiducial)
            tree->SetBranchAddress(cuts::fiducial_branch(), &pass_fiducial);
        if (has_muon)
            tree->SetBranchAddress(cuts::muon_branch(), &pass_muon);
        if (has_selection_pass)
            tree->SetBranchAddress("selection_pass", &selection_pass);

        std::vector<Stage> stages;
        if (has_trigger) stages.push_back({"trigger"});
        if (has_slice) stages.push_back({"slice"});
        if (has_fiducial) stages.push_back({"fiducial"});
        if (has_muon) stages.push_back({"muon"});
        if (has_selection_pass) stages.push_back({"selection_pass"});

        const Long64_t entries = tree->GetEntries();
        double weighted_entries = 0.0;
        for (Long64_t i = 0; i < entries; ++i)
        {
            tree->GetEntry(i);
            const double current_weight = has_weight ? weight : 1.0;
            weighted_entries += current_weight;

            for (auto &stage : stages)
            {
                bool passed = false;
                if (stage.name == "trigger")
                    passed = pass_trigger != kFALSE;
                else if (stage.name == "slice")
                    passed = pass_slice != kFALSE;
                else if (stage.name == "fiducial")
                    passed = pass_fiducial != kFALSE;
                else if (stage.name == "muon")
                    passed = pass_muon != kFALSE;
                else if (stage.name == "selection_pass")
                    passed = selection_pass != 0;

                if (!passed)
                    continue;
                ++stage.passed;
                stage.weighted_passed += current_weight;
            }
        }

        std::cout << "sample=" << sample_key
                  << " origin=" << DatasetIO::Sample::origin_name(sample.origin)
                  << " variation=" << DatasetIO::Sample::variation_name(sample.variation)
                  << " selection_name=" << (metadata.selection_name.empty() ? "-" : metadata.selection_name)
                  << " entries=" << entries
                  << " weighted_entries=" << format_double(weighted_entries)
                  << "\n";

        long long previous_entries = entries;
        double previous_weighted = weighted_entries;
        for (const auto &stage : stages)
        {
            const double eff_entries =
                entries > 0 ? static_cast<double>(stage.passed) / static_cast<double>(entries) : 0.0;
            const double eff_prev =
                previous_entries > 0 ? static_cast<double>(stage.passed) / static_cast<double>(previous_entries) : 0.0;
            const double weighted_eff_entries =
                weighted_entries > 0.0 ? stage.weighted_passed / weighted_entries : 0.0;
            const double weighted_eff_prev =
                previous_weighted > 0.0 ? stage.weighted_passed / previous_weighted : 0.0;

            std::cout << "stage=" << stage.name
                      << " passed=" << stage.passed
                      << " weighted_passed=" << format_double(stage.weighted_passed)
                      << " eff_entries=" << format_double(eff_entries)
                      << " eff_prev=" << format_double(eff_prev)
                      << " weighted_eff_entries=" << format_double(weighted_eff_entries)
                      << " weighted_eff_prev=" << format_double(weighted_eff_prev)
                      << "\n";

            previous_entries = stage.passed;
            previous_weighted = stage.weighted_passed;
        }
    }
}

void inspect_cutflow(const char *read_path = nullptr,
                     const char *sample_key = nullptr)
{
    macro_utils::run_macro("inspect_cutflow", [&]() {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("inspect_cutflow: read_path is required");

        EventListIO eventlist(read_path, EventListIO::Mode::kRead);
        const auto sample_keys = selected_sample_keys(eventlist, sample_key);
        if (sample_keys.empty())
            throw std::runtime_error("inspect_cutflow: no samples found");

        for (const auto &key : sample_keys)
            inspect_sample_cutflow(eventlist, key);
    });
}
