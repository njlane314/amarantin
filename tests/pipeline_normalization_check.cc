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
    //   EventSelectionFilter  tree: run/I, subRun/I, selected/I
    //   SubRun                tree: run/I, subRun/I, pot/D
    //
    // Designed to produce exactly these selected events (selected != 0):
    //   event 0: run=1, subrun=0  -> w_norm = target(1,0)/generated(1,0)
    //   event 1: run=1, subrun=1  -> w_norm = target(1,1)/generated(1,1)
    //   event 2: run=1, subrun=0  -> w_norm = target(1,0)/generated(1,0)
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
            evt.Branch("run", &run, "run/I");
            evt.Branch("subRun", &subRun, "subRun/I");
            evt.Branch("selected", &selected, "selected/I");

            struct Row { int run; int subrun; int sel; };
            const Row rows[] = {
                {1, 0, 1},  // event 0: passes
                {1, 1, 1},  // event 1: passes
                {1, 0, 1},  // event 2: passes
                {1, 1, 0},  // event 3: filtered (selected==0)
            };
            for (const auto &r : rows)
            {
                run = r.run; subRun = r.subrun; selected = r.sel;
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
        selected->SetBranchAddress(
            EventListIO::event_weight_normalisation_branch_name(), &w_norm);
        selected->SetBranchAddress(
            EventListIO::event_weight_branch_name(), &w);
        selected->SetBranchAddress("run", &run);
        selected->SetBranchAddress("subRun", &subRun);

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

            sum_w_norm += w_norm;
        }

        // sum(__w_norm__) = 2.0 + 3.0 + 2.0 = 7.0
        require_close(sum_w_norm, 7.0, "sum of __w_norm__ over all selected events");

        selected->ResetBranchAddresses();
    }
}

int main()
{
    try
    {
        run_normalization_check();
        std::cout << "pipeline_normalization_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
