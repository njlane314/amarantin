#ifndef SAMPLE_CARD_HH
#define SAMPLE_CARD_HH

#include <string>

#include "DatasetIO.hh"

class SampleCard
{
public:
    std::string key;
    std::string family;
    std::string nominal_key;
    std::string variant_name;
    std::string workflow_role;
    std::string source_def;
    std::string campaign;

    DatasetIO::Sample::Variation variation = DatasetIO::Sample::Variation::kUnknown;

    std::string nominal_or_key() const
    {
        return nominal_key.empty() ? key : nominal_key;
    }

    void stamp(DatasetIO::Sample &sample) const
    {
        if (!family.empty())
            sample.family = family;
        if (!nominal_key.empty())
            sample.nominal_key = nominal_key;
        if (!variant_name.empty())
            sample.variant_name = variant_name;
        if (!workflow_role.empty())
            sample.workflow_role = workflow_role;
        if (!source_def.empty())
            sample.source_def = source_def;
        if (!campaign.empty())
            sample.campaign = campaign;
        if (variation != DatasetIO::Sample::Variation::kUnknown)
            sample.variation = variation;
    }
};

#endif // SAMPLE_CARD_HH
