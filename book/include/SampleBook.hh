#ifndef SAMPLE_BOOK_HH
#define SAMPLE_BOOK_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"
#include "SampleCard.hh"

class EventListIO;

class SampleBook
{
public:
    static SampleBook read(const std::string &path);
    static SampleBook from_dataset(const DatasetIO &dataset);
    static SampleBook from_event_list(const EventListIO &event_list);

    bool has(const std::string &key) const;
    const SampleCard &card(const std::string &key) const;
    const std::vector<SampleCard> &cards() const { return cards_; }

    void stamp(const std::string &key, DatasetIO::Sample &sample) const;
    std::vector<std::string> family_keys(const std::string &family) const;
    std::vector<std::string> detector_mates(const std::string &key) const;

private:
    const SampleCard &must_card_(const std::string &key) const;

private:
    std::vector<SampleCard> cards_;
};

#endif // SAMPLE_BOOK_HH
