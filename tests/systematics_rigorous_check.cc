#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DatasetIO.hh"
#include "DistributionIO.hh"
#include "EventListIO.hh"
#include "Systematics.hh"
#include "bits/Detail.hh"

#include "TTree.h"

namespace
{
    constexpr std::size_t kGenieKnobCount = 53;

    struct EventRow
    {
        double x = 0.0;
        double weight = 1.0;
        int sel = 1;
        std::vector<unsigned short> genie;
        std::vector<unsigned short> flux;
        std::vector<unsigned short> ppfx;
        std::vector<unsigned short> reint;
        std::vector<unsigned short> genie_up;
        std::vector<unsigned short> genie_down;
    };

    struct TreeOptions
    {
        bool include_weight = true;
        bool include_selection = false;
        bool include_genie = false;
        bool include_flux = false;
        bool include_ppfx = false;
        bool include_reint = false;
        bool include_genie_knobs = false;
    };

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

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("systematics_rigorous_check: " + message);
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

    void require_close_vector(const std::vector<double> &actual,
                              const std::vector<double> &expected,
                              const std::string &label,
                              double tolerance = 1e-9)
    {
        require(actual.size() == expected.size(),
                label + ": size mismatch");
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            require_close(actual[i],
                          expected[i],
                          label + "[" + std::to_string(i) + "]",
                          tolerance);
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
            {
                fail(label + ": unexpected exception message: " + message);
            }
            return;
        }

