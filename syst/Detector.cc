#include "bits/Detail.hh"

#include <algorithm>

namespace syst::detail
{
    std::vector<std::string> resolve_detector_sample_keys(EventListIO &eventlist,
                                                          const CacheRequest &request)
    {
        if (!request.detector_sample_keys.empty())
            return request.detector_sample_keys;
        return eventlist.detector_mates(request.sample_key);
    }

    std::vector<std::vector<double>> rebin_detector_templates(const CacheEntry &entry,
                                                              const HistogramSpec &target_spec)
    {
        std::vector<std::vector<double>> out;
        if (entry.detector_template_count <= 0 || entry.detector_templates.empty())
            return out;

        out.assign(static_cast<std::size_t>(entry.detector_template_count),
                   std::vector<double>(static_cast<std::size_t>(target_spec.nbins), 0.0));

        const MatrixRowMajor rebin = build_rebin_matrix(entry.spec.nbins,
                                                        entry.spec.xmin,
                                                        entry.spec.xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);
        const Eigen::Map<const MatrixRowMajor> templates(entry.detector_templates.data(),
                                                         entry.detector_template_count,
                                                         entry.spec.nbins);
        const MatrixRowMajor rebinned = templates * rebin.transpose();
        for (int row = 0; row < entry.detector_template_count; ++row)
        {
            for (int col = 0; col < target_spec.nbins; ++col)
                out[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = rebinned(row, col);
        }
        return out;
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
}
