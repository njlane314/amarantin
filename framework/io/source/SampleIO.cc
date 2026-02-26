// SampleIO.cc
#include "SampleIO.hh"
#include "RootUtils.hh"

#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>

SampleIO::SampleIO(std::vector<std::string> input_paths, std::string output_path)
    : input_paths_(std::move(input_paths)), output_path_(std::move(output_path))
{
    if (input_paths_.empty())
        throw std::runtime_error("SampleIO: input_paths_ is empty");

    for (const auto &partition : input_paths_)
    {
        if (partition.empty())
            throw std::runtime_error("SampleIO: empty sample partition/provenance path");
    }

    if (output_path_.empty())
        throw std::runtime_error("SampleIO: output_path_ is empty");
}

SampleIO SampleIO::build(const std::vector<std::string> &input_paths,
                         std::string output_path)
{
    SampleIO out(input_paths, std::move(output_path));

    out.partitions.reserve(out.input_paths_.size());
    for (const auto &partition_sample_list_path : out.input_paths_)
    {
        ArtProvenanceIO partition_provenance(partition_sample_list_path);
        out.partitions.push_back(std::move(partition_provenance));
    }

    return out;
}

void SampleIO::write() const
{
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
        rootu::write_named(s, "origin", origin_name(origin));
        rootu::write_named(s, "variation", variation_name(variation));
        rootu::write_named(s, "beam", beam_name(beam));
        rootu::write_named(s, "polarity", polarity_name(polarity));

        rootu::write_param<double>(s, "subrun_pot_sum", subrun_pot_sum);
        rootu::write_param<double>(s, "db_tortgt_pot_sum", db_tortgt_pot_sum);
        rootu::write_param<double>(s, "normalisation", normalisation);

        TDirectory *pr = rootu::must_dir(f, "part", true);
        for (size_t i = 0; i < partitions.size(); ++i)
            partitions[i].write(rootu::must_subdir(pr, "partition_" + std::to_string(i), true, "part"));

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

        SampleIO out = SampleIO::build(input_paths, rootu::read_named(meta, "output_path"));

        TDirectory *s = rootu::must_dir(f, "sample", false);
        out.origin = origin_from(rootu::read_named(s, "origin"));
        out.variation = variation_from(rootu::read_named(s, "variation"));
        out.beam = beam_from(rootu::read_named(s, "beam"));
        out.polarity = polarity_from(rootu::read_named(s, "polarity"));

        out.subrun_pot_sum = rootu::read_param<double>(s, "subrun_pot_sum");
        out.db_tortgt_pot_sum = rootu::read_param<double>(s, "db_tortgt_pot_sum");
        out.normalisation = rootu::read_param<double>(s, "normalisation");

        if (TDirectory *pr = f->GetDirectory("part"))
        {
            const auto names = rootu::list_keys(pr);
            out.partitions.reserve(names.size());
            for (const auto &name : names)
            {
                TDirectory *pd = pr->GetDirectory(name.c_str());
                if (!pd) throw std::runtime_error("SampleIO: missing part/" + name);
                out.partitions.push_back(ArtProvenanceIO::read(pd));
            }
        }

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
