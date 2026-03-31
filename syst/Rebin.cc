#include "bits/Detail.hh"

#include <algorithm>
#include <stdexcept>

namespace syst::detail
{
    MatrixRowMajor build_rebin_matrix(int source_nbins,
                                      double source_xmin,
                                      double source_xmax,
                                      int target_nbins,
                                      double target_xmin,
                                      double target_xmax)
    {
        MatrixRowMajor rebin = MatrixRowMajor::Zero(target_nbins, source_nbins);
        const double source_width = (source_xmax - source_xmin) / static_cast<double>(source_nbins);
        const double target_width = (target_xmax - target_xmin) / static_cast<double>(target_nbins);
        if (source_width <= 0.0 || target_width <= 0.0)
            throw std::runtime_error("syst: invalid rebinning range");

        for (int target_bin = 0; target_bin < target_nbins; ++target_bin)
        {
            const double target_low = target_xmin + target_width * static_cast<double>(target_bin);
            const double target_high = target_low + target_width;

            for (int source_bin = 0; source_bin < source_nbins; ++source_bin)
            {
                const double source_low = source_xmin + source_width * static_cast<double>(source_bin);
                const double source_high = source_low + source_width;
                const double overlap = std::max(0.0, std::min(source_high, target_high) - std::max(source_low, target_low));
                if (overlap > 0.0)
                    rebin(target_bin, source_bin) = overlap / source_width;
            }
        }
        return rebin;
    }

    std::vector<double> rebin_vector(const std::vector<double> &source,
                                     int source_nbins,
                                     double source_xmin,
                                     double source_xmax,
                                     const HistogramSpec &target_spec)
    {
        if (source.empty())
            return std::vector<double>{};

        const MatrixRowMajor rebin = build_rebin_matrix(source_nbins,
                                                        source_xmin,
                                                        source_xmax,
                                                        target_spec.nbins,
                                                        target_spec.xmin,
                                                        target_spec.xmax);
        const Eigen::Map<const Eigen::VectorXd> source_vec(source.data(), source_nbins);
        const Eigen::VectorXd target = rebin * source_vec;
        return std::vector<double>(target.data(), target.data() + target.size());
    }
}
