#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "DistributionIO.hh"

#include "CollieChannel.hh"
#include "CollieDistribution.hh"
#include "CollieMasspoint.hh"

#include <TFile.h>
#include <TH1D.h>
#include <TNamed.h>

namespace
{
    struct TempDir
    {
        std::filesystem::path path;

        ~TempDir()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    struct LoadedChannel
    {
        TFile file;
        CollieChannel *channel = nullptr;
        CollieMasspoint *point = nullptr;

        LoadedChannel(const std::filesystem::path &path, const std::string &name)
            : file(path.string().c_str(), "READ")
        {
            if (file.IsZombie())
                throw std::runtime_error("failed to open " + path.string());
            channel = CollieChannel::loadChannel(name.c_str(), &file);
            if (!channel)
                throw std::runtime_error("failed to load channel " + name);
            point = channel->getMasspoint(0);
            if (!point)
                throw std::runtime_error("failed to load model point for " + name);
        }
    };

    [[noreturn]] void fail(const std::string &message)
    {
        throw std::runtime_error("collie_export_rigorous_check: " + message);
    }

    void require(bool condition, const std::string &message)
    {
        if (!condition)
            fail(message);
    }

    bool close_enough(double first, double second)
    {
        return std::fabs(first - second) <=
               1e-9 * std::max({1.0, std::fabs(first), std::fabs(second)});
    }

    TempDir make_temp_dir()
    {
        const std::string templ =
            (std::filesystem::temp_directory_path() / "amarantin-collie.XXXXXX").string();
        std::vector<char> buffer(templ.begin(), templ.end());
        buffer.push_back('\0');
        char *directory = mkdtemp(buffer.data());
        if (!directory)
            fail("failed to create temporary directory");
        return TempDir{directory};
    }

    std::string shell_quote(const std::string &value)
    {
        std::string quoted = "'";
        for (char character : value)
            quoted += character == '\'' ? "'\"'\"'" : std::string(1, character);
        return quoted + "'";
    }

    std::string read_text(const std::filesystem::path &path)
    {
        std::ifstream input(path);
        if (!input)
            fail("failed to read " + path.string());
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }

    DistributionIO::Spectrum make_spectrum(const std::string &sample,
                                           const std::string &selection,
                                           std::vector<double> nominal,
                                           std::vector<double> sumw2 = {})
    {
        DistributionIO::Spectrum spectrum;
        spectrum.spec.sample_key = sample;
        spectrum.spec.cache_key = "fit";
        spectrum.spec.branch_expr = "fit_variable";
        spectrum.spec.selection_expr = selection;
        spectrum.spec.nbins = 2;
        spectrum.spec.xmin = 0.0;
        spectrum.spec.xmax = 2.0;
        spectrum.nominal = std::move(nominal);
        spectrum.sumw2 = std::move(sumw2);
        return spectrum;
    }

    void add_genie_universes(DistributionIO::Spectrum &spectrum,
                             const std::vector<double> &first,
                             const std::vector<double> &second,
                             bool retain_universes)
    {
        DistributionIO::UniverseFamily &family = spectrum.genie;
        family.branch_name = "weightsGenie";
        family.n_variations = 2;
        const double first_bin_delta = first[0] - spectrum.nominal[0];
        const double second_bin_delta = first[1] - spectrum.nominal[1];
        family.sigma = {std::fabs(first_bin_delta), std::fabs(second_bin_delta)};
        family.covariance = {
            first_bin_delta * first_bin_delta,
            first_bin_delta * second_bin_delta,
            first_bin_delta * second_bin_delta,
            second_bin_delta * second_bin_delta};
        if (retain_universes)
        {
            family.universe_histograms = {
                first[0], second[0], first[1], second[1]};
        }
    }

