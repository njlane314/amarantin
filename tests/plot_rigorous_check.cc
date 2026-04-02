#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DistributionIO.hh"
#include "EfficiencyPlot.hh"
#include "EventListIO.hh"
#include "EventListPlotting.hh"
#include "PlotDescriptors.hh"
#include "Plotter.hh"
#include "PlottingHelper.hh"

#include "TROOT.h"
#include "TH1D.h"
#include "TTree.h"

namespace
{
    struct TempDir
    {
        std::filesystem::path path;

        ~TempDir()
        {
            if (path.empty())
                return;
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    struct EventRow
    {
        double x = 0.0;
        double weight = 1.0;
        int legacy_category = 0;
        bool legacy_signal = false;
    };

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("plot_rigorous_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    bool approx(double lhs, double rhs, double tolerance = 1e-9)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    void require_close(double actual,
                       double expected,
                       const std::string &label,
                       double tolerance = 1e-9)
    {
        if (!approx(actual, expected, tolerance))
        {
            fail(label + ": expected " + std::to_string(expected) +
                 ", got " + std::to_string(actual));
        }
    }

    void require_equal_vector(const std::vector<std::string> &actual,
                              const std::vector<std::string> &expected,
                              const std::string &label)
    {
        require(actual == expected, label + ": unexpected values");
    }

    void require_throws(const std::function<void()> &fn,
                        const std::string &needle,
                        const std::string &label)
    {
        try
        {
            fn();
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            if (message.find(needle) == std::string::npos)
                fail(label + ": unexpected exception message: " + message);
            return;
        }

        fail(label + ": expected an exception");
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() / "amarantin-plot-rigorous.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");

        TempDir out;
        out.path = dir;
        return out;
    }

    TTree *make_selected_tree(const std::vector<EventRow> &rows)
    {
        auto *tree = new TTree("selected", "selected");
        double x = 0.0;
        double weight = 1.0;
        int legacy_category = 0;
        bool legacy_signal = false;
        tree->Branch("x", &x);
        tree->Branch("__w__", &weight);
        tree->Branch("__analysis_channel__", &legacy_category);
        tree->Branch("__is_signal__", &legacy_signal);
        for (const auto &row : rows)
        {
            x = row.x;
            weight = row.weight;
            legacy_category = row.legacy_category;
            legacy_signal = row.legacy_signal;
            tree->Fill();
        }
        return tree;
    }

    TTree *make_subrun_tree()
    {
        auto *tree = new TTree("SubRun", "SubRun");
        int run = 1;
        int subRun = 1;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subRun);
        tree->Fill();
        return tree;
    }

    DatasetIO::Sample make_sample(DatasetIO::Sample::Origin origin,
                                  DatasetIO::Sample::Variation variation,
                                  const std::string &nominal = "",
                                  const std::string &tag = "",
                                  const std::string &role = "")
    {
        DatasetIO::Sample sample;
        sample.origin = origin;
        sample.variation = variation;
        sample.beam = DatasetIO::Sample::Beam::kNuMI;
        sample.polarity = DatasetIO::Sample::Polarity::kFHC;
        sample.sample = nominal.empty() ? "sample" : nominal;
        sample.nominal = nominal;
        sample.tag = tag;
        sample.role = role;
        return sample;
    }

    void write_sample(EventListIO &eventlist,
                      const std::string &sample_key,
                      const DatasetIO::Sample &sample,
                      const std::vector<EventRow> &rows)
    {
        TTree *selected_tree = make_selected_tree(rows);
        TTree *subrun_tree = make_subrun_tree();
        eventlist.write_sample(sample_key, sample, selected_tree, subrun_tree, "SubRun");
        delete subrun_tree;
        delete selected_tree;
    }

    std::filesystem::path write_plot_eventlist(const std::filesystem::path &path)
    {
        EventListIO eventlist(path.string(), EventListIO::Mode::kWrite);

        EventListIO::Metadata metadata;
        metadata.dataset_path = "plot.dataset.root";
        metadata.dataset_context = "plot-rigorous";
        metadata.event_tree_name = "selected";
        metadata.subrun_tree_name = "SubRun";
        metadata.selection_name = "raw";
        metadata.selection_expr = "1";
        metadata.signal_definition = "signal";
        eventlist.write_metadata(metadata);

        write_sample(eventlist,
                     "beam",
                     make_sample(DatasetIO::Sample::Origin::kOverlay,
                                 DatasetIO::Sample::Variation::kNominal),
                     {{0.25, 2.0, 15, true},
                      {0.75, 1.0, 1, false}});

        write_sample(eventlist,
                     "beam-sce",
                     make_sample(DatasetIO::Sample::Origin::kOverlay,
                                 DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "sce",
                                 "sce"),
                     {{0.25, 50.0, 15, true}});

        write_sample(eventlist,
                     "data",
                     make_sample(DatasetIO::Sample::Origin::kData,
                                 DatasetIO::Sample::Variation::kNominal),
                     {{0.75, 1.0, 99, false}});

        eventlist.flush();
        return path;
    }

