// SampleIO.cc
#include "SampleIO.hh"
#include "ArtProvenanceIO.hh"
#include "RootUtils.hh"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TTree.h>

namespace
{
    static void validate_partitions_unique(const std::vector<SampleIO::Partition> &parts)
    {
        std::vector<std::string> names;
        names.reserve(parts.size());
        for (const auto &p : parts)
        {
            if (p.name.empty()) throw std::runtime_error("SampleIO: partition with empty name");
            names.push_back(p.name);
        }
        std::sort(names.begin(), names.end());
        if (std::adjacent_find(names.begin(), names.end()) != names.end())
            throw std::runtime_error("SampleIO: duplicate partition name");
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

    Partition p;
    p.name = "all";
    ArtProvenanceIO provenance(sample_list_path);
    p.root_files = provenance.input_files();

    p.run_subruns = provenance.run_subruns();
    p.pot_sum = provenance.pot_sum();
    p.n_events = provenance.n_events();

    out.partitions.push_back(std::move(p));
    return out;
}

void SampleIO::Partition::write(TDirectory *d) const
{
    if (!d) throw std::runtime_error("SampleIO::Partition::write: null directory");
    d->cd();

    rootu::write_param<double>(d, "scale", scale);
    rootu::write_param<double>(d, "pot_sum", pot_sum);
    rootu::write_param<long long>(d, "entries", n_entries);

    {
        TTree files_t("root_files", "");
        std::string root_file;
        files_t.Branch("root_file", &root_file);
        for (const auto &p : root_files)
        {
            root_file = p;
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
        for (const auto &p : run_subruns)
        {
            run = p.first;
            subrun = p.second;
            rs.Fill();
        }
        rs.Write("run_subrun", TObject::kOverwrite);
    }
}

SampleIO::Partition SampleIO::Partition::read(TDirectory *d)
{
    if (!d) throw std::runtime_error("SampleIO::Partition::read: null directory");

    Partition p;
    p.name = d->GetName();

    p.scale = rootu::read_param<double>(d, "scale");
    p.pot_sum = rootu::read_param<double>(d, "pot_sum");
    p.n_entries = rootu::read_param<long long>(d, "entries");

    {
        auto *t = dynamic_cast<TTree *>(d->Get("root_files"));
        if (!t) throw std::runtime_error("SampleIO: missing root_files tree in part/" + p.name);

        std::string *root_file = nullptr;
        t->SetBranchAddress("root_file", &root_file);

        const Long64_t n = t->GetEntries();
        p.root_files.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            if (!root_file) throw std::runtime_error("SampleIO: root_files missing root_file in part/" + p.name);
            p.root_files.push_back(*root_file);
        }
    }

    {
        auto *t = dynamic_cast<TTree *>(d->Get("run_subrun"));
        if (!t) throw std::runtime_error("SampleIO: missing run_subrun tree in part/" + p.name);

        Int_t run = 0;
        Int_t subrun = 0;
        t->SetBranchAddress("run", &run);
        t->SetBranchAddress("subrun", &subrun);

        const Long64_t n = t->GetEntries();
        p.run_subruns.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            p.run_subruns.emplace_back(static_cast<int>(run), static_cast<int>(subrun));
        }
    }

    return p;
}

void SampleIO::write(const std::string &path) const
{
    if (context.empty()) throw std::runtime_error("SampleIO::write: context is empty");
    if (key.empty()) throw std::runtime_error("SampleIO::write: key is empty");
    validate_partitions_unique(partitions);

    std::vector<Partition> parts = partitions;
    std::sort(parts.begin(), parts.end(),
              [](const Partition &a, const Partition &b) { return a.name < b.name; });

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
        for (const auto &p : parts)
            p.write(rootu::must_subdir(pr, p.name, true, "part"));

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
                out.partitions.push_back(Partition::read(pd));
            }
        }

        f->Close();
        delete f;

        validate_partitions_unique(out.partitions);
        return out;
    }
    catch (...)
    {
        f->Close();
        delete f;
        throw;
    }
}
