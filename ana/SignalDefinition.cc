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
        if (require_cc_interaction_ && truth.ccnc != 0)
            return false;
        if (require_numu_cc_ && !truth.is_nu_mu_cc)
            return false;
        if (require_truth_vertex_in_fv_ && !truth_vertex_in_fv(truth))
            return false;
        if (required_lambda_pdg_ != 0 && candidate.lambda_pdg != required_lambda_pdg_)
            return false;
        if (require_ppi_decay_ && !candidate.has_ppi_decay)
            return false;
        if (require_sigma0_ancestor_ && !candidate.has_sigma0_ancestor)
            return false;
        if (min_muon_p_ >= 0.0f &&
            (!std::isfinite(truth.mu_p) || truth.mu_p < min_muon_p_))
        {
            return false;
        }
        if (!std::isfinite(candidate.lambda_p))
            return false;
        if (min_lambda_p_ >= 0.0f && candidate.lambda_p < min_lambda_p_)
            return false;
        if (max_lambda_p_ >= 0.0f && candidate.lambda_p > max_lambda_p_)
            return false;
        if ((min_proton_p_ >= 0.0f || max_proton_p_ >= 0.0f) &&
            !std::isfinite(candidate.proton_p))
        {
            return false;
        }
        if (min_proton_p_ >= 0.0f && candidate.proton_p < min_proton_p_)
            return false;
        if (max_proton_p_ >= 0.0f && candidate.proton_p > max_proton_p_)
            return false;
        if ((min_pion_p_ >= 0.0f || max_pion_p_ >= 0.0f) &&
            !std::isfinite(candidate.pion_p))
        {
            return false;
        }
        if (min_pion_p_ >= 0.0f && candidate.pion_p < min_pion_p_)
            return false;
        if (max_pion_p_ >= 0.0f && candidate.pion_p > max_pion_p_)
            return false;
        if (min_lambda_decay_sep_ >= 0.0f &&
            (!std::isfinite(candidate.decay_sep) ||
             candidate.decay_sep < min_lambda_decay_sep_))
        {
            return false;
        }
        if (require_lambda_decay_in_fv_ &&
            !in_fiducial(candidate.decay_x,
                         candidate.decay_y,
                         candidate.decay_z,
                         lambda_decay_fv_))
        {
            return false;
        }
        if (require_ppi_endpoints_in_fv_ &&
            (!in_fiducial(candidate.proton_end_x,
                          candidate.proton_end_y,
                          candidate.proton_end_z,
                          daughter_end_fv_) ||
             !in_fiducial(candidate.pion_end_x,
                          candidate.pion_end_y,
                          candidate.pion_end_z,
                          daughter_end_fv_)))
        {
            return false;
        }
        if (min_reco_contained_fraction_ >= 0.0f &&
            (!std::isfinite(truth.contained_fraction) ||
             truth.contained_fraction < min_reco_contained_fraction_))
        {
            return false;
        }
        return true;
    }

    std::string SignalDefinition::describe() const
    {
        std::ostringstream os;
        os << "require_cc_interaction=" << (require_cc_interaction_ ? 1 : 0)
           << ";require_numu_cc=" << (require_numu_cc_ ? 1 : 0)
           << ";require_truth_vertex_in_fv=" << (require_truth_vertex_in_fv_ ? 1 : 0)
           << ";require_ppi_decay=" << (require_ppi_decay_ ? 1 : 0)
           << ";require_lambda_decay_in_fv=" << (require_lambda_decay_in_fv_ ? 1 : 0)
           << ";require_ppi_endpoints_in_fv=" << (require_ppi_endpoints_in_fv_ ? 1 : 0)
           << ";require_sigma0_ancestor=" << (require_sigma0_ancestor_ ? 1 : 0)
           << ";required_lambda_pdg=" << required_lambda_pdg_
           << ";min_muon_p=" << min_muon_p_
           << ";min_lambda_p=" << min_lambda_p_
           << ";max_lambda_p=" << max_lambda_p_
           << ";min_proton_p=" << min_proton_p_
           << ";max_proton_p=" << max_proton_p_
           << ";min_pion_p=" << min_pion_p_
           << ";max_pion_p=" << max_pion_p_
           << ";min_lambda_decay_sep=" << min_lambda_decay_sep_
           << ";min_reco_contained_fraction=" << min_reco_contained_fraction_
           << ";truth_vertex_fv=" << describe_fiducial_box(truth_vertex_fv_)
           << ";lambda_decay_fv=" << describe_fiducial_box(lambda_decay_fv_)
           << ";daughter_end_fv=" << describe_fiducial_box(daughter_end_fv_);
        return os.str();
    }
}
