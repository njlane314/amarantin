/**
 * NAME
 *   DatasetIO.cc — ROOT I/O for dataset containers.
 *
 * SYNOPSIS
 *   DatasetIO::DatasetIO(path)              // open READ
 *   DatasetIO::DatasetIO(path, context)     // open WRITE (RECREATE)
 *   ds.samples()                            // enumerate samples
 *   ds.samples(Variation)                   // enumerate variation subset
 *
 * DESCRIPTION
 *   Implements persistence for a single-file dataset container storing sample records
 *   and embedded root file provenances, with deterministic enumeration.
 *
 * LAYOUT
 *   meta/context             TNamed
 *   sample/<key>/            TDirectory
 *     origin                 TNamed
 *     variation              TNamed
 *     beam                   TNamed
 *     polarity               TNamed
 *     subrun_pot_sum         TParameter<double>
 *     db_tortgt_pot_sum      TParameter<double>
 *     normalisation          TParameter<double>
 *     root_files             TTree(root_file)
 *     prov/pNNNN/            TDirectory
 *       scale                TParameter<double>
 *       pot_sum              TParameter<double>
 *       entries              TParameter<long long>
 *       input_files          TObjArray(TObjString)
 *       run_subrun           TTree(run,subrun)
 *
 * DIAGNOSTICS
 *   Throws std::runtime_error on missing keys, missing directories, invalid types, or file open failures.
 */
 
#include "DatasetIO.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>
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

/** UTILITIES
 *  Local helpers for token normalisation, typed ROOT reads, and deterministic directory key listing.
 */
 
namespace
{
    static std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static std::string read_named(TDirectory *d, const char *key)
    {
        TObject *obj = d->Get(key);
        auto *n = dynamic_cast<TNamed *>(obj);
        if (!n) throw std::runtime_error(std::string("DatasetIO: missing TNamed: ") + key);
        return std::string(n->GetTitle());
    }

    template <class T>
    static T read_param(TDirectory *d, const char *key)
    {
        TObject *obj = d->Get(key);
        auto *p = dynamic_cast<TParameter<T> *>(obj);
        if (!p) throw std::runtime_error(std::string("DatasetIO: missing TParameter: ") + key);
        return p->GetVal();
    }

    static void write_named(TDirectory *d, const char *key, const std::string &value)
    {
        d->cd();
        TNamed(key, value.c_str()).Write(key, TObject::kOverwrite);
    }

    static std::vector<std::string> list_keys(TDirectory *d)
    {
        std::vector<std::string> out;
        if (!d) return out;

        TIter next(d->GetListOfKeys());
        while (TKey *k = (TKey *)next())
            out.emplace_back(k->GetName());

        std::sort(out.begin(), out.end());
        return out;
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
        case Origin::kEnriched: return "enriched";
        default: return "unknown";
    }
}

DatasetIO::Sample::Origin DatasetIO::Sample::origin_from(const std::string &s)
{
    const std::string o = lower(s);
    if (o == "data") return Origin::kData;
    if (o == "external" || o == "ext") return Origin::kExternal;
    if (o == "overlay") return Origin::kOverlay;
    if (o == "dirt") return Origin::kDirt;
    if (o == "enriched") return Origin::kEnriched;
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
    const std::string b = lower(s);
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
    const std::string p = lower(s);
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
    const std::string v = lower(s);
    if (v == "nominal") return Variation::kNominal;
    if (v == "detector" || v == "detvar") return Variation::kDetector;
    return Variation::kUnknown;
}

/** PROVENANCE
 *  Serialisation for per-sample provenances(scale, exposure summary, input file list, run/subrun set).
 */

