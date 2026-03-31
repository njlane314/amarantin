#ifndef EVENTLIST_BUILD_HH
#define EVENTLIST_BUILD_HH

#include <string>

#include "DatasetIO.hh"
#include "EventListIO.hh"

namespace ana
{
    struct BuildConfig
    {
        std::string event_tree_name = "EventSelectionFilter";
        std::string subrun_tree_name = "SubRun";
        std::string selection_expr = "selected != 0";
        std::string selection_name = "raw";
        int slice_required_count = 1;
        double slice_min_topology_score = 0.06;
        int numi_run_boundary = 16880;
    };

    void build_event_list(const DatasetIO &dataset,
                          EventListIO &event_list,
                          const BuildConfig &config);
}

#endif // EVENTLIST_BUILD_HH
