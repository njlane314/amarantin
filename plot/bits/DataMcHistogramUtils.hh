#ifndef PLOT_BITS_DATA_MC_HISTOGRAM_UTILS_HH
#define PLOT_BITS_DATA_MC_HISTOGRAM_UTILS_HH

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "TH1D.h"
#include "TMatrixDSym.h"

namespace plot_utils
{
    namespace bits
    {
        inline std::pair<int, int> visible_bin_range(const TH1D &hist, double xmin, double xmax)
        {
            const TAxis *axis = hist.GetXaxis();
            int first_bin = 1;
            int last_bin = hist.GetNbinsX();
            if (axis && xmin < xmax)
            {
                first_bin = std::max(1, axis->FindFixBin(xmin));
                last_bin = std::min(hist.GetNbinsX(), axis->FindFixBin(xmax));
                if (xmax <= axis->GetBinLowEdge(last_bin))
                    last_bin = std::max(first_bin, last_bin - 1);
            }
            return {first_bin, last_bin};
        }

        inline void apply_total_errors(TH1D &hist,
                                       const TMatrixDSym *cov,
                                       const std::vector<double> *syst_bin,
                                       bool density_mode)
        {
            for (int i = 1; i <= hist.GetNbinsX(); ++i)
            {
                const double stat = hist.GetBinError(i);
                double syst = 0.0;

                if (cov && i - 1 < cov->GetNrows())
                    syst = std::sqrt(std::max(0.0, (*cov)(i - 1, i - 1)));
                else if (syst_bin && i - 1 < static_cast<int>(syst_bin->size()))
                    syst = std::max(0.0, (*syst_bin)[static_cast<std::size_t>(i - 1)]);

                if (density_mode)
                {
                    const double width = hist.GetXaxis() ? hist.GetXaxis()->GetBinWidth(i) : 0.0;
                    if (width > 0.0)
                        syst /= width;
                }

                hist.SetBinError(i, std::sqrt(stat * stat + syst * syst));
            }
        }

        inline double integral_in_visible_range(const TH1D &hist, double xmin, double xmax)
        {
            const auto [first_bin, last_bin] = visible_bin_range(hist, xmin, xmax);
            if (first_bin > last_bin)
                return 0.0;
            return hist.Integral(first_bin, last_bin);
        }

        inline double maximum_in_visible_range(const TH1D &hist,
                                               double xmin,
                                               double xmax,
                                               bool include_error)
        {
            const auto [first_bin, last_bin] = visible_bin_range(hist, xmin, xmax);
            if (first_bin > last_bin)
                return 0.0;

            double max_y = 0.0;
            for (int i = first_bin; i <= last_bin; ++i)
            {
                const double y = hist.GetBinContent(i) + (include_error ? hist.GetBinError(i) : 0.0);
                max_y = std::max(max_y, y);
            }
            return max_y;
        }

        inline std::unique_ptr<TH1D> make_ratio_histogram(const TH1D &data,
                                                          const TH1D &mc,
                                                          const std::string &name)
        {
            auto ratio = std::unique_ptr<TH1D>(static_cast<TH1D *>(data.Clone(name.c_str())));
            ratio->SetDirectory(nullptr);
            for (int i = 1; i <= ratio->GetNbinsX(); ++i)
            {
                const double denom = mc.GetBinContent(i);
                const double value = data.GetBinContent(i);
                const double error = data.GetBinError(i);
                ratio->SetBinContent(i, denom > 0.0 ? value / denom : 0.0);
                ratio->SetBinError(i, denom > 0.0 ? error / denom : 0.0);
            }
            return ratio;
        }

        inline std::unique_ptr<TH1D> make_ratio_band_histogram(const TH1D &mc,
                                                               const std::string &name)
        {
            auto band = std::unique_ptr<TH1D>(static_cast<TH1D *>(mc.Clone(name.c_str())));
            band->SetDirectory(nullptr);
            for (int i = 1; i <= band->GetNbinsX(); ++i)
            {
                const double denom = mc.GetBinContent(i);
                const double error = mc.GetBinError(i);
                band->SetBinContent(i, 1.0);
                band->SetBinError(i, denom > 0.0 ? error / denom : 0.0);
            }
            return band;
        }
    }
}

#endif // PLOT_BITS_DATA_MC_HISTOGRAM_UTILS_HH