void DatasetIO::Provenance::write(TDirectory *d) const
{
    d->cd();

    TParameter<double>("scale", scale).Write("scale", TObject::kOverwrite);
    TParameter<double>("pot_sum", pot_sum).Write("pot_sum", TObject::kOverwrite);
    TParameter<long long>("entries", n_entries).Write("entries", TObject::kOverwrite);

    {
        TObjArray arr;
        arr.SetOwner(true);
        for (const auto &s : input_files)
            arr.Add(new TObjString(s.c_str()));
        arr.Write("input_files", TObject::kSingleKey | TObject::kOverwrite);
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

DatasetIO::Provenance DatasetIO::Provenance::read(TDirectory *d)
{
    Provenance p;

    p.scale = read_param<double>(d, "scale");
    p.pot_sum = read_param<double>(d, "pot_sum");
    p.n_entries = read_param<long long>(d, "entries");

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

    {
        TObject *obj = d->Get("run_subrun");
        auto *t = dynamic_cast<TTree *>(obj);
        if (!t) throw std::runtime_error("DatasetIO: missing run_subrun tree");

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

/** SAMPLE
 *  Serialisation for sample records (classification tags, normalisation scalars, resolved ROOT files, provenance list).
 */

void DatasetIO::Sample::write(TDirectory *d) const
{
    d->cd();

    TNamed("origin", origin_name(origin)).Write("origin", TObject::kOverwrite);
    TNamed("variation", variation_name(variation)).Write("variation", TObject::kOverwrite);
    TNamed("beam", beam_name(beam)).Write("beam", TObject::kOverwrite);
    TNamed("polarity", polarity_name(polarity)).Write("polarity", TObject::kOverwrite);

    TParameter<double>("subrun_pot_sum", subrun_pot_sum).Write("subrun_pot_sum", TObject::kOverwrite);
    TParameter<double>("db_tortgt_pot_sum", db_tortgt_pot_sum).Write("db_tortgt_pot_sum", TObject::kOverwrite);
    TParameter<double>("normalisation", normalisation).Write("normalisation", TObject::kOverwrite);

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

    s.origin = origin_from(read_named(d, "origin"));
    s.variation = variation_from(read_named(d, "variation"));
    s.beam = beam_from(read_named(d, "beam"));
    s.polarity = polarity_from(read_named(d, "polarity"));

    s.subrun_pot_sum = read_param<double>(d, "subrun_pot_sum");
    s.db_tortgt_pot_sum = read_param<double>(d, "db_tortgt_pot_sum");
    s.normalisation = read_param<double>(d, "normalisation");

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

    TDirectory *prov_root = d->GetDirectory("prov");
    if (!prov_root) return s;

    const auto keys = list_keys(prov_root);
    s.provenance_list.reserve(keys.size());
    for (const auto &k : keys)
    {
        TDirectory *pd = prov_root->GetDirectory(k.c_str());
        if (!pd) throw std::runtime_error("DatasetIO: missing sample/prov/" + k);
        s.provenance_list.push_back(Provenance::read(pd));
    }

    return s;
}

/** DATASET
 *  File lifecycle, minimal layout creation (meta/ and sample/), and sample enumeration/filtering.
 */

DatasetIO::DatasetIO(const std::string &path)
{
    path_ = path;
    write_ = false;

    file_ = TFile::Open(path.c_str(), "READ");
    if (!file_ || file_->IsZombie())
        throw std::runtime_error("DatasetIO: failed to open: " + path);

    if (TDirectory *meta = file_->GetDirectory("meta"))
    {
        try { context_ = read_named(meta, "context"); }
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
        file_ = nullptr;
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

    TDirectory *meta = must_dir_(file_, "meta", true);
    write_named(meta, "context", context_);

    samples_root_(true);
}

void DatasetIO::add_sample_(const std::string &key, const Sample &s)
{
    require_open_();
    if (!write_) throw std::runtime_error("DatasetIO: add_sample_ requires write mode");

    TDirectory *root = samples_root_(true);
    TDirectory *sd = sample_dir_(root, key, true);
    s.write(sd);
}

DatasetIO::Sample DatasetIO::get_sample_(const std::string &key) const
{
    require_open_();

    TDirectory *root = samples_root_(false);
    TDirectory *sd = sample_dir_(root, key, false);
    return Sample::read(sd);
}

std::vector<DatasetIO::Sample> DatasetIO::samples() const
{
    require_open_();

    std::vector<Sample> out;
    TDirectory *root = samples_root_(false);
    if (!root) return out;

    const auto keys = list_keys(root);
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