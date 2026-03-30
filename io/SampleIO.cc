// SampleIO.cc
#include "SampleIO.hh"
#include "bits/RootUtils.hh"
#include "bits/RunDatabaseService.hh"

#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TTree.h>

namespace
{
    constexpr const char *kDefaultRunDbPath = "/exp/uboone/data/uboonebeam/beamdb/run.db";
    constexpr double kRunDatabasePotScale = 1.0e12;

    std::string trim_copy(const std::string &input)
    {
        const auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";

        const auto last = input.find_last_not_of(" \t\r\n");
        return input.substr(first, last - first + 1);
    }

    std::vector<std::string> load_paths_from_file(const std::string &path)
    {
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("SampleIO::parse_input_paths: failed to open input path file: " + path);

        std::vector<std::string> paths;
        std::string line;
        while (std::getline(file, line))
        {
            const std::string trimmed = trim_copy(line);
            if (trimmed.empty() || trimmed[0] == '#')
                continue;
            paths.push_back(trimmed);
        }

        if (paths.empty())
            throw std::runtime_error("SampleIO::parse_input_paths: no input paths found in file: " + path);

        return paths;
    }

    std::string strip_comment(const std::string &line)
    {
        const std::string::size_type pos = line.find('#');
        if (pos == std::string::npos)
            return line;
        return line.substr(0, pos);
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
        std::istringstream input(line);
        std::vector<std::string> out;
        std::string field;
        while (input >> field)
            out.push_back(field);
        return out;
    }

    using RunSubrunKey = std::pair<int, int>;

    std::map<RunSubrunKey, double> aggregate_generated_exposures(const std::vector<InputPartitionIO> &partitions)
    {
        std::map<RunSubrunKey, double> out;
        for (const auto &partition : partitions)
        {
            if (!partition.generated_exposures().empty())
            {
                for (const auto &entry : partition.generated_exposures())
                    out[RunSubrunKey{entry.run, entry.subrun}] += entry.generated_exposure;
                continue;
            }

            for (const auto &pair : partition.run_subruns())
                out.emplace(pair, 0.0);
        }

        return out;
    }
}

std::string SampleIO::default_run_db_path()
{
    if (const char *env = std::getenv("AMARANTIN_RUN_DB"))
    {
        const std::string value = trim_copy(env);
        if (!value.empty())
            return value;
    }

    return kDefaultRunDbPath;
}

void SampleIO::set_metadata(Origin origin,
                            Variation variation,
                            Beam beam,
                            Polarity polarity)
{
    origin_ = origin;
    variation_ = variation;
    beam_ = beam;
    polarity_ = polarity;
}

void SampleIO::validate_metadata() const
{
    if (origin_ == Origin::kUnknown)
        throw std::runtime_error("SampleIO: unknown origin metadata");
    if (variation_ == Variation::kUnknown)
        throw std::runtime_error("SampleIO: unknown variation metadata");
    if (beam_ == Beam::kUnknown)
        throw std::runtime_error("SampleIO: unknown beam metadata");
    if (beam_ == Beam::kNuMI && polarity_ == Polarity::kUnknown)
        throw std::runtime_error("SampleIO: NuMI samples require an explicit polarity");
    if (beam_ == Beam::kBNB && polarity_ != Polarity::kUnknown)
        throw std::runtime_error("SampleIO: BNB samples must not set a polarity");
}

std::vector<std::string> SampleIO::parse_input_paths(const std::string &input_paths_spec)
{
    const std::string trimmed_spec = trim_copy(input_paths_spec);
    if (trimmed_spec.empty())
        throw std::runtime_error("SampleIO::parse_input_paths: input_paths_spec is empty");

    const std::string file_path =
        (trimmed_spec.front() == '@') ? trim_copy(trimmed_spec.substr(1)) : trimmed_spec;
    if (file_path.empty())
        throw std::runtime_error("SampleIO::parse_input_paths: input path file is empty");

    return load_paths_from_file(file_path);
}

