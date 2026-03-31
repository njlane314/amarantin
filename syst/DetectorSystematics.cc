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

    std::vector<DetectorSourceMatch> resolve_detector_source_matches(
        EventListIO &eventlist,
        const std::string &sample_key,
        const std::vector<std::string> &detector_sample_keys)
    {
        const DatasetIO::Sample seed = eventlist.sample(sample_key);
        const std::string seed_nominal = nominal_or_key(sample_key, seed);

        std::vector<std::string> siblings = eventlist.detector_mates(sample_key);
        if (is_detector_cv(seed))
            siblings.push_back(sample_key);

        std::map<std::string, std::string> detector_cv_by_role;
        std::vector<std::string> detector_cv_keys;
        for (const auto &key : siblings)
        {
            const DatasetIO::Sample sample = eventlist.sample(key);
            if (!is_detector_cv(sample))
                continue;
            if (nominal_or_key(key, sample) != seed_nominal)
                continue;

            detector_cv_keys.push_back(key);
            if (!sample.role.empty())
            {
                const auto it = detector_cv_by_role.find(sample.role);
                if (it != detector_cv_by_role.end() && it->second != key)
                {
                    throw std::runtime_error(
                        "syst: multiple detector CV samples found for nominal " +
                        seed_nominal + " role " + sample.role);
                }
                detector_cv_by_role[sample.role] = key;
            }
        }

        std::vector<DetectorSourceMatch> out;
        std::set<std::string> seen_varied_keys;
        std::set<std::string> seen_source_labels;
        for (const auto &key : detector_sample_keys)
        {
            if (key.empty() || !seen_varied_keys.insert(key).second)
                continue;

            const DatasetIO::Sample sample = eventlist.sample(key);
            if (sample.variation != DatasetIO::Sample::Variation::kDetector)
                continue;
            if (is_detector_cv(sample))
                continue;

            const std::string candidate_nominal = nominal_or_key(key, sample);
            if (candidate_nominal != seed_nominal)
            {
                throw std::runtime_error(
                    "syst: detector sample " + key +
                    " does not match nominal " + seed_nominal);
            }

            std::string cv_sample_key = sample_key;
            if (!sample.role.empty())
            {
                const auto it = detector_cv_by_role.find(sample.role);
                if (it != detector_cv_by_role.end())
                    cv_sample_key = it->second;
            }
            if (cv_sample_key == sample_key && detector_cv_keys.size() == 1)
                cv_sample_key = detector_cv_keys.front();

            const std::string source_label = sample.tag.empty() ? key : sample.tag;
            if (!seen_source_labels.insert(source_label).second)
            {
                throw std::runtime_error(
                    "syst: duplicate detector source label " + source_label +
                    " for nominal " + seed_nominal);
            }

            out.push_back(DetectorSourceMatch{source_label, cv_sample_key, key});
        }

        return out;
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