    void write_distribution(const std::filesystem::path &path,
                            bool complete_universes,
                            bool unlabeled_detector_covariance = false)
    {
        DistributionIO output(path.string(), DistributionIO::Mode::kWrite);
        output.write_metadata({"synthetic.eventlist.root", "event-list-uuid", 1, 4});

        output.write("data_sr", "fit",
                     make_spectrum("data_sr", "region == 1", {41.0, 58.0}));
        DistributionIO::Spectrum signal_sr =
            make_spectrum("signal_sr", "region == 1", {10.0, 20.0}, {1.0, 4.0});
        signal_sr.detector_source_count = 1;
        signal_sr.detector_source_labels = {"space_charge"};
        signal_sr.detector_sample_keys = {"signal_sr_sce"};
        signal_sr.detector_shift_vectors = {1.0, -1.0};
        add_genie_universes(signal_sr, {11.0, 18.0}, {9.0, 22.0}, true);
        output.write("signal_sr", "fit", signal_sr);
        DistributionIO::Spectrum background_sr =
            make_spectrum("background_sr", "region == 1", {30.0, 40.0}, {1.0, 1.0});
        if (unlabeled_detector_covariance)
            background_sr.detector_covariance = {1.0, 0.0, 0.0, 1.0};
        output.write("background_sr", "fit", background_sr);

        output.write("data_cr", "fit",
                     make_spectrum("data_cr", "region == 0", {25.0, 33.0}));
        DistributionIO::Spectrum signal_cr =
            make_spectrum("signal_cr", "region == 0", {5.0, 8.0}, {0.25, 1.0});
        add_genie_universes(signal_cr, {5.5, 9.0}, {4.5, 7.0}, complete_universes);
        output.write("signal_cr", "fit", signal_cr);
        DistributionIO::Spectrum background_cr =
            make_spectrum("background_cr", "region == 0", {20.0, 24.0}, {1.0, 1.0});
        background_cr.genie_knob_source_count = 1;
        background_cr.genie_knob_source_labels = {"axial_mass"};
        background_cr.genie_knob_shift_vectors = {0.5, -0.5};
        output.write("background_cr", "fit", background_cr);
        output.flush();
    }

    void write_manifest(const std::filesystem::path &path)
    {
        std::ofstream output(path);
        require(static_cast<bool>(output), "failed to create manifest");
        output <<
            "process run1_fhc_sr observed data data_sr fit\n"
            "process run1_fhc_sr measurement_signal signal signal_sr fit\n"
            "process run1_fhc_sr other_strange_background background background_sr fit\n"
            "process run1_fhc_cr observed data data_cr fit\n"
            "process run1_fhc_cr measurement_signal signal signal_cr fit\n"
            "process run1_fhc_cr other_strange_background background background_cr fit\n"
            "rate run1_fhc_sr measurement_signal flux_norm 0.05 0.08 lognormal\n"
            "rate run1_fhc_cr measurement_signal flux_norm 0.05 0.08 lognormal\n";
    }

    int run_export(const std::string &binary,
                   const std::filesystem::path &manifest,
                   const std::filesystem::path &distribution,
                   const std::filesystem::path &output,
                   const std::filesystem::path &log)
    {
        const std::string command = shell_quote(binary) + " --manifest " +
                                    shell_quote(manifest.string()) + " " +
                                    shell_quote(distribution.string()) + " " +
                                    shell_quote(output.string()) + " >" +
                                    shell_quote(log.string()) + " 2>&1";
        const int status = std::system(command.c_str());
        if (status == -1)
            fail("failed to launch mk_collie");
        return status;
    }

    const TH1D *positive_effect(const CollieDistribution *distribution,
                                const std::string &name)
    {
        const int index = distribution ? distribution->getSystIndex(name) : -1;
        const TH1D *effect = index < 0 ? nullptr : distribution->getPositiveSyst(index);
        if (!effect)
            fail("missing nuisance " + name);
        return effect;
    }