std::vector<SampleIO::ShardInput> SampleIO::parse_manifest(const std::string &manifest_path)
{
    const std::string path = trim_copy(manifest_path);
    if (path.empty())
        throw std::runtime_error("SampleIO::parse_manifest: manifest path is empty");

    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("SampleIO::parse_manifest: failed to open manifest: " + path);

    std::vector<ShardInput> out;
    std::set<std::string> shard_names;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line))
    {
        ++line_number;
        const std::string trimmed = trim_copy(strip_comment(line));
        if (trimmed.empty())
            continue;

        const std::vector<std::string> fields = split_fields(trimmed);
        if (fields.size() != 2)
        {
            throw std::runtime_error("SampleIO::parse_manifest: expected 2 fields at line " +
                                     std::to_string(line_number) + " in " + path);
        }

        ShardInput shard;
        shard.shard = fields[0];
        shard.sample_list_path = fields[1];
        if (shard.shard.empty())
        {
            throw std::runtime_error("SampleIO::parse_manifest: empty shard name at line " +
                                     std::to_string(line_number) + " in " + path);
        }
        if (!shard_names.insert(shard.shard).second)
        {
            throw std::runtime_error("SampleIO::parse_manifest: duplicate shard name '" +
                                     shard.shard + "' in " + path);
        }

        out.push_back(std::move(shard));
    }

    if (out.empty())
        throw std::runtime_error("SampleIO::parse_manifest: manifest is empty: " + path);

    return out;
}

void SampleIO::build_from(const std::vector<std::string> &input_paths)
{
    std::vector<ShardInput> shards;
    shards.reserve(input_paths.size());
    for (const auto &path : input_paths)
        shards.push_back(ShardInput{"", path});
    build_from_shards(shards);
}

void SampleIO::build_from_shards(const std::vector<ShardInput> &shards)
{
    input_paths_.clear();
    input_paths_.reserve(shards.size());
    for (const auto &shard : shards)
    {
        const std::string sample_list_path = trim_copy(shard.sample_list_path);
        if (sample_list_path.empty())
            throw std::runtime_error("SampleIO: shard sample-list path is empty");
        input_paths_.push_back(sample_list_path);
    }
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO: input_paths is empty");

    partitions_.clear();
    run_subrun_normalisations_.clear();
    normalisation_mode_.clear();
    subrun_pot_sum_ = 0.0;
    db_tortgt_pot_sum_ = 0.0;
    normalisation_ = 1.0;
    normalised_pot_sum_ = 0.0;
    built_ = false;

    partitions_.reserve(shards.size());
    for (const auto &shard : shards)
    {
        InputPartitionIO partition_provenance(shard.sample_list_path, shard.shard);
        subrun_pot_sum_ += partition_provenance.subrun_pot_sum();
        partitions_.push_back(std::move(partition_provenance));
    }
}

void SampleIO::load_run_database_normalisation(const std::string &run_db_path)
{
    run_subrun_normalisations_.clear();

    const auto generated_exposures = aggregate_generated_exposures(partitions_);
    if (generated_exposures.empty())
    {
        normalisation_mode_.clear();
        db_tortgt_pot_sum_ = 0.0;
        return;
    }

    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(generated_exposures.size());
    for (const auto &entry : generated_exposures)
        pairs.push_back(entry.first);

    std::map<RunSubrunKey, double> target_exposures;
    const std::string resolved_run_db_path = trim_copy(run_db_path.empty() ? default_run_db_path() : run_db_path);
    if (!resolved_run_db_path.empty())
    {
        RunDatabaseService db(resolved_run_db_path);
        for (const auto &entry : db.lookup_tortgt(pairs))
            target_exposures[RunSubrunKey{entry.run, entry.subrun}] =
                entry.tortgt * kRunDatabasePotScale;
    }

    db_tortgt_pot_sum_ = 0.0;
    run_subrun_normalisations_.reserve(generated_exposures.size());
    for (const auto &entry : generated_exposures)
    {
        DatasetIO::RunSubrunNormalisation normalisation_entry;
        normalisation_entry.run = entry.first.first;
        normalisation_entry.subrun = entry.first.second;
        normalisation_entry.generated_exposure = entry.second;

        const auto target_it = target_exposures.find(entry.first);
        if (target_it != target_exposures.end())
            normalisation_entry.target_exposure = target_it->second;

        db_tortgt_pot_sum_ += normalisation_entry.target_exposure;
        run_subrun_normalisations_.push_back(normalisation_entry);
    }

    normalisation_mode_ = (db_tortgt_pot_sum_ > 0.0) ? "run_subrun_pot" : "unit";
    for (auto &entry : run_subrun_normalisations_)
    {
        if (!(entry.generated_exposure > 0.0))
        {
            entry.normalisation = 0.0;
            continue;
        }

        if (normalisation_mode_ == "unit")
        {
            entry.normalisation = 1.0;
            continue;
        }

        entry.normalisation =
            (entry.target_exposure > 0.0)
                ? (entry.target_exposure / entry.generated_exposure)
                : 0.0;
    }
}

