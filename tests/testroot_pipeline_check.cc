#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "Cuts.hh"
#include "DatasetIO.hh"
#include "DistributionIO.hh"
#include "EfficiencyPlot.hh"
#include "EventListIO.hh"
#include "EventListPlotting.hh"
#include "SampleIO.hh"
#include "Snapshot.hh"

#include "TCanvas.h"
#include "TFile.h"
#include "TMatrixT.h"
#include "TROOT.h"
#include "TTree.h"

namespace
{
    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("testroot_pipeline_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    bool contains(const std::vector<std::string> &values, const std::string &target)
    {
        return std::find(values.begin(), values.end(), target) != values.end();
    }

    template <class TObjectType>
    TObjectType *must_get(TFile &input, const std::string &name)
    {
        TObject *object = input.Get(name.c_str());
        if (!object)
            fail("missing object: " + name);
        auto *typed = dynamic_cast<TObjectType *>(object);
        if (!typed)
            fail("unexpected object type: " + name);
        return typed;
    }

    void require_branch(TTree *tree,
                        const char *branch_name,
                        const std::string &tree_label)
    {
        require(tree != nullptr, "missing tree for " + tree_label);
        require(tree->GetBranch(branch_name) != nullptr,
                tree_label + " is missing branch " + branch_name);
    }

    void require_any_branch(TTree *tree,
                            const std::vector<std::string> &branch_names,
                            const std::string &tree_label)
    {
        require(tree != nullptr, "missing tree for " + tree_label);
        for (const auto &branch_name : branch_names)
        {
            if (tree->GetBranch(branch_name.c_str()) != nullptr)
                return;
        }

        std::string joined;
        for (std::size_t i = 0; i < branch_names.size(); ++i)
        {
            if (i != 0)
                joined += ", ";
            joined += branch_names[i];
        }
        fail(tree_label + " is missing all of: " + joined);
    }

    int extract_int_field(const std::string &report,
                          const std::string &key)
    {
        const std::string needle = key + ": ";
        const std::size_t pos = report.find(needle);
        require(pos != std::string::npos, "fit report is missing field " + key);

        const std::size_t value_start = pos + needle.size();
        const std::size_t value_end = report.find('\n', value_start);
        return std::stoi(report.substr(value_start, value_end - value_start));
    }

    void require_selected_tree_core(TTree *selected,
                                    const std::string &tree_label)
    {
        require(selected != nullptr, "missing tree for " + tree_label);
        require(selected->GetEntries() > 0, tree_label + " should contain events");
        require_branch(selected, "run", tree_label);
        require_any_branch(selected, {"subRun", "sub"}, tree_label);
        require_branch(selected, "selection_pass", tree_label);
        require_branch(selected, "topological_score", tree_label);
        require_branch(selected, EventListIO::event_weight_normalisation_branch_name(),
                       tree_label);
        require_branch(selected, EventListIO::event_weight_central_value_branch_name(),
                       tree_label);
        require_branch(selected, EventListIO::event_weight_branch_name(),
                       tree_label);
        require_branch(selected, EventListIO::event_weight_squared_branch_name(),
                       tree_label);
    }

    void require_cut_branches(TTree *selected,
                              const std::string &tree_label)
    {
        require_branch(selected, cuts::trigger_branch(), tree_label);
        require_branch(selected, cuts::slice_branch(), tree_label);
        require_branch(selected, cuts::fiducial_branch(), tree_label);
        require_branch(selected, cuts::muon_branch(), tree_label);
    }

    void require_all_entries_true(TTree *tree,
                                  const char *branch_name,
                                  const std::string &tree_label)
    {
        require(tree != nullptr, "missing tree for " + tree_label);
        Bool_t value = kFALSE;
        tree->SetBranchAddress(branch_name, &value);
        const Long64_t entries = tree->GetEntries();
        for (Long64_t i = 0; i < entries; ++i)
        {
            tree->GetEntry(i);
            require(value != kFALSE,
                    tree_label + " should have " + branch_name + " set for every selected row");
        }
        tree->ResetBranchAddresses();
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 12)
        {
            fail("expected <sample.root> <dataset.root> <eventlist.root> "
                 "<preset-eventlist.root> <dist.root> <cov.root> <fit.txt> <plot.png> <snapshot.root> "
                 "<rowplot.png> <fixture.root>");
        }

        const std::string sample_path = argv[1] ? argv[1] : "";
        const std::string dataset_path = argv[2] ? argv[2] : "";
        const std::string eventlist_path = argv[3] ? argv[3] : "";
        const std::string preset_eventlist_path = argv[4] ? argv[4] : "";
        const std::string dist_path = argv[5] ? argv[5] : "";
        const std::string cov_path = argv[6] ? argv[6] : "";
        const std::string fit_path = argv[7] ? argv[7] : "";
        const std::string plot_path = argv[8] ? argv[8] : "";
        const std::string snapshot_path = argv[9] ? argv[9] : "";
        const std::string rowplot_path = argv[10] ? argv[10] : "";
        const std::string fixture_path = argv[11] ? argv[11] : "";

        SampleIO sample;
        sample.read(sample_path);
        require(sample.sample_ == "beam", "sample key should round-trip as beam");
        require(sample.origin_ == SampleIO::Origin::kExternal,
                "sample origin should be external");
        require(sample.variation_ == SampleIO::Variation::kNominal,
                "sample variation should be nominal");
        require(sample.beam_ == SampleIO::Beam::kNuMI, "sample beam should be numi");
        require(sample.polarity_ == SampleIO::Polarity::kFHC,
                "sample polarity should be fhc");
        require(sample.shards_.size() == 1, "expected one shard in sample");
        require(sample.shards_.front().files().size() == 1,
                "expected one input ROOT file in sample shard");
        require(sample.shards_.front().files().front() == fixture_path,
                "sample shard should point at fixture ROOT file");
        require(!sample.run_subrun_normalisations_.empty(),
                "sample should carry run/subrun normalisation rows");

        DatasetIO dataset(dataset_path);
        const std::vector<std::string> dataset_keys = dataset.sample_keys();
        require(dataset_keys.size() == 1 && dataset_keys.front() == "beam",
                "dataset should contain only the beam sample");
        const DatasetIO::Sample dataset_sample = dataset.sample("beam");
        require(dataset_sample.origin == DatasetIO::Sample::Origin::kExternal,
                "dataset sample origin should be external");
        require(dataset_sample.variation == DatasetIO::Sample::Variation::kNominal,
                "dataset sample variation should be nominal");
        require(dataset_sample.beam == DatasetIO::Sample::Beam::kNuMI,
                "dataset sample beam should be numi");
        require(dataset_sample.polarity == DatasetIO::Sample::Polarity::kFHC,
                "dataset sample polarity should be fhc");
        require(dataset_sample.provenance_list.size() == 1,
                "dataset sample should carry one provenance row");
        require(contains(dataset_sample.root_files, fixture_path),
                "dataset sample should reference the fixture ROOT file");
        require(!dataset_sample.run_subrun_normalisations.empty(),
                "dataset sample should carry run/subrun normalisation rows");

        EventListIO eventlist(eventlist_path, EventListIO::Mode::kRead);
        const EventListIO::Metadata metadata = eventlist.metadata();
        require(metadata.dataset_path == dataset_path,
                "event list metadata should record the source dataset path");
        require(metadata.event_tree_name == "nuselection/EventSelectionFilter",
                "event list should remember the explicit event tree path");
        require(metadata.subrun_tree_name == "nuselection/SubRun",
                "event list should remember the explicit subrun tree path");
        require(metadata.selection_name == "raw",
                "event list selection name should stay raw for explicit selections");
        require(metadata.selection_expr == "1",
                "event list metadata should record the smoke selection");

        const std::vector<std::string> eventlist_keys = eventlist.sample_keys();
        require(eventlist_keys.size() == 1 && eventlist_keys.front() == "beam",
                "event list should contain only the beam sample");
        const DatasetIO::Sample eventlist_sample = eventlist.sample("beam");
        require(eventlist_sample.origin == DatasetIO::Sample::Origin::kExternal,
                "event list sample origin should be external");

        TTree *selected = eventlist.selected_tree("beam");
        require_selected_tree_core(selected, "selected tree");
        require_cut_branches(selected, "selected tree");
        const Long64_t raw_selected_entries = selected->GetEntries();

        TTree *subrun = eventlist.subrun_tree("beam");
        require(subrun != nullptr, "missing subrun tree in event list");
        require(subrun->GetEntries() > 0, "subrun tree should contain entries");

        EventListIO preset_eventlist(preset_eventlist_path, EventListIO::Mode::kRead);
        const EventListIO::Metadata preset_metadata = preset_eventlist.metadata();
        require(preset_metadata.dataset_path == dataset_path,
                "preset event list metadata should record the source dataset path");
        require(preset_metadata.event_tree_name == "nuselection/EventSelectionFilter",
                "preset event list should remember the explicit event tree path");
        require(preset_metadata.subrun_tree_name == "nuselection/SubRun",
                "preset event list should remember the explicit subrun tree path");
        require(preset_metadata.selection_name == "muon",
                "preset event list should record the muon preset");
        require(preset_metadata.selection_expr.empty(),
                "preset event list should keep an empty explicit selection expression");
        TTree *preset_selected = preset_eventlist.selected_tree("beam");
        require_selected_tree_core(preset_selected, "preset selected tree");
        require_cut_branches(preset_selected, "preset selected tree");
        require(preset_selected->GetEntries() <= raw_selected_entries,
                "muon preset selection should not produce more rows than the raw selection");
        require_all_entries_true(preset_selected,
                                 cuts::muon_branch(),
                                 "preset selected tree");
        TTree *preset_subrun = preset_eventlist.subrun_tree("beam");
        require(preset_subrun != nullptr, "missing subrun tree in preset event list");
        require(preset_subrun->GetEntries() > 0,
                "preset subrun tree should contain entries");

        DistributionIO dist(dist_path, DistributionIO::Mode::kRead);
        const DistributionIO::Metadata dist_metadata = dist.metadata();
        require(dist_metadata.eventlist_path == eventlist_path,
                "distribution metadata should record the source eventlist path");
        const std::vector<std::string> dist_sample_keys = dist.sample_keys();
        require(dist_sample_keys.size() == 1 && dist_sample_keys.front() == "beam",
                "distribution file should contain only the beam sample");
        const std::vector<std::string> cache_keys = dist.dist_keys("beam");
        require(cache_keys.size() == 1,
                "distribution file should contain exactly one cache entry");
        const DistributionIO::Spectrum spectrum = dist.read("beam", cache_keys.front());
        require(spectrum.spec.sample_key == "beam",
                "cached spectrum should keep the beam sample key");
        require(spectrum.spec.branch_expr == "topological_score",
                "cached spectrum should use topological_score");
        require(spectrum.spec.selection_expr == "selection_pass != 0",
                "cached spectrum should preserve the requested selection");
        require(spectrum.spec.nbins == 10,
                "cached spectrum should use the requested binning");
        require(spectrum.nominal.size() == 10,
                "cached nominal payload should match the requested bin count");
        require(spectrum.sumw2.size() == 10,
                "cached sumw2 payload should match the requested bin count");
        require(spectrum.genie.branch_name == "weightsGenie",
                "GENIE family should persist on the test.root cache");
        require(spectrum.flux.branch_name == "weightsPPFX",
                "flux family should prefer weightsPPFX on the test.root cache");
        require(spectrum.reint.branch_name == "weightsReint",
                "reint family should persist on the test.root cache");
        require(spectrum.genie.sigma.size() == 10,
                "GENIE sigma payload should match the requested bin count");
        require(spectrum.flux.sigma.size() == 10,
                "flux sigma payload should match the requested bin count");
        require(spectrum.reint.sigma.size() == 10,
                "reint sigma payload should match the requested bin count");
        require(spectrum.genie_knob_source_count == 0,
                "empty GENIE knob payloads should be ignored for the test.root cache");
        require(spectrum.genie_knob_source_labels.empty(),
                "ignored GENIE knob payloads should not persist labels");
        require(spectrum.genie_knob_covariance.empty(),
                "ignored GENIE knob payloads should not persist covariance");
        require(spectrum.total_down.size() == 10 && spectrum.total_up.size() == 10,
                "total envelopes should match the requested bin count");

        double nominal_sum = 0.0;
        for (double value : spectrum.nominal)
            nominal_sum += value;
        require(nominal_sum > 0.0,
                "cached nominal histogram should contain events");

        {
            std::ifstream fit_input(fit_path);
            require(fit_input.good(), "missing fit report");
            const std::string fit_report((std::istreambuf_iterator<char>(fit_input)),
                                         std::istreambuf_iterator<char>());
            require(fit_report.find("channel_key: beam_fit") != std::string::npos,
                    "fit report should record beam_fit");
            require(fit_report.find("converged: true") != std::string::npos,
                    "fit report should report converged: true");
            require(fit_report.find("minimizer_status: 0") != std::string::npos,
                    "fit report should report minimizer_status: 0");
            require(fit_report.find("observed_source_keys: beam") != std::string::npos,
                    "fit report should record the beam observed source");
            require(extract_int_field(fit_report, "nuisance_count") > 0,
                    "fit report should include non-zero nuisance_count for the systematic cache");
        }

        {
            TFile cov_input(cov_path.c_str(), "READ");
            require(!cov_input.IsZombie(), "failed to open mk_cov output");
            auto *fractional = must_get<TMatrixT<float>>(cov_input, "frac_covariance");
            auto *absolute = must_get<TMatrixT<double>>(cov_input, "abs_covariance");
            must_get<TMatrixT<double>>(cov_input, "genie_covariance");
            must_get<TMatrixT<double>>(cov_input, "flux_covariance");
            must_get<TMatrixT<double>>(cov_input, "reint_covariance");
            require(cov_input.Get("genie_knobs_covariance") == nullptr,
                    "mk_cov should skip GENIE knob covariance when the payload is empty");
            require(fractional->GetNrows() == 10 && fractional->GetNcols() == 10,
                    "fractional covariance dimensions should match the cached bins");
            require(absolute->GetNrows() == 10 && absolute->GetNcols() == 10,
                    "absolute covariance dimensions should match the cached bins");
        }

        gROOT->SetBatch(kTRUE);

        snapshot::Spec snapshot_spec;
        snapshot_spec.columns = {"topological_score", "__w__"};
        snapshot_spec.selection = "selection_pass != 0";
        snapshot_spec.tree_name = "train";
        const unsigned long long snapshot_entries =
            snapshot::merged(eventlist, snapshot_path, snapshot_spec);
        require(snapshot_entries > 0, "snapshot::merged should write entries for the fixture");
        {
            TFile snapshot_input(snapshot_path.c_str(), "READ");
            require(!snapshot_input.IsZombie(), "failed to open snapshot output");
            TTree *snapshot_tree = must_get<TTree>(snapshot_input, "train");
            require(static_cast<unsigned long long>(snapshot_tree->GetEntries()) == snapshot_entries,
                    "snapshot entry count should match snapshot::merged");
            require_branch(snapshot_tree, "topological_score", "snapshot tree");
            require_branch(snapshot_tree, "__w__", "snapshot tree");
            require_branch(snapshot_tree, "sample_id", "snapshot tree");
        }

        {
            TCanvas *row_plot = plot_utils::draw_distribution(eventlist,
                                                              "topological_score",
                                                              50,
                                                              0.0,
                                                              1.0,
                                                              "c_testroot_topological_score",
                                                              "beam");
            require(row_plot != nullptr, "row-wise distribution plot should be created");
            row_plot->SaveAs(rowplot_path.c_str());
            delete row_plot;
            require(std::filesystem::exists(rowplot_path),
                    "row-wise distribution plot output file should exist");
        }

        plot_utils::EfficiencyPlot::Spec plot_spec;
        plot_spec.branch_expr = "topological_score";
        plot_spec.nbins = 10;
        plot_spec.xmin = 0.0;
        plot_spec.xmax = 1.0;
        plot_spec.hist_name = "fixture_efficiency";

        plot_utils::EfficiencyPlot::Config plot_cfg;
        plot_cfg.print_stats = false;
        plot_cfg.draw_stats_text = false;
        plot_utils::EfficiencyPlot plot(plot_spec, plot_cfg);
        plot.compute(eventlist, "1", "selection_pass != 0", "true", "beam");
        require(plot.ready(), "efficiency plot should be ready after compute()");
        require(plot.denom_entries() > 0,
                "efficiency plot denominator selection should match rows");
        plot.draw_and_save(plot_path, "");
        require(std::filesystem::exists(plot_path),
                "efficiency plot output file should exist");

        std::cout << "testroot_pipeline_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
