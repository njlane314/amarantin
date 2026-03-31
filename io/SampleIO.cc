// SampleIO.cc
#include "SampleIO.hh"
#include "bits/RootUtils.hh"
#include "bits/RunDatabaseService.hh"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
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

    using RunSubrunKey = std::pair<int, int>;

    std::string format_run_subrun_pair(const RunSubrunKey &pair)
    {
        return "(" + std::to_string(pair.first) + ", " + std::to_string(pair.second) + ")";
    }

    std::string summarise_missing_pairs(const std::vector<RunSubrunKey> &pairs,
                                        std::size_t limit)
    {
        if (pairs.empty())
            return "";

        std::string out;
        const std::size_t n = std::min(limit, pairs.size());
        for (std::size_t i = 0; i < n; ++i)
        {
            if (!out.empty())
                out += ", ";
            out += format_run_subrun_pair(pairs[i]);
        }
        if (pairs.size() > n)
            out += ", ...";
        return out;
    }

    std::map<RunSubrunKey, double> aggregate_generated_exposures(const std::vector<ShardIO> &shards)
    {
        std::map<RunSubrunKey, double> out;
        for (const auto &shard : shards)
        {
            if (!shard.generated_exposures().empty())
            {
                for (const auto &entry : shard.generated_exposures())
                    out[RunSubrunKey{entry.run, entry.subrun}] += entry.generated_exposure;
                continue;
            }

            for (const auto &pair : shard.subruns())
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

void SampleIO::load_shards(const std::vector<ShardInput> &shards)
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

    shards_.clear();
    run_subrun_normalisations_.clear();
    normalisation_mode_.clear();
    subrun_pot_sum_ = 0.0;
    db_tortgt_pot_sum_ = 0.0;
    normalisation_ = 1.0;
    normalised_pot_sum_ = 0.0;
    built_ = false;

    shards_.reserve(shards.size());
    for (const auto &shard : shards)
    {
        ShardIO shard_io(shard.sample_list_path, shard.shard);
        subrun_pot_sum_ += shard_io.subrun_pot_sum();
        shards_.push_back(std::move(shard_io));
    }
}

void SampleIO::load_run_database_normalisation(const std::string &run_db_path)
{
    run_subrun_normalisations_.clear();

    const auto generated_exposures = aggregate_generated_exposures(shards_);
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

        if (!target_exposures.empty() && target_exposures.size() != generated_exposures.size())
        {
            std::vector<RunSubrunKey> missing_pairs;
            missing_pairs.reserve(generated_exposures.size() - target_exposures.size());
            for (const auto &entry : generated_exposures)
            {
                if (target_exposures.find(entry.first) == target_exposures.end())
                    missing_pairs.push_back(entry.first);
            }

            throw std::runtime_error("SampleIO: partial run-database coverage for " +
                                     resolved_run_db_path + "; missing run/subrun pairs " +
                                     summarise_missing_pairs(missing_pairs, 8));
        }
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
        for (const auto &path : input_paths_)
        {
            input_path = path;
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
        for (size_t i = 0; i < shards_.size(); ++i)
            shards_[i].write(utils::must_subdir(pr, "partition_" + std::to_string(i), true, "part"));

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

    out.provenance_list.reserve(shards_.size());
    for (const auto &shard : shards_)
    {
        DatasetIO::Provenance provenance;
        provenance.scale = normalisation_;
        provenance.shard = shard.shard();
        provenance.sample_list_path = shard.list_path();
        provenance.input_files = shard.files();
        provenance.pot_sum = shard.subrun_pot_sum();
        provenance.n_entries = shard.entries();
        provenance.run_subruns = shard.subruns();
        provenance.generated_exposures.reserve(shard.generated_exposures().size());
        for (const auto &entry : shard.generated_exposures())
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

void SampleIO::build(const std::string &sample,
                     const std::vector<ShardInput> &shards,
                     const std::string &origin,
                     const std::string &variation,
                     const std::string &beam,
                     const std::string &polarity,
                     const std::string &run_db_path)
{
    sample_ = trim_copy(sample);
    set_metadata(origin_from(origin),
                 variation_from(variation),
                 beam_from(beam),
                 polarity_from(polarity));
    validate_metadata();
    load_shards(shards);
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

        shards_.clear();
        if (TDirectory *pr = f->GetDirectory("part"))
        {
            const auto names = utils::list_keys(pr);
            shards_.reserve(names.size());
            for (const auto &name : names)
            {
                TDirectory *pd = pr->GetDirectory(name.c_str());
                if (!pd) throw std::runtime_error("SampleIO: missing part/" + name);
                shards_.push_back(ShardIO::read(pd));
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