double SampleIO::compute_normalisation(double subrun_pot_sum,
                                       double db_tortgt_pot_sum)
{
    if (subrun_pot_sum <= 0.0)
    {
        throw std::runtime_error("SampleIO::compute_normalisation: subrun_pot_sum must be > 0.");
    }

    if (db_tortgt_pot_sum <= 0.0)
    {
        return 1.0;
    }

    return db_tortgt_pot_sum / subrun_pot_sum;
}

void SampleIO::write(const std::string &output_path) const
{
    if (!built_)
        throw std::runtime_error("SampleIO::write: sample has not been successfully built or read");
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO::write: input_paths_ is empty");
    if (output_path.empty())
        throw std::runtime_error("SampleIO::write: output_path is empty");

    std::unique_ptr<TFile> f(TFile::Open(output_path.c_str(), "RECREATE"));
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to create: " + output_path);

    try
    {
        TDirectory *meta = utils::must_dir(f.get(), "meta", true);
        utils::write_named(meta, "output_path", output_path);
        meta->cd();

        TTree part_t("input_paths", "");
        std::string input_path;
        part_t.Branch("input_path", &input_path);
        for (const auto &partition : input_paths_)
        {
            input_path = partition;
            part_t.Fill();
        }
        part_t.Write("input_paths", TObject::kOverwrite);

        TDirectory *s = utils::must_dir(f.get(), "sample", true);
        utils::write_named(s, "sample", sample_);
        utils::write_named(s, "origin", origin_name(origin_));
        utils::write_named(s, "variation", variation_name(variation_));
        utils::write_named(s, "beam", beam_name(beam_));
        utils::write_named(s, "polarity", polarity_name(polarity_));
        utils::write_named(s, "normalisation_mode", normalisation_mode_);

        utils::write_param<double>(s, "subrun_pot_sum", subrun_pot_sum_);
        utils::write_param<double>(s, "db_tortgt_pot_sum", db_tortgt_pot_sum_);
        utils::write_param<double>(s, "normalisation", normalisation_);
        utils::write_param<double>(s, "normalised_pot_sum", normalised_pot_sum_);

        {
            TTree normalisation_t("run_subrun_normalisation", "");
            Int_t run = 0;
            Int_t subrun = 0;
            Double_t generated_exposure = 0.0;
            Double_t target_exposure = 0.0;
            Double_t normalisation = 1.0;
            normalisation_t.Branch("run", &run, "run/I");
            normalisation_t.Branch("subrun", &subrun, "subrun/I");
            normalisation_t.Branch("generated_exposure", &generated_exposure, "generated_exposure/D");
            normalisation_t.Branch("target_exposure", &target_exposure, "target_exposure/D");
            normalisation_t.Branch("normalisation", &normalisation, "normalisation/D");
            for (const auto &entry : run_subrun_normalisations_)
            {
                run = entry.run;
                subrun = entry.subrun;
                generated_exposure = entry.generated_exposure;
                target_exposure = entry.target_exposure;
                normalisation = entry.normalisation;
                normalisation_t.Fill();
            }
            normalisation_t.Write("run_subrun_normalisation", TObject::kOverwrite);
        }

        TDirectory *pr = utils::must_dir(f.get(), "part", true);
        for (size_t i = 0; i < partitions_.size(); ++i)
            partitions_[i].write(utils::must_subdir(pr, "partition_" + std::to_string(i), true, "part"));

        f->Write();
        f->Close();
    }
    catch (...)
    {
        f->Close();
        throw;
    }
}

