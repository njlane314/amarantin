#include "EventListSelection.hh"

#include <stdexcept>
#include <string>

namespace
{
    std::string join_and(const std::string &lhs, const std::string &rhs)
    {
        if (lhs.empty())
            return rhs;
        if (rhs.empty())
            return lhs;
        return "(" + lhs + ") && (" + rhs + ")";
    }
}

const char *EventListSelection::preset_name(Preset preset)
{
    switch (preset)
    {
        case Preset::kTrigger: return "trigger";
        case Preset::kSlice: return "slice";
        case Preset::kFiducial: return "fiducial";
        case Preset::kMuon: return "muon";
        case Preset::kRaw:
        default: return "raw";
    }
}

const char *EventListSelection::preset_label(Preset preset)
{
    switch (preset)
    {
        case Preset::kTrigger: return "Trigger Selection";
        case Preset::kSlice: return "Slice Selection";
        case Preset::kFiducial: return "Fiducial Selection";
        case Preset::kMuon: return "Muon Selection";
        case Preset::kRaw:
        default: return "Raw Selection";
    }
}

EventListSelection::Preset EventListSelection::preset_from_string(const std::string &name)
{
    if (name == "raw" || name == "empty" || name == "none")
        return Preset::kRaw;
    if (name == "trigger")
        return Preset::kTrigger;
    if (name == "slice")
        return Preset::kSlice;
    if (name == "fiducial")
        return Preset::kFiducial;
    if (name == "muon")
        return Preset::kMuon;

    throw std::runtime_error("EventListSelection: unknown preset: " + name);
}

const char *EventListSelection::trigger_branch()
{
    return "__pass_trigger__";
}

const char *EventListSelection::slice_branch()
{
    return "__pass_slice__";
}

const char *EventListSelection::fiducial_branch()
{
    return "__pass_fiducial__";
}

const char *EventListSelection::muon_branch()
{
    return "__pass_muon__";
}

bool EventListSelection::has_column(const std::vector<std::string> &columns, const std::string &name)
{
    for (const auto &column : columns)
    {
        if (column == name)
            return true;
    }
    return false;
}

void EventListSelection::require_column(const std::vector<std::string> &columns,
                                        const std::string &name,
                                        const char *context)
{
    if (!has_column(columns, name))
        throw std::runtime_error(std::string("EventListSelection: missing required column for ") +
                                 context + ": " + name);
}

std::string EventListSelection::expression(Preset preset,
                                           const DatasetIO::Sample &sample,
                                           const std::vector<std::string> &columns,
                                           const Config &config)
{
    if (preset == Preset::kRaw)
        return "";

    std::string trigger_expr;
    if (has_column(columns, trigger_branch()))
    {
        trigger_expr = trigger_branch();
    }
    else if (has_column(columns, "sel_trigger"))
    {
        trigger_expr = "sel_trigger";
    }
    else
    {
        require_column(columns, "software_trigger", "trigger preset");

        if (sample.beam == DatasetIO::Sample::Beam::kNuMI &&
            has_column(columns, "run") &&
            has_column(columns, "software_trigger_pre") &&
            has_column(columns, "software_trigger_post"))
        {
            trigger_expr = "((run < " + std::to_string(config.numi_run_boundary) +
                           ") ? (software_trigger_pre > 0) : (software_trigger_post > 0))";
        }
        else
        {
            trigger_expr = "(software_trigger > 0)";
        }
    }

    if (preset == Preset::kTrigger)
        return trigger_expr;

    std::string slice_base_expr;
    if (has_column(columns, slice_branch()))
    {
        slice_base_expr = slice_branch();
    }
    else if (has_column(columns, "sel_slice"))
    {
        slice_base_expr = "sel_slice";
    }
    else
    {
        require_column(columns, "num_slices", "slice preset");
        require_column(columns, "topological_score", "slice preset");
        slice_base_expr = "(num_slices == " + std::to_string(config.slice_required_count) +
                          ") && (topological_score > " + std::to_string(config.slice_min_topology_score) + ")";
    }
    const std::string slice_expr = join_and(trigger_expr, slice_base_expr);

    if (preset == Preset::kSlice)
        return slice_expr;

    std::string fiducial_base_expr;
    if (has_column(columns, fiducial_branch()))
    {
        fiducial_base_expr = fiducial_branch();
    }
    else if (has_column(columns, "sel_fiducial"))
    {
        fiducial_base_expr = "sel_fiducial";
    }
    else if (has_column(columns, "in_reco_fiducial"))
    {
        fiducial_base_expr = "in_reco_fiducial";
    }
    else
    {
        throw std::runtime_error("EventListSelection: fiducial preset requires sel_fiducial or in_reco_fiducial");
    }
    const std::string fiducial_expr = has_column(columns, fiducial_branch())
                                          ? fiducial_base_expr
                                          : join_and(slice_expr, fiducial_base_expr);

    if (preset == Preset::kFiducial)
        return fiducial_expr;

    if (has_column(columns, muon_branch()))
        return muon_branch();
    if (has_column(columns, "sel_muon"))
        return join_and(trigger_expr, "sel_muon");
    if (has_column(columns, "selection_pass"))
        return join_and(fiducial_expr, "(selection_pass > 0)");

    throw std::runtime_error("EventListSelection: muon preset requires sel_muon or selection_pass");
}
