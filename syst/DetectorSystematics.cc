#include "bits/Detail.hh"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

#include <Eigen/Dense>

namespace
{
    using MatrixRowMajor = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;

    std::string nominal_or_key(const std::string &key, const DatasetIO::Sample &sample)
    {
        return sample.nominal.empty() ? key : sample.nominal;
    }

    bool is_detector_cv(const DatasetIO::Sample &sample)
    {
        return sample.variation == DatasetIO::Sample::Variation::kDetector &&
               sample.tag == "cv";
    }
}

namespace syst::detail
{
    std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                          const CacheRequest &request)
    {
        if (!request.detector_sample_keys.empty())
            return request.detector_sample_keys;
        return eventlist.detector_mates(request.sample_key);
    }

    std::vector<DetectorShiftSource> resolve_detector_shift_sources(
        EventListIO &eventlist,
        const std::string &sample_key,
        const std::vector<std::string> &detector_sample_keys)
    {
        const DatasetIO::Sample requested_sample = eventlist.sample(sample_key);
        const std::string requested_nominal_key =
            nominal_or_key(sample_key, requested_sample);

        std::vector<std::string> detector_mate_keys =
            eventlist.detector_mates(sample_key);
        if (is_detector_cv(requested_sample))
            detector_mate_keys.push_back(sample_key);

        std::map<std::string, std::string> detector_cv_by_role;
        std::vector<std::string> detector_cv_keys;
        std::string default_detector_cv_key;
        for (const auto &candidate_key : detector_mate_keys)
        {
            const DatasetIO::Sample candidate_sample =
                eventlist.sample(candidate_key);
            if (!is_detector_cv(candidate_sample))
                continue;
            if (nominal_or_key(candidate_key, candidate_sample) !=
                requested_nominal_key)
                continue;

            detector_cv_keys.push_back(candidate_key);
            if (!candidate_sample.role.empty())
            {
                const auto role_it =
                    detector_cv_by_role.find(candidate_sample.role);
                if (role_it != detector_cv_by_role.end() &&
                    role_it->second != candidate_key)
                {
                    throw std::runtime_error(
                        "syst: multiple detector CV samples found for nominal " +
                        requested_nominal_key + " role " + candidate_sample.role);
                }
                detector_cv_by_role[candidate_sample.role] = candidate_key;
            }
            else if (!default_detector_cv_key.empty() &&
                     default_detector_cv_key != candidate_key)
            {
                throw std::runtime_error(
                    "syst: multiple detector CV samples found for nominal " +
                    requested_nominal_key + " without an explicit role");
            }
            else
            {
                default_detector_cv_key = candidate_key;
            }
        }

        std::vector<DetectorShiftSource> shift_sources;
        std::set<std::string> seen_shifted_sample_keys;
        std::set<std::string> seen_source_labels;
        for (const auto &candidate_key : detector_sample_keys)
        {
            if (candidate_key.empty() ||
                !seen_shifted_sample_keys.insert(candidate_key).second)
                continue;

            const DatasetIO::Sample shifted_sample =
                eventlist.sample(candidate_key);
            if (shifted_sample.variation != DatasetIO::Sample::Variation::kDetector)
                continue;
            if (is_detector_cv(shifted_sample))
                continue;

            const std::string shifted_nominal_key =
                nominal_or_key(candidate_key, shifted_sample);
            if (shifted_nominal_key != requested_nominal_key)
            {
                throw std::runtime_error(
                    "syst: detector sample " + candidate_key +
                    " does not match nominal " + requested_nominal_key);
            }

            std::string baseline_sample_key = sample_key;
            if (!shifted_sample.role.empty())
            {
                const auto role_it =
                    detector_cv_by_role.find(shifted_sample.role);
                if (role_it != detector_cv_by_role.end())
                    baseline_sample_key = role_it->second;
            }
            if (baseline_sample_key == sample_key &&
                !default_detector_cv_key.empty())
            {
                baseline_sample_key = default_detector_cv_key;
            }
            if (baseline_sample_key == sample_key &&
                detector_cv_keys.size() == 1)
            {
                baseline_sample_key = detector_cv_keys.front();
            }

            const std::string source_label =
                shifted_sample.tag.empty() ? candidate_key : shifted_sample.tag;
            if (!seen_source_labels.insert(source_label).second)
            {
                throw std::runtime_error(
                    "syst: duplicate detector source label " + source_label +
                    " for nominal " + requested_nominal_key);
            }

            shift_sources.push_back(
                DetectorShiftSource{
                    source_label,
                    baseline_sample_key,
                    candidate_key});
        }

        return shift_sources;
    }

    std::vector<double> detector_covariance_from_shift_vectors(const std::vector<double> &shift_vectors,
                                                               int source_count,
                                                               int nbins)
    {
        if (source_count <= 0 || shift_vectors.empty())
            return {};
        if (shift_vectors.size() != static_cast<std::size_t>(source_count * nbins))
        {
            throw std::runtime_error("syst: detector shift payload is truncated");
        }

        const Eigen::Map<const MatrixRowMajor> shifts(shift_vectors.data(),
                                                      source_count,
                                                      nbins);
        const MatrixRowMajor covariance = shifts.transpose() * shifts;
        return std::vector<double>(covariance.data(),
                                   covariance.data() + covariance.size());
    }

    Envelope detector_envelope(const std::vector<double> &nominal,
                               const std::vector<std::vector<double>> &variations)
    {
        Envelope out;
        if (variations.empty())
            return out;

        out.down = nominal;
        out.up = nominal;
        for (const auto &variation : variations)
        {
            if (variation.size() != nominal.size())
                continue;
            for (std::size_t bin = 0; bin < nominal.size(); ++bin)
            {
                out.down[bin] = std::min(out.down[bin], variation[bin]);
                out.up[bin] = std::max(out.up[bin], variation[bin]);
            }
        }
        return out;
    }

    Envelope detector_envelope_from_covariance(const std::vector<double> &nominal,
                                               const std::vector<double> &covariance)
    {
        Envelope out;
        if (nominal.empty() || covariance.empty())
            return out;

        const std::size_t nbins = nominal.size();
        if (covariance.size() != nbins * nbins)
            throw std::runtime_error("syst: detector covariance size does not match nominal bins");

        out.down = nominal;
        out.up = nominal;
        for (std::size_t bin = 0; bin < nbins; ++bin)
        {
            const double variance = covariance[bin * nbins + bin];
            const double sigma = std::sqrt(std::max(0.0, variance));
            out.down[bin] = std::max(0.0, nominal[bin] - sigma);
            out.up[bin] = nominal[bin] + sigma;
        }
        return out;
    }
}
