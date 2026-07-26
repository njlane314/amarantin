#include "Snapshot.hh"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
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

    class SnapshotOutputLock
    {
    public:
        explicit SnapshotOutputLock(const std::string &output_path)
            : lock_path_(output_path + ".lock"),
              descriptor_(::open(lock_path_.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666))
        {
            if (descriptor_ < 0)
            {
                throw std::runtime_error(
                    "snapshot: failed to open output lock: " + lock_path_ + ": " +
                    std::strerror(errno));
            }

            while (::flock(descriptor_, LOCK_EX) != 0)
            {
                if (errno == EINTR)
                    continue;

                const int error_number = errno;
                ::close(descriptor_);
                descriptor_ = -1;
                throw std::runtime_error(
                    "snapshot: failed to acquire output lock: " + lock_path_ + ": " +
                    std::strerror(error_number));
            }
        }

        ~SnapshotOutputLock()
        {
            if (descriptor_ >= 0)
                ::close(descriptor_);
        }

        SnapshotOutputLock(const SnapshotOutputLock &) = delete;
        SnapshotOutputLock &operator=(const SnapshotOutputLock &) = delete;

    private:
        std::string lock_path_;
        int descriptor_ = -1;
    };

    std::string unused_staged_output_path(const std::string &output_path)
    {
        const std::string base_path =
            output_path + ".tmp." + std::to_string(::getpid());

        for (std::size_t suffix = 0;; ++suffix)
        {
            const std::string candidate =
                suffix == 0 ? base_path : base_path + "." + std::to_string(suffix);
            std::error_code error;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(candidate, error);
            if ((!error && status.type() == std::filesystem::file_type::not_found) ||
                error == std::errc::no_such_file_or_directory)
            {
                return candidate;
            }
            if (error)
            {
                throw std::runtime_error(
                    "snapshot: failed to inspect staged output path: " + candidate +
                    ": " + error.message());
            }
        }
    }

    void copy_output_if_present(const std::string &output_path,
                                const std::string &staged_path)
    {
        std::error_code error;
        const bool output_exists = std::filesystem::exists(output_path, error);
        if (error)
        {
            throw std::runtime_error(
                "snapshot: failed to inspect existing output file: " + output_path + ": " +
                error.message());
        }
        if (!output_exists)
            return;

        const bool copied = std::filesystem::copy_file(
            output_path,
            staged_path,
            std::filesystem::copy_options::none,
            error);
        if (error || !copied)
        {
            const std::string detail =
                error ? error.message() : "copy did not create a file";
            throw std::runtime_error(
                "snapshot: failed to copy existing output file: " + output_path + ": " +
                detail);
        }
    }

    template <class UpdateStagedOutput>
    bool update_output_atomically(const std::string &output_path,
                                  UpdateStagedOutput update_staged_output)
    {
        const SnapshotOutputLock output_lock(output_path);
        const std::string staged_path = unused_staged_output_path(output_path);
        try
        {
            copy_output_if_present(output_path, staged_path);
            if (!update_staged_output(staged_path))
            {
                std::remove(staged_path.c_str());
                return false;
            }
            if (std::rename(staged_path.c_str(), output_path.c_str()) != 0)
            {
                const int error_number = errno;
                throw std::runtime_error(
                    "snapshot: failed to publish output file: " +
                    std::string(std::strerror(error_number)));
            }
            return true;
        }
        catch (...)
        {
            std::remove(staged_path.c_str());
            throw;
        }
    }

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
                "snapshot: existing output tree schema does not match incoming snapshot tree");
    }

    std::unique_ptr<TFile> open_existing_or_create(const std::string &out_path)
    {
        std::unique_ptr<TFile> file(TFile::Open(out_path.c_str(), "UPDATE"));
        if (file && !file->IsZombie())
            return file;

        file.reset(TFile::Open(out_path.c_str(), "RECREATE"));
        if (!file || file->IsZombie())
            throw std::runtime_error("snapshot: failed to open output file: " + out_path);
        return file;
    }

    bool tree_exists(const std::string &out_path, const std::string &tree_name)
    {
        std::error_code error;
        const bool output_exists = std::filesystem::exists(out_path, error);
        if (error)
        {
            throw std::runtime_error(
                "snapshot: failed to inspect output file: " + out_path + ": " +
                error.message());
        }
        if (!output_exists)
            return false;

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
        const bool write_failed = file->TestBit(TFile::kWriteError);
        file->Close();
        if (write_failed || file->TestBit(TFile::kWriteError))
            throw std::runtime_error("snapshot: failed to update staged output file");
    }

    void write_tree_from_scratch(const std::string &out_path,
                                 const std::string &scratch_file,
                                 const std::string &tree_name,
                                 bool append)
    {
        std::unique_ptr<TFile> source_file(TFile::Open(scratch_file.c_str(), "READ"));
        if (!source_file || source_file->IsZombie())
            throw std::runtime_error("snapshot: failed to open scratch snapshot file: " + scratch_file);

        TTree *source_tree = dynamic_cast<TTree *>(source_file->Get(tree_name.c_str()));
        if (!source_tree)
            throw std::runtime_error("snapshot: scratch snapshot missing tree: " + tree_name);

        std::unique_ptr<TFile> output_file = open_existing_or_create(out_path);
        TTree *output_tree = dynamic_cast<TTree *>(output_file->Get(tree_name.c_str()));
        output_file->cd();

        if (!output_tree || !append)
        {
            std::unique_ptr<TTree> cloned_tree(source_tree->CloneTree(-1, "fast"));
            if (!cloned_tree)
                throw std::runtime_error("snapshot: failed to clone scratch snapshot tree");
            cloned_tree->SetName(tree_name.c_str());
            if (cloned_tree->Write(tree_name.c_str(), TObject::kOverwrite) <= 0)
                throw std::runtime_error("snapshot: failed to write cloned snapshot tree");
        }
        else
        {
            validate_matching_schema(output_tree, source_tree);
            output_tree->SetDirectory(output_file.get());
            if (output_tree->CopyEntries(source_tree, -1, "fast") < 0)
                throw std::runtime_error("snapshot: failed to append scratch snapshot tree");
            if (output_tree->Write("", TObject::kOverwrite) <= 0)
                throw std::runtime_error("snapshot: failed to write appended snapshot tree");
        }

        const bool write_failed = output_file->TestBit(TFile::kWriteError);
        output_file->Close();
        if (write_failed || output_file->TestBit(TFile::kWriteError))
            throw std::runtime_error("snapshot: failed to write staged output file");
    }

    std::filesystem::path snapshot_scratch_dir()
    {
        const std::filesystem::path scratch_dir = std::filesystem::temp_directory_path() / "amarantin_snapshot";
        std::error_code ec;
        std::filesystem::create_directories(scratch_dir, ec);
        if (ec)
        {
            throw std::runtime_error(
                "snapshot: failed to create scratch directory: " + scratch_dir.string() +
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
        options.fCompressionAlgorithm =
            static_cast<ROOT::RDF::RSnapshotOptions::ECAlgo>(
                ROOT::RCompressionSetting::EAlgorithm::kLZ4);
        options.fCompressionLevel = 1;
        options.fAutoFlush = -50LL * 1024 * 1024;
        options.fSplitLevel = 0;
        return options;
    }

    std::vector<std::string> snapshot_columns(const SnapshotSpec &spec)
    {
        if (spec.columns.empty())
            throw std::runtime_error("snapshot: snapshot columns must not be empty");
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
        static std::atomic<unsigned long long> next_scratch_id{0};
        for (;;)
        {
            const unsigned long long scratch_id =
                next_scratch_id.fetch_add(1, std::memory_order_relaxed);
            const std::filesystem::path candidate =
                scratch_dir / ("amarantin_snapshot_" + tree_name + "_" + suffix + "_" +
                               std::to_string(::getpid()) + "_" +
                               std::to_string(scratch_id) + ".root");
            std::error_code error;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(candidate, error);
            if ((!error && status.type() == std::filesystem::file_type::not_found) ||
                error == std::errc::no_such_file_or_directory)
            {
                return candidate.string();
            }
            if (error)
            {
                throw std::runtime_error(
                    "snapshot: failed to inspect scratch path: " + candidate.string() +
                    ": " + error.message());
            }
        }
    }

    struct ScratchSnapshot
    {
        std::string path;
        unsigned long long entry_count = 0;
    };

    void remove_scratch_file(const std::string &scratch_file)
    {
        std::error_code error;
        std::filesystem::remove(scratch_file, error);
    }

    ScratchSnapshot create_scratch_snapshot(const EventListIO &event_list,
                                            const std::string &sample_key,
                                            const std::filesystem::path &scratch_dir,
                                            const std::string &tree_name,
                                            const SnapshotSpec &spec,
                                            std::vector<std::string> columns,
                                            const int sample_id = -1)
    {
        ROOT::RDataFrame dataframe(selected_tree_path(sample_key), event_list.path());
        ROOT::RDF::RNode node = apply_selection(dataframe, spec.selection);

        if (sample_id >= 0)
        {
            node = node.Define("sample_id", [sample_id]() { return sample_id; });
            if (std::find(columns.begin(), columns.end(), "sample_id") == columns.end())
                columns.push_back("sample_id");
        }

        const std::string scratch_file =
            scratch_file_path(scratch_dir, tree_name, snapshot::sanitise_root_key(sample_key));

        try
        {
            auto entry_count = node.Count();
            auto snapshot_result =
                node.Snapshot(tree_name, scratch_file, columns, snapshot_options());
            ROOT::RDF::RunGraphs({entry_count, snapshot_result});
            (void)snapshot_result.GetValue();
            return {scratch_file, entry_count.GetValue()};
        }
        catch (...)
        {
            remove_scratch_file(scratch_file);
            throw;
        }
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

unsigned long long snapshot::sample(const EventListIO &event_list,
                                    const std::string &out_path,
                                    const std::string &sample_key,
                                    const Spec &spec)
{
    const std::string tree_name = sanitise_root_key(spec.tree_name) + "_" + sanitise_root_key(sample_key);

    if (!spec.overwrite_if_exists && tree_exists(out_path, tree_name))
        return 0;

    const std::vector<std::string> columns = snapshot_columns(spec);
    const std::filesystem::path scratch_dir = snapshot_scratch_dir();
    const ScratchSnapshot scratch_snapshot =
        create_scratch_snapshot(event_list, sample_key, scratch_dir, tree_name, spec, columns);

    try
    {
        const bool published = update_output_atomically(
            out_path,
            [&](const std::string &staged_path)
            {
                if (!spec.overwrite_if_exists && tree_exists(staged_path, tree_name))
                    return false;
                if (spec.overwrite_if_exists)
                    delete_tree_if_present(staged_path, tree_name);
                write_tree_from_scratch(staged_path, scratch_snapshot.path, tree_name, false);
                return true;
            });
        remove_scratch_file(scratch_snapshot.path);
        return published ? scratch_snapshot.entry_count : 0;
    }
    catch (...)
    {
        remove_scratch_file(scratch_snapshot.path);
        throw;
    }
}

unsigned long long snapshot::merged(const EventListIO &event_list,
                                    const std::string &out_path,
                                    const Spec &spec)
{
    const auto keys = event_list.sample_keys();
    const std::vector<std::string> base_columns = snapshot_columns(spec);
    const std::filesystem::path scratch_dir = snapshot_scratch_dir();
    const std::string tree_name = sanitise_root_key(spec.tree_name);
    unsigned long long total_entries = 0;

    if (!spec.overwrite_if_exists && tree_exists(out_path, tree_name))
        return 0;

    const bool published = update_output_atomically(
        out_path,
        [&](const std::string &staged_path)
        {
            const bool existing_tree = tree_exists(staged_path, tree_name);
            if (!spec.overwrite_if_exists && existing_tree)
                return false;
            if (spec.overwrite_if_exists)
                delete_tree_if_present(staged_path, tree_name);

            bool append = false;
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const std::string &sample_key = keys[i];
                const int sample_id = spec.include_sample_id ? static_cast<int>(i) : -1;
                const ScratchSnapshot scratch_snapshot = create_scratch_snapshot(
                    event_list,
                    sample_key,
                    scratch_dir,
                    tree_name,
                    spec,
                    base_columns,
                    sample_id);

                try
                {
                    write_tree_from_scratch(
                        staged_path, scratch_snapshot.path, tree_name, append);
                }
                catch (...)
                {
                    remove_scratch_file(scratch_snapshot.path);
                    throw;
                }
                remove_scratch_file(scratch_snapshot.path);
                append = true;
                total_entries += scratch_snapshot.entry_count;
            }

            return existing_tree || !keys.empty();
        });

    return published ? total_entries : 0;
}
