// SampleIO.cc
#include "SampleIO.hh"
#include "RootUtils.hh"

#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TTree.h>

namespace
{
    void write_provenance(TDirectory *d, const ArtProvenanceIO &provenance)
    {
        if (!d) throw std::runtime_error("SampleIO::write_provenance: null directory");
        d->cd();

        rootu::write_param<double>(d, "pot_sum", provenance.subrun_pot_sum());
        rootu::write_param<long long>(d, "entries", provenance.n_events());

        {
            TTree files_t("root_files", "");
            std::string root_file;
            files_t.Branch("root_file", &root_file);
            for (const auto &path : provenance.sample_files())
            {
                root_file = path;
                files_t.Fill();
            }
            files_t.Write("root_files", TObject::kOverwrite);
        }

        {
            TTree rs("run_subrun", "");
            Int_t run = 0;
            Int_t subrun = 0;
            rs.Branch("run", &run, "run/I");
            rs.Branch("subrun", &subrun, "subrun/I");
            for (const auto &pair : provenance.run_subruns())
            {
                run = pair.first;
                subrun = pair.second;
                rs.Fill();
            }
            rs.Write("run_subrun", TObject::kOverwrite);
        }
    }

    ArtProvenanceIO read_provenance(TDirectory *d)
    {
        if (!d) throw std::runtime_error("SampleIO::read_provenance: null directory");

        std::vector<std::string> root_files;
        {
            auto *t = dynamic_cast<TTree *>(d->Get("root_files"));
            if (!t) throw std::runtime_error("SampleIO: missing root_files tree in part/" + std::string(d->GetName()));

            std::string *root_file = nullptr;
            t->SetBranchAddress("root_file", &root_file);

            const Long64_t n = t->GetEntries();
            root_files.reserve(static_cast<size_t>(n));
            for (Long64_t i = 0; i < n; ++i)
            {
                t->GetEntry(i);
                if (!root_file) throw std::runtime_error("SampleIO: root_files missing root_file in part/" + std::string(d->GetName()));
                root_files.push_back(*root_file);
            }
        }

        return ArtProvenanceIO(std::move(root_files));
    }
} // namespace

SampleIO::SampleIO(std::string c, std::string k) : context(std::move(c)), key(std::move(k))
{
    if (context.empty()) throw std::runtime_error("SampleIO: context is empty");
    if (key.empty()) throw std::runtime_error("SampleIO: key is empty");
}

SampleIO SampleIO::build(std::string context, std::string key,
                         const std::string &sample_list_path)
{
    SampleIO out(std::move(context), std::move(key));

    if (sample_list_path.empty())
        return out;

    out.partitions.emplace_back(sample_list_path);
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
            write_provenance(rootu::must_subdir(pr, "partition_" + std::to_string(i), true, "part"), partitions[i]);

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
                out.partitions.push_back(read_provenance(pd));
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
