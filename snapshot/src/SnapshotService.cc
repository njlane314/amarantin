#include "SnapshotService.hh"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <Compression.h>
#include <ROOT/RDataFrame.hxx>
#include <ROOT/RDFHelpers.hxx>
#include <ROOT/RSnapshotOptions.hxx>

#include <TBranch.h>
#include <TFile.h>
#include <TObjArray.h>
#include <TObject.h>
#include <TTree.h>

namespace
{
    std::string selected_tree_path(const std::string &sample_key)
    {
        return "samples/" + sample_key + "/events/selected";
    }

    std::map<std::string, std::string> branch_schema(const TTree *tree)
    {
        std::map<std::string, std::string> schema;
        if (!tree)
            return schema;

        TObjArray *branches = const_cast<TTree *>(tree)->GetListOfBranches();
        if (!branches)
            return schema;

        for (int i = 0; i < branches->GetEntriesFast(); ++i)
        {
            const TBranch *branch = dynamic_cast<const TBranch *>(branches->At(i));
            if (!branch)
                continue;

            std::string type = branch->GetClassName();
            if (type.empty())
                type = branch->GetTitle();
            schema.emplace(branch->GetName(), type);
        }

        return schema;
    }

    void validate_matching_schema(const TTree *existing, const TTree *incoming)
    {
        if (branch_schema(existing) != branch_schema(incoming))
            throw std::runtime_error(
                "SnapshotService: existing output tree schema does not match incoming snapshot tree");
    }

    void append_tree_fast(const std::string &out_path,
                          const std::string &scratch_file,
                          const std::string &tree_name)
    {
        std::unique_ptr<TFile> fin(TFile::Open(scratch_file.c_str(), "READ"));
        if (!fin || fin->IsZombie())
            throw std::runtime_error("SnapshotService: failed to open scratch snapshot file: " + scratch_file);

        TTree *tin = dynamic_cast<TTree *>(fin->Get(tree_name.c_str()));
        if (!tin)
            throw std::runtime_error("SnapshotService: scratch snapshot missing tree: " + tree_name);

        std::unique_ptr<TFile> fout(TFile::Open(out_path.c_str(), "UPDATE"));
        if (!fout || fout->IsZombie())
            throw std::runtime_error("SnapshotService: failed to open output for append: " + out_path);

        TTree *tout = dynamic_cast<TTree *>(fout->Get(tree_name.c_str()));
        fout->cd();

        if (!tout)
        {
            std::unique_ptr<TTree> cloned(tin->CloneTree(-1, "fast"));
            cloned->SetName(tree_name.c_str());
            cloned->Write(tree_name.c_str(), TObject::kOverwrite);
        }
        else
        {
            validate_matching_schema(tout, tin);
            tout->SetDirectory(fout.get());
            tout->CopyEntries(tin, -1, "fast");
            tout->Write("", TObject::kOverwrite);
        }
    }

    std::filesystem::path snapshot_scratch_dir()
    {
        const std::filesystem::path scratch_dir = std::filesystem::temp_directory_path() / "amarantin_snapshot";
        std::error_code ec;
        std::filesystem::create_directories(scratch_dir, ec);
        if (ec)
        {
            throw std::runtime_error(
                "SnapshotService: failed to create scratch directory: " + scratch_dir.string() +
                " (" + ec.message() + ")");
        }
        return scratch_dir;
    }

    ROOT::RDF::RSnapshotOptions snapshot_options()
    {
        ROOT::RDF::RSnapshotOptions options;
        options.fMode = "RECREATE";
        options.fOverwriteIfExists = false;
        options.fLazy = true;
        options.fCompressionAlgorithm = ROOT::kLZ4;
        options.fCompressionLevel = 1;
        options.fAutoFlush = -50LL * 1024 * 1024;
        options.fSplitLevel = 0;
        return options;
    }

    std::vector<std::string> snapshot_columns(const SnapshotService::SnapshotSpec &spec)
    {
        if (spec.columns.empty())
            throw std::runtime_error("SnapshotService: snapshot columns must not be empty");
        return spec.columns;
    }

    ROOT::RDF::RNode apply_selection(ROOT::RDF::RNode node, const std::string &selection)
    {
        if (!selection.empty() && selection != "true")
            return node.Filter(selection, "snapshot_selection");
        return node;
    }
}

std::string SnapshotService::sanitise_root_key(std::string s)
{
    for (char &c : s)
    {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_'))
            c = '_';
    }
    if (s.empty())
        s = "sample";
    return s;
}

unsigned long long SnapshotService::snapshot_sample(const EventListIO &event_list,
                                                    const std::string &out_path,
                                                    const std::string &sample_key,
                                                    const SnapshotSpec &spec)
{
    ROOT::RDataFrame df(selected_tree_path(sample_key), event_list.path());
    ROOT::RDF::RNode node = apply_selection(df, spec.selection);

    const std::string tree_name =
        sanitise_root_key(spec.tree_name) + "_" + sanitise_root_key(sample_key);
    const std::vector<std::string> columns = snapshot_columns(spec);
    const auto options = snapshot_options();

    if (!spec.overwrite_if_exists)
    {
        std::unique_ptr<TFile> check_file(TFile::Open(out_path.c_str(), "READ"));
        if (check_file && check_file->Get(tree_name.c_str()))
            return 0;
    }

    auto count = node.Count();
    auto snapshot = node.Snapshot(tree_name, out_path, columns, options);
    ROOT::RDF::RunGraphs({count, snapshot});
    (void)snapshot.GetValue();
    return count.GetValue();
}

unsigned long long SnapshotService::snapshot_merged(const EventListIO &event_list,
                                                    const std::string &out_path,
                                                    const SnapshotSpec &spec)
{
    const auto keys = event_list.sample_keys();
    const std::vector<std::string> base_columns = snapshot_columns(spec);
    const std::filesystem::path scratch_dir = snapshot_scratch_dir();
    const std::string tree_name = sanitise_root_key(spec.tree_name);
    unsigned long long total = 0;

    for (size_t i = 0; i < keys.size(); ++i)
    {
        const std::string &sample_key = keys[i];
        ROOT::RDataFrame df(selected_tree_path(sample_key), event_list.path());
        ROOT::RDF::RNode node = apply_selection(df, spec.selection);

        std::vector<std::string> columns = base_columns;
        if (spec.include_sample_id)
        {
            node = node.Define("sample_id", [i]() { return static_cast<int>(i); });
            if (std::find(columns.begin(), columns.end(), "sample_id") == columns.end())
                columns.push_back("sample_id");
        }

        const std::string scratch_file =
            (scratch_dir / ("amarantin_snapshot_" + tree_name + "_" + sanitise_root_key(sample_key) + "_" +
                            std::to_string(::getpid()) + ".root")).string();

        auto count = node.Count();
        auto snapshot = node.Snapshot(tree_name, scratch_file, columns, snapshot_options());
        ROOT::RDF::RunGraphs({count, snapshot});
        (void)snapshot.GetValue();
        append_tree_fast(out_path, scratch_file, tree_name);
        total += count.GetValue();

        std::error_code ec;
        std::filesystem::remove(scratch_file, ec);
    }

    return total;
}
