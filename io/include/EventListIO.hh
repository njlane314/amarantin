#ifndef EVENTLIST_IO_HH
#define EVENTLIST_IO_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

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
              const std::string &event_tree_name,
              const std::string &subrun_tree_name,
              const std::string &selection_expr);

private:
    std::string path_;
    Mode mode_;
};

#endif // EVENTLIST_IO_HH
