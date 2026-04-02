#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <sqlite3.h>

#include "DatasetIO.hh"
#include "DistributionIO.hh"
#include "EventListIO.hh"
#include "SampleIO.hh"
#include "ShardIO.hh"

#include "TDirectory.h"
#include "TFile.h"
#include "TNamed.h"
#include "TObject.h"
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

    struct SubrunRow
    {
        int run = 0;
        int subrun = 0;
        double pot = 0.0;
    };

    struct SelectedRow
    {
        int run = 0;
        int subrun = 0;
        double value = 0.0;
        int legacy_category = 0;
        bool legacy_signal = false;
    };

    struct RunDbRow
    {
        int run = 0;
        int subrun = 0;
        double tortgt = 0.0;
    };

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("io_rigorous_check: " + message);
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

    void require_pair_vector(const std::vector<std::pair<int, int>> &actual,
                             const std::vector<std::pair<int, int>> &expected,
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
            (std::filesystem::temp_directory_path() / "amarantin-io-rigorous.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *dir = mkdtemp(buffer.data());
        if (!dir)
            fail("failed to create temporary directory");

        TempDir out;
        out.path = dir;
        return out;
    }

    TDirectory *ensure_dir_path(TDirectory *base, const std::string &path)
    {
        if (!base)
            fail("ensure_dir_path: null base directory");

        TDirectory *current = base;
        std::size_t start = 0;
        while (start < path.size())
        {
            const std::size_t end = path.find('/', start);
            const std::string part =
                path.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!part.empty())
            {
                TDirectory *next = current->GetDirectory(part.c_str());
                if (!next)
                    next = current->mkdir(part.c_str());
                if (!next)
                    fail("failed to create directory component: " + part);
                current = next;
            }
            if (end == std::string::npos)
                break;
            start = end + 1;
        }

        return current;
    }

    std::pair<std::string, std::string> split_tree_path(const std::string &tree_path)
    {
        const std::string::size_type pos = tree_path.find_last_of('/');
        if (pos == std::string::npos)
            return {"", tree_path};
        if (pos + 1 >= tree_path.size())
            return {tree_path.substr(0, pos), ""};
        return {tree_path.substr(0, pos), tree_path.substr(pos + 1)};
    }

    void write_subrun_tree(TDirectory *base,
                           const std::string &tree_path,
                           const std::vector<SubrunRow> &rows)
    {
        const auto split = split_tree_path(tree_path);
        TDirectory *dir = ensure_dir_path(base, split.first);
        if (split.second.empty())
            fail("subrun tree name must not be empty");

        dir->cd();
        TTree tree(split.second.c_str(), "SubRun");
        int run = 0;
        int subRun = 0;
        double pot = 0.0;
        tree.Branch("run", &run);
        tree.Branch("subRun", &subRun);
        tree.Branch("pot", &pot);
        for (const auto &row : rows)
        {
            run = row.run;
            subRun = row.subrun;
            pot = row.pot;
            tree.Fill();
        }
        tree.Write(split.second.c_str(), TObject::kOverwrite);
    }

    std::filesystem::path write_input_root(const std::filesystem::path &path,
                                           const std::string &subrun_tree_path,
                                           const std::vector<SubrunRow> &rows)
    {
        TFile file(path.c_str(), "RECREATE");
        if (file.IsZombie())
            fail("failed to create ROOT input: " + path.string());
        write_subrun_tree(&file, subrun_tree_path, rows);
        file.Write();
        file.Close();
        return path;
    }

    std::filesystem::path write_list_file(const std::filesystem::path &path,
                                          const std::vector<std::filesystem::path> &files)
    {
        std::string contents;
        for (const auto &file : files)
            contents += file.string() + "\n";
        if (!std::filesystem::exists(path.parent_path()))
            std::filesystem::create_directories(path.parent_path());
        std::FILE *handle = std::fopen(path.c_str(), "w");
        if (!handle)
            fail("failed to create list file: " + path.string());
        if (!contents.empty())
            std::fwrite(contents.data(), 1, contents.size(), handle);
        std::fclose(handle);
        return path;
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

    std::filesystem::path write_run_db(const std::filesystem::path &path,
                                       const std::vector<RunDbRow> &rows)
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK || !db)
            fail("failed to create SQLite DB: " + path.string());

        sqlite_exec(db,
                    "CREATE TABLE runinfo(run INTEGER, subrun INTEGER, tortgt REAL);");

        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                               "INSERT INTO runinfo(run, subrun, tortgt) VALUES(?, ?, ?);",
                               -1,
                               &stmt,
                               nullptr) != SQLITE_OK || !stmt)
        {
            const std::string message = sqlite3_errmsg(db);
            sqlite3_close(db);
            fail("failed to prepare SQLite insert: " + message);
        }

        for (const auto &row : rows)
        {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sqlite3_bind_int(stmt, 1, row.run);
            sqlite3_bind_int(stmt, 2, row.subrun);
            sqlite3_bind_double(stmt, 3, row.tortgt);
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
        return path;
    }

    TTree *make_selected_tree(const std::vector<SelectedRow> &rows)
    {
        auto *tree = new TTree("selected", "selected");
        int run = 0;
        int subRun = 0;
        double value = 0.0;
        int legacy_category = 0;
        bool legacy_signal = false;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subRun);
        tree->Branch("value", &value);
        tree->Branch("__analysis_channel__", &legacy_category);
        tree->Branch("__is_signal__", &legacy_signal);
        for (const auto &row : rows)
        {
            run = row.run;
            subRun = row.subrun;
            value = row.value;
            legacy_category = row.legacy_category;
            legacy_signal = row.legacy_signal;
            tree->Fill();
        }
        return tree;
    }

    TTree *make_eventlist_subrun_tree(const std::vector<SubrunRow> &rows)
    {
        auto *tree = new TTree("SubRun", "SubRun");
        int run = 0;
        int subRun = 0;
        double pot = 0.0;
        tree->Branch("run", &run);
        tree->Branch("subRun", &subRun);
        tree->Branch("pot", &pot);
        for (const auto &row : rows)
        {
            run = row.run;
            subRun = row.subrun;
            pot = row.pot;
            tree->Fill();
        }
        return tree;
    }

    DatasetIO::Sample make_detector_sample(const DatasetIO::Sample &nominal,
                                           const std::string &tag,
                                           const std::string &role)
    {
        DatasetIO::Sample sample = nominal;
        sample.variation = DatasetIO::Sample::Variation::kDetector;
        sample.nominal = "beam";
        sample.tag = tag;
        sample.role = role;
        sample.provenance_list.clear();
        sample.root_files.clear();
        return sample;
    }

    DistributionIO::Spectrum make_valid_spectrum()
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = "beam";
        spectrum.spec.branch_expr = "value";
        spectrum.spec.selection_expr = "1";
        spectrum.spec.cache_key = "shape";
        spectrum.spec.nbins = 4;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 4.0;

        spectrum.nominal = {1.0, 2.0, 3.0, 4.0};
        spectrum.sumw2 = {1.0, 4.0, 9.0, 16.0};

        spectrum.detector_source_labels = {"sce", "wire"};
        spectrum.detector_sample_keys = {"beam-sce", "beam-wire"};
        spectrum.detector_shift_vectors = {1.0, 2.0, 3.0, 4.0,
                                           10.0, 20.0, 30.0, 40.0};
        spectrum.detector_source_count = 2;
        spectrum.detector_covariance = {1.0, 0.0, 0.0, 0.0,
                                        0.0, 4.0, 0.0, 0.0,
                                        0.0, 0.0, 9.0, 0.0,
                                        0.0, 0.0, 0.0, 16.0};
        spectrum.detector_down = {0.5, 1.0, 1.5, 2.0};
        spectrum.detector_up = {1.5, 2.0, 2.5, 3.0};
        spectrum.detector_templates = {1.0, 1.0, 1.0, 1.0,
                                       2.0, 2.0, 2.0, 2.0};
        spectrum.detector_template_count = 2;

        spectrum.genie_knob_source_labels = {"knob0"};
        spectrum.genie_knob_shift_vectors = {0.1, 0.2, 0.3, 0.4};
        spectrum.genie_knob_source_count = 1;
        spectrum.genie_knob_covariance = {0.01, 0.0, 0.0, 0.0,
                                          0.0, 0.04, 0.0, 0.0,
                                          0.0, 0.0, 0.09, 0.0,
                                          0.0, 0.0, 0.0, 0.16};

        spectrum.genie.branch_name = "weightsGenie";
        spectrum.genie.n_variations = 2;
        spectrum.genie.eigen_rank = 1;
        spectrum.genie.sigma = {0.5, 0.5, 0.5, 0.5};
        spectrum.genie.covariance = {1.0, 0.0, 0.0, 0.0,
                                     0.0, 4.0, 0.0, 0.0,
                                     0.0, 0.0, 9.0, 0.0,
                                     0.0, 0.0, 0.0, 16.0};
        spectrum.genie.eigenvalues = {5.0};
        spectrum.genie.eigenmodes = {1.0, 2.0, 3.0, 4.0};
        spectrum.genie.universe_histograms = {1.0, 10.0,
                                              2.0, 20.0,
                                              3.0, 30.0,
                                              4.0, 40.0};

        spectrum.flux.branch_name = "weightsFlux";
        spectrum.flux.n_variations = 1;
        spectrum.flux.sigma = {0.2, 0.2, 0.2, 0.2};
        spectrum.flux.covariance = {0.04, 0.0, 0.0, 0.0,
                                    0.0, 0.04, 0.0, 0.0,
                                    0.0, 0.0, 0.04, 0.0,
                                    0.0, 0.0, 0.0, 0.04};
        spectrum.flux.universe_histograms = {5.0, 6.0, 7.0, 8.0};

        spectrum.reint.branch_name = "weightsReint";
        spectrum.reint.n_variations = 1;
        spectrum.reint.sigma = {0.3, 0.3, 0.3, 0.3};
        spectrum.reint.covariance = {0.09, 0.0, 0.0, 0.0,
                                     0.0, 0.09, 0.0, 0.0,
                                     0.0, 0.0, 0.09, 0.0,
                                     0.0, 0.0, 0.0, 0.09};
        spectrum.reint.universe_histograms = {9.0, 10.0, 11.0, 12.0};

        spectrum.total_down = {0.8, 1.1, 1.6, 2.1};
        spectrum.total_up = {1.2, 1.9, 2.4, 2.9};
        return spectrum;
    }

    void test_shard_scan_tracks_files_and_exposure()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path first =
            write_input_root(temp.path / "first.root",
                             "SubRun",
                             {{1, 1, 1.0}, {1, 2, 2.0}});
        const std::filesystem::path second =
            write_input_root(temp.path / "second.root",
                             "SubRun",
                             {{1, 1, 0.5}});

        ShardIO shard;
        shard.scan({first.string(), second.string()});

        require_equal_vector(shard.files(),
                             {first.string(), second.string()},
                             "ShardIO::scan should retain input file paths");
        require_pair_vector(shard.subruns(),
                            {{1, 1}, {1, 2}},
                            "ShardIO subruns");
        require(shard.generated_exposures().size() == 2,
                "ShardIO should aggregate exposures by run/subrun");
        require_close(shard.generated_exposures()[0].generated_exposure,
                      1.5,
                      "ShardIO exposure (1,1)");
        require_close(shard.generated_exposures()[1].generated_exposure,
                      2.0,
                      "ShardIO exposure (1,2)");
        require_close(shard.subrun_pot_sum(), 3.5, "ShardIO pot sum");
        require(shard.entries() == 3, "ShardIO entry count");
    }

    void test_shard_scan_rejects_mixed_layouts()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path flat =
            write_input_root(temp.path / "flat.root",
                             "SubRun",
                             {{1, 1, 1.0}});
        const std::filesystem::path nested =
            write_input_root(temp.path / "nested.root",
                             "nuselection/SubRun",
                             {{1, 1, 1.0}});

        ShardIO shard;
        require_throws([&]() { shard.scan({flat.string(), nested.string()}); },
                       "mixed SubRun tree layouts",
                       "ShardIO should reject mixed tree layouts");
    }

    void test_sample_dataset_and_eventlist_roundtrip()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path first =
            write_input_root(temp.path / "first.root",
                             "SubRun",
                             {{1, 1, 1.0}, {1, 2, 2.0}});
        const std::filesystem::path second =
            write_input_root(temp.path / "second.root",
                             "SubRun",
                             {{1, 1, 0.5}});
        const std::filesystem::path list_path =
            write_list_file(temp.path / "beam.list", {first, second});
        const std::filesystem::path run_db_path =
            write_run_db(temp.path / "run.db",
                         {{1, 1, 1.5e-12}, {1, 2, 2.0e-12}});

        SampleIO sample;
        sample.build("beam",
                     {SampleIO::ShardInput{"part0", list_path.string()}},
                     "overlay",
                     "nominal",
                     "numi",
                     "fhc",
                     run_db_path.string());

        require(sample.built_, "SampleIO should mark the sample as built");
        require(sample.shards_.size() == 1, "SampleIO shard count");
        require_equal_vector(sample.shards_.front().files(),
                             {first.string(), second.string()},
                             "SampleIO shard input files");
        require_close(sample.subrun_pot_sum_, 3.5, "SampleIO subrun_pot_sum");
        require_close(sample.db_tortgt_pot_sum_, 3.5, "SampleIO db_tortgt_pot_sum");
        require_close(sample.normalisation_, 1.0, "SampleIO normalisation");
        require(sample.normalisation_mode_ == "run_subrun_pot",
                "SampleIO normalisation mode");
        require(sample.run_subrun_normalisations_.size() == 2,
                "SampleIO run/subrun normalisation rows");
        require_close(sample.run_subrun_normalisations_[0].normalisation,
                      1.0,
                      "SampleIO normalisation for first run/subrun");
        require_close(sample.run_subrun_normalisations_[1].normalisation,
                      1.0,
                      "SampleIO normalisation for second run/subrun");

        const std::filesystem::path sample_path = temp.path / "beam.sample.root";
        sample.write(sample_path.string());

        SampleIO sample_roundtrip;
        sample_roundtrip.read(sample_path.string());
        require(sample_roundtrip.sample_ == "beam", "SampleIO sample key roundtrip");
        require(sample_roundtrip.origin_ == SampleIO::Origin::kOverlay,
                "SampleIO origin roundtrip");
        require(sample_roundtrip.variation_ == SampleIO::Variation::kNominal,
                "SampleIO variation roundtrip");
        require(sample_roundtrip.beam_ == SampleIO::Beam::kNuMI,
                "SampleIO beam roundtrip");
        require(sample_roundtrip.polarity_ == SampleIO::Polarity::kFHC,
                "SampleIO polarity roundtrip");
        require(sample_roundtrip.shards_.size() == 1,
                "SampleIO shard roundtrip");
        require_equal_vector(sample_roundtrip.shards_.front().files(),
                             {first.string(), second.string()},
                             "SampleIO shard files roundtrip");

        const std::filesystem::path invalid_sample_path = temp.path / "invalid.sample.root";
        std::filesystem::copy_file(sample_path,
                                   invalid_sample_path,
                                   std::filesystem::copy_options::overwrite_existing);
        {
            TFile file(invalid_sample_path.c_str(), "UPDATE");
            if (file.IsZombie())
                fail("failed to open invalid sample file for mutation");
            TDirectory *sample_dir = file.GetDirectory("sample");
            if (!sample_dir)
                fail("invalid sample file is missing the sample directory");
            sample_dir->cd();
            TNamed("beam", "bnb").Write("beam", TObject::kOverwrite);
            file.Write();
            file.Close();
        }
        require_throws(
            [&]()
            {
                SampleIO invalid;
                invalid.read(invalid_sample_path.string());
            },
            "BNB samples must not set a polarity",
            "SampleIO::read should validate persisted metadata");

        const std::filesystem::path dataset_path = temp.path / "beam.dataset.root";
        {
            DatasetIO dataset(dataset_path.string(), "io-rigorous");
            dataset.add_sample("beam", sample.to_dataset_sample());
        }

        DatasetIO dataset(dataset_path.string());
        require(dataset.context() == "io-rigorous", "DatasetIO context roundtrip");
        require_equal_vector(dataset.sample_keys(),
                             {"beam"},
                             "DatasetIO sample keys");
        const DatasetIO::Sample dataset_sample = dataset.sample("beam");
        require(dataset_sample.origin == DatasetIO::Sample::Origin::kOverlay,
                "DatasetIO origin roundtrip");
        require(dataset_sample.variation == DatasetIO::Sample::Variation::kNominal,
                "DatasetIO variation roundtrip");
        require(dataset_sample.provenance_list.size() == 1,
                "DatasetIO provenance count");
        require_equal_vector(dataset_sample.root_files,
                             {first.string(), second.string()},
                             "DatasetIO root_files roundtrip");
        require_equal_vector(dataset_sample.provenance_list.front().input_files,
                             {first.string(), second.string()},
                             "DatasetIO provenance input_files roundtrip");
        require(dataset_sample.provenance_list.front().generated_exposures.size() == 2,
                "DatasetIO generated exposure count");
        require_close(dataset_sample.provenance_list.front().generated_exposures[0].generated_exposure,
                      1.5,
                      "DatasetIO generated exposure (1,1)");
        require_close(dataset_sample.provenance_list.front().generated_exposures[1].generated_exposure,
                      2.0,
                      "DatasetIO generated exposure (1,2)");

        const std::filesystem::path eventlist_path = temp.path / "beam.eventlist.root";
        {
            EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kWrite);
            EventListIO::Metadata metadata;
            metadata.dataset_path = dataset_path.string();
            metadata.dataset_context = "io-rigorous";
            metadata.event_tree_name = "events/selected";
            metadata.subrun_tree_name = "nuselection/SubRun";
            metadata.selection_name = "raw";
            metadata.selection_expr = "1";
            metadata.signal_definition = "signal";
            eventlist.write_metadata(metadata);

            TTree *selected_tree = make_selected_tree(
                {{1, 1, 0.5, 7, true}, {1, 2, 1.5, 8, false}});
            TTree *subrun_tree =
                make_eventlist_subrun_tree({{1, 1, 1.5}, {1, 2, 2.0}});
            eventlist.write_sample("beam",
                                   dataset_sample,
                                   selected_tree,
                                   subrun_tree,
                                   metadata.subrun_tree_name);
            delete selected_tree;
            delete subrun_tree;

            DatasetIO::Sample detector_sce = make_detector_sample(dataset_sample,
                                                                  "sce",
                                                                  "sce");
            selected_tree = make_selected_tree({{1, 1, 0.25, 3, false}});
            subrun_tree = make_eventlist_subrun_tree({{1, 1, 1.5}});
            eventlist.write_sample("beam-sce",
                                   detector_sce,
                                   selected_tree,
                                   subrun_tree,
                                   metadata.subrun_tree_name);
            delete selected_tree;
            delete subrun_tree;

            DatasetIO::Sample detector_wire = make_detector_sample(dataset_sample,
                                                                   "wire",
                                                                   "wire");
            selected_tree = make_selected_tree({{1, 2, 0.75, 4, true}});
            subrun_tree = make_eventlist_subrun_tree({{1, 2, 2.0}});
            eventlist.write_sample("beam-wire",
                                   detector_wire,
                                   selected_tree,
                                   subrun_tree,
                                   metadata.subrun_tree_name);
            delete selected_tree;
            delete subrun_tree;

            eventlist.flush();
        }

        EventListIO eventlist(eventlist_path.string(), EventListIO::Mode::kRead);
        const EventListIO::Metadata metadata = eventlist.metadata();
        require(metadata.dataset_path == dataset_path.string(),
                "EventListIO dataset path roundtrip");
        require(metadata.subrun_tree_name == "nuselection/SubRun",
                "EventListIO should preserve the explicit subrun tree path");
        require_equal_vector(eventlist.sample_keys(),
                             {"beam", "beam-sce", "beam-wire"},
                             "EventListIO sample keys");
        require_equal_vector(eventlist.detector_mates("beam"),
                             {"beam-sce", "beam-wire"},
                             "EventListIO detector mates");

        TTree *selected = eventlist.selected_tree("beam");
        require(selected != nullptr, "EventListIO selected tree");
        require(selected->GetEntries() == 2, "EventListIO selected entries");
        require(selected->GetAlias(EventListIO::event_category_branch_name()) != nullptr,
                "EventListIO should alias the legacy event-category branch");
        require(selected->GetAlias(EventListIO::passes_signal_definition_branch_name()) != nullptr,
                "EventListIO should alias the legacy signal-definition branch");

        TTree *subrun = eventlist.subrun_tree("beam");
        require(subrun != nullptr, "EventListIO subrun tree");
        require(subrun->GetEntries() == 2, "EventListIO subrun entries");
        require(std::string(subrun->GetName()) == "SubRun",
                "EventListIO should store subrun trees by leaf name");
    }

    void test_distribution_roundtrip_and_rebinning()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path dist_path = temp.path / "beam.dist.root";

        DistributionIO::Spectrum spectrum = make_valid_spectrum();
        {
            DistributionIO dist(dist_path.string(), DistributionIO::Mode::kUpdate);
            dist.write_metadata(DistributionIO::Metadata{"beam.eventlist.root", 2});
            dist.write("beam", "shape", spectrum);
            dist.flush();
        }

        DistributionIO dist(dist_path.string(), DistributionIO::Mode::kRead);
        require(dist.metadata().eventlist_path == "beam.eventlist.root",
                "DistributionIO metadata eventlist_path");
        require(dist.metadata().build_version == 2,
                "DistributionIO metadata build_version");
        require_equal_vector(dist.sample_keys(),
                             {"beam"},
                             "DistributionIO sample keys");
        require_equal_vector(dist.dist_keys("beam"),
                             {"shape"},
                             "DistributionIO dist keys");
        require(dist.has("beam", "shape"),
                "DistributionIO should report the stored cache entry");

        const DistributionIO::Spectrum roundtrip = dist.read("beam", "shape");
        require(roundtrip.spec.sample_key == "beam",
                "DistributionIO sample_key roundtrip");
        require(roundtrip.spec.cache_key == "shape",
                "DistributionIO cache_key roundtrip");
        require(roundtrip.detector_source_count == 2,
                "DistributionIO detector source count");
        require(roundtrip.genie_knob_source_count == 1,
                "DistributionIO GENIE knob source count");
        require(roundtrip.genie.n_variations == 2,
                "DistributionIO GENIE universe count");
        require_equal_vector(roundtrip.detector_source_labels,
                             {"sce", "wire"},
                             "DistributionIO detector labels");
        require_equal_vector(roundtrip.detector_sample_keys,
                             {"beam-sce", "beam-wire"},
                             "DistributionIO detector sample keys");
        require_close_vector(roundtrip.nominal,
                             {1.0, 2.0, 3.0, 4.0},
                             "DistributionIO nominal roundtrip");
        require_close_vector(roundtrip.genie.universe_histograms,
                             {1.0, 10.0, 2.0, 20.0, 3.0, 30.0, 4.0, 40.0},
                             "DistributionIO universe payload roundtrip");

        DistributionIO::HistogramSpec coarse;
        coarse.sample_key = "beam";
        coarse.cache_key = "coarse";
        coarse.branch_expr = "value";
        coarse.nbins = 2;
        coarse.xmin = 0.0;
        coarse.xmax = 4.0;

        require_close_vector(roundtrip.rebinned_values(roundtrip.nominal, coarse),
                             {3.0, 7.0},
                             "DistributionIO rebinned values");
        require_close_vector(roundtrip.rebinned_covariance(roundtrip.detector_covariance, coarse),
                             {5.0, 0.0, 0.0, 25.0},
                             "DistributionIO rebinned covariance");
        require_close_vector(
            roundtrip.rebinned_source_major_payload(roundtrip.detector_shift_vectors,
                                                    roundtrip.detector_source_count,
                                                    coarse),
            {3.0, 7.0, 30.0, 70.0},
            "DistributionIO rebinned source-major payload");
        require_close_vector(
            roundtrip.rebinned_bin_major_payload(roundtrip.genie.universe_histograms,
                                                 static_cast<int>(roundtrip.genie.n_variations),
                                                 coarse),
            {3.0, 30.0, 7.0, 70.0},
            "DistributionIO rebinned bin-major payload");
    }

    void test_distribution_rejects_bad_payloads()
    {
        const TempDir temp = make_temp_dir();
        const std::filesystem::path dist_path = temp.path / "invalid.dist.root";
        DistributionIO dist(dist_path.string(), DistributionIO::Mode::kUpdate);

        DistributionIO::Spectrum wrong_sample_key = make_valid_spectrum();
        wrong_sample_key.spec.sample_key = "other";
        require_throws(
            [&]() { dist.write("beam", "shape", wrong_sample_key); },
            "spectrum sample_key does not match write key",
            "DistributionIO should reject mismatched sample keys");

        DistributionIO::Spectrum truncated_detector = make_valid_spectrum();
        truncated_detector.detector_shift_vectors.pop_back();
        require_throws(
            [&]() { dist.write("beam", "shape", truncated_detector); },
            "detector_shift_vectors size does not match source-major payload shape",
            "DistributionIO should reject truncated detector payloads");

        DistributionIO::Spectrum truncated_universes = make_valid_spectrum();
        truncated_universes.genie.universe_histograms.pop_back();
        require_throws(
            [&]() { dist.write("beam", "shape", truncated_universes); },
            "genie universe histograms size does not match bin-major payload shape",
            "DistributionIO should reject truncated universe payloads");
    }
}

int main()
{
    try
    {
        test_shard_scan_tracks_files_and_exposure();
        test_shard_scan_rejects_mixed_layouts();
        test_sample_dataset_and_eventlist_roundtrip();
        test_distribution_roundtrip_and_rebinning();
        test_distribution_rejects_bad_payloads();
        std::cout << "io_rigorous_check=ok\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
