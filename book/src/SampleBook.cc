#include "SampleBook.hh"

#include "EventListIO.hh"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

namespace
{
    using json = nlohmann::json;

    std::string json_string_or(const json &obj,
                               const std::initializer_list<const char *> &keys,
                               const std::string &fallback = std::string())
    {
        for (const char *key : keys)
        {
            const auto it = obj.find(key);
            if (it != obj.end() && it->is_string())
                return it->get<std::string>();
        }
        return fallback;
    }

    DatasetIO::Sample::Variation json_variation_or(const json &obj)
    {
        const std::string value = json_string_or(obj, {"variation", "variation_name", "variant"});
        if (value.empty())
            return DatasetIO::Sample::Variation::kUnknown;
        return DatasetIO::Sample::variation_from(value);
    }

    SampleCard card_from_json(const json &obj)
    {
        if (!obj.is_object())
            throw std::runtime_error("SampleBook: each manifest entry must be a JSON object");

        SampleCard card;
        card.key = json_string_or(obj, {"key", "sample_key", "name"});
        card.family = json_string_or(obj, {"family", "group", "sample_group"});
        card.nominal_key = json_string_or(obj, {"nominal_key", "nominal", "nominal_sample_key"});
        card.variant_name = json_string_or(obj, {"variant_name", "variant", "variation_name"});
        card.workflow_role = json_string_or(obj, {"workflow_role", "role"});
        card.source_def = json_string_or(obj, {"source_def", "source", "definition"});
        card.campaign = json_string_or(obj, {"campaign"});
        card.variation = json_variation_or(obj);

        if (card.key.empty())
            throw std::runtime_error("SampleBook: manifest entry is missing key");
        return card;
    }

    SampleCard card_from_sample(const std::string &key, const DatasetIO::Sample &sample)
    {
        SampleCard card;
        card.key = key;
        card.family = sample.family;
        card.nominal_key = sample.nominal_key;
        card.variant_name = sample.variant_name;
        card.workflow_role = sample.workflow_role;
        card.source_def = sample.source_def;
        card.campaign = sample.campaign;
        card.variation = sample.variation;
        return card;
    }

    bool same_nominal(const SampleCard &lhs, const SampleCard &rhs)
    {
        return lhs.nominal_or_key() == rhs.nominal_or_key();
    }
}

SampleBook SampleBook::read(const std::string &path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("SampleBook: failed to open manifest: " + path);

    json root;
    input >> root;

    const json *entries = &root;
    if (root.is_object())
    {
        const auto it = root.find("samples");
        if (it == root.end())
            throw std::runtime_error("SampleBook: manifest object must contain a samples array");
        entries = &(*it);
    }

    if (!entries->is_array())
        throw std::runtime_error("SampleBook: manifest must be an array or an object with a samples array");

    SampleBook book;
    book.cards_.reserve(entries->size());
    for (const auto &entry : *entries)
        book.cards_.push_back(card_from_json(entry));

    std::sort(book.cards_.begin(), book.cards_.end(),
              [](const SampleCard &lhs, const SampleCard &rhs) { return lhs.key < rhs.key; });
    return book;
}

SampleBook SampleBook::from_dataset(const DatasetIO &dataset)
{
    SampleBook book;
    const auto keys = dataset.sample_keys();
    book.cards_.reserve(keys.size());
    for (const auto &key : keys)
        book.cards_.push_back(card_from_sample(key, dataset.sample(key)));
    return book;
}

SampleBook SampleBook::from_event_list(const EventListIO &event_list)
{
    SampleBook book;
    const auto keys = event_list.sample_keys();
    book.cards_.reserve(keys.size());
    for (const auto &key : keys)
        book.cards_.push_back(card_from_sample(key, event_list.sample(key)));
    return book;
}

bool SampleBook::has(const std::string &key) const
{
    return std::any_of(cards_.begin(), cards_.end(),
                       [&](const SampleCard &card) { return card.key == key; });
}

const SampleCard &SampleBook::must_card_(const std::string &key) const
{
    const auto it = std::find_if(cards_.begin(), cards_.end(),
                                 [&](const SampleCard &card) { return card.key == key; });
    if (it == cards_.end())
        throw std::runtime_error("SampleBook: missing card for sample key: " + key);
    return *it;
}

const SampleCard &SampleBook::card(const std::string &key) const
{
    return must_card_(key);
}

void SampleBook::stamp(const std::string &key, DatasetIO::Sample &sample) const
{
    must_card_(key).stamp(sample);
}

std::vector<std::string> SampleBook::family_keys(const std::string &family) const
{
    std::vector<std::string> out;
    for (const auto &card : cards_)
    {
        if (card.family == family)
            out.push_back(card.key);
    }
    return out;
}

std::vector<std::string> SampleBook::detector_mates(const std::string &key) const
{
    const SampleCard &seed = must_card_(key);

    if (seed.family.empty())
        return {};

    std::vector<std::string> out;
    for (const auto &card : cards_)
    {
        if (card.key == key)
            continue;
        if (card.family != seed.family)
            continue;
        if (!same_nominal(seed, card))
            continue;
        if (card.variation != DatasetIO::Sample::Variation::kDetector)
            continue;
        out.push_back(card.key);
    }

    std::sort(out.begin(), out.end());
    return out;
}
