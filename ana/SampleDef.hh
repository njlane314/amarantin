#ifndef SAMPLE_DEF_HH
#define SAMPLE_DEF_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

class EventListIO;

namespace ana
{
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
    std::vector<std::string> detector_mates(const EventListIO &event_list,
                                            const std::string &sample_key);
}

#endif // SAMPLE_DEF_HH
