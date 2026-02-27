#ifndef EVENTLIST_IO_HH
#define EVENTLIST_IO_HH

#include <string>

#include <ROOT/RDataFrame.hxx>

#include "DatasetIO.hh"

class AnalysisModel;
class Selection;

class EventListIO
{
public:
    enum class Mode { kRead, kWrite };

    explicit EventListIO(const std::string &path, Mode mode = Mode::kRead);
    ~EventListIO();

    EventListIO(const EventListIO &) = delete;
    EventListIO &operator=(const EventListIO &) = delete;

    const std::string &path() const { return path_; }

    void skim(const DatasetIO &ds,
              const AnalysisModel &analysis,
              const Selection &selection);

private:
    std::string path_;
    Mode mode_;
};

#endif // EVENTLIST_IO_HH
