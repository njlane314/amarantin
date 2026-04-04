#include "SignalDefinition.hh"

#include <cmath>
#include <sstream>

namespace
{
    std::string describe_fiducial_box(const ana::SignalDefinition::FiducialBox &box)
    {
        std::ostringstream os;
        os << "active=("
           << box.active_min_x << "," << box.active_max_x << ","
           << box.active_min_y << "," << box.active_max_y << ","
           << box.active_min_z << "," << box.active_max_z << ")"
           << " margins=("
           << box.x_start << "," << box.x_end << ","
           << box.y_start << "," << box.y_end << ","
           << box.z_start << "," << box.z_end << ")";
        os << ";excluded_z=("
           << box.excluded_z_min << "," << box.excluded_z_max << ")";
        return os.str();
    }

    bool has_qualifying_measurement_hyperon(
        const ana::SignalDefinition::StrangeTruthSummary &summary)
    {
        return summary.qualifying_exit_lambda0_count > 0 ||
               summary.qualifying_exit_sigma0_count > 0;
    }
}

namespace ana
{
    const SignalDefinition &SignalDefinition::canonical()
    {
        static const SignalDefinition signal_definition;
        return signal_definition;
    }

    const SignalDefinition::FiducialBox &SignalDefinition::canonical_fiducial_box()
    {
        return canonical().truth_vertex_fv_;
    }

    SignalDefinition::MeasurementTruthContract SignalDefinition::canonical_contract()
    {
        return canonical().contract_;
    }

    const char *SignalDefinition::measurement_truth_category_name(
        MeasurementTruthCategory category)
    {
        switch (category)
        {
            case MeasurementTruthCategory::kMeasurementSignal:
                return "measurement_signal";
            case MeasurementTruthCategory::kNeutralHyperonOutOfPhaseSpace:
                return "neutral_hyperon_out_of_phase_space";
            case MeasurementTruthCategory::kOtherStrangeBackground:
                return "other_strange_background";
            case MeasurementTruthCategory::kDetectorSecondaryHyperonBackground:
                return "detector_secondary_hyperon_background";
            case MeasurementTruthCategory::kNonstrangeOverlay:
                return "nonstrange_overlay";
            case MeasurementTruthCategory::kUnknown:
            default:
                return "unknown";
        }
    }

    SampleSelectionRule sample_selection_rule(const DatasetIO::Sample &sample)
    {
        using Origin = DatasetIO::Sample::Origin;

        if (sample.origin == Origin::kOverlay)
            return {"count_strange == 0", "count_strange"};
        if (sample.origin == Origin::kSignal)
            return {"count_strange > 0", "count_strange"};
        return {};
    }

    bool SignalDefinition::in_fiducial(float x,
                                       float y,
                                       float z,
                                       const FiducialBox &box)
    {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return false;
        const bool in_outer_box =
            x >= (box.active_min_x + box.x_start) &&
            x <= (box.active_max_x - box.x_end) &&
            y >= (box.active_min_y + box.y_start) &&
            y <= (box.active_max_y - box.y_end) &&
            z >= (box.active_min_z + box.z_start) &&
            z <= (box.active_max_z - box.z_end);
        if (!in_outer_box)
            return false;

        if (std::isfinite(box.excluded_z_min) &&
            std::isfinite(box.excluded_z_max) &&
            box.excluded_z_min < box.excluded_z_max &&
            z > box.excluded_z_min &&
            z < box.excluded_z_max)
        {
            return false;
        }

        return true;
    }

    bool SignalDefinition::truth_vertex_in_fv(const TruthInput &truth) const
    {
        if (std::isfinite(truth.truth_vtx_x) &&
            std::isfinite(truth.truth_vtx_y) &&
            std::isfinite(truth.truth_vtx_z))
        {
            return in_fiducial(truth.truth_vtx_x,
                               truth.truth_vtx_y,
                               truth.truth_vtx_z,
                               truth_vertex_fv_);
        }
        return truth.truth_in_fiducial;
    }

    bool SignalDefinition::passes(const TruthInput &truth,
                                  const LambdaTruthCandidate &candidate) const
    {
        if (!candidate.valid)
            return false;
        if (!std::isfinite(candidate.lambda_p))
            return false;
        StrangeTruthSummary summary;
        summary.has_strange_final_state = true;
        if (candidate.has_sigma0_ancestor || candidate.lambda_pdg == 3212)
        {
            summary.has_exit_sigma0 = true;
            summary.qualifying_exit_sigma0_count =
                candidate.lambda_p >= contract_.min_sigma0_p ? 1 : 0;
        }
        else
        {
            summary.has_exit_lambda0 = true;
            summary.qualifying_exit_lambda0_count =
                candidate.lambda_p >= contract_.min_lambda0_p ? 1 : 0;
        }
        return passes_measurement_signal(truth, summary);
    }

    bool SignalDefinition::passes_measurement_signal(
        const TruthInput &truth,
        const StrangeTruthSummary &summary) const
    {
        if (contract_.require_cc_interaction && truth.ccnc != 0)
            return false;
        if (contract_.require_muon_flavour_cc && !truth.is_nu_mu_cc)
            return false;
        if (contract_.require_true_vertex_in_fv && !truth_vertex_in_fv(truth))
            return false;
        return has_qualifying_measurement_hyperon(summary);
    }

    SignalDefinition::MeasurementTruthCategory
    SignalDefinition::classify_measurement_truth(
        const TruthInput &truth,
        const StrangeTruthSummary &summary) const
    {
        if (passes_measurement_signal(truth, summary))
            return MeasurementTruthCategory::kMeasurementSignal;

        if (summary.has_exit_lambda0 || summary.has_exit_sigma0)
            return MeasurementTruthCategory::kNeutralHyperonOutOfPhaseSpace;

        if (summary.has_detector_secondary_lambda0)
            return MeasurementTruthCategory::kDetectorSecondaryHyperonBackground;

        if (summary.has_strange_final_state)
            return MeasurementTruthCategory::kOtherStrangeBackground;

        return MeasurementTruthCategory::kNonstrangeOverlay;
    }

    std::string SignalDefinition::describe() const
    {
        std::ostringstream os;
        os << "observable=event_level_cc_mu_flavour_plus_neutral_hyperon"
           << ";contract_file=ana/ccnumu_hyperon_measurement_contract.json"
           << ";poi=kappa=sigma_phase_space/sigma_phase_space_nominal"
           << ";counting_rule=event_once"
           << ";flux_averaged=" << (contract_.flux_averaged ? 1 : 0)
           << ";include_antineutrino=" << (contract_.include_antineutrino ? 1 : 0)
           << ";require_cc_interaction=" << (contract_.require_cc_interaction ? 1 : 0)
           << ";require_muon_flavour_cc=" << (contract_.require_muon_flavour_cc ? 1 : 0)
           << ";require_true_vertex_in_fv=" << (contract_.require_true_vertex_in_fv ? 1 : 0)
           << ";lambda0_min_p=" << contract_.min_lambda0_p
           << ";sigma0_min_p=" << contract_.min_sigma0_p
           << ";signal_truth=exit_state_lambda0_or_sigma0"
           << ";detector_secondary_without_exit_state_ancestor=background"
           << ";truth_vertex_fv=" << describe_fiducial_box(truth_vertex_fv_);
        return os.str();
    }
}
