// SampleIO.hh
#ifndef SAMPLE_IO_HH
#define SAMPLE_IO_HH

#include "ArtProvenanceIO.hh"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

class SampleIO
{
public:
    enum class Origin    { kData, kExternal, kOverlay, kDirt, kEnriched, kUnknown };
    enum class Beam      { kNuMI, kBNB, kUnknown };
    enum class Polarity  { kFHC, kRHC, kUnknown };
    enum class Variation { kNominal, kDetector, kUnknown };

public:
    struct BuildOptions
    {
        BuildOptions() = default;
        Origin origin = Origin::kUnknown;
        Variation variation = Variation::kUnknown;
        Beam beam = Beam::kUnknown;
        Polarity polarity = Polarity::kUnknown;
        std::string db_path;

        static BuildOptions from_strings(const std::string &origin,
                                         const std::string &variation,
                                         const std::string &beam,
                                         const std::string &polarity,
                                         const std::string &db_path = "");
    };

    explicit SampleIO(std::string output_path);

    void set_metadata(Origin origin, Variation variation, Beam beam, Polarity polarity);
    void set_metadata_from_strings(const std::string &origin,
                                   const std::string &variation,
                                   const std::string &beam,
                                   const std::string &polarity);

    void build(const std::vector<std::string> &input_paths, const std::string &db_path);
    void build_from_spec(const std::string &input_paths_spec, const std::string &db_path);
    static std::vector<std::string> parse_input_paths(const std::string &input_paths_spec);
    static double compute_normalisation(double subrun_pot_sum, double db_tortgt_pot_sum);

    void write() const;
    static SampleIO build_and_write(std::string output_path,
                                    std::vector<std::string> input_paths,
                                    BuildOptions options);
    static SampleIO build_and_write_from_spec(std::string output_path,
                                              const std::string &input_paths_spec,
                                              BuildOptions options);
    static SampleIO read(const std::string &path);

public:
    std::vector<std::string> input_paths_;
    std::string output_path_;

    Origin origin_ = Origin::kUnknown;
    Variation variation_ = Variation::kUnknown;
    Beam beam_ = Beam::kUnknown;
    Polarity polarity_ = Polarity::kUnknown;

    double subrun_pot_sum_ = 0.0;
    double db_tortgt_pot_sum_ = 0.0;
    double normalisation_ = 1.0;
    double normalised_pot_sum_ = 0.0;

    std::vector<ArtProvenanceIO> partitions_;
    bool built_ = false;

public:
    static const char *origin_name(Origin o)
    {
        switch (o)
        {
            case Origin::kData:     return "data";
            case Origin::kExternal: return "external";
            case Origin::kOverlay:  return "overlay";
            case Origin::kDirt:     return "dirt";
            case Origin::kEnriched: return "enriched";
            default:                return "unknown";
        }
    }

    static Origin origin_from(std::string s)
    {
        s = lower_(std::move(s));
        if (s == "data") return Origin::kData;
        if (s == "external" || s == "ext") return Origin::kExternal;
        if (s == "overlay") return Origin::kOverlay;
        if (s == "dirt") return Origin::kDirt;
        if (s == "enriched") return Origin::kEnriched;
        return Origin::kUnknown;
    }

    static const char *beam_name(Beam b)
    {
        switch (b)
        {
            case Beam::kBNB:  return "bnb";
            case Beam::kNuMI: return "numi";
            default:          return "unknown";
        }
    }

    static Beam beam_from(std::string s)
    {
        s = lower_(std::move(s));
        if (s == "bnb") return Beam::kBNB;
        if (s == "numi") return Beam::kNuMI;
        return Beam::kUnknown;
    }

    static const char *polarity_name(Polarity p)
    {
        switch (p)
        {
            case Polarity::kFHC: return "fhc";
            case Polarity::kRHC: return "rhc";
            default:             return "unknown";
        }
    }

    static Polarity polarity_from(std::string s)
    {
        s = lower_(std::move(s));
        if (s == "fhc") return Polarity::kFHC;
        if (s == "rhc") return Polarity::kRHC;
        return Polarity::kUnknown;
    }

    static const char *variation_name(Variation v)
    {
        switch (v)
        {
            case Variation::kNominal:  return "nominal";
            case Variation::kDetector: return "detector";
            default:                   return "unknown";
        }
    }

    static Variation variation_from(std::string s)
    {
        s = lower_(std::move(s));
        if (s == "nominal") return Variation::kNominal;
        if (s == "detector" || s == "detvar") return Variation::kDetector;
        return Variation::kUnknown;
    }

private:
    static std::string lower_(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
};

#endif // SAMPLE_IO_HH
