#include "DatasetIO.hh"
#include "bits/RootUtils.hh"

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TDirectory.h>
#include <TFile.h>
#include <TKey.h>
#include <TNamed.h>
#include <TObjArray.h>
#include <TObjString.h>
#include <TParameter.h>
#include <TTree.h>

namespace
{
    void write_run_subrun_exposures(const char *tree_name,
                                    const std::vector<DatasetIO::RunSubrunExposure> &exposures,
                                    const std::vector<std::pair<int, int>> &fallback_pairs)
    {
        TTree tree(tree_name, "");
        Int_t run = 0;
        Int_t subrun = 0;
        Double_t generated_exposure = 0.0;
        tree.Branch("run", &run, "run/I");
        tree.Branch("subrun", &subrun, "subrun/I");
        tree.Branch("generated_exposure", &generated_exposure, "generated_exposure/D");

        if (!exposures.empty())
        {
            for (const auto &entry : exposures)
            {
                run = entry.run;
                subrun = entry.subrun;
                generated_exposure = entry.generated_exposure;
                tree.Fill();
            }
        }
        else
        {
            for (const auto &pair : fallback_pairs)
            {
                run = pair.first;
                subrun = pair.second;
                generated_exposure = 0.0;
                tree.Fill();
            }
        }

        tree.Write(tree_name, TObject::kOverwrite);
    }

    std::vector<DatasetIO::RunSubrunExposure> read_run_subrun_exposures(TDirectory *d,
                                                                        const char *tree_name,
                                                                        std::vector<std::pair<int, int>> *run_subruns_out)
    {
        auto *tree = dynamic_cast<TTree *>(d->Get(tree_name));
        if (!tree) throw std::runtime_error(std::string("DatasetIO: missing tree: ") + tree_name);

        Int_t run = 0;
        Int_t subrun = 0;
        Double_t generated_exposure = 0.0;
        tree->SetBranchAddress("run", &run);
        tree->SetBranchAddress("subrun", &subrun);

        const bool have_generated_exposure = tree->GetBranch("generated_exposure") != nullptr;
        if (have_generated_exposure)
            tree->SetBranchAddress("generated_exposure", &generated_exposure);

        const Long64_t n = tree->GetEntries();
        std::vector<DatasetIO::RunSubrunExposure> exposures;
        exposures.reserve(static_cast<size_t>(n));
        if (run_subruns_out)
            run_subruns_out->reserve(static_cast<size_t>(n));

        for (Long64_t i = 0; i < n; ++i)
        {
            tree->GetEntry(i);
            if (run_subruns_out)
                run_subruns_out->emplace_back(static_cast<int>(run), static_cast<int>(subrun));

            DatasetIO::RunSubrunExposure entry;
            entry.run = static_cast<int>(run);
            entry.subrun = static_cast<int>(subrun);
            entry.generated_exposure = have_generated_exposure ? static_cast<double>(generated_exposure) : 0.0;
            exposures.push_back(entry);
        }

        return exposures;
    }

    void write_run_subrun_normalisations(const std::vector<DatasetIO::RunSubrunNormalisation> &entries)
    {
        TTree tree("run_subrun_normalisation", "");
        Int_t run = 0;
        Int_t subrun = 0;
        Double_t generated_exposure = 0.0;
        Double_t target_exposure = 0.0;
        Double_t normalisation = 1.0;
        tree.Branch("run", &run, "run/I");
        tree.Branch("subrun", &subrun, "subrun/I");
        tree.Branch("generated_exposure", &generated_exposure, "generated_exposure/D");
        tree.Branch("target_exposure", &target_exposure, "target_exposure/D");
        tree.Branch("normalisation", &normalisation, "normalisation/D");

        for (const auto &entry : entries)
        {
            run = entry.run;
            subrun = entry.subrun;
            generated_exposure = entry.generated_exposure;
            target_exposure = entry.target_exposure;
            normalisation = entry.normalisation;
            tree.Fill();
        }

        tree.Write("run_subrun_normalisation", TObject::kOverwrite);
    }

