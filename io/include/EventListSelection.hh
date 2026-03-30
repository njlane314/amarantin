#ifndef EVENTLIST_SELECTION_HH
#define EVENTLIST_SELECTION_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

class EventListSelection
{
public:
    enum class Preset
    {
        kRaw,
        kTrigger,
        kSlice,
        kFiducial,
        kMuon
    };

    struct Config
    {
        int slice_required_count = 1;
        double slice_min_topology_score = 0.06;
        int numi_run_boundary = 16880;
    };

    static const char *preset_name(Preset preset);
    static const char *preset_label(Preset preset);
    static Preset preset_from_string(const std::string &name);
    static const char *trigger_branch();
    static const char *slice_branch();
    static const char *fiducial_branch();
    static const char *muon_branch();

    static std::string expression(Preset preset,
                                  const DatasetIO::Sample &sample,
                                  const std::vector<std::string> &columns,
                                  const Config &config);

private:
    static bool has_column(const std::vector<std::string> &columns, const std::string &name);
    static void require_column(const std::vector<std::string> &columns,
                               const std::string &name,
                               const char *context);
};

#endif // EVENTLIST_SELECTION_HH
