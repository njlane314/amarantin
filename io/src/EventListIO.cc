#include "EventListIO.hh"
#include "RootUtils.hh"

#include <memory>
#include <stdexcept>
#include <string>

#include <TChain.h>
#include <TDirectory.h>
#include <TFile.h>
#include <TObject.h>
#include <TTree.h>
#include <TTreeFormula.h>

namespace
{
    void write_sample_metadata(TDirectory *meta_dir, const DatasetIO::Sample &sample)
    {
        utils::write_named(meta_dir, "origin", DatasetIO::Sample::origin_name(sample.origin));
        utils::write_named(meta_dir, "variation", DatasetIO::Sample::variation_name(sample.variation));
        utils::write_named(meta_dir, "beam", DatasetIO::Sample::beam_name(sample.beam));
        utils::write_named(meta_dir, "polarity", DatasetIO::Sample::polarity_name(sample.polarity));

        utils::write_param<double>(meta_dir, "subrun_pot_sum", sample.subrun_pot_sum);
        utils::write_param<double>(meta_dir, "db_tortgt_pot_sum", sample.db_tortgt_pot_sum);
        utils::write_param<double>(meta_dir, "normalisation", sample.normalisation);
    }

    TTree *copy_selected_tree(TDirectory *events_dir,
                              const DatasetIO::Sample &sample,
                              const std::string &event_tree_name,
                              const std::string &selection_expr)
    {
        TChain chain(event_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("EventListIO: no input trees found for event tree " + event_tree_name);

        events_dir->cd();
        std::unique_ptr<TTreeFormula> selection(new TTreeFormula("eventlist_selection",
                                                                 selection_expr.c_str(),
                                                                 &chain));
        if (!selection || !selection->GetTree())
            throw std::runtime_error("EventListIO: failed to compile selection expression: " + selection_expr);

        std::unique_ptr<TTree> selected(chain.CloneTree(0));
        if (!selected)
            throw std::runtime_error("EventListIO: failed to clone event tree structure");
        selected->SetName("selected");
        selected->SetTitle("Selected event list");

        int current_tree_number = -1;
        const Long64_t n_entries = chain.GetEntries();
        for (Long64_t i = 0; i < n_entries; ++i)
        {
            const Long64_t local_entry = chain.LoadTree(i);
            if (local_entry < 0)
                break;

            if (chain.GetTreeNumber() != current_tree_number)
            {
                current_tree_number = chain.GetTreeNumber();
                selection->UpdateFormulaLeaves();
            }

            chain.GetEntry(i);
            if (selection->EvalInstance() != 0.0)
                selected->Fill();
        }

        selected->Write("selected", TObject::kOverwrite);
        return selected.release();
    }

    void copy_subrun_tree(TDirectory *subruns_dir,
                          const DatasetIO::Sample &sample,
                          const std::string &subrun_tree_name)
    {
        TChain chain(subrun_tree_name.c_str());
        for (const auto &path : sample.root_files)
            chain.Add(path.c_str());

        if (chain.GetNtrees() == 0)
            throw std::runtime_error("EventListIO: no input trees found for subrun tree " + subrun_tree_name);

        subruns_dir->cd();
        std::unique_ptr<TTree> copied(chain.CloneTree(-1, "fast"));
        if (!copied)
            throw std::runtime_error("EventListIO: failed to copy subrun tree");
        copied->SetName(subrun_tree_name.c_str());
        copied->Write(subrun_tree_name.c_str(), TObject::kOverwrite);
    }
}

EventListIO::EventListIO(const std::string &path, Mode mode)
    : path_(path), mode_(mode)
{
}

EventListIO::~EventListIO() = default;

void EventListIO::skim(const DatasetIO &ds,
                       const std::string &event_tree_name,
                       const std::string &subrun_tree_name,
                       const std::string &selection_expr)
{
    if (mode_ != Mode::kWrite)
        throw std::runtime_error("EventListIO: skim requires write mode");
    if (event_tree_name.empty())
        throw std::runtime_error("EventListIO: event_tree_name must not be empty");
    if (subrun_tree_name.empty())
        throw std::runtime_error("EventListIO: subrun_tree_name must not be empty");
    if (selection_expr.empty())
        throw std::runtime_error("EventListIO: selection_expr must not be empty");

    std::unique_ptr<TFile> output(TFile::Open(path_.c_str(), "RECREATE"));
    if (!output || output->IsZombie())
        throw std::runtime_error("EventListIO: failed to create: " + path_);

    try
    {
        TDirectory *meta_dir = utils::must_dir(output.get(), "meta", true);
        utils::write_named(meta_dir, "dataset_path", ds.path());
        utils::write_named(meta_dir, "dataset_context", ds.context());
        utils::write_named(meta_dir, "event_tree_name", event_tree_name);
        utils::write_named(meta_dir, "subrun_tree_name", subrun_tree_name);
        utils::write_named(meta_dir, "selection_expr", selection_expr);

        TDirectory *samples_root = utils::must_dir(output.get(), "samples", true);
        for (const auto &key : ds.sample_keys())
        {
            const DatasetIO::Sample sample = ds.sample(key);

            TDirectory *sample_dir = utils::must_subdir(samples_root, key, true, "samples");
            TDirectory *sample_meta_dir = utils::must_dir(sample_dir, "meta", true);
            TDirectory *events_dir = utils::must_dir(sample_dir, "events", true);
            TDirectory *subruns_dir = utils::must_dir(sample_dir, "subruns", true);

            write_sample_metadata(sample_meta_dir, sample);
            copy_selected_tree(events_dir, sample, event_tree_name, selection_expr);
            copy_subrun_tree(subruns_dir, sample, subrun_tree_name);
        }

        output->Write();
        output->Close();
    }
    catch (...)
    {
        output->Close();
        throw;
    }
}
