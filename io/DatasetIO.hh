#ifndef DATASET_IO_HH
#define DATASET_IO_HH

#include <string>
#include <utility>
#include <vector>

class TFile;
class TDirectory;

/**
 * NAME
 *   DatasetIO.hh - ROOT I/O for dataset containers.
 *
 * SYNOPSIS
 *   DatasetIO::DatasetIO(path)              // open READ
 *   DatasetIO::DatasetIO(path, context)     // open WRITE (RECREATE)
 *   ds.samples()                            // enumerate samples
 *   ds.samples(Variation)                   // enumerate variation subset
 *
 * DESCRIPTION
 *   Implements persistence for a single-file dataset container storing sample
 *   records and embedded root file provenances, with deterministic
 *   enumeration.
 *
 * LAYOUT
 *   meta/context               TNamed
 *   sample/<key>/              TDirectory
 *     sample                   TNamed
 *     origin                   TNamed
 *     variation                TNamed
 *     beam                     TNamed
 *     polarity                 TNamed
 *     normalisation_mode       TNamed
 *     subrun_pot_sum           TParameter<double>
 *     db_tortgt_pot_sum        TParameter<double>
 *     normalisation            TParameter<double>
 *     nominal                  TNamed
 *     tag                      TNamed
 *     role                     TNamed
 *     defname                  TNamed
 *     campaign                 TNamed
 *     provenance_count         TParameter<int>
 *     run_subrun_normalisation TTree(run,subrun,generated_exposure,target_exposure,normalisation)
 *     root_files               TTree(root_file)
 *     prov/pNNNN/              TDirectory
 *       scale                  TParameter<double>
 *       shard                  TNamed
 *       sample_list_path       TNamed
 *       pot_sum                TParameter<double>
 *       entries                TParameter<long long>
 *       input_files            TObjArray(TObjString)
 *       run_subrun             TTree(run,subrun,generated_exposure)
 *
 * DIAGNOSTICS
 *   Throws std::runtime_error on missing keys, missing directories, invalid
 *   types, or file open failures.
 *
 * NOTE
 *   This header comment is the local layout contract for DatasetIO. If the
 *   on-disk structure changes, update this comment in the same change.
 */
class DatasetIO
{
public:
    struct RunSubrunExposure
    {
        int run = 0;
        int subrun = 0;
        double generated_exposure = 0.0;
    };

    struct RunSubrunNormalisation
    {
        int run = 0;
        int subrun = 0;
        double generated_exposure = 0.0;
        double target_exposure = 0.0;
        double normalisation = 1.0;
    };

    struct Provenance
    {
        double scale = 1.0;
        std::string shard;
        std::string sample_list_path;
        std::vector<std::string> input_files;
        double pot_sum = 0.0;
        long long n_entries = 0;
        std::vector<std::pair<int, int>> run_subruns;
        std::vector<RunSubrunExposure> generated_exposures;

        void write(TDirectory *d) const;
        static Provenance read(TDirectory *d);
    };

    struct Sample
    {
        enum class Origin { kData, kExternal, kOverlay, kDirt, kSignal, kUnknown };

        enum class Beam { kNuMI, kBNB, kUnknown };
        enum class Polarity { kFHC, kRHC, kUnknown };
        
        enum class Variation { kNominal, kDetector, kUnknown };

        Origin origin = Origin::kUnknown;
        Variation variation = Variation::kUnknown;

        Beam beam = Beam::kUnknown;
        Polarity polarity = Polarity::kUnknown; // meaningful iff beam==kNuMI

        std::string sample;
        std::string normalisation_mode;
        std::vector<Provenance> provenance_list;
        std::vector<RunSubrunNormalisation> run_subrun_normalisations;

        double subrun_pot_sum = 0.0;
        double db_tortgt_pot_sum = 0.0;
        double normalisation = 1.0;

        std::string nominal;
        std::string tag;
        std::string role;
        std::string defname;
        std::string campaign;

        std::vector<std::string> root_files;

        void write(TDirectory *d) const;
        static Sample read(TDirectory *d);

        static const char *origin_name(Origin);
        static Origin origin_from(const std::string &);

        static const char *beam_name(Beam);
        static Beam beam_from(const std::string &);

        static const char *polarity_name(Polarity);
        static Polarity polarity_from(const std::string &);

        static const char *variation_name(Variation);
        static Variation variation_from(const std::string &);
    };

public:
    explicit DatasetIO(const std::string &path);
    DatasetIO(const std::string &path, const std::string &context);
    ~DatasetIO();

    DatasetIO(const DatasetIO &) = delete;
    DatasetIO &operator=(const DatasetIO &) = delete;

    const std::string &path() const { return path_; }
    const std::string &context() const { return context_; }
    bool is_open() const { return file_ != nullptr; }
    bool is_write() const { return write_; }

    void add_sample(const std::string &key, const Sample &s);

    std::vector<std::string> sample_keys() const;
    Sample sample(const std::string &key) const;
    std::vector<Sample> samples() const;
    std::vector<Sample> samples(Sample::Variation v) const;

private:
    void add_sample_(const std::string &key, const Sample &s);
    Sample get_sample_(const std::string &key) const;

private:
    void require_open_() const;
    void ensure_layout_();

    TDirectory *must_dir_(TDirectory *base, const char *name, bool create) const;
    TDirectory *samples_root_(bool create) const;
    TDirectory *sample_dir_(TDirectory *samples_root, const std::string &key, bool create) const;

private:
    TFile *file_ = nullptr;
    
    std::string path_;
    std::string context_;

    bool write_ = false;
};

#endif
