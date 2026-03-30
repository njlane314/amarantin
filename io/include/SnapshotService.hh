#ifndef SNAPSHOT_SERVICE_HH
#define SNAPSHOT_SERVICE_HH

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

    unsigned long long snapshot_sample(const EventListIO &event_list,
                                       const std::string &out_path,
                                       const std::string &sample_key,
                                       const Spec &spec);

    unsigned long long snapshot_merged(const EventListIO &event_list,
                                       const std::string &out_path,
                                       const Spec &spec);
}

class SnapshotService
{
public:
    using SnapshotSpec = snapshot::Spec;

    static std::string sanitise_root_key(std::string s);

    static unsigned long long snapshot_sample(const EventListIO &event_list,
                                              const std::string &out_path,
                                              const std::string &sample_key,
                                              const SnapshotSpec &spec);

    static unsigned long long snapshot_merged(const EventListIO &event_list,
                                              const std::string &out_path,
                                              const SnapshotSpec &spec);
};

#endif // SNAPSHOT_SERVICE_HH
