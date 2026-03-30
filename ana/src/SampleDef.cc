#include "SampleDef.hh"

#include "EventListIO.hh"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace
{
    std::string strip_comment(const std::string &line)
    {
        const std::string::size_type pos = line.find('#');
        if (pos == std::string::npos)
            return line;
        return line.substr(0, pos);
    }

    std::string normalise_token(const std::string &token)
    {
        return token == "-" ? std::string() : token;
    }

    bool has_explicit_token(const std::string &token)
    {
        return !normalise_token(token).empty();
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
        std::istringstream input(strip_comment(line));
        std::vector<std::string> fields;
        std::string field;
        while (input >> field)
            fields.push_back(field);
        return fields;
    }

    const ana::SampleDef &must_find_def(const std::vector<ana::SampleDef> &defs,
                                        const std::string &key)
    {
        const auto it = std::find_if(defs.begin(), defs.end(),
                                     [&](const ana::SampleDef &def) { return def.key == key; });
        if (it == defs.end())
            throw std::runtime_error("SampleDef: missing definition for sample key: " + key);
        return *it;
    }

    std::string nominal_or_key(const std::string &key, const DatasetIO::Sample &sample)
    {
        return sample.nominal.empty() ? key : sample.nominal;
    }
}

namespace ana
{
    std::vector<SampleDef> read_sample_defs(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("SampleDef: failed to open definitions: " + path);

        std::vector<SampleDef> defs;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::vector<std::string> fields = split_fields(line);
            if (fields.empty())
                continue;
            if (fields.size() < 6 || fields.size() > 7)
            {
                throw std::runtime_error(
                    "SampleDef: expected 6 or 7 fields at line " + std::to_string(line_number) +
                    " in " + path);
            }

            SampleDef def;
            def.key = normalise_token(fields[0]);
            def.variation = DatasetIO::Sample::variation_from(fields[1]);
            def.nominal = normalise_token(fields[2]);
            def.tag = normalise_token(fields[3]);
            def.role = normalise_token(fields[4]);
            def.defname = normalise_token(fields[5]);
            if (fields.size() == 7)
                def.campaign = normalise_token(fields[6]);

            if (def.key.empty())
                throw std::runtime_error("SampleDef: empty key at line " + std::to_string(line_number));
            if (def.variation == DatasetIO::Sample::Variation::kUnknown &&
                has_explicit_token(fields[1]))
            {
                throw std::runtime_error("SampleDef: invalid variation at line " +
                                         std::to_string(line_number));
            }
            defs.push_back(def);
        }

        std::sort(defs.begin(), defs.end(),
                  [](const SampleDef &lhs, const SampleDef &rhs) { return lhs.key < rhs.key; });
        const auto duplicate = std::adjacent_find(defs.begin(), defs.end(),
                                                  [](const SampleDef &lhs, const SampleDef &rhs) {
                                                      return lhs.key == rhs.key;
                                                  });
        if (duplicate != defs.end())
            throw std::runtime_error("SampleDef: duplicate key in definitions: " + duplicate->key);
        return defs;
    }

    void apply_sample_defs(const std::vector<SampleDef> &defs,
                           const std::string &key,
                           DatasetIO::Sample &sample)
    {
        must_find_def(defs, key).apply(sample);
    }

    std::vector<std::string> detector_mates(const EventListIO &event_list,
                                            const std::string &sample_key)
    {
        const DatasetIO::Sample seed = event_list.sample(sample_key);
        const std::string seed_nominal = nominal_or_key(sample_key, seed);

        std::vector<std::string> out;
        for (const auto &key : event_list.sample_keys())
        {
            if (key == sample_key)
                continue;

            const DatasetIO::Sample sample = event_list.sample(key);
            if (sample.variation != DatasetIO::Sample::Variation::kDetector)
                continue;
            if (nominal_or_key(key, sample) != seed_nominal)
                continue;
            out.push_back(key);
        }

        std::sort(out.begin(), out.end());
        return out;
    }
}
