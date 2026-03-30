#ifndef CUTS_HH
#define CUTS_HH

#include <string>
#include <vector>

#include "DatasetIO.hh"

namespace cuts
{
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

    const char *preset_name(Preset preset);
    const char *preset_label(Preset preset);
    Preset preset_from_string(const std::string &name);
    const char *trigger_branch();
    const char *slice_branch();
    const char *fiducial_branch();
    const char *muon_branch();

    std::string expression(Preset preset,
                           const DatasetIO::Sample &sample,
                           const std::vector<std::string> &columns,
                           const Config &config);
}

#endif // CUTS_HH
