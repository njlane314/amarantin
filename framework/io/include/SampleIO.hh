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
    static SampleIO build(const std::vector<std::string> &input_paths,
                          std::string output_path);
    static SampleIO build(const std::vector<std::string> &input_paths,
                          std::string output_path,
                          const std::string &db_path);
    static double compute_normalisation(double subrun_pot_sum,
                                        double db_tortgt_pot_sum);

    void write() const;
    static SampleIO read(const std::string &path);

public:
    std::vector<std::string> input_paths_;
    std::string output_path_;

    Origin origin = Origin::kUnknown;
    Variation variation = Variation::kUnknown;
    Beam beam = Beam::kUnknown;
    Polarity polarity = Polarity::kUnknown;

    double subrun_pot_sum = 0.0;
    double db_tortgt_pot_sum = 0.0;
    double normalisation = 1.0;
    double normalised_pot_sum = 0.0;

    std::vector<ArtProvenanceIO> partitions;

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
    SampleIO(std::vector<std::string> input_paths, std::string output_path);

    static std::string lower_(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }
};

#endif // SAMPLE_IO_HH
