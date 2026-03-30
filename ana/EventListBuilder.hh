#ifndef EVENTLIST_BUILDER_HH
#define EVENTLIST_BUILDER_HH

#include <string>

#include "DatasetIO.hh"
#include "EventListIO.hh"
#include "EventListSelection.hh"

namespace ana
{
    struct EventListConfig
    {
        std::string event_tree_name = "EventSelectionFilter";
        std::string subrun_tree_name = "SubRun";
        std::string selection_expr = "selected != 0";
        std::string selection_name = "raw";
        EventListSelection::Config selection_config;
    };

    void build_event_list(const DatasetIO &dataset,
                          EventListIO &event_list,
                          const EventListConfig &config);
}

#endif // EVENTLIST_BUILDER_HH