    std::vector<DatasetIO::RunSubrunNormalisation> read_run_subrun_normalisations(TDirectory *d)
    {
        auto *tree = dynamic_cast<TTree *>(d->Get("run_subrun_normalisation"));
        if (!tree)
            return {};

        Int_t run = 0;
        Int_t subrun = 0;
        Double_t generated_exposure = 0.0;
        Double_t target_exposure = 0.0;
        Double_t normalisation = 1.0;
        tree->SetBranchAddress("run", &run);
        tree->SetBranchAddress("subrun", &subrun);
        tree->SetBranchAddress("generated_exposure", &generated_exposure);
        tree->SetBranchAddress("target_exposure", &target_exposure);
        tree->SetBranchAddress("normalisation", &normalisation);

        const Long64_t n = tree->GetEntries();
        std::vector<DatasetIO::RunSubrunNormalisation> out;
        out.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            tree->GetEntry(i);
            DatasetIO::RunSubrunNormalisation entry;
            entry.run = static_cast<int>(run);
            entry.subrun = static_cast<int>(subrun);
            entry.generated_exposure = static_cast<double>(generated_exposure);
            entry.target_exposure = static_cast<double>(target_exposure);
            entry.normalisation = static_cast<double>(normalisation);
            out.push_back(entry);
        }

        return out;
    }

    int read_optional_provenance_count(TDirectory *d)
    {
        TObject *obj = d ? d->Get("provenance_count") : nullptr;
        auto *count = dynamic_cast<TParameter<int> *>(obj);
        if (!count)
            return -1;
        return count->GetVal();
    }
}

const char *DatasetIO::Sample::origin_name(Origin o)
{
    switch (o)
    {
        case Origin::kData: return "data";
        case Origin::kExternal: return "external";
        case Origin::kOverlay: return "overlay";
        case Origin::kDirt: return "dirt";
        case Origin::kSignal: return "signal";
        default: return "unknown";
    }
}

DatasetIO::Sample::Origin DatasetIO::Sample::origin_from(const std::string &s)
{
    const std::string o = utils::lower(s);
    if (o == "data") return Origin::kData;
    if (o == "external" || o == "ext") return Origin::kExternal;
    if (o == "overlay") return Origin::kOverlay;
    if (o == "dirt") return Origin::kDirt;
    if (o == "signal" || o == "enriched") return Origin::kSignal;
    return Origin::kUnknown;
}

const char *DatasetIO::Sample::beam_name(Beam b)
{
    switch (b)
    {
        case Beam::kBNB: return "bnb";
        case Beam::kNuMI: return "numi";
        default: return "unknown";
    }
}

DatasetIO::Sample::Beam DatasetIO::Sample::beam_from(const std::string &s)
{
    const std::string b = utils::lower(s);
    if (b == "bnb") return Beam::kBNB;
    if (b == "numi") return Beam::kNuMI;
    return Beam::kUnknown;
}

const char *DatasetIO::Sample::polarity_name(Polarity p)
{
    switch (p)
    {
        case Polarity::kFHC: return "fhc";
        case Polarity::kRHC: return "rhc";
        default: return "unknown";
    }
}

DatasetIO::Sample::Polarity DatasetIO::Sample::polarity_from(const std::string &s)
{
    const std::string p = utils::lower(s);
    if (p == "fhc") return Polarity::kFHC;
    if (p == "rhc") return Polarity::kRHC;
    return Polarity::kUnknown;
}

const char *DatasetIO::Sample::variation_name(Variation v)
{
    switch (v)
    {
        case Variation::kNominal: return "nominal";
        case Variation::kDetector: return "detector";
        default: return "unknown";
    }
}

