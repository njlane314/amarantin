#ifndef SNAPSHOT_HH
#define SNAPSHOT_HH

#include <string>
#include <vector>

#include "EventListIO.hh"

namespace snapshot
{
    struct Spec
    {
        std::string tree_name = "train";
        std::vector<std::string> columns;
        std::string selection = "true";
        bool overwrite_if_exists = true;
        bool include_sample_id = true;
    };

    std::string sanitise_root_key(std::string s);

    unsigned long long sample(const EventListIO &event_list,
                              const std::string &out_path,
                              const std::string &sample_key,
                              const Spec &spec);

    unsigned long long merged(const EventListIO &event_list,
                              const std::string &out_path,
                              const Spec &spec);
}

#endif // SNAPSHOT_HH
