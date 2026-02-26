// SampleIO.cc
#include "SampleIO.hh"
#include "RootUtils.hh"

#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>

SampleIO::SampleIO(std::string c, std::string k) : context(std::move(c)), key(std::move(k))
{
    if (context.empty()) throw std::runtime_error("SampleIO: context is empty");
    if (key.empty()) throw std::runtime_error("SampleIO: key is empty");
}

SampleIO SampleIO::build(std::string context, std::string key,
                         const std::vector<std::string> &sample_list_path)
{
    SampleIO out(std::move(context), std::move(key));

    out.partitions.reserve(sample_list_path.size());
    for (const auto &partition_sample_list_path : sample_list_path)
    {
        if (partition_sample_list_path.empty())
            throw std::runtime_error("SampleIO::build: empty sample list path in partitions");

        ArtProvenanceIO partition_provenance(partition_sample_list_path);
        out.partitions.push_back(std::move(partition_provenance));
    }

    return out;
}

void SampleIO::write(const std::string &path) const
{
    if (context.empty()) throw std::runtime_error("SampleIO::write: context is empty");
    if (key.empty()) throw std::runtime_error("SampleIO::write: key is empty");

    TFile *f = TFile::Open(path.c_str(), "RECREATE");
    if (!f || f->IsZombie())
        throw std::runtime_error("SampleIO: failed to create: " + path);

    try
    {
        TDirectory *meta = rootu::must_dir(f, "meta", true);
        rootu::write_named(meta, "context", context);
        rootu::write_named(meta, "key", key);

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
        SampleIO out = SampleIO::build(rootu::read_named(meta, "context"),
                                       rootu::read_named(meta, "key"));

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
