#ifndef DATASET_IO_HH
#define DATASET_IO_HH

#include <string>
#include <utility>
#include <vector>

class TFile;
class TDirectory;

class DatasetIO
{
public:
    struct Provenance
    {
        double scale = 1.0;
        std::vector<std::string> input_files;
        double pot_sum = 0.0;
        long long n_entries = 0;
        std::vector<std::pair<int, int>> run_subruns;

        void write(TDirectory *d) const;
        static Provenance read(TDirectory *d);
    };

    struct Sample
    {
        enum class Origin { kData, kExternal, kOverlay, kDirt, kEnriched, kUnknown };

        enum class Beam { kNuMI, kBNB, kUnknown };
        enum class Polarity { kFHC, kRHC, kUnknown };
        
        enum class Variation { kNominal, kDetector, kUnknown };

        Origin origin = Origin::kUnknown;
        Variation variation = Variation::kUnknown;

        Beam beam = Beam::kUnknown;
        Polarity polarity = Polarity::kUnknown; // meaningful iff beam==kNuMI

        std::vector<Provenance> provenance_list;

        double subrun_pot_sum = 0.0;
        double db_tortgt_pot_sum = 0.0;
        double normalisation = 1.0;

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
