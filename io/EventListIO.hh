#ifndef EVENTLIST_IO_HH
#define EVENTLIST_IO_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

class TFile;
class TTree;

class EventListIO
{
public:
    enum class Mode { kRead, kWrite, kUpdate };

    struct Metadata
    {
        std::string dataset_path;
        std::string dataset_context;
        std::string event_tree_name;
        std::string subrun_tree_name;
        std::string selection_name;
        std::string selection_expr;
        int slice_required_count = 1;
        double slice_min_topology_score = 0.06;
        int numi_run_boundary = 16880;
    };

    explicit EventListIO(const std::string &path, Mode mode = Mode::kRead);
    ~EventListIO();

    EventListIO(const EventListIO &) = delete;
    EventListIO &operator=(const EventListIO &) = delete;

    const std::string &path() const { return path_; }
    Mode mode() const { return mode_; }

    Metadata metadata() const;
    void write_metadata(const Metadata &metadata);
    void write_sample(const std::string &sample_key,
                      const DatasetIO::Sample &sample,
                      TTree *selected_tree,
                      TTree *subrun_tree,
                      const std::string &subrun_tree_name);
    void flush();

    std::vector<std::string> sample_keys() const;
    DatasetIO::Sample sample(const std::string &sample_key) const;
    std::vector<std::string> detector_mates(const std::string &sample_key) const;
    TTree *selected_tree(const std::string &sample_key) const;
    TTree *subrun_tree(const std::string &sample_key) const;

private:
    void require_open_() const;

    std::string path_;
    Mode mode_;
    TFile *file_ = nullptr;
};

#endif // EVENTLIST_IO_HH
