#ifndef SAMPLE_DEF_HH
#define SAMPLE_DEF_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

namespace ana
{
    struct DatasetScope
    {
        std::string run;
        DatasetIO::Sample::Beam beam = DatasetIO::Sample::Beam::kUnknown;
        DatasetIO::Sample::Polarity polarity = DatasetIO::Sample::Polarity::kUnknown;
        std::string campaign;
    };

    struct SampleDef
    {
        std::string key;
        DatasetIO::Sample::Variation variation = DatasetIO::Sample::Variation::kUnknown;
        std::string nominal;
        std::string tag;
        std::string role;
        std::string defname;
        std::string campaign;

        std::string nominal_or_key() const
        {
            return nominal.empty() ? key : nominal;
        }

        void apply(DatasetIO::Sample &sample) const
        {
            if (variation != DatasetIO::Sample::Variation::kUnknown)
                sample.variation = variation;
            if (!nominal.empty())
                sample.nominal = nominal;
            if (!tag.empty())
                sample.tag = tag;
            if (!role.empty())
                sample.role = role;
            if (!defname.empty())
                sample.defname = defname;
            if (!campaign.empty())
                sample.campaign = campaign;
        }
    };

    std::vector<SampleDef> read_sample_defs(const std::string &path);
    void apply_sample_defs(const std::vector<SampleDef> &defs,
                           const std::string &key,
                           DatasetIO::Sample &sample);
    std::string infer_sample_run(const SampleDef &def);
    void validate_sample_scope(const SampleDef &def,
                               const DatasetScope &scope);
}

#endif // SAMPLE_DEF_HH