    DistributionIO::Spectrum make_distribution_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam";
        spectrum.spec.cache_key = "cached";
        spectrum.spec.branch_expr = "x";
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 1.0;
        spectrum.nominal = {4.0, 9.0};
        spectrum.sumw2 = {1.0, 4.0};
        return spectrum;
    }

    void test_default_sample_selection_skips_detector_variations()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = write_plot_eventlist(temp.path / "plot.eventlist.root");

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        const std::vector<std::string> selected = plot_utils::selected_sample_keys(eventlist, nullptr);
        require_equal_vector(selected,
                             {"beam", "data"},
                             "default event-list selection");

        const std::vector<plot_utils::Entry> entries = plot_utils::make_entries(eventlist);
        require(entries.size() == 2, "plot entry count");
        require(entries[0].sample_key == "beam", "first plot entry should be beam");
        require(entries[1].sample_key == "data", "second plot entry should be data");

        std::vector<const plot_utils::Entry *> mc;
        std::vector<const plot_utils::Entry *> data;
        plot_utils::split_entries(entries, mc, data);
        require(mc.size() == 1, "MC plot entry count");
        require(data.size() == 1, "data plot entry count");
        require(mc.front()->sample_key == "beam", "MC entry key");
        require(data.front()->sample_key == "data", "data entry key");

        const std::unique_ptr<TH1D> combined =
            plot_utils::make_histogram(eventlist, "x", 2, 0.0, 1.0, "h_combined");
        require_close(combined->GetBinContent(1), 2.0, "combined histogram bin 1");
        require_close(combined->GetBinContent(2), 2.0, "combined histogram bin 2");

        const std::unique_ptr<TH1D> detector =
            plot_utils::make_histogram(eventlist, "x", 2, 0.0, 1.0, "h_detector", "beam-sce");
        require_close(detector->GetBinContent(1), 50.0, "explicit detector histogram bin 1");
        require_close(detector->GetBinContent(2), 0.0, "explicit detector histogram bin 2");

        plot_utils::EfficiencyPlot::Spec spec;
        spec.branch_expr = "x";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 1.0;
        spec.hist_name = "eff";

        plot_utils::EfficiencyPlot plot(spec);
        plot.compute(eventlist, "1", "x < 0.5");
        require(plot.ready(), "efficiency plot should be ready");
        require(plot.denom_entries() == 3, "efficiency denominator rows");
        require(plot.pass_entries() == 1, "efficiency numerator rows");
        require_close(plot.denom_total(), 3.0, "efficiency denominator total");
        require_close(plot.pass_total(), 1.0, "efficiency numerator total");
    }

    void test_plotter_renders_with_eventlist_aliases()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = write_plot_eventlist(temp.path / "plot.eventlist.root");

        gROOT->SetBatch(kTRUE);

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        plot_utils::Options options;
        options.out_dir = (temp.path / "plots").string();
        options.image_format = "png";
        options.unstack_event_category_keys = {15, 1};

        plot_utils::TH1DModel spec;
        spec.id = "stacked_plot";
        spec.name = "x";
        spec.expr = "x";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        plot_utils::Plotter plotter(options);
        plotter.draw_stack(spec, eventlist);
        require(std::filesystem::exists(temp.path / "plots" / "stacked_plot.png"),
                "stacked plot output should exist");

        spec.id = "unstacked_plot";
        plotter.draw_unstack(spec, eventlist);
        require(std::filesystem::exists(temp.path / "plots" / "unstacked_plot.png"),
                "unstacked plot output should exist");
    }

    void test_invalid_plot_inputs_fail_fast()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = write_plot_eventlist(temp.path / "plot.eventlist.root");

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        require_throws(
            [&]()
            {
                (void)plot_utils::make_histogram(eventlist,
                                                 "missing_branch",
                                                 2,
                                                 0.0,
                                                 1.0,
                                                 "h_invalid");
            },
            "failed to draw histogram",
            "event-list histogram should fail on bad expressions");

        plot_utils::EfficiencyPlot::Spec spec;
        spec.branch_expr = "missing_branch";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        require_throws(
            [&]()
            {
                plot_utils::EfficiencyPlot plot(spec);
                plot.compute(eventlist, "1", "1");
            },
            "failed to draw denominator histogram",
            "efficiency plot should fail on bad expressions");
    }

    void test_distribution_histogram_shape_validation()
    {
        const DistributionIO::Spectrum spectrum = make_distribution_spectrum();
        const std::unique_ptr<TH1D> hist = plot_utils::make_histogram(spectrum, "h_cached");
        require_close(hist->GetBinContent(1), 4.0, "distribution histogram bin 1");
        require_close(hist->GetBinContent(2), 9.0, "distribution histogram bin 2");
        require_close(hist->GetBinError(1), 1.0, "distribution histogram error 1");
        require_close(hist->GetBinError(2), 2.0, "distribution histogram error 2");

        DistributionIO::Spectrum bad_nominal = spectrum;
        bad_nominal.nominal.pop_back();
        require_throws(
            [&]() { (void)plot_utils::make_histogram(bad_nominal, "h_bad_nominal"); },
            "nominal size does not match histogram bins",
            "cached histogram should reject truncated nominal payloads");

        DistributionIO::Spectrum bad_sumw2 = spectrum;
        bad_sumw2.sumw2.pop_back();
        require_throws(
            [&]() { (void)plot_utils::make_histogram(bad_sumw2, "h_bad_sumw2"); },
            "sumw2 size does not match histogram bins",
            "cached histogram should reject truncated sumw2 payloads");
    }
}

int main()
{
    try
    {
        test_default_sample_selection_skips_detector_variations();
        test_plotter_renders_with_eventlist_aliases();
        test_invalid_plot_inputs_fail_fast();
        test_distribution_histogram_shape_validation();
        std::cout << "plot_rigorous_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
