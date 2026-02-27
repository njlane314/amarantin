#include "EventListIO.hh"

EventListIO::EventListIO(const std::string &path, Mode mode)
    : path_(path), mode_(mode)
{
}

EventListIO::~EventListIO() = default;

void EventListIO::skim(const DatasetIO &, const AnalysisModel &, const Selection &)
{
}
