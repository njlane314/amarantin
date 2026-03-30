#ifndef PLOTTING_HELPER_HH
#define PLOTTING_HELPER_HH

#include <memory>
#include <string>
#include <vector>

#include "PlotDescriptors.hh"

class TH1D;
class TTree;

namespace plot_utils
{
    std::vector<Entry> make_entries(const EventListIO &event_list);
    void split_entries(const std::vector<Entry> &entries,
                       std::vector<const Entry *> &mc,
                       std::vector<const Entry *> &data);

    bool is_data_origin(DatasetIO::Sample::Origin origin);
    bool is_mc_like_origin(DatasetIO::Sample::Origin origin);

    TH1DModel make_spec(const std::string &expr,
                        int nbins,
                        double xmin,
                        double xmax,
                        const std::string &weight = "__w__");
    TH1DModel make_spec(const std::string &expr,
                        const std::vector<double> &bin_edges,
                        const std::string &weight = "__w__");

    std::string combine_selection(const std::vector<std::string> &parts);
    std::string equality_selection(const std::string &column, int value);
    std::unique_ptr<TH1D> book_histogram(const TH1DModel &spec,
                                         const std::string &hist_name,
                                         const std::string &hist_title = "");
    void fill_histogram(TTree *tree,
                        TH1D &hist,
                        const TH1DModel &spec,
                        const std::string &selection,
                        bool use_weights);
}

#endif // PLOTTING_HELPER_HH
