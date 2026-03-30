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

    struct SystematicsFamilyCache
    {
        std::string branch_name;
        long long n_variations = 0;
        int eigen_rank = 0;
        std::vector<double> sigma;
        std::vector<double> covariance;
        std::vector<double> eigenvalues;
        std::vector<double> eigenmodes;

        bool empty() const
        {
            return branch_name.empty() &&
                   sigma.empty() &&
                   covariance.empty() &&
                   eigenvalues.empty() &&
                   eigenmodes.empty();
        }
    };

    struct SystematicsCacheEntry
    {
        int version = 1;
        std::string branch_expr;
        std::string selection_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;

        std::vector<std::string> detector_sample_keys;
        int detector_template_count = 0;
        std::vector<double> nominal;
        std::vector<double> detector_down;
        std::vector<double> detector_up;
        std::vector<double> detector_templates;
        std::vector<double> total_down;
        std::vector<double> total_up;

        SystematicsFamilyCache genie;
        SystematicsFamilyCache flux;
        SystematicsFamilyCache reint;
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
    TTree *selected_tree(const std::string &sample_key) const;
    TTree *subrun_tree(const std::string &sample_key) const;
    std::vector<std::string> systematics_cache_keys(const std::string &sample_key) const;
    bool has_systematics_cache(const std::string &sample_key, const std::string &cache_key) const;
    SystematicsCacheEntry read_systematics_cache(const std::string &sample_key,
                                                 const std::string &cache_key) const;
    void write_systematics_cache(const std::string &sample_key,
                                 const std::string &cache_key,
                                 const SystematicsCacheEntry &entry);

private:
    void require_open_() const;

    std::string path_;
    Mode mode_;
    TFile *file_ = nullptr;
};

#endif // EVENTLIST_IO_HH