DatasetIO::Sample SampleIO::to_dataset_sample() const
{
    if (!built_)
        throw std::runtime_error("SampleIO::to_dataset_sample: sample has not been successfully built or read");

    DatasetIO::Sample out;
    out.sample = sample_;
    out.origin = DatasetIO::Sample::origin_from(origin_name(origin_));
    out.variation = DatasetIO::Sample::variation_from(variation_name(variation_));
    out.beam = DatasetIO::Sample::beam_from(beam_name(beam_));
    out.polarity = DatasetIO::Sample::polarity_from(polarity_name(polarity_));
    out.normalisation_mode =
        normalisation_mode_.empty()
            ? ((normalisation_ > 0.0) ? "scalar" : std::string())
            : normalisation_mode_;
    out.run_subrun_normalisations = run_subrun_normalisations_;

    out.subrun_pot_sum = subrun_pot_sum_;
    out.db_tortgt_pot_sum = db_tortgt_pot_sum_;
    out.normalisation = normalisation_;

    out.provenance_list.reserve(partitions_.size());
    for (const auto &partition : partitions_)
    {
        DatasetIO::Provenance provenance;
        provenance.scale = normalisation_;
        provenance.shard = partition.shard();
        provenance.sample_list_path = partition.sample_list_path();
        provenance.input_files = partition.sample_files();
        provenance.pot_sum = partition.subrun_pot_sum();
        provenance.n_entries = partition.n_events();
        provenance.run_subruns = partition.run_subruns();
        provenance.generated_exposures.reserve(partition.generated_exposures().size());
        for (const auto &entry : partition.generated_exposures())
        {
            provenance.generated_exposures.push_back(
                DatasetIO::RunSubrunExposure{entry.run, entry.subrun, entry.generated_exposure});
        }

        out.root_files.insert(out.root_files.end(),
                              provenance.input_files.begin(),
                              provenance.input_files.end());
        out.provenance_list.push_back(std::move(provenance));
    }

    return out;
}

void SampleIO::build(const std::string &input_paths_spec,
                     const std::string &origin,
                     const std::string &variation,
                     const std::string &beam,
                     const std::string &polarity,
                     const std::string &run_db_path)
{
    sample_.clear();
    set_metadata(origin_from(origin),
                 variation_from(variation),
                 beam_from(beam),
                 polarity_from(polarity));
    validate_metadata();
    const std::string trimmed_spec = trim_copy(input_paths_spec);
    if (trimmed_spec.empty())
        throw std::runtime_error("SampleIO::build: input path is empty");

    if (!trimmed_spec.empty() && trimmed_spec.front() == '@')
    {
        build_from(parse_input_paths(trimmed_spec));
    }
    else
    {
        build_from_shards({ShardInput{"", trimmed_spec}});
    }
    load_run_database_normalisation(run_db_path);
    normalisation_ = compute_normalisation(subrun_pot_sum_, db_tortgt_pot_sum_);
    normalised_pot_sum_ = subrun_pot_sum_ * normalisation_;
    built_ = true;
}

void SampleIO::build_from_manifest(const std::string &sample,
                                   const std::string &manifest_path,
                                   const std::string &origin,
                                   const std::string &variation,
                                   const std::string &beam,
                                   const std::string &polarity,
                                   const std::string &run_db_path)
{
    sample_ = trim_copy(sample);
    if (sample_.empty())
        throw std::runtime_error("SampleIO::build_from_manifest: sample name is empty");

    set_metadata(origin_from(origin),
                 variation_from(variation),
                 beam_from(beam),
                 polarity_from(polarity));
    validate_metadata();
    build_from_shards(parse_manifest(manifest_path));
    load_run_database_normalisation(run_db_path);
    normalisation_ = compute_normalisation(subrun_pot_sum_, db_tortgt_pot_sum_);
    normalised_pot_sum_ = subrun_pot_sum_ * normalisation_;
    built_ = true;
}

