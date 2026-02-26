#ifndef EVENTLIST_IO
#define EVENTLIST_IO

#include <map>
#include <vector>
#include <string>
#include <vector>

#include <ROOT/RDataFrame.hxx>

#include "DatasetIO.hh"

class EventListIO 
{
public: 
    enum class Mode { kRead, kWrite };

public: 
    explicit EventListIO(const std::string &path, Mode mode = Mode::kRead);
    ~EventListIO();

    EventListIO(const EventListIO &) = delete;
    EventListIO &operator=(const EventListIO &) = delete; 

    const std::string &path() const { return path_ };

    void skim(const DatasetIO &ds,
            const AnalysisModel &analysis,
            const Selection &selection)

private: 
    std::string path_;
    Mode mode_;
}

#endif //EVENTLIST_IO