DatasetIO::Sample::Variation DatasetIO::Sample::variation_from(const std::string &s)
{
    const std::string v = utils::lower(s);
    if (v == "nominal") return Variation::kNominal;
    if (v == "detector" || v == "detvar") return Variation::kDetector;
    return Variation::kUnknown;
}

void DatasetIO::Provenance::write(TDirectory *d) const
{
    d->cd();

    TParameter<double>("scale", scale).Write("scale", TObject::kOverwrite);
    TNamed("shard", shard.c_str()).Write("shard", TObject::kOverwrite);
    TNamed("sample_list_path", sample_list_path.c_str()).Write("sample_list_path", TObject::kOverwrite);
    TParameter<double>("pot_sum", pot_sum).Write("pot_sum", TObject::kOverwrite);
    TParameter<long long>("entries", n_entries).Write("entries", TObject::kOverwrite);

    {
        TObjArray arr;
        arr.SetOwner(true);
        for (const auto &s : input_files)
            arr.Add(new TObjString(s.c_str()));
        arr.Write("input_files", TObject::kSingleKey | TObject::kOverwrite);
    }

    write_run_subrun_exposures("run_subrun", generated_exposures, run_subruns);
}

DatasetIO::Provenance DatasetIO::Provenance::read(TDirectory *d)
{
    Provenance p;

    p.scale = utils::read_param<double>(d, "scale");
    p.shard = utils::read_named_or(d, "shard");
    p.sample_list_path = utils::read_named_or(d, "sample_list_path");
    p.pot_sum = utils::read_param<double>(d, "pot_sum");
    p.n_entries = utils::read_param<long long>(d, "entries");

    {
        TObject *obj = d->Get("input_files");
        auto *arr = dynamic_cast<TObjArray *>(obj);
        if (!arr) throw std::runtime_error("DatasetIO: missing input_files array");

        const int n = arr->GetEntries();
        p.input_files.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            auto *s = dynamic_cast<TObjString *>(arr->At(i));
            if (!s) throw std::runtime_error("DatasetIO: invalid entry in input_files");
            p.input_files.emplace_back(s->GetString().Data());
        }
    }

    p.generated_exposures = read_run_subrun_exposures(d, "run_subrun", &p.run_subruns);

    return p;
}