    std::vector<double> mode_delta(const CollieDistribution *distribution,
                                   const std::vector<double> &nominal)
    {
        std::vector<std::string> modes;
        for (int index = 0; index < distribution->getNsystematics(); ++index)
        {
            const std::string name = distribution->getSystName(index);
            if (name.rfind("genie__weightsGenie__mode_", 0) == 0)
                modes.push_back(name);
        }
        require(modes.size() == 1, "rank-one family should persist as one joint mode");
        const TH1D *effect = positive_effect(distribution, modes.front());
        return {effect->GetBinContent(1) * nominal[0],
                effect->GetBinContent(2) * nominal[1]};
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2 || !argv[1] || argv[1][0] == '\0')
            fail("expected <mk_collie binary>");

        const TempDir temp = make_temp_dir();
        const std::filesystem::path manifest = temp.path / "model.manifest";
        const std::filesystem::path distribution = temp.path / "model.dists.root";
        const std::filesystem::path output = temp.path / "collie-model";
        const std::filesystem::path log = temp.path / "mk_collie.log";
        write_manifest(manifest);
        write_distribution(distribution, true);
        const int export_status = run_export(argv[1], manifest, distribution, output, log);
        require(export_status == 0, "valid export failed: " + read_text(log));

        require(std::filesystem::is_regular_file(output / "run1_fhc_sr.root"),
                "signal-region native file is missing");
        require(std::filesystem::is_regular_file(output / "run1_fhc_cr.root"),
                "control-region native file is missing");
        require(std::filesystem::is_regular_file(output / "inputs.list"),
                "inputs.list is missing");
        require(!std::filesystem::exists(output / "fv_run1_fhc_sr.root"),
                "inspection files should not be published");

        LoadedChannel signal_region(output / "run1_fhc_sr.root", "run1_fhc_sr");
        LoadedChannel control_region(output / "run1_fhc_cr.root", "run1_fhc_cr");
        require(signal_region.channel->getNSignals() == 1 &&
                    std::string(signal_region.channel->getSignalName()) == "measurement_signal",
                "signal identity changed");
        require(signal_region.channel->getNBackgrounds() == 1 &&
                    std::string(signal_region.channel->getBackgroundName()) ==
                        "other_strange_background",
                "background identity changed");

        const CollieDistribution *signal_sr = signal_region.point->getSignalDist();
        const CollieDistribution *signal_cr = control_region.point->getSignalDist();
        require(close_enough(signal_sr->getBinStatErr(0, -1), 0.0) &&
                    close_enough(signal_cr->getBinStatErr(0, -1), 0.0),
                "native histogram errors would duplicate explicit MC-stat nuisances");
        require(signal_sr->hasSystematic("flux_norm") &&
                    signal_cr->hasSystematic("flux_norm"),
                "shared rate nuisance is missing");
        require(const_cast<CollieDistribution *>(signal_sr)->getLogNormalFlag("flux_norm"),
                "rate prior should remain lognormal");
        require(close_enough(positive_effect(signal_sr, "flux_norm")->GetBinContent(1),
                             std::log1p(0.08)),
                "lognormal up fraction was not converted to Collie parameter space");
        const int flux_index = signal_sr->getSystIndex("flux_norm");
        require(close_enough(signal_sr->getNegativeSyst(flux_index)->GetBinContent(1),
                             -std::log1p(-0.05)),
                "lognormal down fraction was not converted to Collie parameter space");
        std::vector<std::string> nuisance_names;
        for (int index = 0; index < signal_sr->getNsystematics(); ++index)
            nuisance_names.push_back(signal_sr->getSystName(index));
        CollieDistribution *varied_signal = const_cast<CollieDistribution *>(signal_sr);
        varied_signal->linearize(nuisance_names);
        std::vector<double> nuisance_values(nuisance_names.size(), 0.0);
        nuisance_values[static_cast<std::size_t>(flux_index)] = 1.0;
        require(close_enough(varied_signal->getEfficiencyVaried(
                                 0, -1, nuisance_values.data()),
                             10.0 * 1.08),
                "lognormal +1 sigma yield changed");
        nuisance_values[static_cast<std::size_t>(flux_index)] = -1.0;
        require(close_enough(varied_signal->getEfficiencyVaried(
                                 0, -1, nuisance_values.data()),
                             10.0 * 0.95),
                "lognormal -1 sigma yield changed");
        require(signal_sr->hasSystematic("detector__space_charge"),
                "detector source nuisance is missing");
        require(signal_sr->hasSystematic(
                    "mcstat__run1_fhc_sr__measurement_signal__bin_0"),
                "local MC-stat nuisance is missing");
        require(!signal_cr->hasSystematic(
                    "mcstat__run1_fhc_sr__measurement_signal__bin_0"),
                "MC-stat nuisances must not correlate across channels");
        require(control_region.point->getBkgdDist()->hasSystematic(
                    "genie_knob__axial_mass"),
                "GENIE knob source nuisance is missing");

