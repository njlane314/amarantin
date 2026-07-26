// pipeline_normalization_check.cc
//
// Full-pipeline golden-output test for the normalization chain.
//
// Creates a synthetic dataset with known per-(run,subrun) POT values and
// run-database target exposures, runs the full sample->dataset->eventlist
// pipeline, and verifies that every persisted __w_norm__ value equals
// target_exposure / generated_exposure for that run/subrun.  The sum of
// weights is also verified against the expected total.
//
// This test catches normalization bugs that per-module unit tests miss because
// it exercises the complete shard->SampleIO->DatasetIO->EventListIO path.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "DatasetIO.hh"
#include "Cuts.hh"
#include "EventListBuild.hh"
#include "EventListIO.hh"
#include "SampleIO.hh"

#include "TFile.h"
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

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("pipeline_normalization_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    void require_close(double actual,
                       double expected,
                       const std::string &label,
                       double tolerance = 1e-9)
    {
        if (std::fabs(actual - expected) > tolerance)
        {
            fail(label + ": expected " + std::to_string(expected) +
                 ", got " + std::to_string(actual));
        }
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() /
             "amarantin-pipeline-norm.XXXXXX")
                .string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");
        TempDir out;
        out.path = dir;
        return out;
    }

    // Write a synthetic ROOT file with:
    //   EventSelectionFilter  tree: run/I, subRun/I, selected/I, plus cut inputs
    //   SubRun                tree: run/I, subRun/I, pot/D
    //
    // Designed to produce exactly these selected events (selected != 0):
    //   event 0: run=1, subrun=0  -> w_norm = target(1,0)/generated(1,0)
    //                                passes trigger/slice/fiducial/muon
    //   event 1: run=1, subrun=1  -> w_norm = target(1,1)/generated(1,1)
    //                                fails fiducial, so muon must fail too
    //   event 2: run=1, subrun=0  -> w_norm = target(1,0)/generated(1,0)
    //                                fails slice, so fiducial and muon must fail too
    //
    // event 3 (run=1, subrun=1, selected=0) is filtered out by the selection.
    //
    // SubRun POT (generated exposure):
    //   (run=1, subrun=0): pot = 2e12
    //   (run=1, subrun=1): pot = 5e12
    //
    // Run database tortgt (in units of 1e12 POT, scaled internally by SampleIO):
    //   (run=1, subrun=0): tortgt = 4.0  -> target = 4e12
    //   (run=1, subrun=1): tortgt = 15.0 -> target = 15e12
    //
    // Expected w_norm per event: [2.0, 3.0, 2.0]
    // Expected sum of w_norm:     7.0
    void write_input_root(const std::string &path)
    {
        TFile file(path.c_str(), "RECREATE");
        if (file.IsZombie())
            fail("failed to create input ROOT file: " + path);

        // EventSelectionFilter tree
        {
            file.cd();
            TTree evt("EventSelectionFilter", "");
            Int_t run = 0;
            Int_t subRun = 0;
            Int_t selected = 0;
            Int_t software_trigger = 0;
            Int_t num_slices = 0;
            Float_t topological_score = 0.0f;
            Int_t in_reco_fiducial = 0;
            Int_t sel_muon = 0;
            evt.Branch("run", &run, "run/I");
            evt.Branch("subRun", &subRun, "subRun/I");
            evt.Branch("selected", &selected, "selected/I");
            evt.Branch("software_trigger", &software_trigger, "software_trigger/I");
            evt.Branch("num_slices", &num_slices, "num_slices/I");
            evt.Branch("topological_score", &topological_score, "topological_score/F");
            evt.Branch("in_reco_fiducial", &in_reco_fiducial, "in_reco_fiducial/I");
            evt.Branch("sel_muon", &sel_muon, "sel_muon/I");

            struct Row
            {
                int run;
                int subrun;
                int sel;
                int trigger;
                int slices;
                float topo;
                int fiducial;
                int muon;
            };
            const Row rows[] = {
                {1, 0, 1, 1, 1, 0.20f, 1, 1},  // event 0: passes all stages
                {1, 1, 1, 1, 1, 0.20f, 0, 1},  // event 1: fiducial false, muon input true
                {1, 0, 1, 1, 0, 0.20f, 1, 1},  // event 2: slice false, muon input true
                {1, 1, 0, 1, 1, 0.20f, 1, 1},  // event 3: filtered (selected==0)
            };
            for (const auto &r : rows)
            {
                run = r.run;
                subRun = r.subrun;
                selected = r.sel;
                software_trigger = r.trigger;
                num_slices = r.slices;
                topological_score = r.topo;
                in_reco_fiducial = r.fiducial;
                sel_muon = r.muon;
                evt.Fill();
            }
            evt.Write("EventSelectionFilter", TObject::kOverwrite);
        }

        // SubRun tree
        {
            file.cd();
            TTree sr("SubRun", "");
            Int_t run = 0;
            Int_t subRun = 0;
            Double_t pot = 0.0;
            sr.Branch("run", &run, "run/I");
            sr.Branch("subRun", &subRun, "subRun/I");
            sr.Branch("pot", &pot, "pot/D");

            struct SubRow { int run; int subrun; double pot; };
            const SubRow rows[] = {
                {1, 0, 2.0e12},
                {1, 1, 5.0e12},
            };
            for (const auto &r : rows)
            {
                run = r.run; subRun = r.subrun; pot = r.pot;
                sr.Fill();
            }
            sr.Write("SubRun", TObject::kOverwrite);
        }

        file.Write();
        file.Close();
    }

    void write_list_file(const std::string &path, const std::string &root_file)
    {
        std::ofstream out(path);
        if (!out)
            fail("failed to create list file: " + path);
        out << root_file << "\n";
    }

    void sqlite_exec(sqlite3 *db, const std::string &sql)
    {
        char *error = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK)
        {
            const std::string message = error ? error : "sqlite3_exec failed";
            sqlite3_free(error);
            fail("SQLite exec failed: " + message);
        }
    }

    void write_run_db(const std::string &path)
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK || !db)
            fail("failed to create SQLite DB: " + path);

        sqlite_exec(db,
                    "CREATE TABLE runinfo(run INTEGER, subrun INTEGER, tortgt REAL);");

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                               "INSERT INTO runinfo(run, subrun, tortgt) VALUES(?, ?, ?);",
                               -1, &stmt, nullptr) != SQLITE_OK || !stmt)
        {
            const std::string message = sqlite3_errmsg(db);
            sqlite3_close(db);
            fail("failed to prepare SQLite insert: " + message);
        }

        // tortgt is in units of 1e12 POT (SampleIO multiplies by 1e12 internally)
        struct DbRow { int run; int subrun; double tortgt; };
        const DbRow rows[] = {
            {1, 0, 4.0},   // target = 4e12 POT
            {1, 1, 15.0},  // target = 15e12 POT
        };
        for (const auto &r : rows)
        {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sqlite3_bind_int(stmt, 1, r.run);
            sqlite3_bind_int(stmt, 2, r.subrun);
            sqlite3_bind_double(stmt, 3, r.tortgt);
            if (sqlite3_step(stmt) != SQLITE_DONE)
            {
                const std::string message = sqlite3_errmsg(db);
                sqlite3_finalize(stmt);
                sqlite3_close(db);
                fail("failed to insert SQLite row: " + message);
            }
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }

    void run_normalization_check()
    {
        const TempDir tmp = make_temp_dir();
        const std::string root_path  = (tmp.path / "input.root").string();
        const std::string list_path  = (tmp.path / "input.list").string();
        const std::string db_path    = (tmp.path / "run.db").string();
        const std::string sample_path  = (tmp.path / "sample.root").string();
        const std::string dataset_path = (tmp.path / "dataset.root").string();
        const std::string evlist_path  = (tmp.path / "eventlist.root").string();

        write_input_root(root_path);
        write_list_file(list_path, root_path);
        write_run_db(db_path);

        // --- mk_sample equivalent ---
        SampleIO sample;
        sample.build(
            "beam",
            {{/*shard=*/"", /*sample_list_path=*/list_path}},
            "external",
            "nominal",
            "numi",
            "fhc",
            db_path);
        sample.write(sample_path);

        require(!sample.run_subrun_normalisations_.empty(),
                "SampleIO should have populated run/subrun normalisations");

        // Verify the SampleIO-level normalisation entries match expectations.
        // normalisation = target_exposure / generated_exposure
        //   (run=1,subrun=0): 4e12 / 2e12 = 2.0
        //   (run=1,subrun=1): 15e12 / 5e12 = 3.0
        for (const auto &entry : sample.run_subrun_normalisations_)
        {
            if (entry.run == 1 && entry.subrun == 0)
                require_close(entry.normalisation, 2.0, "SampleIO normalisation(1,0)");
            else if (entry.run == 1 && entry.subrun == 1)
                require_close(entry.normalisation, 3.0, "SampleIO normalisation(1,1)");
            else
                fail("unexpected run/subrun in SampleIO normalisations");
        }

        // --- mk_dataset equivalent ---
        {
            DatasetIO ds(dataset_path, "pipeline_normalization_check");
            ds.add_sample("beam", sample.to_dataset_sample());
        }

        // --- mk_eventlist equivalent ---
        {
            DatasetIO dataset(dataset_path);
            EventListIO eventlist(evlist_path, EventListIO::Mode::kWrite);

            ana::BuildConfig config;
            config.event_tree_name = "EventSelectionFilter";
            config.subrun_tree_name = "SubRun";
            config.selection_expr = "selected != 0";
            config.selection_name = "raw";

            ana::build_event_list(dataset, eventlist, config);
        }

        // --- Verify EventListIO weights ---
        EventListIO eventlist(evlist_path, EventListIO::Mode::kRead);

        const std::vector<std::string> keys = eventlist.sample_keys();
        require(keys.size() == 1 && keys.front() == "beam",
                "eventlist should contain exactly the beam sample");

        TTree *selected = eventlist.selected_tree("beam");
        require(selected != nullptr, "selected tree should exist");

        const Long64_t n_entries = selected->GetEntries();
        require(n_entries == 3,
                "expected exactly 3 selected events (event with selected==0 should be filtered)");

        Double_t w_norm = 0.0;
        Double_t w = 0.0;
        Int_t run = 0;
        Int_t subRun = 0;
        Bool_t pass_trigger = kFALSE;
        Bool_t pass_slice = kFALSE;
        Bool_t pass_fiducial = kFALSE;
        Bool_t pass_muon = kFALSE;
        selected->SetBranchAddress(
            EventListIO::event_weight_normalisation_branch_name(), &w_norm);
        selected->SetBranchAddress(
            EventListIO::event_weight_branch_name(), &w);
        selected->SetBranchAddress("run", &run);
        selected->SetBranchAddress("subRun", &subRun);
        selected->SetBranchAddress(cuts::trigger_branch(), &pass_trigger);
        selected->SetBranchAddress(cuts::slice_branch(), &pass_slice);
        selected->SetBranchAddress(cuts::fiducial_branch(), &pass_fiducial);
        selected->SetBranchAddress(cuts::muon_branch(), &pass_muon);

        // Expected weights per event in insertion order:
        //   event 0: (run=1,subrun=0) -> w_norm=2.0
        //   event 1: (run=1,subrun=1) -> w_norm=3.0
        //   event 2: (run=1,subrun=0) -> w_norm=2.0
        const double kExpected[] = {2.0, 3.0, 2.0};
        double sum_w_norm = 0.0;

        for (Long64_t i = 0; i < n_entries; ++i)
        {
            selected->GetEntry(i);

            require_close(w_norm, kExpected[i],
                          "__w_norm__ for event " + std::to_string(i));

            // For external-origin samples __w_cv__ == 1.0 so __w__ == __w_norm__
            require_close(w, kExpected[i],
                          "__w__ for event " + std::to_string(i));

            if (i == 0)
            {
                require(pass_trigger != kFALSE, "event 0 should pass trigger");
                require(pass_slice != kFALSE, "event 0 should pass slice");
                require(pass_fiducial != kFALSE, "event 0 should pass fiducial");
                require(pass_muon != kFALSE, "event 0 should pass muon");
            }
            else if (i == 1)
            {
                require(pass_trigger != kFALSE, "event 1 should pass trigger");
                require(pass_slice != kFALSE, "event 1 should pass slice");
                require(pass_fiducial == kFALSE, "event 1 should fail fiducial");
                require(pass_muon == kFALSE,
                        "event 1 should fail muon when fiducial is false even if sel_muon is true");
            }
            else if (i == 2)
            {
                require(pass_trigger != kFALSE, "event 2 should pass trigger");
                require(pass_slice == kFALSE, "event 2 should fail slice");
                require(pass_fiducial == kFALSE, "event 2 should fail fiducial after slice fails");
                require(pass_muon == kFALSE,
                        "event 2 should fail muon when earlier cumulative cuts fail");
            }

            sum_w_norm += w_norm;
        }

        // sum(__w_norm__) = 2.0 + 3.0 + 2.0 = 7.0
        require_close(sum_w_norm, 7.0, "sum of __w_norm__ over all selected events");

        selected->ResetBranchAddresses();
    }

    void write_missing_cut_inputs_fixture(const std::string &path)
    {
        TFile file(path.c_str(), "RECREATE");
        if (file.IsZombie())
            fail("failed to create input ROOT file: " + path);

        {
            file.cd();
            TTree event_tree("EventSelectionFilter", "");
            Int_t run = 0;
            Int_t subRun = 0;
            Int_t selected = 0;
            event_tree.Branch("run", &run, "run/I");
            event_tree.Branch("subRun", &subRun, "subRun/I");
            event_tree.Branch("selected", &selected, "selected/I");

            run = 1;
            subRun = 0;
            selected = 1;
            event_tree.Fill();
            event_tree.Write("EventSelectionFilter", TObject::kOverwrite);
        }

        {
            file.cd();
            TTree subrun_tree("SubRun", "");
            Int_t run = 1;
            Int_t subRun = 0;
            Double_t pot = 2.0e12;
            subrun_tree.Branch("run", &run, "run/I");
            subrun_tree.Branch("subRun", &subRun, "subRun/I");
            subrun_tree.Branch("pot", &pot, "pot/D");
            subrun_tree.Fill();
            subrun_tree.Write("SubRun", TObject::kOverwrite);
        }

        file.Write();
        file.Close();
    }

    void run_missing_cut_inputs_rejection_check()
    {
        const TempDir tmp = make_temp_dir();
        const std::string root_path = (tmp.path / "missing-cut-inputs.root").string();
        const std::string list_path = (tmp.path / "missing-cut-inputs.list").string();
        const std::string db_path = (tmp.path / "run.db").string();
        const std::string sample_path = (tmp.path / "sample.root").string();
        const std::string dataset_path = (tmp.path / "dataset.root").string();
        const std::string evlist_path = (tmp.path / "eventlist.root").string();

        write_missing_cut_inputs_fixture(root_path);
        write_list_file(list_path, root_path);
        write_run_db(db_path);

        SampleIO sample;
        sample.build(
            "beam",
            {{/*shard=*/"", /*sample_list_path=*/list_path}},
            "external",
            "nominal",
            "numi",
            "fhc",
            db_path);
        sample.write(sample_path);

        {
            DatasetIO ds(dataset_path, "pipeline_missing_cut_inputs_check");
            ds.add_sample("beam", sample.to_dataset_sample());
        }

        DatasetIO dataset(dataset_path);
        EventListIO eventlist(evlist_path, EventListIO::Mode::kWrite);

        ana::BuildConfig config;
        config.event_tree_name = "EventSelectionFilter";
        config.subrun_tree_name = "SubRun";
        config.selection_expr = "selected != 0";
        config.selection_name = "raw";

        bool saw_expected_failure = false;
        try
        {
            ana::build_event_list(dataset, eventlist, config);
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            require(message.find("trigger preset") != std::string::npos,
                    "missing cut inputs should mention the trigger preset");
            saw_expected_failure = true;
        }

        require(saw_expected_failure,
                "build_event_list should reject event trees missing helper-cut inputs");
    }

    struct McFixtureSchema
    {
        bool include_sigma0_flag = true;
        bool include_subrun_pot = true;
        bool count_strange_as_float = false;
        bool is_nu_mu_cc_as_int = false;
    };

    void write_mc_schema_fixture(const std::string &path,
                                 int subrun,
                                 const McFixtureSchema &schema)
    {
        TFile file(path.c_str(), "RECREATE");
        if (file.IsZombie())
            fail("failed to create MC schema fixture: " + path);

        {
            file.cd();
            TTree event_tree("EventSelectionFilter", "");
            Int_t run = 1;
            Int_t subRun = subrun;
            Int_t selected = 1;
            Int_t software_trigger = 1;
            Int_t num_slices = 1;
            Float_t topological_score = 0.2f;
            Int_t in_reco_fiducial = 1;
            Int_t sel_muon = 1;
            Int_t count_strange = 0;
            Float_t count_strange_float = 0.0f;
            Int_t int_ccnc = 0;
            Bool_t is_nu_mu_cc = kTRUE;
            Int_t is_nu_mu_cc_int = 1;
            Bool_t nu_vtx_in_fv = kTRUE;
            Bool_t truth_has_strange_fs = kFALSE;
            Bool_t truth_has_fs_lambda0 = kFALSE;
            Bool_t truth_has_fs_sigma0 = kFALSE;
            Bool_t truth_has_g4_lambda0 = kFALSE;
            Bool_t truth_has_g4_lambda0_from_sigma0 = kFALSE;
            std::vector<float> truth_fs_lambda0_p;
            std::vector<float> truth_fs_sigma0_p;

            event_tree.Branch("run", &run, "run/I");
            event_tree.Branch("subRun", &subRun, "subRun/I");
            event_tree.Branch("selected", &selected, "selected/I");
            event_tree.Branch("software_trigger", &software_trigger, "software_trigger/I");
            event_tree.Branch("num_slices", &num_slices, "num_slices/I");
            event_tree.Branch("topological_score", &topological_score, "topological_score/F");
            event_tree.Branch("in_reco_fiducial", &in_reco_fiducial, "in_reco_fiducial/I");
            event_tree.Branch("sel_muon", &sel_muon, "sel_muon/I");
            if (schema.count_strange_as_float)
            {
                event_tree.Branch("count_strange", &count_strange_float, "count_strange/F");
            }
            else
            {
                event_tree.Branch("count_strange", &count_strange, "count_strange/I");
            }
            event_tree.Branch("int_ccnc", &int_ccnc, "int_ccnc/I");
            if (schema.is_nu_mu_cc_as_int)
            {
                event_tree.Branch("is_nu_mu_cc", &is_nu_mu_cc_int, "is_nu_mu_cc/I");
            }
            else
            {
                event_tree.Branch("is_nu_mu_cc", &is_nu_mu_cc, "is_nu_mu_cc/O");
            }
            event_tree.Branch("nu_vtx_in_fv", &nu_vtx_in_fv, "nu_vtx_in_fv/O");
            event_tree.Branch("truth_has_strange_fs", &truth_has_strange_fs,
                              "truth_has_strange_fs/O");
            event_tree.Branch("truth_has_fs_lambda0", &truth_has_fs_lambda0,
                              "truth_has_fs_lambda0/O");
            if (schema.include_sigma0_flag)
            {
                event_tree.Branch("truth_has_fs_sigma0", &truth_has_fs_sigma0,
                                  "truth_has_fs_sigma0/O");
            }
            event_tree.Branch("truth_has_g4_lambda0", &truth_has_g4_lambda0,
                              "truth_has_g4_lambda0/O");
            event_tree.Branch("truth_has_g4_lambda0_from_sigma0",
                              &truth_has_g4_lambda0_from_sigma0,
                              "truth_has_g4_lambda0_from_sigma0/O");
            event_tree.Branch("truth_fs_lambda0_p", &truth_fs_lambda0_p);
            event_tree.Branch("truth_fs_sigma0_p", &truth_fs_sigma0_p);
            event_tree.Fill();
            event_tree.Write("EventSelectionFilter", TObject::kOverwrite);
        }

        {
            file.cd();
            TTree subrun_tree("SubRun", "");
            Int_t run = 1;
            Int_t subRun = subrun;
            Double_t pot = 1.0e12;
            subrun_tree.Branch("run", &run, "run/I");
            subrun_tree.Branch("subRun", &subRun, "subRun/I");
            if (schema.include_subrun_pot)
                subrun_tree.Branch("pot", &pot, "pot/D");
            subrun_tree.Fill();
            subrun_tree.Write("SubRun", TObject::kOverwrite);
        }

        file.Write();
        file.Close();
    }

    void require_event_list_input_rejection(
        const McFixtureSchema &first_schema,
        const McFixtureSchema &second_schema,
        const std::string &expected_tree,
        const std::string &expected_branch,
        const std::string &expected_difference,
        bool expect_schema_mismatch)
    {
        const TempDir tmp = make_temp_dir();
        const std::string first_shard_path = (tmp.path / "first.root").string();
        const std::string second_shard_path = (tmp.path / "second.root").string();
        const std::string dataset_path = (tmp.path / "dataset.root").string();
        const std::string eventlist_path = (tmp.path / "eventlist.root").string();

        write_mc_schema_fixture(first_shard_path, 0, first_schema);
        write_mc_schema_fixture(second_shard_path, 1, second_schema);

        DatasetIO::Sample sample;
        sample.sample = "overlay";
        sample.origin = DatasetIO::Sample::Origin::kOverlay;
        sample.variation = DatasetIO::Sample::Variation::kNominal;
        sample.beam = DatasetIO::Sample::Beam::kNuMI;
        sample.polarity = DatasetIO::Sample::Polarity::kFHC;
        sample.normalisation_mode = "run_subrun_pot";
        sample.root_files = {first_shard_path, second_shard_path};
        sample.run_subrun_normalisations = {
            {1, 0, 1.0e12, 1.0e12, 1.0},
            {1, 1, 1.0e12, 1.0e12, 1.0},
        };

        {
            DatasetIO dataset(dataset_path, "pipeline_schema_rejection_check");
            dataset.add_sample("overlay", sample);
            dataset.close();
        }

        DatasetIO dataset(dataset_path);
        EventListIO eventlist(eventlist_path, EventListIO::Mode::kWrite);
        ana::BuildConfig config;
        config.event_tree_name = "EventSelectionFilter";
        config.subrun_tree_name = "SubRun";
        config.selection_expr = "selected != 0";
        config.selection_name = "raw";

        bool rejected_input = false;
        try
        {
            ana::build_event_list(dataset, eventlist, config);
        }
        catch (const std::exception &error)
        {
            const std::string message = error.what();
            require(message.find("sample overlay") != std::string::npos,
                    "input rejection should identify the logical sample");
            require(message.find(expected_tree) != std::string::npos,
                    "input rejection should identify the affected tree");
            require(message.find(expected_branch) != std::string::npos,
                    "input rejection should identify the affected branch");
            require(message.find(expected_difference) != std::string::npos,
                    "input rejection should identify the failure mode");
            if (expect_schema_mismatch)
            {
                require(message.find("schema") != std::string::npos,
                        "cross-shard rejection should identify a schema mismatch");
                require(message.find(second_shard_path) != std::string::npos,
                        "cross-shard rejection should identify the mismatched file");
            }
            rejected_input = true;
        }

        require(rejected_input,
                "build_event_list should reject incompatible event-list input trees");
    }

    void run_cross_shard_schema_rejection_checks()
    {
        McFixtureSchema missing_event_branch;
        missing_event_branch.include_sigma0_flag = false;
        require_event_list_input_rejection(
            McFixtureSchema{},
            missing_event_branch,
            "EventSelectionFilter",
            "truth_has_fs_sigma0",
            "missing branch",
            true);

        McFixtureSchema missing_subrun_branch;
        missing_subrun_branch.include_subrun_pot = false;
        require_event_list_input_rejection(
            McFixtureSchema{},
            missing_subrun_branch,
            "SubRun",
            "pot",
            "missing branch",
            true);

        McFixtureSchema changed_event_branch_type;
        changed_event_branch_type.count_strange_as_float = true;
        require_event_list_input_rejection(
            McFixtureSchema{},
            changed_event_branch_type,
            "EventSelectionFilter",
            "count_strange",
            "different persisted type",
            true);

        McFixtureSchema incompatible_binding;
        incompatible_binding.is_nu_mu_cc_as_int = true;
        require_event_list_input_rejection(
            incompatible_binding,
            incompatible_binding,
            "EventSelectionFilter",
            "is_nu_mu_cc",
            "incompatible persisted type",
            false);
    }
}

int main()
{
    try
    {
        run_normalization_check();
        run_missing_cut_inputs_rejection_check();
        run_cross_shard_schema_rejection_checks();
        std::cout << "pipeline_normalization_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
