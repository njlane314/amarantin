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
#include <TKey.h>
#include <TObjArray.h>
#include <TObject.h>
#include <TTree.h>

namespace
{
    using SnapshotSpec = snapshot::Spec;

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

    std::unique_ptr<TFile> open_existing_or_create(const std::string &out_path)
    {
        std::unique_ptr<TFile> file(TFile::Open(out_path.c_str(), "UPDATE"));
        if (file && !file->IsZombie())
            return file;

        file.reset(TFile::Open(out_path.c_str(), "RECREATE"));
        if (!file || file->IsZombie())
            throw std::runtime_error("SnapshotService: failed to open output file: " + out_path);
        return file;
    }

    bool tree_exists(const std::string &out_path, const std::string &tree_name)
    {
        std::unique_ptr<TFile> file(TFile::Open(out_path.c_str(), "READ"));
        return file && !file->IsZombie() && file->Get(tree_name.c_str());
    }

    void delete_tree_if_present(const std::string &out_path, const std::string &tree_name)
    {
        std::unique_ptr<TFile> file(TFile::Open(out_path.c_str(), "UPDATE"));
        if (!file || file->IsZombie())
            return;
        if (file->Get(tree_name.c_str()))
            file->Delete((tree_name + ";*").c_str());
    }

    void write_tree_from_scratch(const std::string &out_path,
                                 const std::string &scratch_file,
                                 const std::string &tree_name,
                                 bool append)
    {
        std::unique_ptr<TFile> fin(TFile::Open(scratch_file.c_str(), "READ"));
        if (!fin || fin->IsZombie())
            throw std::runtime_error("SnapshotService: failed to open scratch snapshot file: " + scratch_file);

        TTree *tin = dynamic_cast<TTree *>(fin->Get(tree_name.c_str()));
        if (!tin)
            throw std::runtime_error("SnapshotService: scratch snapshot missing tree: " + tree_name);

        std::unique_ptr<TFile> fout = open_existing_or_create(out_path);
        TTree *tout = dynamic_cast<TTree *>(fout->Get(tree_name.c_str()));
        fout->cd();

        if (!tout || !append)
        {
            std::unique_ptr<TTree> cloned(tin->CloneTree(-1, "fast"));
            cloned->SetName(tree_name.c_str());
            cloned->Write(tree_name.c_str(), TObject::kOverwrite);
            return;
        }

        validate_matching_schema(tout, tin);
        tout->SetDirectory(fout.get());
        tout->CopyEntries(tin, -1, "fast");
        tout->Write("", TObject::kOverwrite);
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

    std::vector<std::string> snapshot_columns(const SnapshotSpec &spec)
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

    std::string scratch_file_path(const std::filesystem::path &scratch_dir,
                                  const std::string &tree_name,
                                  const std::string &suffix)
    {
        return (scratch_dir / ("amarantin_snapshot_" + tree_name + "_" + suffix + "_" +
                               std::to_string(::getpid()) + ".root")).string();
    }

    struct ScratchSnapshot
    {
        std::string file;
        unsigned long long count = 0;
    };

    ScratchSnapshot snapshot_to_scratch(const EventListIO &event_list,
                                        const std::string &sample_key,
                                        const std::filesystem::path &scratch_dir,
                                        const std::string &tree_name,
                                        const SnapshotSpec &spec,
                                        std::vector<std::string> columns,
                                        const int sample_id = -1)
    {
        ROOT::RDataFrame df(selected_tree_path(sample_key), event_list.path());
        ROOT::RDF::RNode node = apply_selection(df, spec.selection);

        if (sample_id >= 0)
        {
            node = node.Define("sample_id", [sample_id]() { return sample_id; });
            if (std::find(columns.begin(), columns.end(), "sample_id") == columns.end())
                columns.push_back("sample_id");
        }

        const std::string scratch_file =
            scratch_file_path(scratch_dir, tree_name, snapshot::sanitise_root_key(sample_key));

        auto count = node.Count();
        auto snapshot = node.Snapshot(tree_name, scratch_file, columns, snapshot_options());
        ROOT::RDF::RunGraphs({count, snapshot});
        (void)snapshot.GetValue();

        return {scratch_file, count.GetValue()};
    }

    void remove_scratch_file(const std::string &scratch_file)
    {
        std::error_code ec;
        std::filesystem::remove(scratch_file, ec);
    }
}

std::string snapshot::sanitise_root_key(std::string s)
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

unsigned long long snapshot::snapshot_sample(const EventListIO &event_list,
                                             const std::string &out_path,
                                             const std::string &sample_key,
                                             const Spec &spec)
{
    const std::string tree_name = sanitise_root_key(spec.tree_name) + "_" + sanitise_root_key(sample_key);

    if (!spec.overwrite_if_exists && tree_exists(out_path, tree_name))
        return 0;

    const std::vector<std::string> columns = snapshot_columns(spec);
    const std::filesystem::path scratch_dir = snapshot_scratch_dir();
    const ScratchSnapshot snap =
        snapshot_to_scratch(event_list, sample_key, scratch_dir, tree_name, spec, columns);

    if (spec.overwrite_if_exists)
        delete_tree_if_present(out_path, tree_name);
    write_tree_from_scratch(out_path, snap.file, tree_name, false);

    remove_scratch_file(snap.file);

    return snap.count;
}

unsigned long long snapshot::snapshot_merged(const EventListIO &event_list,
                                             const std::string &out_path,
                                             const Spec &spec)
{
    const auto keys = event_list.sample_keys();
    const std::vector<std::string> base_columns = snapshot_columns(spec);
    const std::filesystem::path scratch_dir = snapshot_scratch_dir();
    const std::string tree_name = sanitise_root_key(spec.tree_name);
    unsigned long long total = 0;

    if (!spec.overwrite_if_exists && tree_exists(out_path, tree_name))
        return 0;
    if (spec.overwrite_if_exists)
        delete_tree_if_present(out_path, tree_name);

    bool append = false;
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const std::string &sample_key = keys[i];
        const int sample_id = spec.include_sample_id ? static_cast<int>(i) : -1;
        const ScratchSnapshot snap =
            snapshot_to_scratch(event_list, sample_key, scratch_dir, tree_name, spec, base_columns, sample_id);

        write_tree_from_scratch(out_path, snap.file, tree_name, append);
        append = true;
        total += snap.count;

        remove_scratch_file(snap.file);
    }

    return total;
}

std::string SnapshotService::sanitise_root_key(std::string s)
{
    return snapshot::sanitise_root_key(std::move(s));
}

unsigned long long SnapshotService::snapshot_sample(const EventListIO &event_list,
                                                    const std::string &out_path,
                                                    const std::string &sample_key,
                                                    const SnapshotSpec &spec)
{
    return snapshot::snapshot_sample(event_list, out_path, sample_key, spec);
}

unsigned long long SnapshotService::snapshot_merged(const EventListIO &event_list,
                                                    const std::string &out_path,
                                                    const SnapshotSpec &spec)
{
    return snapshot::snapshot_merged(event_list, out_path, spec);
}