        const std::vector<double> sr_delta = mode_delta(signal_sr, {10.0, 20.0});
        const std::vector<double> cr_delta = mode_delta(signal_cr, {5.0, 8.0});
        const std::vector<double> expected = {1.0, -2.0, 0.5, 1.0};
        const std::vector<double> loaded = {sr_delta[0], sr_delta[1],
                                            cr_delta[0], cr_delta[1]};
        for (std::size_t row = 0; row < expected.size(); ++row)
        {
            for (std::size_t column = 0; column < expected.size(); ++column)
            {
                require(close_enough(loaded[row] * loaded[column],
                                     expected[row] * expected[column]),
                        "joint multisim covariance changed in native output");
            }
        }

        TNamed *provenance = nullptr;
        signal_region.file.GetObject("amarantin_export", provenance);
        require(provenance && std::string(provenance->GetTitle()).find(
                                  "distribution_uuid=") != std::string::npos,
                "native provenance is missing");
        require(read_text(output / "model.manifest").find("# source ") == 0,
                "resolved manifest is missing its source identity");
        const std::filesystem::path replayed = temp.path / "replayed-model";
        const std::filesystem::path replayed_log = temp.path / "replayed.log";
        const int replay_status = run_export(argv[1],
                                             output / "model.manifest",
                                             distribution,
                                             replayed,
                                             replayed_log);
        require(replay_status == 0,
                "resolved manifest is not reusable: " + read_text(replayed_log));

        const std::filesystem::path incomplete = temp.path / "incomplete.dists.root";
        const std::filesystem::path rejected = temp.path / "rejected-model";
        const std::filesystem::path rejected_log = temp.path / "rejected.log";
        write_distribution(incomplete, false);
        require(run_export(argv[1], manifest, incomplete, rejected, rejected_log) != 0,
                "cross-process export should reject missing retained universes");
        require(read_text(rejected_log).find("mk_dist --retain-universes") != std::string::npos,
                "missing-universe rejection should explain how to rebuild the cache");
        require(!std::filesystem::exists(rejected),
                "failed export should not publish a partial directory");

        const std::filesystem::path unlabeled = temp.path / "unlabeled.dists.root";
        const std::filesystem::path unlabeled_output = temp.path / "unlabeled-model";
        const std::filesystem::path unlabeled_log = temp.path / "unlabeled.log";
        write_distribution(unlabeled, true, true);
        {
            DistributionIO persisted(unlabeled.string(), DistributionIO::Mode::kRead);
            require(persisted.read("background_sr", "fit").detector_covariance ==
                        std::vector<double>({1.0, 0.0, 0.0, 1.0}),
                    "covariance-only fixture did not survive a persistence round trip");
        }
        require(run_export(argv[1], manifest, unlabeled, unlabeled_output, unlabeled_log) != 0,
                "covariance-only detector export should fail");
        require(read_text(unlabeled_log).find(
                    "covariance has no labeled source shifts") != std::string::npos,
                "unlabeled covariance rejection should explain the missing source model");
        require(!std::filesystem::exists(unlabeled_output),
                "unlabeled covariance should not publish a partial directory");
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
