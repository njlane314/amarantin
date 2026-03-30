#ifndef EVENTLIST_PLOTTING_HH
#define EVENTLIST_PLOTTING_HH

#include <memory>
#include <string>
#include <vector>

#include "EventListIO.hh"

class TH1D;
class TCanvas;

namespace plot_utils
{
    std::vector<std::string> selected_sample_keys(const EventListIO &eventlist,
                                                  const char *sample_key);

    std::unique_ptr<TH1D> make_histogram(const EventListIO &eventlist,
                                         const char *branch_expr,
                                         int nbins,
                                         double xmin,
                                         double xmax,
                                         const char *hist_name = "h_eventlist",
                                         const char *sample_key = nullptr);

    TCanvas *draw_distribution(const EventListIO &eventlist,
                               const char *branch_expr,
                               int nbins,
                               double xmin,
                               double xmax,
                               const char *canvas_name = "c_eventlist",
                               const char *sample_key = nullptr);

    TCanvas *draw_distribution(const char *read_path,
                               const char *branch_expr,
                               int nbins,
                               double xmin,
                               double xmax,
                               const char *canvas_name = "c_eventlist",
                               const char *sample_key = nullptr);
}

#endif // EVENTLIST_PLOTTING_HH