void SampleIO::read(const std::string &path)
{
    std::unique_ptr<TFile> f(TFile::Open(path.c_str(), "READ"));
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to open: " + path);

    try
    {
        TDirectory *meta = utils::must_dir(f.get(), "meta", false);
        std::vector<std::string> input_paths;
        {
            auto *t = dynamic_cast<TTree *>(meta->Get("input_paths"));
            if (!t) throw std::runtime_error("SampleIO: missing input_paths tree in meta");

            std::string *input_path = nullptr;
            t->SetBranchAddress("input_path", &input_path);

            const Long64_t n = t->GetEntries();
            input_paths.reserve(static_cast<size_t>(n));
            for (Long64_t i = 0; i < n; ++i)
            {
                t->GetEntry(i);
                if (!input_path)
                    throw std::runtime_error("SampleIO: input_paths missing input_path");
                input_paths.push_back(*input_path);
            }
        }

        (void)utils::read_named(meta, "output_path");
        input_paths_ = std::move(input_paths);
        if (input_paths_.empty())
            throw std::runtime_error("SampleIO: input_paths is empty");

        TDirectory *s = utils::must_dir(f.get(), "sample", false);
        sample_ = utils::read_named_or(s, "sample");
        origin_ = origin_from(utils::read_named(s, "origin"));
        variation_ = variation_from(utils::read_named(s, "variation"));
        beam_ = beam_from(utils::read_named(s, "beam"));
        polarity_ = polarity_from(utils::read_named(s, "polarity"));
        normalisation_mode_ = utils::read_named_or(s, "normalisation_mode");

        subrun_pot_sum_ = utils::read_param<double>(s, "subrun_pot_sum");
        db_tortgt_pot_sum_ = utils::read_param<double>(s, "db_tortgt_pot_sum");
        normalisation_ = utils::read_param<double>(s, "normalisation");
        normalised_pot_sum_ = utils::read_param<double>(s, "normalised_pot_sum");
        run_subrun_normalisations_.clear();
        if (auto *normalisation_t = dynamic_cast<TTree *>(s->Get("run_subrun_normalisation")))
        {
            Int_t run = 0;
            Int_t subrun = 0;
            Double_t generated_exposure = 0.0;
            Double_t target_exposure = 0.0;
            Double_t normalisation = 1.0;
            normalisation_t->SetBranchAddress("run", &run);
            normalisation_t->SetBranchAddress("subrun", &subrun);
            normalisation_t->SetBranchAddress("generated_exposure", &generated_exposure);
            normalisation_t->SetBranchAddress("target_exposure", &target_exposure);
            normalisation_t->SetBranchAddress("normalisation", &normalisation);

            const Long64_t n = normalisation_t->GetEntries();
            run_subrun_normalisations_.reserve(static_cast<size_t>(n));
            for (Long64_t i = 0; i < n; ++i)
            {
                normalisation_t->GetEntry(i);
                run_subrun_normalisations_.push_back(
                    DatasetIO::RunSubrunNormalisation{
                        static_cast<int>(run),
                        static_cast<int>(subrun),
                        static_cast<double>(generated_exposure),
                        static_cast<double>(target_exposure),
                        static_cast<double>(normalisation)});
            }
        }
        if (normalisation_mode_.empty())
            normalisation_mode_ = run_subrun_normalisations_.empty()
                                      ? ((normalisation_ > 0.0) ? "scalar" : std::string())
                                      : "run_subrun_pot";

        partitions_.clear();
        if (TDirectory *pr = f->GetDirectory("part"))
        {
            const auto names = utils::list_keys(pr);
            partitions_.reserve(names.size());
            for (const auto &name : names)
            {
                TDirectory *pd = pr->GetDirectory(name.c_str());
                if (!pd) throw std::runtime_error("SampleIO: missing part/" + name);
                partitions_.push_back(InputPartitionIO::read(pd));
            }
        }

        built_ = true;

        f->Close();
        return;
    }
    catch (...)
    {
        f->Close();
        throw;
    }
}