        fail(label + ": expected an exception");
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() / "amarantin-syst-rigorous.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");

        TempDir out;
        out.path = dir;
        return out;
    }

    std::vector<unsigned short> knob_weights(std::size_t source,
                                             unsigned short value)
    {
        std::vector<unsigned short> out(kGenieKnobCount, 1000);
        if (source < out.size())
            out[source] = value;
        return out;
    }

    EventRow make_plain_row(double x)
    {
        EventRow row;
        row.x = x;
        return row;
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

    TTree *make_selected_tree(const std::vector<EventRow> &rows,
                              const TreeOptions &options)
    {
        auto *tree = new TTree("selected", "selected");

        double x = 0.0;
        double weight = 1.0;
        int sel = 1;
        std::vector<unsigned short> genie;
        std::vector<unsigned short> flux;
        std::vector<unsigned short> ppfx;
        std::vector<unsigned short> reint;
        std::vector<unsigned short> genie_up;
        std::vector<unsigned short> genie_down;

        tree->Branch("x", &x);
        if (options.include_weight)
            tree->Branch("__w__", &weight);
        if (options.include_selection)
            tree->Branch("sel", &sel);
        if (options.include_genie)
            tree->Branch("weightsGenie", &genie);
        if (options.include_flux)
            tree->Branch("weightsFlux", &flux);
        if (options.include_ppfx)
            tree->Branch("weightsPPFX", &ppfx);
        if (options.include_reint)
            tree->Branch("weightsReint", &reint);
        if (options.include_genie_knobs)
        {
            tree->Branch("weightsGenieUp", &genie_up);
            tree->Branch("weightsGenieDn", &genie_down);
        }

        for (const auto &row : rows)
        {
            x = row.x;
            weight = row.weight;
            sel = row.sel;
            genie = row.genie;
            flux = row.flux;
            ppfx = row.ppfx;
            reint = row.reint;
            genie_up = row.genie_up;
            genie_down = row.genie_down;
            tree->Fill();
        }

        return tree;
    }

    DatasetIO::Sample make_sample(DatasetIO::Sample::Variation variation,
                                  const std::string &nominal = "",
                                  const std::string &tag = "",
                                  const std::string &role = "")
    {
        DatasetIO::Sample sample;
        sample.origin = DatasetIO::Sample::Origin::kOverlay;
        sample.variation = variation;
        sample.beam = DatasetIO::Sample::Beam::kNuMI;
        sample.polarity = DatasetIO::Sample::Polarity::kFHC;
        sample.nominal = nominal;
        sample.tag = tag;
        sample.role = role;
        return sample;
    }

    void write_sample(EventListIO &eventlist,
                      const std::string &sample_key,
                      const DatasetIO::Sample &sample,
                      TTree *selected_tree)
    {
        TTree *subrun_tree = make_subrun_tree();
        eventlist.write_sample(sample_key, sample, selected_tree, subrun_tree, "SubRun");
        delete subrun_tree;
        delete selected_tree;
    }

    void write_base_metadata(EventListIO &eventlist)
    {
        EventListIO::Metadata metadata;
        metadata.dataset_path = "synthetic.dataset.root";
        metadata.dataset_context = "smoke";
        metadata.event_tree_name = "EventSelectionFilter";
        metadata.subrun_tree_name = "SubRun";
        metadata.selection_name = "raw";
        metadata.selection_expr = "sel != 0";
        eventlist.write_metadata(metadata);
    }

    std::vector<EventRow> rigorous_nominal_rows()
    {
        return {
            EventRow{0.5,
                     1.0,
                     1,
                     {2000, 500},
                     {3000, 1000},
                     {1500, 500},
                     {1000, 1000},
                     knob_weights(0, 1500),
                     knob_weights(0, 500)},
            EventRow{1.5,
                     1.0,
                     1,
                     {2000, 500},
                     {3000, 1000},
                     {1500, 500},
                     {1000, 1000},
                     knob_weights(0, 1500),
                     knob_weights(0, 500)},
            EventRow{2.5,
                     1.0,
                     1,
                     {1000, 1000},
                     {4000, 1000},
                     {1000, 1000},
                     {2000, 500},
                     knob_weights(1, 2000),
                     knob_weights(1, 1000)},
            EventRow{3.5,
                     1.0,
                     1,
                     {1000, 1000},
                     {4000, 1000},
                     {1000, 1000},
                     {2000, 500},
                     knob_weights(1, 2000),
                     knob_weights(1, 1000)},
        };
    }

    void write_rigorous_eventlist(const std::filesystem::path &path)
    {
        EventListIO eventlist(path.string(), EventListIO::Mode::kWrite);
        write_base_metadata(eventlist);

        write_sample(eventlist,
                     "beam",
                     make_sample(DatasetIO::Sample::Variation::kNominal),
                     make_selected_tree(rigorous_nominal_rows(),
                                        TreeOptions{true, false, true, true, true, true, true}));

        write_sample(eventlist,
                     "beam-cv-sce",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "cv",
                                 "sce"),
                     make_selected_tree({make_plain_row(0.5), make_plain_row(1.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "beam-sce",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "sce",
                                 "sce"),
                     make_selected_tree({make_plain_row(0.5), make_plain_row(0.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "beam-cv-wire",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "cv",
                                 "wire"),
                     make_selected_tree({make_plain_row(2.5), make_plain_row(3.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "beam-wire",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "wire",
                                 "wire"),
                     make_selected_tree({make_plain_row(2.5),
                                         make_plain_row(2.5),
                                         make_plain_row(2.5),
                                         make_plain_row(3.5)},
                                        TreeOptions{}));

        eventlist.flush();
    }

    void write_duplicate_label_eventlist(const std::filesystem::path &path)
    {
        EventListIO eventlist(path.string(), EventListIO::Mode::kWrite);
        write_base_metadata(eventlist);

        write_sample(eventlist,
                     "beam",
                     make_sample(DatasetIO::Sample::Variation::kNominal),
                     make_selected_tree({make_plain_row(0.5), make_plain_row(1.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "beam-a",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "same-label",
                                 "a"),
                     make_selected_tree({make_plain_row(0.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "beam-b",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "beam",
                                 "same-label",
                                 "b"),
                     make_selected_tree({make_plain_row(1.5)},
                                        TreeOptions{}));

        eventlist.flush();
    }

    void write_mismatched_nominal_eventlist(const std::filesystem::path &path)
    {
        EventListIO eventlist(path.string(), EventListIO::Mode::kWrite);
        write_base_metadata(eventlist);

        write_sample(eventlist,
                     "beam",
                     make_sample(DatasetIO::Sample::Variation::kNominal),
                     make_selected_tree({make_plain_row(0.5)},
                                        TreeOptions{}));

        write_sample(eventlist,
                     "other-detvar",
                     make_sample(DatasetIO::Sample::Variation::kDetector,
                                 "other",
                                 "sce",
                                 "sce"),
                     make_selected_tree({make_plain_row(0.5)},
                                        TreeOptions{}));

        eventlist.flush();
    }

    void test_compute_sample_math()
    {
        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 4.0;
        spec.selection_expr = "sel != 0";

        const std::vector<EventRow> rows = {
            EventRow{0.5,
                     2.0,
                     1,
                     {2000, 0},
                     {3000, 1000},
                     {1500, 500},
                     {1000, 2000},
                     knob_weights(0, 1500),
                     knob_weights(0, 500)},
            EventRow{1.5,
                     3.0,
                     0,
                     {500, 500},
                     {1000, 1000},
                     {1000, 1000},
                     {1000, 1000},
                     knob_weights(0, 1000),
                     knob_weights(0, 1000)},
            EventRow{2.5,
                     1.0,
                     1,
                     {500, 1000},
                     {4000, 1000},
                     {1000, 2000},
                     {2000, 1000},
                     knob_weights(1, 2000),
                     knob_weights(1, 1000)},
        };

        std::unique_ptr<TTree> tree(make_selected_tree(
            rows,
            TreeOptions{true, true, true, true, true, true, true}));

        syst::SystematicsOptions options;
        options.enable_genie = true;
        options.enable_flux = true;
        options.enable_reint = true;
        options.enable_genie_knobs = true;
        options.enable_eigenmode_compression = false;

        const syst::detail::ComputedSample out =
            syst::detail::compute_sample(tree.get(), spec, options);

        require_close_vector(out.nominal, {2.0, 1.0}, "compute nominal");
        require_close_vector(out.sumw2, {4.0, 1.0}, "compute sumw2");

        require(out.genie.has_value(), "GENIE family should be present");
        require(out.flux.has_value(), "flux family should be present");
        require(out.reint.has_value(), "reint family should be present");
        require(out.genie_knobs.has_value(), "GENIE knob pairs should be present");

        require(out.genie->branch_name == "weightsGenie",
                "GENIE family should bind weightsGenie");
        require(out.flux->branch_name == "weightsPPFX",
                "flux family should prefer weightsPPFX");
        require(out.reint->branch_name == "weightsReint",
                "reint family should bind weightsReint");

        require_close_vector(out.genie->histograms,
                             {4.0, 2.0, 0.5, 1.0},
                             "GENIE histograms");
        require_close_vector(out.flux->histograms,
                             {3.0, 1.0, 1.0, 2.0},
                             "flux histograms");
        require_close_vector(out.reint->histograms,
                             {2.0, 4.0, 2.0, 1.0},
                             "reint histograms");

        require(out.genie_knobs->source_labels.size() == kGenieKnobCount,
                "GENIE knob labels should follow the reviewed knob contract");
        require(out.genie_knobs->source_labels.front() == "AGKYpT1pi_UBGenie",
                "unexpected first GENIE knob label");
        require_close(out.genie_knobs->shift_vectors[0], 1.0, "GENIE knob source 0 bin 0");
        require_close(out.genie_knobs->shift_vectors[1], 0.0, "GENIE knob source 0 bin 1");
        require_close(out.genie_knobs->shift_vectors[2], 0.0, "GENIE knob source 1 bin 0");
        require_close(out.genie_knobs->shift_vectors[3], 0.5, "GENIE knob source 1 bin 1");

        std::unique_ptr<TTree> fallback_tree(make_selected_tree(
            rows,
            TreeOptions{true, true, false, true, false, false, false}));
        syst::SystematicsOptions flux_only;
        flux_only.enable_flux = true;

        const syst::detail::ComputedSample fallback =
            syst::detail::compute_sample(fallback_tree.get(), spec, flux_only);
        require(fallback.flux.has_value(), "weightsFlux fallback should be present");
        require(fallback.flux->branch_name == "weightsFlux",
                "weightsFlux should be used when PPFX is absent");
    }

    void test_detector_disable_gate()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = temp.path / "disabled-detector.eventlist.root";

        write_rigorous_eventlist(eventlist_path);

        syst::clear_cache();

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 4.0;

        syst::SystematicsOptions options;
        options.enable_memory_cache = false;
        options.enable_detector = false;
        options.detector_sample_keys = {"beam-sce", "beam-wire"};

        const syst::SystematicsResult result =
            syst::evaluate(eventlist, "beam", spec, options);

        require(result.detector.empty(),
                "detector lane should stay disabled when enable_detector is false");
    }

    void test_rebinned_persistent_cache_math()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = temp.path / "rigorous.eventlist.root";
        const std::filesystem::path dist_path = temp.path / "rigorous.dists.root";

        write_rigorous_eventlist(eventlist_path);

        syst::clear_cache();

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 2;
        spec.xmin = 0.0;
        spec.xmax = 4.0;

        syst::SystematicsOptions options;
        options.enable_memory_cache = false;
        options.persistent_cache = syst::CachePolicy::kComputeIfMissing;
        options.cache_nbins = 4;
        options.enable_detector = true;
        options.detector_sample_keys = {"beam-sce", "beam-wire"};
        options.enable_genie = true;
        options.enable_genie_knobs = true;
        options.enable_flux = true;
        options.enable_reint = true;
        options.build_full_covariance = true;
        options.retain_universe_histograms = true;
        options.enable_eigenmode_compression = false;

        syst::SystematicsResult first;
        {
            EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);
            DistributionIO distfile(dist_path.string(), DistributionIO::Mode::kUpdate);

            first = syst::evaluate(eventlist, distfile, "beam", spec, options);
            require(!first.loaded_from_persistent_cache,
                    "first persistent-cache evaluation should compute fresh content");

            require_close_vector(first.nominal, {2.0, 2.0}, "nominal");
            require_close_vector(first.detector.down, {2.0, 0.0}, "detector down");
            require_close_vector(first.detector.up, {2.0, 4.0}, "detector up");
            require_close_vector(first.genie_knobs.down, {1.0, 1.0}, "GENIE knob down");
            require_close_vector(first.genie_knobs.up, {3.0, 3.0}, "GENIE knob up");

            require(first.genie.has_value(), "GENIE result missing");
            require(first.flux.has_value(), "flux result missing");
            require(first.reint.has_value(), "reint result missing");

            require(first.genie->branch_name == "weightsGenie",
                    "GENIE branch name mismatch");
            require(first.flux->branch_name == "weightsPPFX",
                    "flux branch should prefer PPFX");
            require(first.reint->branch_name == "weightsReint",
                    "reint branch name mismatch");

            const double root_two_point_five = std::sqrt(2.5);
            require_close_vector(first.genie->sigma, {root_two_point_five, 0.0}, "GENIE sigma");
            require_close_vector(first.flux->sigma, {1.0, 0.0}, "flux sigma");
            require_close_vector(first.reint->sigma, {0.0, root_two_point_five}, "reint sigma");

            require_close_vector(first.genie->covariance, {2.5, 0.0, 0.0, 0.0}, "GENIE covariance");
            require_close_vector(first.flux->covariance, {1.0, 0.0, 0.0, 0.0}, "flux covariance");
            require_close_vector(first.reint->covariance, {0.0, 0.0, 0.0, 2.5}, "reint covariance");

            require(first.genie->universe_histograms.size() == 2,
                    "GENIE universe histograms should be retained");
            require_close_vector(first.genie->universe_histograms[0], {4.0, 2.0}, "GENIE universe 0");
            require_close_vector(first.genie->universe_histograms[1], {1.0, 2.0}, "GENIE universe 1");

            require(first.genie_knob_source_count == static_cast<int>(kGenieKnobCount),
                    "GENIE knob source count mismatch");
            require(first.genie_knob_source_labels.front() == "AGKYpT1pi_UBGenie",
                    "GENIE knob labels should round-trip");

            require_close_vector(first.total_up,
                                 {2.0 + std::sqrt(4.5), 2.0 + std::sqrt(7.5)},
                                 "total up");
            require_close_vector(first.total_down,
                                 {0.0, 0.0},
                                 "total down");

            const DistributionIO::Metadata metadata = distfile.metadata();
            require(metadata.eventlist_path == eventlist_path.string(),
                    "distribution metadata should track the eventlist path");

            const std::string key = syst::cache_key(spec, options);
            require(distfile.has("beam", key), "persistent cache entry should exist");

            const DistributionIO::Spectrum cached = distfile.read("beam", key);
            require(cached.spec.nbins == 4,
                    "persistent cache should use the fine-grained cache binning");
            require(cached.detector_source_count == 2,
                    "detector source count should persist");
            require_equal_vector(cached.detector_source_labels,
                                 {"sce", "wire"},
                                 "detector source labels");
            require(cached.detector_shift_vectors.size() == 8,
                    "detector shift vectors should persist");
            require(cached.detector_covariance.size() == 16,
                    "detector covariance should persist");
            require(cached.genie.n_variations == 2,
                    "GENIE universe count should persist");
            require(cached.genie.covariance.size() == 16,
                    "GENIE covariance should persist");
            require(cached.flux.branch_name == "weightsPPFX",
                    "cached flux family should record PPFX");
            require(cached.reint.n_variations == 2,
                    "reint universe count should persist");
            require(cached.genie_knob_source_count == static_cast<int>(kGenieKnobCount),
                    "GENIE knob count should persist");
            require(cached.genie_knob_covariance.size() == 16,
                    "GENIE knob covariance should persist");
        }

        options.persistent_cache = syst::CachePolicy::kLoadOnly;

        {
            EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);
            DistributionIO distfile(dist_path.string(), DistributionIO::Mode::kRead);

            const syst::SystematicsResult cached =
                syst::evaluate(eventlist, distfile, "beam", spec, options);

            require(cached.loaded_from_persistent_cache,
                    "load-only evaluation should come from the persistent cache");
            require_close_vector(cached.nominal, first.nominal, "cached nominal");
            require_close_vector(cached.detector.down, first.detector.down, "cached detector down");
            require_close_vector(cached.detector.up, first.detector.up, "cached detector up");
            require_close_vector(cached.genie->sigma, first.genie->sigma, "cached GENIE sigma");
            require_close_vector(cached.flux->sigma, first.flux->sigma, "cached flux sigma");
            require_close_vector(cached.reint->sigma, first.reint->sigma, "cached reint sigma");
            require_close_vector(cached.total_up, first.total_up, "cached total up");
            require_close_vector(cached.total_down, first.total_down, "cached total down");
        }
    }

    void test_missing_weight_branch_rejected()
    {
        std::unique_ptr<TTree> tree(make_selected_tree({make_plain_row(0.5)},
                                                       TreeOptions{false, false, false, false, false, false, false}));

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 1;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        require_throws(
            [&]()
            {
                (void)syst::detail::compute_sample(tree.get(), spec, syst::SystematicsOptions{});
            },
            "__w__",
            "missing central-weight branch");
    }

    void test_inconsistent_universe_count_rejected()
    {
        EventRow first = make_plain_row(0.5);
        first.genie = {1000, 1000};
        EventRow second = make_plain_row(0.5);
        second.genie = {1000, 1000, 1000};
        const std::vector<EventRow> rows = {first, second};
        std::unique_ptr<TTree> tree(make_selected_tree(
            rows,
            TreeOptions{true, false, true, false, false, false, false}));

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 1;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        syst::SystematicsOptions options;
        options.enable_genie = true;

        require_throws(
            [&]()
            {
                (void)syst::detail::compute_sample(tree.get(), spec, options);
            },
            "changed size across entries",
            "inconsistent universe count");
    }

    void test_knob_size_mismatch_rejected()
    {
        EventRow row;
        row.x = 0.5;
        row.weight = 1.0;
        row.genie_up.assign(kGenieKnobCount - 1, 1000);
        row.genie_down.assign(kGenieKnobCount - 1, 1000);

        std::unique_ptr<TTree> tree(make_selected_tree(
            {row},
            TreeOptions{true, false, false, false, false, false, true}));

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 1;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        syst::SystematicsOptions options;
        options.enable_genie_knobs = true;

        require_throws(
            [&]()
            {
                (void)syst::detail::compute_sample(tree.get(), spec, options);
            },
            "GENIE knob-pair payload size",
            "GENIE knob size mismatch");
    }

    void test_duplicate_detector_labels_rejected()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = temp.path / "duplicate-label.eventlist.root";

        write_duplicate_label_eventlist(eventlist_path);

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 1;
        spec.xmin = 0.0;
        spec.xmax = 2.0;

        syst::SystematicsOptions options;
        options.enable_memory_cache = false;
        options.enable_detector = true;
        options.detector_sample_keys = {"beam-a", "beam-b"};

        require_throws(
            [&]()
            {
                (void)syst::evaluate(eventlist, "beam", spec, options);
            },
            "duplicate detector source label",
            "duplicate detector labels");
    }

    void test_detector_nominal_mismatch_rejected()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path eventlist_path = temp.path / "nominal-mismatch.eventlist.root";

        write_mismatched_nominal_eventlist(eventlist_path);

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);

        syst::HistogramSpec spec;
        spec.branch_expr = "x";
        spec.nbins = 1;
        spec.xmin = 0.0;
        spec.xmax = 1.0;

        syst::SystematicsOptions options;
        options.enable_memory_cache = false;
        options.enable_detector = true;
        options.detector_sample_keys = {"other-detvar"};

        require_throws(
            [&]()
            {
                (void)syst::evaluate(eventlist, "beam", spec, options);
            },
            "does not match nominal",
            "detector nominal mismatch");
    }
}

int main()
{
    try
    {
        test_compute_sample_math();
        test_detector_disable_gate();
        test_rebinned_persistent_cache_math();
        test_missing_weight_branch_rejected();
        test_inconsistent_universe_count_rejected();
        test_knob_size_mismatch_rejected();
        test_duplicate_detector_labels_rejected();
        test_detector_nominal_mismatch_rejected();
        std::cout << "systematics_rigorous_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
