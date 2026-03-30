#ifndef EVENTLIST_BUILDER_HH
#define EVENTLIST_BUILDER_HH

#include <string>

#include "EventListIO.hh"
#include "EventListSelection.hh"

namespace ana
{
    class EventListBuilder
    {
    public:
        struct Options
        {
            std::string event_tree_name = "EventSelectionFilter";
            std::string subrun_tree_name = "SubRun";
            std::string selection_expr = "selected != 0";
            std::string selection_name = "raw";
            EventListSelection::Config selection_config;
        };

        static void build(const DatasetIO &dataset,
                          EventListIO &event_list,
                          const Options &options);
    };
}

#endif // EVENTLIST_BUILDER_HH