void DatasetIO::Sample::write(TDirectory *d) const
{
    d->cd();

    TNamed("sample", sample.c_str()).Write("sample", TObject::kOverwrite);
    TNamed("origin", origin_name(origin)).Write("origin", TObject::kOverwrite);
    TNamed("variation", variation_name(variation)).Write("variation", TObject::kOverwrite);
    TNamed("beam", beam_name(beam)).Write("beam", TObject::kOverwrite);
    TNamed("polarity", polarity_name(polarity)).Write("polarity", TObject::kOverwrite);
    TNamed("normalisation_mode", normalisation_mode.c_str()).Write("normalisation_mode", TObject::kOverwrite);

    TParameter<double>("subrun_pot_sum", subrun_pot_sum).Write("subrun_pot_sum", TObject::kOverwrite);
    TParameter<double>("db_tortgt_pot_sum", db_tortgt_pot_sum).Write("db_tortgt_pot_sum", TObject::kOverwrite);
    TParameter<double>("normalisation", normalisation).Write("normalisation", TObject::kOverwrite);

    TNamed("nominal", nominal.c_str()).Write("nominal", TObject::kOverwrite);
    TNamed("tag", tag.c_str()).Write("tag", TObject::kOverwrite);
    TNamed("role", role.c_str()).Write("role", TObject::kOverwrite);
    TNamed("defname", defname.c_str()).Write("defname", TObject::kOverwrite);
    TNamed("campaign", campaign.c_str()).Write("campaign", TObject::kOverwrite);
    if (provenance_list.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("DatasetIO: provenance_list is too large to persist");
    TParameter<int>("provenance_count", static_cast<int>(provenance_list.size()))
        .Write("provenance_count", TObject::kOverwrite);
    write_run_subrun_normalisations(run_subrun_normalisations);

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

    TDirectory *prov_root = d->GetDirectory("prov");
    if (!prov_root) prov_root = d->mkdir("prov");
    if (!prov_root) throw std::runtime_error("DatasetIO: failed to create sample/prov directory");

    for (size_t i = 0; i < provenance_list.size(); ++i)
    {
        char key[32];
        std::snprintf(key, sizeof(key), "p%04zu", i);

        TDirectory *pd = prov_root->GetDirectory(key);
        if (!pd) pd = prov_root->mkdir(key);
        if (!pd) throw std::runtime_error(std::string("DatasetIO: failed to create provenance dir: ") + key);

        provenance_list[i].write(pd);
    }
}

DatasetIO::Sample DatasetIO::Sample::read(TDirectory *d)
{
    Sample s;

    s.sample = utils::read_named_or(d, "sample");
    s.origin = origin_from(utils::read_named(d, "origin"));
    s.variation = variation_from(utils::read_named(d, "variation"));
    s.beam = beam_from(utils::read_named(d, "beam"));
    s.polarity = polarity_from(utils::read_named(d, "polarity"));
    s.normalisation_mode = utils::read_named_or(d, "normalisation_mode");

    s.subrun_pot_sum = utils::read_param<double>(d, "subrun_pot_sum");
    s.db_tortgt_pot_sum = utils::read_param<double>(d, "db_tortgt_pot_sum");
    s.normalisation = utils::read_param<double>(d, "normalisation");
    s.nominal = utils::read_named_or(d, "nominal", utils::read_named_or(d, "nominal_key"));
    s.tag = utils::read_named_or(d, "tag", utils::read_named_or(d, "variant_name"));
    s.role = utils::read_named_or(d, "role", utils::read_named_or(d, "workflow_role"));
    s.defname = utils::read_named_or(d, "defname", utils::read_named_or(d, "source_def"));
    s.campaign = utils::read_named_or(d, "campaign");
    s.run_subrun_normalisations = read_run_subrun_normalisations(d);
    if (s.normalisation_mode.empty())
        s.normalisation_mode = s.run_subrun_normalisations.empty()
                                   ? ((s.normalisation > 0.0) ? "scalar" : std::string())
                                   : "run_subrun_pot";

    {
        auto *t = dynamic_cast<TTree *>(d->Get("root_files"));
        if (t)
        {
            std::string *root_file = nullptr;
            t->SetBranchAddress("root_file", &root_file);

            const Long64_t n = t->GetEntries();
            s.root_files.reserve(static_cast<size_t>(n));
            for (Long64_t i = 0; i < n; ++i)
            {
                t->GetEntry(i);
                if (!root_file) throw std::runtime_error("DatasetIO: root_files missing root_file");
                s.root_files.push_back(*root_file);
            }
        }
    }

    const int provenance_count = read_optional_provenance_count(d);
    TDirectory *prov_root = d->GetDirectory("prov");
    if (!prov_root)
    {
        if (provenance_count > 0)
            throw std::runtime_error("DatasetIO: missing sample/prov directory");
        return s;
    }

    if (provenance_count >= 0)
    {
        s.provenance_list.reserve(static_cast<std::size_t>(provenance_count));
        for (int i = 0; i < provenance_count; ++i)
        {
            char key[32];
            std::snprintf(key, sizeof(key), "p%04d", i);
            TDirectory *pd = prov_root->GetDirectory(key);
            if (!pd) throw std::runtime_error("DatasetIO: missing sample/prov/" + std::string(key));
            s.provenance_list.push_back(Provenance::read(pd));
        }
        return s;
    }

    const auto keys = utils::list_keys(prov_root);
    s.provenance_list.reserve(keys.size());
    for (const auto &k : keys)
    {
        TDirectory *pd = prov_root->GetDirectory(k.c_str());
        if (!pd) throw std::runtime_error("DatasetIO: missing sample/prov/" + k);
        s.provenance_list.push_back(Provenance::read(pd));
    }

    return s;
}

DatasetIO::DatasetIO(const std::string &path)
{
    path_ = path;

    file_ = TFile::Open(path.c_str(), "READ");
    if (!file_ || file_->IsZombie())
        throw std::runtime_error("DatasetIO: failed to open: " + path);

    if (TDirectory *meta = file_->GetDirectory("meta"))
    {
        try { context_ = utils::read_named(meta, "context"); }
        catch (...) { context_.clear(); }
    }
}

DatasetIO::DatasetIO(const std::string &path, const std::string &context)
{
    path_ = path;
    context_ = context;
    write_ = true;

    file_ = TFile::Open(path.c_str(), "RECREATE");
    if (!file_ || file_->IsZombie())
        throw std::runtime_error("DatasetIO: failed to create: " + path);

    ensure_layout_();
}

DatasetIO::~DatasetIO()
{
    if (file_)
    {
        if (write_) file_->Write();
        file_->Close();
        delete file_;
    }
}

void DatasetIO::require_open_() const
{
    if (!file_ || file_->IsZombie())
        throw std::runtime_error("DatasetIO: file is not open");
}

TDirectory *DatasetIO::must_dir_(TDirectory *base, const char *name, bool create) const
{
    if (!base) throw std::runtime_error("DatasetIO: null base directory");

    TDirectory *d = base->GetDirectory(name);
    if (!d && create) d = base->mkdir(name);
    if (!d) throw std::runtime_error(std::string("DatasetIO: missing directory: ") + name);

    return d;
}

TDirectory *DatasetIO::samples_root_(bool create) const
{
    return must_dir_(file_, "sample", create);
}

TDirectory *DatasetIO::sample_dir_(TDirectory *samples_root, const std::string &key, bool create) const
{
    if (!samples_root) throw std::runtime_error("DatasetIO: null samples_root");

    TDirectory *d = samples_root->GetDirectory(key.c_str());
    if (!d && create) d = samples_root->mkdir(key.c_str());
    if (!d) throw std::runtime_error("DatasetIO: missing sample/" + key);

    return d;
}

void DatasetIO::ensure_layout_()
{
    require_open_();
    utils::write_named(must_dir_(file_, "meta", true), "context", context_);
    samples_root_(true);
}

void DatasetIO::add_sample_(const std::string &key, const Sample &s)
{
    require_open_();
    if (!write_) throw std::runtime_error("DatasetIO: add_sample_ requires write mode");
    s.write(sample_dir_(samples_root_(true), key, true));
}

void DatasetIO::add_sample(const std::string &key, const Sample &s)
{
    if (key.empty())
        throw std::runtime_error("DatasetIO: sample key must not be empty");
    add_sample_(key, s);
}

DatasetIO::Sample DatasetIO::get_sample_(const std::string &key) const
{
    require_open_();
    return Sample::read(sample_dir_(samples_root_(false), key, false));
}

std::vector<std::string> DatasetIO::sample_keys() const
{
    require_open_();
    return utils::list_keys(samples_root_(false));
}

DatasetIO::Sample DatasetIO::sample(const std::string &key) const
{
    return get_sample_(key);
}

std::vector<DatasetIO::Sample> DatasetIO::samples() const
{
    require_open_();

    std::vector<Sample> out;
    const auto keys = sample_keys();
    out.reserve(keys.size());
    for (const auto &k : keys)
        out.push_back(get_sample_(k));

    return out;
}

std::vector<DatasetIO::Sample> DatasetIO::samples(Sample::Variation v) const
{
    const auto all = samples();
    std::vector<Sample> out;
    out.reserve(all.size());
    for (const auto &s : all)
        if (s.variation == v)
            out.push_back(s);
    out.shrink_to_fit();
    return out;
}
