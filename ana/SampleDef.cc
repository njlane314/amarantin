#include "SampleDef.hh"

#include <algorithm>
#include <cctype>
#include <fstream>
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

    std::string trim_copy(const std::string &input)
    {
        const std::string::size_type first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";

        const std::string::size_type last = input.find_last_not_of(" \t\r\n");
        return input.substr(first, last - first + 1);
    }

    std::vector<std::string> split_scope_tokens(const std::string &value)
    {
        std::string spaced = value;
        std::transform(spaced.begin(), spaced.end(), spaced.begin(),
                       [](unsigned char c) {
                           return std::isalnum(c) ? static_cast<char>(c) : ' ';
                       });

        std::istringstream input(spaced);
        std::vector<std::string> out;
        std::string token;
        while (input >> token)
            out.push_back(token);
        return out;
    }

    bool is_run_token(const std::string &token)
    {
        if (token.size() < 4)
            return false;
        if (token.rfind("run", 0) != 0)
            return false;
        return std::isdigit(static_cast<unsigned char>(token[3])) != 0;
    }

    void merge_run_hint(std::string &current,
                        const std::string &candidate,
                        const std::string &field_name,
                        const std::string &sample_key)
    {
        const std::string trimmed = trim_copy(candidate);
        if (trimmed.empty())
            return;

        if (current.empty())
        {
            current = trimmed;
            return;
        }

        if (current != trimmed)
        {
            throw std::runtime_error("SampleDef: conflicting inferred run scope for " +
                                     sample_key + " (" + current + " vs " + trimmed +
                                     " from " + field_name + ")");
        }
    }

    std::string infer_run_from_value(const std::string &value)
    {
        std::string out;
        for (const auto &token : split_scope_tokens(value))
        {
            if (!is_run_token(token))
                continue;
            merge_run_hint(out, token, "value", value);
        }
        return out;
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

    std::string infer_sample_run(const SampleDef &def)
    {
        std::string out;
        merge_run_hint(out, infer_run_from_value(def.campaign), "campaign", def.key);
        merge_run_hint(out, infer_run_from_value(def.defname), "defname", def.key);
        return out;
    }

    void validate_sample_scope(const SampleDef &def,
                               const DatasetScope &scope)
    {
        const std::string inferred_run = infer_sample_run(def);
        if (!scope.run.empty() && !inferred_run.empty() && inferred_run != scope.run)
        {
            throw std::runtime_error("SampleDef: sample " + def.key +
                                     " implies run " + inferred_run +
                                     " but dataset scope requested " + scope.run);
        }

        if (!scope.campaign.empty() && !def.campaign.empty() && def.campaign != scope.campaign)
        {
            throw std::runtime_error("SampleDef: sample " + def.key +
                                     " has campaign " + def.campaign +
                                     " but dataset scope requested " + scope.campaign);
        }
    }
}
