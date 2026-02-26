// SampleIO.cc
#include "SampleIO.hh"
#include "RootUtils.hh"
#include "RunDatabaseService.hh"

#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>

namespace
{
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

void SampleIO::build_from(const std::vector<std::string> &input_paths,
                          const std::string &db_path)
{
    input_paths_ = input_paths;
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO: input_paths is empty");

    partitions_.clear();
    subrun_pot_sum_ = 0.0;
    db_tortgt_pot_sum_ = 0.0;
    normalisation_ = 1.0;
    normalised_pot_sum_ = 0.0;
    built_ = false;

    partitions_.reserve(input_paths_.size());
    for (const auto &partition_sample_list_path : input_paths_)
    {
        ArtProvenanceIO partition_provenance(partition_sample_list_path);
        subrun_pot_sum_ += partition_provenance.subrun_pot_sum();
        partitions_.push_back(std::move(partition_provenance));
    }

    if (!db_path.empty())
    {
        RunDatabaseService db(db_path);
        for (const auto &partition : partitions_)
        {
            const RunInfoSums runinfo = db.sum_run_info(partition.run_subruns());
            const double db_pot_scale = 1.0e12;
            db_tortgt_pot_sum_ += runinfo.tortgt_sum * db_pot_scale;
        }
    }

    normalisation_ = compute_normalisation(subrun_pot_sum_, db_tortgt_pot_sum_);
    normalised_pot_sum_ = subrun_pot_sum_ * normalisation_;
    built_ = true;
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

    TFile *f = TFile::Open(output_path.c_str(), "RECREATE");
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to create: " + output_path);

    try
    {
        TDirectory *meta = rootu::must_dir(f, "meta", true);
        rootu::write_named(meta, "output_path", output_path);

        TTree part_t("input_paths", "");
        std::string input_path;
        part_t.Branch("input_path", &input_path);
        for (const auto &partition : input_paths_)
        {
            input_path = partition;
            part_t.Fill();
        }
        part_t.Write("input_paths", TObject::kOverwrite);

        TDirectory *s = rootu::must_dir(f, "sample", true);
        rootu::write_named(s, "origin", origin_name(origin_));
        rootu::write_named(s, "variation", variation_name(variation_));
        rootu::write_named(s, "beam", beam_name(beam_));
        rootu::write_named(s, "polarity", polarity_name(polarity_));

        rootu::write_param<double>(s, "subrun_pot_sum", subrun_pot_sum_);
        rootu::write_param<double>(s, "db_tortgt_pot_sum", db_tortgt_pot_sum_);
        rootu::write_param<double>(s, "normalisation", normalisation_);
        rootu::write_param<double>(s, "normalised_pot_sum", normalised_pot_sum_);

        TDirectory *pr = rootu::must_dir(f, "part", true);
        for (size_t i = 0; i < partitions_.size(); ++i)
            partitions_[i].write(rootu::must_subdir(pr, "partition_" + std::to_string(i), true, "part"));

        f->Write();
        f->Close();
        delete f;
    }
    catch (...)
    {
        f->Close();
        delete f;
        throw;
    }
}

void SampleIO::build(const std::string &input_paths_spec,
                     const std::string &origin,
                     const std::string &variation,
                     const std::string &beam,
                     const std::string &polarity,
                     const std::string &db_path)
{
    set_metadata(origin_from(origin),
                 variation_from(variation),
                 beam_from(beam),
                 polarity_from(polarity));
    build_from(parse_input_paths(input_paths_spec), db_path);
}

void SampleIO::read(const std::string &path)
{
    TFile *f = TFile::Open(path.c_str(), "READ");
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to open: " + path);

    try
    {
        TDirectory *meta = rootu::must_dir(f, "meta", false);
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

        (void)rootu::read_named(meta, "output_path");
        build_from(input_paths, "");

        TDirectory *s = rootu::must_dir(f, "sample", false);
        origin_ = origin_from(rootu::read_named(s, "origin"));
        variation_ = variation_from(rootu::read_named(s, "variation"));
        beam_ = beam_from(rootu::read_named(s, "beam"));
        polarity_ = polarity_from(rootu::read_named(s, "polarity"));

        subrun_pot_sum_ = rootu::read_param<double>(s, "subrun_pot_sum");
        db_tortgt_pot_sum_ = rootu::read_param<double>(s, "db_tortgt_pot_sum");
        normalisation_ = rootu::read_param<double>(s, "normalisation");
        normalised_pot_sum_ = rootu::read_param<double>(s, "normalised_pot_sum");

        partitions_.clear();
        if (TDirectory *pr = f->GetDirectory("part"))
        {
            const auto names = rootu::list_keys(pr);
            partitions_.reserve(names.size());
            for (const auto &name : names)
            {
                TDirectory *pd = pr->GetDirectory(name.c_str());
                if (!pd) throw std::runtime_error("SampleIO: missing part/" + name);
                partitions_.push_back(ArtProvenanceIO::read(pd));
            }
        }

        built_ = true;

        f->Close();
        delete f;
        return;
    }
    catch (...)
    {
        f->Close();
        delete f;
        throw;
    }
}
