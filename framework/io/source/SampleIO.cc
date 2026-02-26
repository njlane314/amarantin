// SampleIO.cc
#include "SampleIO.hh"
#include "RootUtils.hh"
#include "RunDatabaseService.hh"

#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>

SampleIO::SampleIO(std::string output_path)
    : output_path_(std::move(output_path))
{
    if (output_path_.empty())
        throw std::runtime_error("SampleIO: output_path_ is empty");
}

void SampleIO::build(const std::vector<std::string> &input_paths,
                     const std::string &db_path)
    : input_paths_(std::move(inputs_paths))      
{
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO: input_paths is empty");

    // make partitions_ here

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

void SampleIO::write() const
{
    if (!built_)
        throw std::runtime_error("SampleIO::write: sample has not been successfully built or read");
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO::write: input_paths_ is empty");
    if (output_path_.empty())
        throw std::runtime_error("SampleIO::write: output_path_ is empty");

    TFile *f = TFile::Open(output_path_.c_str(), "RECREATE");
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to create: " + output_path_);

    try
    {
        TDirectory *meta = rootu::must_dir(f, "meta", true);
        rootu::write_named(meta, "output_path", output_path_);

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

SampleIO SampleIO::read(const std::string &path)
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

        SampleIO out(rootu::read_named(meta, "output_path"));
        out.build(input_paths, "");

        TDirectory *s = rootu::must_dir(f, "sample", false);
        out.origin_ = origin_from(rootu::read_named(s, "origin"));
        out.variation_ = variation_from(rootu::read_named(s, "variation"));
        out.beam_ = beam_from(rootu::read_named(s, "beam"));
        out.polarity_ = polarity_from(rootu::read_named(s, "polarity"));

        out.subrun_pot_sum_ = rootu::read_param<double>(s, "subrun_pot_sum");
        out.db_tortgt_pot_sum_ = rootu::read_param<double>(s, "db_tortgt_pot_sum");
        out.normalisation_ = rootu::read_param<double>(s, "normalisation");
        out.normalised_pot_sum_ = rootu::read_param<double>(s, "normalised_pot_sum");

        if (TDirectory *pr = f->GetDirectory("part"))
        {
            const auto names = rootu::list_keys(pr);
            out.partitions_.reserve(names.size());
            for (const auto &name : names)
            {
                TDirectory *pd = pr->GetDirectory(name.c_str());
                if (!pd) throw std::runtime_error("SampleIO: missing part/" + name);
                out.partitions_.push_back(ArtProvenanceIO::read(pd));
            }
        }

        out.built_ = true;

        f->Close();
        delete f;
        return out;
    }
    catch (...)
    {
        f->Close();
        delete f;
        throw;
    }
}
