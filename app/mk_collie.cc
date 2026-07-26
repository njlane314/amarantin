#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "CliPaths.hh"
#include "DistributionIO.hh"

#include "CollieChannel.hh"
#include "CollieDistribution.hh"
#include "CollieIOFile.hh"
#include "CollieMasspoint.hh"

#include <Eigen/Eigenvalues>

#include <TFile.h>
#include <TH1.h>
#include <TH1D.h>
#include <TNamed.h>
#include <TObject.h>
#include <TROOT.h>

namespace
{
    constexpr int kModelPoint = 0;
    constexpr std::size_t kMaxChannelNameLength = 64;
    constexpr std::size_t kMaxProcessNameLength = 32;
    constexpr std::size_t kMaxNuisanceNameLength = 140;
    constexpr double kCollieMinimumEffect = 1e-5;

    struct HelpRequested final {};

    enum class ProcessRole
    {
        kData,
        kSignal,
        kBackground
    };

    enum class Prior
    {
        kGaussian,
        kLogNormal
    };

    enum class FamilyKind
    {
        kGenie,
        kFlux,
        kReint
    };

    struct CliOptions
    {
        std::string manifest_path;
        std::string distribution_path;
        std::string output_directory;
    };

    struct ProcessSpec
    {
        std::string channel_name;
        std::string process_name;
        ProcessRole role = ProcessRole::kBackground;
        std::string sample_key;
        std::string cache_key;
        int line_number = 0;
    };

    struct RateSpec
    {
        std::string channel_name;
        std::string process_name;
        std::string nuisance_name;
        double down_fraction = 0.0;
        double up_fraction = 0.0;
        Prior prior = Prior::kGaussian;
        int line_number = 0;
    };

    struct Manifest
    {
        std::vector<ProcessSpec> processes;
        std::vector<RateSpec> rates;
    };

    struct SystematicEffect
    {
        std::string name;
        std::vector<double> positive_fraction;
        std::vector<double> negative_fraction;
        Prior prior = Prior::kGaussian;
    };

    struct LoadedProcess
    {
        ProcessSpec spec;
        DistributionIO::Spectrum spectrum;
        std::vector<SystematicEffect> effects;
    };

    struct ChannelModel
    {
        std::string name;
        DistributionIO::HistogramSpec histogram;
        bool have_histogram = false;
        std::size_t data_process = std::numeric_limits<std::size_t>::max();
        std::vector<std::size_t> signal_processes;
        std::vector<std::size_t> background_processes;
    };

    struct ExportModel
    {
        Manifest manifest;
        DistributionIO::Metadata metadata;
        std::string distribution_path;
        std::string distribution_uuid;
        int distribution_revision = 0;
        std::vector<LoadedProcess> processes;
        std::map<std::string, ChannelModel> channels;
        std::map<std::string, Prior> nuisance_priors;
        std::map<std::string, std::string> nuisance_origins;
    };

    void print_usage(std::ostream &stream)
    {
        stream << "usage: mk_collie --manifest <model.manifest> "
                  "<input.dists.root> <output-directory>\n\n"
                  "manifest rows:\n"
                  "  process <channel> <process> <data|signal|background> "
                  "<sample-key> <cache-key>\n"
                  "  rate <channel> <process> <nuisance> <down> <up> "
                  "<gaussian|lognormal>\n";
    }

    [[noreturn]] void invalid_arguments(const std::string &message)
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_collie: " + message);
    }

    bool looks_like_option_token(const char *value)
    {
        return !value || value[0] == '\0' || value[0] == '-';
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;
        int index = 1;
        for (; index < argc; ++index)
        {
            const std::string argument = argv[index] ? argv[index] : "";
            if (argument == "-h" || argument == "--help")
            {
                print_usage(std::cout);
                throw HelpRequested{};
            }
            if (argument == "--manifest")
            {
                if (++index >= argc || looks_like_option_token(argv[index]))
                    invalid_arguments("--manifest requires a path");
                options.manifest_path = argv[index];
                continue;
            }
            if (argument.rfind("--", 0) == 0)
                throw std::runtime_error("mk_collie: unknown option: " + argument);
            break;
        }

        if (options.manifest_path.empty())
            invalid_arguments("--manifest is required");
        if (argc - index != 2)
            invalid_arguments("expected an input distribution and output directory");

        options.distribution_path = argv[index] ? argv[index] : "";
        options.output_directory = argv[index + 1] ? argv[index + 1] : "";
        if (options.distribution_path.empty() || options.output_directory.empty())
            invalid_arguments("input and output paths must not be empty");
        return options;
    }

    std::string trim_copy(const std::string &value)
    {
        const std::string::size_type first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        const std::string::size_type last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string strip_comment(const std::string &line)
    {
        const std::string::size_type comment = line.find('#');
        return comment == std::string::npos ? line : line.substr(0, comment);
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
        std::istringstream input(line);
        std::vector<std::string> fields;
        std::string field;
        while (input >> field)
            fields.push_back(field);
        return fields;
    }

    bool is_safe_name(const std::string &name)
    {
        if (name.empty())
            return false;
        return std::all_of(name.begin(), name.end(), [](unsigned char character)
        {
            return std::isalnum(character) || character == '_' ||
                   character == '-' || character == '.';
        });
    }

    void require_safe_name(const std::string &name,
                           const std::string &label,
                           int line_number)
    {
        if (!is_safe_name(name))
        {
            throw std::runtime_error(
                "mk_collie: " + label + " must use only letters, digits, '.', '-', or '_' "
                "at manifest line " + std::to_string(line_number));
        }
    }

    std::string safe_component(const std::string &value)
    {
        std::string result;
        result.reserve(value.size());
        for (unsigned char character : value)
            result.push_back(std::isalnum(character) ? static_cast<char>(character) : '_');
        return result;
    }

    std::string lowercase_copy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        return value;
    }

    ProcessRole parse_role(const std::string &value, int line_number)
    {
        if (value == "data")
            return ProcessRole::kData;
        if (value == "signal")
            return ProcessRole::kSignal;
        if (value == "background")
            return ProcessRole::kBackground;
        throw std::runtime_error(
            "mk_collie: process role must be data, signal, or background at manifest line " +
            std::to_string(line_number));
    }

    Prior parse_prior(const std::string &value, int line_number)
    {
        if (value == "gaussian")
            return Prior::kGaussian;
        if (value == "lognormal")
            return Prior::kLogNormal;
        throw std::runtime_error(
            "mk_collie: rate prior must be gaussian or lognormal at manifest line " +
            std::to_string(line_number));
    }

    double parse_fraction(const std::string &value,
                          const std::string &label,
                          int line_number)
    {
        try
        {
            std::size_t consumed = 0;
            const double parsed = std::stod(value, &consumed);
            if (consumed != value.size() || !std::isfinite(parsed) || parsed < 0.0)
                throw std::invalid_argument("invalid fraction");
            return parsed;
        }
        catch (...)
        {
            throw std::runtime_error(
                "mk_collie: invalid " + label + " at manifest line " +
                std::to_string(line_number) + ": " + value);
        }
    }

    Manifest read_manifest(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_collie: failed to open manifest: " + path);

        Manifest manifest;
        std::set<std::pair<std::string, std::string>> process_keys;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::vector<std::string> fields =
                split_fields(trim_copy(strip_comment(line)));
            if (fields.empty())
                continue;

            if (fields.front() == "process")
            {
                if (fields.size() != 6)
                {
                    throw std::runtime_error(
                        "mk_collie: process rows require 6 fields at manifest line " +
                        std::to_string(line_number));
                }

                ProcessSpec process;
                process.channel_name = fields[1];
                process.process_name = fields[2];
                process.role = parse_role(fields[3], line_number);
                process.sample_key = fields[4];
                process.cache_key = fields[5];
                process.line_number = line_number;
                require_safe_name(process.channel_name, "channel name", line_number);
                require_safe_name(process.process_name, "process name", line_number);
                require_safe_name(process.sample_key, "sample key", line_number);
                require_safe_name(process.cache_key, "cache key", line_number);
                if (process.channel_name.size() < 3)
                {
                    throw std::runtime_error(
                        "mk_collie: channel names must contain at least 3 characters at manifest line " +
                        std::to_string(line_number));
                }
                if (process.channel_name.size() > kMaxChannelNameLength ||
                    process.process_name.size() > kMaxProcessNameLength)
                {
                    throw std::runtime_error(
                        "mk_collie: channel or process name is too long for native Collie I/O at "
                        "manifest line " + std::to_string(line_number));
                }

                const auto key = std::make_pair(process.channel_name, process.process_name);
                if (!process_keys.insert(key).second)
                {
                    throw std::runtime_error(
                        "mk_collie: duplicate process " + process.channel_name + "/" +
                        process.process_name);
                }
                manifest.processes.push_back(std::move(process));
                continue;
            }

            if (fields.front() == "rate")
            {
                if (fields.size() != 7)
                {
                    throw std::runtime_error(
                        "mk_collie: rate rows require 7 fields at manifest line " +
                        std::to_string(line_number));
                }

                RateSpec rate;
                rate.channel_name = fields[1];
                rate.process_name = fields[2];
                rate.nuisance_name = fields[3];
                rate.down_fraction = parse_fraction(fields[4], "down fraction", line_number);
                rate.up_fraction = parse_fraction(fields[5], "up fraction", line_number);
                rate.prior = parse_prior(fields[6], line_number);
                rate.line_number = line_number;
                require_safe_name(rate.channel_name, "channel name", line_number);
                require_safe_name(rate.process_name, "process name", line_number);
                require_safe_name(rate.nuisance_name, "nuisance name", line_number);
                if (rate.down_fraction > 1.0)
                {
                    throw std::runtime_error(
                        "mk_collie: rate down fraction must not exceed 1 at manifest line " +
                        std::to_string(line_number));
                }
                if (rate.prior == Prior::kLogNormal && rate.down_fraction == 1.0)
                {
                    throw std::runtime_error(
                        "mk_collie: lognormal rate down fraction must be less than 1 at "
                        "manifest line " + std::to_string(line_number));
                }
                manifest.rates.push_back(std::move(rate));
                continue;
            }

            throw std::runtime_error(
                "mk_collie: unknown manifest directive at line " +
                std::to_string(line_number) + ": " + fields.front());
        }

        if (manifest.processes.empty())
            throw std::runtime_error("mk_collie: manifest contains no process rows: " + path);
        return manifest;
    }

    bool same_histogram(const DistributionIO::HistogramSpec &first,
                        const DistributionIO::HistogramSpec &second)
    {
        return first.nbins == second.nbins &&
               first.xmin == second.xmin &&
               first.xmax == second.xmax &&
               first.branch_expr == second.branch_expr &&
               first.selection_expr == second.selection_expr;
    }

    bool nearly_equal(double first, double second)
    {
        const double scale = std::max({1.0, std::fabs(first), std::fabs(second)});
        return std::fabs(first - second) <= 1e-10 * scale;
    }

    void validate_bins(const LoadedProcess &process)
    {
        const int nbins = process.spectrum.spec.nbins;
        if (nbins <= 0 || process.spectrum.nominal.size() != static_cast<std::size_t>(nbins))
        {
            throw std::runtime_error(
                "mk_collie: invalid nominal payload for " + process.spec.channel_name + "/" +
                process.spec.process_name);
        }
        if (process.spec.role != ProcessRole::kData &&
            process.spectrum.sumw2.size() != process.spectrum.nominal.size())
        {
            throw std::runtime_error(
                "mk_collie: missing sumw2 payload for " + process.spec.channel_name + "/" +
                process.spec.process_name);
        }

        for (std::size_t bin = 0; bin < process.spectrum.nominal.size(); ++bin)
        {
            const double nominal = process.spectrum.nominal[bin];
            if (!std::isfinite(nominal) || nominal < 0.0)
            {
                throw std::runtime_error(
                    "mk_collie: non-finite or negative nominal bin " +
                    std::to_string(bin) + " in " + process.spec.channel_name + "/" +
                    process.spec.process_name);
            }
            if (process.spec.role != ProcessRole::kData)
            {
                const double sumw2 = process.spectrum.sumw2[bin];
                if (!std::isfinite(sumw2) || sumw2 < 0.0)
                {
                    throw std::runtime_error(
                        "mk_collie: non-finite or negative sumw2 bin " +
                        std::to_string(bin) + " in " + process.spec.channel_name + "/" +
                        process.spec.process_name);
                }
            }
        }
    }

    void register_nuisance(ExportModel &model,
                           const std::string &name,
                           const std::string &origin,
                           Prior prior)
    {
        if (name.size() > kMaxNuisanceNameLength)
            throw std::runtime_error("mk_collie: nuisance name is too long: " + name);
        const auto origin_result = model.nuisance_origins.emplace(name, origin);
        if (!origin_result.second && origin_result.first->second != origin)
        {
            throw std::runtime_error(
                "mk_collie: nuisance name collision for " + name + " between " +
                origin_result.first->second + " and " + origin);
        }

        const auto prior_result = model.nuisance_priors.emplace(name, prior);
        if (!prior_result.second && prior_result.first->second != prior)
            throw std::runtime_error("mk_collie: nuisance prior mismatch for " + name);
    }

    void add_effect(ExportModel &model,
                    std::size_t process_index,
                    SystematicEffect effect,
                    const std::string &origin)
    {
        LoadedProcess &process = model.processes.at(process_index);
        if (process.spec.role == ProcessRole::kData)
            throw std::runtime_error("mk_collie: data processes cannot carry systematics");
        if (effect.positive_fraction.size() != process.spectrum.nominal.size() ||
            effect.negative_fraction.size() != process.spectrum.nominal.size())
        {
            throw std::runtime_error("mk_collie: internal systematic payload size mismatch");
        }
        double maximum_effect = 0.0;
        for (double value : effect.positive_fraction)
            maximum_effect = std::max(maximum_effect, std::fabs(value));
        for (double value : effect.negative_fraction)
            maximum_effect = std::max(maximum_effect, std::fabs(value));
        if (maximum_effect == 0.0)
            return;
        if (maximum_effect <= kCollieMinimumEffect)
        {
            throw std::runtime_error(
                "mk_collie: nuisance " + effect.name + " is below native Collie precision on " +
                process.spec.channel_name + "/" + process.spec.process_name);
        }
        const auto duplicate = std::find_if(
            process.effects.begin(), process.effects.end(),
            [&](const SystematicEffect &existing) { return existing.name == effect.name; });
        if (duplicate != process.effects.end())
        {
            throw std::runtime_error(
                "mk_collie: duplicate nuisance " + effect.name + " on " +
                process.spec.channel_name + "/" + process.spec.process_name);
        }

        register_nuisance(model, effect.name, origin, effect.prior);
        process.effects.push_back(std::move(effect));
    }

    SystematicEffect symmetric_effect(const LoadedProcess &process,
                                      const std::string &name,
                                      const std::vector<double> &delta)
    {
        if (delta.size() != process.spectrum.nominal.size())
            throw std::runtime_error("mk_collie: internal shift payload size mismatch");

        SystematicEffect effect;
        effect.name = name;
        effect.positive_fraction.resize(delta.size(), 0.0);
        effect.negative_fraction.resize(delta.size(), 0.0);
        for (std::size_t bin = 0; bin < delta.size(); ++bin)
        {
            if (!std::isfinite(delta[bin]))
                throw std::runtime_error("mk_collie: non-finite systematic shift in " + name);
            const double nominal = process.spectrum.nominal[bin];
            if (nominal == 0.0)
            {
                if (std::fabs(delta[bin]) > 1e-12)
                {
                    throw std::runtime_error(
                        "mk_collie: nuisance " + name + " changes zero nominal bin " +
                        std::to_string(bin) + " in " + process.spec.channel_name + "/" +
                        process.spec.process_name);
                }
                continue;
            }
            const double fraction = delta[bin] / nominal;
            if (std::fabs(fraction) > 1.0 + 1e-12)
            {
                throw std::runtime_error(
                    "mk_collie: nuisance " + name + " would make bin " +
                    std::to_string(bin) + " negative in " + process.spec.channel_name + "/" +
                    process.spec.process_name + "; merge unstable bins before export");
            }
            effect.positive_fraction[bin] = fraction;
            effect.negative_fraction[bin] = fraction;
        }
        return effect;
    }

    void add_source_shifts(ExportModel &model,
                           std::size_t process_index,
                           const std::vector<std::string> &labels,
                           const std::vector<double> &shifts,
                           const std::vector<double> &covariance,
                           int source_count,
                           const std::string &prefix)
    {
        if (source_count == 0 && labels.empty() && shifts.empty())
        {
            if (!covariance.empty())
            {
                throw std::runtime_error(
                    "mk_collie: " + prefix +
                    " covariance has no labeled source shifts for native export");
            }
            return;
        }
        LoadedProcess &process = model.processes.at(process_index);
        const int nbins = process.spectrum.spec.nbins;
        if (source_count <= 0 || labels.size() != static_cast<std::size_t>(source_count) ||
            shifts.size() != static_cast<std::size_t>(source_count) *
                                 static_cast<std::size_t>(nbins))
        {
            throw std::runtime_error(
                "mk_collie: malformed " + prefix + " source payload for " +
                process.spec.channel_name + "/" + process.spec.process_name);
        }
        if (!covariance.empty())
        {
            const std::size_t covariance_size =
                static_cast<std::size_t>(nbins) * static_cast<std::size_t>(nbins);
            if (covariance.size() != covariance_size)
                throw std::runtime_error("mk_collie: malformed " + prefix + " covariance");
            for (int row = 0; row < nbins; ++row)
            {
                for (int column = 0; column < nbins; ++column)
                {
                    double reconstructed = 0.0;
                    for (int source = 0; source < source_count; ++source)
                    {
                        const std::size_t source_offset =
                            static_cast<std::size_t>(source) *
                            static_cast<std::size_t>(nbins);
                        reconstructed += shifts[source_offset + static_cast<std::size_t>(row)] *
                                         shifts[source_offset +
                                                static_cast<std::size_t>(column)];
                    }
                    const double cached = covariance[
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(nbins) +
                        static_cast<std::size_t>(column)];
                    if (!std::isfinite(cached) || !nearly_equal(cached, reconstructed))
                    {
                        throw std::runtime_error(
                            "mk_collie: " + prefix +
                            " covariance disagrees with its labeled source shifts in " +
                            process.spec.channel_name + "/" + process.spec.process_name);
                    }
                }
            }
        }

        std::set<std::string> seen_labels;
        for (int source = 0; source < source_count; ++source)
        {
            const std::string &label = labels[static_cast<std::size_t>(source)];
            if (label.empty() || !seen_labels.insert(label).second)
            {
                throw std::runtime_error(
                    "mk_collie: empty or duplicate " + prefix + " source label for " +
                    process.spec.channel_name + "/" + process.spec.process_name);
            }
            const std::string name = prefix + "__" + safe_component(label);
            std::vector<double> delta(static_cast<std::size_t>(nbins), 0.0);
            for (int bin = 0; bin < nbins; ++bin)
            {
                delta[static_cast<std::size_t>(bin)] =
                    shifts[static_cast<std::size_t>(source) *
                               static_cast<std::size_t>(nbins) +
                           static_cast<std::size_t>(bin)];
            }
            add_effect(model,
                       process_index,
                       symmetric_effect(process, name, delta),
                       prefix + " source " + label);
        }
    }

    const DistributionIO::UniverseFamily &family_for(const LoadedProcess &process,
                                                      FamilyKind kind)
    {
        switch (kind)
        {
            case FamilyKind::kGenie: return process.spectrum.genie;
            case FamilyKind::kFlux: return process.spectrum.flux;
            case FamilyKind::kReint: return process.spectrum.reint;
        }
        throw std::runtime_error("mk_collie: invalid family kind");
    }

    const char *family_name(FamilyKind kind)
    {
        switch (kind)
        {
            case FamilyKind::kGenie: return "genie";
            case FamilyKind::kFlux: return "flux";
            case FamilyKind::kReint: return "reint";
        }
        return "unknown";
    }

    Eigen::MatrixXd family_covariance(const ExportModel &model,
                                      FamilyKind kind,
                                      const std::vector<std::size_t> &process_indices)
    {
        int dimension = 0;
        for (std::size_t process_index : process_indices)
        {
            const int nbins = model.processes.at(process_index).spectrum.spec.nbins;
            if (nbins > std::numeric_limits<int>::max() - dimension)
                throw std::runtime_error("mk_collie: joint covariance is too large");
            dimension += nbins;
        }
        Eigen::MatrixXd covariance = Eigen::MatrixXd::Zero(dimension, dimension);

        if (process_indices.size() == 1)
        {
            const LoadedProcess &process = model.processes.at(process_indices.front());
            const DistributionIO::UniverseFamily &family = family_for(process, kind);
            const int nbins = process.spectrum.spec.nbins;
            const std::size_t covariance_size =
                static_cast<std::size_t>(nbins) * static_cast<std::size_t>(nbins);
            if (family.covariance.size() == covariance_size)
            {
                double maximum_value = 0.0;
                double maximum_asymmetry = 0.0;
                for (int row = 0; row < nbins; ++row)
                {
                    for (int column = 0; column < nbins; ++column)
                    {
                        const std::size_t index =
                            static_cast<std::size_t>(row) * static_cast<std::size_t>(nbins) +
                            static_cast<std::size_t>(column);
                        const std::size_t transpose_index =
                            static_cast<std::size_t>(column) * static_cast<std::size_t>(nbins) +
                            static_cast<std::size_t>(row);
                        const double value = family.covariance[index];
                        const double transpose = family.covariance[transpose_index];
                        if (!std::isfinite(value))
                            throw std::runtime_error("mk_collie: non-finite family covariance");
                        maximum_value = std::max(maximum_value, std::fabs(value));
                        maximum_asymmetry =
                            std::max(maximum_asymmetry, std::fabs(value - transpose));
                        covariance(row, column) = value;
                    }
                }
                if (maximum_asymmetry > 1e-10 * std::max(1.0, maximum_value))
                    throw std::runtime_error("mk_collie: asymmetric family covariance");
                covariance = 0.5 * (covariance + covariance.transpose());
                return covariance;
            }
            if (!family.covariance.empty())
                throw std::runtime_error("mk_collie: malformed family covariance");
        }

        const DistributionIO::UniverseFamily &reference =
            family_for(model.processes.at(process_indices.front()), kind);
        if (reference.n_variations <= 0 ||
            reference.n_variations > std::numeric_limits<int>::max())
        {
            throw std::runtime_error(
                "mk_collie: " + std::string(family_name(kind)) +
                " cross-process export requires retained universes");
        }
        const int universe_count = static_cast<int>(reference.n_variations);

        for (std::size_t process_index : process_indices)
        {
            const LoadedProcess &process = model.processes.at(process_index);
            const DistributionIO::UniverseFamily &family = family_for(process, kind);
            const int nbins = process.spectrum.spec.nbins;
            const std::size_t expected_universes =
                static_cast<std::size_t>(nbins) *
                static_cast<std::size_t>(universe_count);
            if (family.n_variations != reference.n_variations ||
                family.universe_histograms.size() != expected_universes)
            {
                throw std::runtime_error(
                    "mk_collie: " + std::string(family_name(kind)) + " family " +
                    reference.branch_name +
                    " requires matching retained universes across every process; "
                    "rebuild the cache with mk_dist --retain-universes");
            }
        }

        for (int universe = 0; universe < universe_count; ++universe)
        {
            Eigen::VectorXd delta(dimension);
            int offset = 0;
            for (std::size_t process_index : process_indices)
            {
                const LoadedProcess &process = model.processes.at(process_index);
                const DistributionIO::UniverseFamily &family = family_for(process, kind);
                const int nbins = process.spectrum.spec.nbins;
                for (int bin = 0; bin < nbins; ++bin)
                {
                    const double varied = family.universe_histograms[
                        static_cast<std::size_t>(bin) *
                            static_cast<std::size_t>(universe_count) +
                        static_cast<std::size_t>(universe)];
                    if (!std::isfinite(varied))
                        throw std::runtime_error("mk_collie: non-finite retained universe bin");
                    delta(offset + bin) =
                        varied - process.spectrum.nominal[static_cast<std::size_t>(bin)];
                }
                offset += nbins;
            }
            covariance.noalias() += delta * delta.transpose();
        }
        covariance /= static_cast<double>(universe_count);

        int offset = 0;
        for (std::size_t process_index : process_indices)
        {
            const LoadedProcess &process = model.processes.at(process_index);
            const DistributionIO::UniverseFamily &family = family_for(process, kind);
            const int nbins = process.spectrum.spec.nbins;
            const std::size_t expected_covariance =
                static_cast<std::size_t>(nbins) * static_cast<std::size_t>(nbins);
            if (!family.covariance.empty() && family.covariance.size() != expected_covariance)
                throw std::runtime_error("mk_collie: malformed family covariance");
            for (int row = 0; row < nbins && !family.covariance.empty(); ++row)
            {
                for (int column = 0; column < nbins; ++column)
                {
                    const double cached = family.covariance[
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(nbins) +
                        static_cast<std::size_t>(column)];
                    if (!std::isfinite(cached) ||
                        !nearly_equal(cached, covariance(offset + row, offset + column)))
                    {
                        throw std::runtime_error(
                            "mk_collie: retained universes disagree with cached " +
                            std::string(family_name(kind)) + " covariance in " +
                            process.spec.channel_name + "/" + process.spec.process_name);
                    }
                }
            }
            offset += nbins;
        }
        return covariance;
    }

    void add_family_modes(ExportModel &model,
                          FamilyKind kind,
                          const std::vector<std::size_t> &process_indices,
                          const std::string &branch_name)
    {
        const Eigen::MatrixXd covariance = family_covariance(model, kind, process_indices);
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(covariance);
        if (solver.info() != Eigen::Success)
        {
            throw std::runtime_error(
                "mk_collie: failed to decompose " + std::string(family_name(kind)) +
                " covariance for " + branch_name);
        }

        const Eigen::VectorXd eigenvalues = solver.eigenvalues();
        const double maximum = eigenvalues.size() == 0 ? 0.0 : eigenvalues.maxCoeff();
        const double tolerance = std::max(1e-12, std::fabs(maximum) * 1e-10);
        if (eigenvalues.size() > 0 && eigenvalues.minCoeff() < -tolerance)
        {
            throw std::runtime_error(
                "mk_collie: " + std::string(family_name(kind)) +
                " covariance is not positive semidefinite for " + branch_name);
        }

        int mode_number = 0;
        for (int eigen_index = static_cast<int>(eigenvalues.size()) - 1;
             eigen_index >= 0;
             --eigen_index)
        {
            const double eigenvalue = eigenvalues(eigen_index);
            if (eigenvalue <= tolerance)
                continue;

            Eigen::VectorXd mode =
                solver.eigenvectors().col(eigen_index) * std::sqrt(eigenvalue);
            Eigen::Index pivot = 0;
            mode.cwiseAbs().maxCoeff(&pivot);
            if (mode(pivot) < 0.0)
                mode = -mode;

            std::ostringstream mode_suffix;
            mode_suffix << std::setw(3) << std::setfill('0') << mode_number++;
            const std::string name = std::string(family_name(kind)) + "__" +
                                     safe_component(branch_name) + "__mode_" +
                                     mode_suffix.str();
            const std::string origin = std::string(family_name(kind)) +
                                       " family " + branch_name + " joint mode " +
                                       mode_suffix.str();

            int offset = 0;
            for (std::size_t process_index : process_indices)
            {
                LoadedProcess &process = model.processes.at(process_index);
                const int nbins = process.spectrum.spec.nbins;
                std::vector<double> delta(static_cast<std::size_t>(nbins), 0.0);
                for (int bin = 0; bin < nbins; ++bin)
                    delta[static_cast<std::size_t>(bin)] = mode(offset + bin);
                offset += nbins;
                add_effect(model,
                           process_index,
                           symmetric_effect(process, name, delta),
                           origin);
            }
        }
    }

    void add_multisim_effects(ExportModel &model)
    {
        for (FamilyKind kind : {FamilyKind::kGenie, FamilyKind::kFlux, FamilyKind::kReint})
        {
            std::map<std::string, std::vector<std::size_t>> groups;
            for (std::size_t index = 0; index < model.processes.size(); ++index)
            {
                const LoadedProcess &process = model.processes[index];
                if (process.spec.role == ProcessRole::kData)
                    continue;
                const DistributionIO::UniverseFamily &family = family_for(process, kind);
                if (family.empty())
                    continue;
                if (family.branch_name.empty())
                {
                    throw std::runtime_error(
                        "mk_collie: non-empty " + std::string(family_name(kind)) +
                        " payload has no branch name in " + process.spec.channel_name + "/" +
                        process.spec.process_name);
                }
                groups[family.branch_name].push_back(index);
            }
            for (const auto &group : groups)
                add_family_modes(model, kind, group.second, group.first);
        }
    }

    void add_mc_stat_effects(ExportModel &model)
    {
        for (std::size_t process_index = 0;
             process_index < model.processes.size();
             ++process_index)
        {
            LoadedProcess &process = model.processes[process_index];
            if (process.spec.role == ProcessRole::kData)
                continue;
            for (std::size_t bin = 0; bin < process.spectrum.sumw2.size(); ++bin)
            {
                const double uncertainty = std::sqrt(process.spectrum.sumw2[bin]);
                if (uncertainty == 0.0)
                    continue;
                const double nominal = process.spectrum.nominal[bin];
                if (nominal == 0.0 || uncertainty > nominal)
                {
                    throw std::runtime_error(
                        "mk_collie: MC-stat uncertainty exceeds the nominal yield in " +
                        process.spec.channel_name + "/" + process.spec.process_name +
                        " bin " + std::to_string(bin) + "; merge unstable bins before export");
                }

                std::vector<double> delta(process.spectrum.nominal.size(), 0.0);
                delta[bin] = uncertainty;
                const std::string name = "mcstat__" + process.spec.channel_name + "__" +
                                         process.spec.process_name + "__bin_" +
                                         std::to_string(bin);
                add_effect(model,
                           process_index,
                           symmetric_effect(process, name, delta),
                           "MC stat " + process.spec.channel_name + "/" +
                               process.spec.process_name + " bin " + std::to_string(bin));
            }
        }
    }

    void validate_channel_filenames(const std::map<std::string, ChannelModel> &channels)
    {
        std::map<std::string, std::string> portable_names;
        for (const auto &entry : channels)
        {
            const std::string portable_name = lowercase_copy(entry.first);
            const auto inserted = portable_names.emplace(portable_name, entry.first);
            if (!inserted.second && inserted.first->second != entry.first)
            {
                throw std::runtime_error(
                    "mk_collie: channel filenames differ only by case: " +
                    inserted.first->second + " and " + entry.first);
            }
        }
        for (const auto &entry : portable_names)
        {
            const auto inspection_collision = portable_names.find("fv_" + entry.first);
            if (inspection_collision != portable_names.end())
            {
                throw std::runtime_error(
                    "mk_collie: channel filename collides with Collie inspection output: " +
                    inspection_collision->second);
            }
        }
    }

    ExportModel build_model(const CliOptions &options, Manifest manifest)
    {
        DistributionIO distribution(options.distribution_path, DistributionIO::Mode::kRead);
        ExportModel model;
        model.manifest = std::move(manifest);
        model.metadata = distribution.metadata();
        model.distribution_path = cli::normalised_path(options.distribution_path).string();
        model.distribution_uuid = distribution.file_uuid();
        model.distribution_revision = distribution.content_revision();
        model.processes.reserve(model.manifest.processes.size());

        std::string shared_signal_name;
        for (const ProcessSpec &spec : model.manifest.processes)
        {
            if (!distribution.has(spec.sample_key, spec.cache_key))
            {
                throw std::runtime_error(
                    "mk_collie: missing cache " + spec.sample_key + "/" + spec.cache_key +
                    " for manifest line " + std::to_string(spec.line_number));
            }

            LoadedProcess process;
            process.spec = spec;
            process.spectrum = distribution.read(spec.sample_key, spec.cache_key);
            validate_bins(process);
            const std::size_t process_index = model.processes.size();
            model.processes.push_back(std::move(process));

            ChannelModel &channel = model.channels[spec.channel_name];
            channel.name = spec.channel_name;
            if (!channel.have_histogram)
            {
                channel.histogram = model.processes.back().spectrum.spec;
                channel.have_histogram = true;
            }
            else if (!same_histogram(channel.histogram, model.processes.back().spectrum.spec))
            {
                throw std::runtime_error(
                    "mk_collie: process " + spec.channel_name + "/" + spec.process_name +
                    " does not match its channel histogram definition");
            }

            if (spec.role == ProcessRole::kData)
            {
                if (channel.data_process != std::numeric_limits<std::size_t>::max())
                    throw std::runtime_error("mk_collie: channel " + spec.channel_name +
                                             " has more than one data process");
                channel.data_process = process_index;
            }
            else if (spec.role == ProcessRole::kSignal)
            {
                channel.signal_processes.push_back(process_index);
                if (shared_signal_name.empty())
                    shared_signal_name = spec.process_name;
                else if (shared_signal_name != spec.process_name)
                {
                    throw std::runtime_error(
                        "mk_collie: every channel must use the same signal process name; found " +
                        shared_signal_name + " and " + spec.process_name);
                }
            }
            else
            {
                channel.background_processes.push_back(process_index);
            }
        }

        for (const auto &entry : model.channels)
        {
            const ChannelModel &channel = entry.second;
            if (channel.data_process == std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("mk_collie: channel " + channel.name + " has no data process");
            if (channel.signal_processes.size() != 1)
                throw std::runtime_error("mk_collie: channel " + channel.name +
                                         " must have exactly one signal process");
            if (channel.background_processes.empty())
                throw std::runtime_error("mk_collie: channel " + channel.name +
                                         " has no background process");
        }
        validate_channel_filenames(model.channels);

        std::map<std::pair<std::string, std::string>, std::size_t> process_lookup;
        for (std::size_t index = 0; index < model.processes.size(); ++index)
        {
            const LoadedProcess &process = model.processes[index];
            process_lookup.emplace(
                std::make_pair(process.spec.channel_name, process.spec.process_name), index);
        }
        for (const RateSpec &rate : model.manifest.rates)
        {
            const auto found = process_lookup.find(
                std::make_pair(rate.channel_name, rate.process_name));
            if (found == process_lookup.end())
            {
                throw std::runtime_error(
                    "mk_collie: rate row references unknown process " + rate.channel_name + "/" +
                    rate.process_name + " at manifest line " +
                    std::to_string(rate.line_number));
            }
            LoadedProcess &process = model.processes.at(found->second);
            if (process.spec.role == ProcessRole::kData)
                throw std::runtime_error("mk_collie: rate nuisances cannot target data processes");

            SystematicEffect effect;
            effect.name = rate.nuisance_name;
            effect.prior = rate.prior;
            const double positive_parameter =
                rate.prior == Prior::kLogNormal ? std::log1p(rate.up_fraction)
                                                : rate.up_fraction;
            const double negative_parameter =
                rate.prior == Prior::kLogNormal ? -std::log1p(-rate.down_fraction)
                                                : rate.down_fraction;
            effect.positive_fraction.assign(process.spectrum.nominal.size(), positive_parameter);
            effect.negative_fraction.assign(process.spectrum.nominal.size(), negative_parameter);
            add_effect(model,
                       found->second,
                       std::move(effect),
                       "manifest rate " + rate.nuisance_name);
        }

        for (std::size_t index = 0; index < model.processes.size(); ++index)
        {
            const LoadedProcess &process = model.processes[index];
            if (process.spec.role == ProcessRole::kData)
                continue;
            add_source_shifts(model,
                              index,
                              process.spectrum.detector_source_labels,
                              process.spectrum.detector_shift_vectors,
                              process.spectrum.detector_covariance,
                              process.spectrum.detector_source_count,
                              "detector");
            add_source_shifts(model,
                              index,
                              process.spectrum.genie_knob_source_labels,
                              process.spectrum.genie_knob_shift_vectors,
                              process.spectrum.genie_knob_covariance,
                              process.spectrum.genie_knob_source_count,
                              "genie_knob");
        }
        add_multisim_effects(model);
        add_mc_stat_effects(model);
        return model;
    }

    std::unique_ptr<TH1D> make_histogram(const std::string &name,
                                         const DistributionIO::HistogramSpec &spec,
                                         const std::vector<double> &values)
    {
        if (values.size() != static_cast<std::size_t>(spec.nbins))
            throw std::runtime_error("mk_collie: internal histogram size mismatch");
        auto histogram = std::make_unique<TH1D>(
            name.c_str(), name.c_str(), spec.nbins, spec.xmin, spec.xmax);
        histogram->SetDirectory(nullptr);
        for (int bin = 0; bin < spec.nbins; ++bin)
        {
            histogram->SetBinContent(bin + 1, values[static_cast<std::size_t>(bin)]);
            histogram->SetBinError(bin + 1, 0.0);
        }
        return histogram;
    }

    void write_effect(CollieIOFile &output,
                      const LoadedProcess &process,
                      int process_index,
                      const SystematicEffect &effect,
                      std::vector<std::unique_ptr<TH1D>> &histograms)
    {
        histograms.push_back(make_histogram(
            effect.name + "_positive", process.spectrum.spec, effect.positive_fraction));
        TH1D *positive = histograms.back().get();
        histograms.push_back(make_histogram(
            effect.name + "_negative", process.spectrum.spec, effect.negative_fraction));
        TH1D *negative = histograms.back().get();
        if (process.spec.role == ProcessRole::kSignal)
        {
            output.createSigSystematic(
                process_index, effect.name, positive, negative, kModelPoint);
        }
        else
        {
            output.createBkgdSystematic(
                process_index, effect.name, positive, negative, kModelPoint);
        }
    }

    void append_provenance(const std::filesystem::path &path,
                           const ExportModel &model,
                           const ChannelModel &channel)
    {
        TFile file(path.string().c_str(), "UPDATE");
        if (file.IsZombie())
            throw std::runtime_error("mk_collie: failed to reopen native output for provenance");
        std::ostringstream text;
        text << "schema=1\n"
             << "distribution_path=" << model.distribution_path << "\n"
             << "distribution_uuid=" << model.distribution_uuid << "\n"
             << "distribution_revision=" << model.distribution_revision << "\n"
             << "eventlist_path=" << model.metadata.eventlist_path << "\n"
             << "eventlist_uuid=" << model.metadata.eventlist_uuid << "\n"
             << "channel=" << channel.name << "\n";
        TNamed("amarantin_export", text.str().c_str())
            .Write("amarantin_export", TObject::kOverwrite);
        file.Write();
        if (file.TestBit(TFile::kWriteError))
            throw std::runtime_error("mk_collie: failed to write native output provenance");
        file.Close();
    }

    void validate_distribution(const CollieDistribution *loaded,
                               const LoadedProcess &expected)
    {
        if (!loaded || loaded->getNXbins() != expected.spectrum.spec.nbins ||
            !nearly_equal(loaded->getMinX(), expected.spectrum.spec.xmin) ||
            !nearly_equal(loaded->getMaxX(), expected.spectrum.spec.xmax))
        {
            throw std::runtime_error(
                "mk_collie: native output changed process " + expected.spec.process_name);
        }
        for (int bin = 0; bin < expected.spectrum.spec.nbins; ++bin)
        {
            if (!nearly_equal(loaded->getEfficiency(bin),
                              expected.spectrum.nominal[static_cast<std::size_t>(bin)]))
            {
                throw std::runtime_error(
                    "mk_collie: native output changed a bin in " +
                    expected.spec.process_name);
            }
            if (!nearly_equal(loaded->getBinStatErr(bin, -1), 0.0))
            {
                throw std::runtime_error(
                    "mk_collie: native output duplicated MC-stat uncertainty in " +
                    expected.spec.process_name);
            }
        }
        if (loaded->getNsystematics() != static_cast<int>(expected.effects.size()))
        {
            throw std::runtime_error(
                "mk_collie: native output changed the nuisance count for " +
                expected.spec.process_name);
        }
        for (const SystematicEffect &effect : expected.effects)
        {
            const int index = loaded->getSystIndex(effect.name);
            if (index < 0)
            {
                throw std::runtime_error(
                    "mk_collie: native output lost nuisance " + effect.name + " from " +
                    expected.spec.process_name);
            }
            const TH1D *positive = loaded->getPositiveSyst(index);
            const TH1D *negative = loaded->getNegativeSyst(index);
            // Collie's read-only flag accessor is not const-qualified.
            const bool lognormal =
                const_cast<CollieDistribution *>(loaded)->getLogNormalFlag(effect.name);
            if (!positive || !negative || lognormal != (effect.prior == Prior::kLogNormal))
            {
                throw std::runtime_error(
                    "mk_collie: native output lost nuisance " + effect.name + " from " +
                    expected.spec.process_name);
            }
            for (int bin = 0; bin < expected.spectrum.spec.nbins; ++bin)
            {
                const std::size_t offset = static_cast<std::size_t>(bin);
                if (!nearly_equal(positive->GetBinContent(bin + 1),
                                  effect.positive_fraction[offset]) ||
                    !nearly_equal(negative->GetBinContent(bin + 1),
                                  effect.negative_fraction[offset]))
                {
                    throw std::runtime_error(
                        "mk_collie: native output changed nuisance " + effect.name + " in " +
                        expected.spec.process_name);
                }
            }
        }
    }

    void validate_native_file(const std::filesystem::path &path,
                              const ExportModel &model,
                              const ChannelModel &expected)
    {
        TFile file(path.string().c_str(), "READ");
        if (file.IsZombie())
            throw std::runtime_error("mk_collie: failed to reopen " + path.string());
        CollieChannel *channel = CollieChannel::loadChannel(expected.name.c_str(), &file);
        if (!channel || channel->getNMasspoints() != 1 ||
            channel->getNSignals() != static_cast<int>(expected.signal_processes.size()) ||
            channel->getNBackgrounds() !=
                static_cast<int>(expected.background_processes.size()))
        {
            throw std::runtime_error("mk_collie: native output changed channel " + expected.name);
        }
        CollieMasspoint *point = channel->getMasspoint(kModelPoint);
        if (!point)
            throw std::runtime_error("mk_collie: native output lost its model point");

        const LoadedProcess &data = model.processes.at(expected.data_process);
        validate_distribution(point->getDataDist(), data);
        for (std::size_t index = 0; index < expected.signal_processes.size(); ++index)
        {
            const LoadedProcess &process =
                model.processes.at(expected.signal_processes[index]);
            if (process.spec.process_name != channel->getSignalName(static_cast<int>(index)))
                throw std::runtime_error("mk_collie: native output changed a signal name");
            validate_distribution(point->getSignalDist(static_cast<int>(index)), process);
        }
        for (std::size_t index = 0; index < expected.background_processes.size(); ++index)
        {
            const LoadedProcess &process =
                model.processes.at(expected.background_processes[index]);
            if (process.spec.process_name != channel->getBackgroundName(static_cast<int>(index)))
                throw std::runtime_error("mk_collie: native output changed a background name");
            validate_distribution(point->getBkgdDist(static_cast<int>(index)), process);
        }
        file.Close();
    }

    void write_channel_file(const std::filesystem::path &path,
                            const ExportModel &model,
                            const ChannelModel &channel)
    {
        TH1::AddDirectory(false);
        const LoadedProcess &data_process = model.processes.at(channel.data_process);
        std::unique_ptr<TH1D> data = make_histogram(
            channel.name + "_data", channel.histogram, data_process.spectrum.nominal);

        std::vector<std::unique_ptr<TH1D>> signal_histograms;
        std::vector<std::unique_ptr<TH1D>> background_histograms;
        std::vector<std::unique_ptr<TH1D>> systematic_histograms;
        std::vector<TH1D *> signal_pointers;
        std::vector<TH1D *> background_pointers;
        std::vector<std::string> signal_names;
        std::vector<std::string> background_names;

        for (std::size_t process_index : channel.signal_processes)
        {
            const LoadedProcess &process = model.processes.at(process_index);
            signal_names.push_back(process.spec.process_name);
            signal_histograms.push_back(make_histogram(
                channel.name + "_" + process.spec.process_name,
                channel.histogram,
                process.spectrum.nominal));
            signal_pointers.push_back(signal_histograms.back().get());
        }
        for (std::size_t process_index : channel.background_processes)
        {
            const LoadedProcess &process = model.processes.at(process_index);
            background_names.push_back(process.spec.process_name);
            background_histograms.push_back(make_histogram(
                channel.name + "_" + process.spec.process_name,
                channel.histogram,
                process.spectrum.nominal));
            background_pointers.push_back(background_histograms.back().get());
        }

        {
            CollieIOFile output;
            output.setNoviceFlag(false);
            output.setSystematicsOverride(true);
            output.setSmooth(false);
            output.setVerbosity(false);
            output.initFile(path.string(), channel.name);
            output.setInputHist(channel.histogram.xmin,
                                channel.histogram.xmax,
                                channel.histogram.nbins);
            output.setRebin(1);
            output.createChannel(background_names, signal_names);
            std::vector<double> signal_alphas(signal_pointers.size(), -1.0);
            std::vector<double> background_alphas(background_pointers.size(), -1.0);
            output.createMassPoint(kModelPoint,
                                   data.get(),
                                   signal_pointers,
                                   signal_alphas,
                                   background_pointers,
                                   background_alphas);

            for (std::size_t index = 0; index < channel.signal_processes.size(); ++index)
            {
                const LoadedProcess &process =
                    model.processes.at(channel.signal_processes[index]);
                for (const SystematicEffect &effect : process.effects)
                    write_effect(output,
                                 process,
                                 static_cast<int>(index),
                                 effect,
                                 systematic_histograms);
            }
            for (std::size_t index = 0; index < channel.background_processes.size(); ++index)
            {
                const LoadedProcess &process =
                    model.processes.at(channel.background_processes[index]);
                for (const SystematicEffect &effect : process.effects)
                    write_effect(output,
                                 process,
                                 static_cast<int>(index),
                                 effect,
                                 systematic_histograms);
            }

            std::set<std::string> lognormal_names;
            for (std::size_t process_index : channel.signal_processes)
            {
                for (const SystematicEffect &effect : model.processes.at(process_index).effects)
                    if (effect.prior == Prior::kLogNormal)
                        lognormal_names.insert(effect.name);
            }
            for (std::size_t process_index : channel.background_processes)
            {
                for (const SystematicEffect &effect : model.processes.at(process_index).effects)
                    if (effect.prior == Prior::kLogNormal)
                        lognormal_names.insert(effect.name);
            }
            for (const std::string &name : lognormal_names)
                output.setLogNormalFlag(name, true, kModelPoint);
            output.storeFile();
        }

        const std::filesystem::path inspection =
            path.parent_path() / ("fv_" + path.filename().string());
        std::error_code remove_error;
        std::filesystem::remove(inspection, remove_error);
        if (remove_error)
            throw std::runtime_error("mk_collie: failed to remove Collie inspection file: " +
                                     remove_error.message());
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("mk_collie: CollieIOFile did not create " + path.string());

        append_provenance(path, model, channel);
        validate_native_file(path, model, channel);
    }

    void write_model_manifest(const std::filesystem::path &path,
                              const ExportModel &model)
    {
        std::ofstream output(path);
        if (!output)
            throw std::runtime_error("mk_collie: failed to create resolved model manifest");
        output << "# source " << model.distribution_path << " "
               << (model.distribution_uuid.empty() ? "-" : model.distribution_uuid) << " "
               << model.distribution_revision << "\n";
        for (const ProcessSpec &process : model.manifest.processes)
        {
            const char *role = process.role == ProcessRole::kData
                                   ? "data"
                                   : (process.role == ProcessRole::kSignal ? "signal" : "background");
            output << "process " << process.channel_name << " " << process.process_name << " "
                   << role << " " << process.sample_key << " " << process.cache_key << "\n";
        }
        for (const RateSpec &rate : model.manifest.rates)
        {
            output << "rate " << rate.channel_name << " " << rate.process_name << " "
                   << rate.nuisance_name << " " << std::setprecision(17)
                   << rate.down_fraction << " " << rate.up_fraction << " "
                   << (rate.prior == Prior::kGaussian ? "gaussian" : "lognormal") << "\n";
        }
        if (!output)
            throw std::runtime_error("mk_collie: failed to write resolved model manifest");
    }

    void publish_model(const CliOptions &options, const ExportModel &model)
    {
        const std::filesystem::path final_directory =
            cli::normalised_path(options.output_directory);
        std::error_code error;
        if (std::filesystem::exists(final_directory, error))
            throw std::runtime_error("mk_collie: output directory already exists: " +
                                     final_directory.string());
        if (error)
            throw std::runtime_error("mk_collie: failed to inspect output directory: " +
                                     error.message());

        const std::filesystem::path parent = final_directory.parent_path();
        std::filesystem::create_directories(parent, error);
        if (error)
            throw std::runtime_error("mk_collie: failed to create output parent: " +
                                     error.message());

        const std::filesystem::path temporary_directory =
            cli::unused_temporary_output_path(final_directory.string());
        std::filesystem::create_directory(temporary_directory, error);
        if (error)
            throw std::runtime_error("mk_collie: failed to create temporary output directory: " +
                                     error.message());

        try
        {
            for (const auto &entry : model.channels)
            {
                write_channel_file(
                    temporary_directory / (entry.first + ".root"), model, entry.second);
            }
            write_model_manifest(temporary_directory / "model.manifest", model);

            std::ofstream inputs(temporary_directory / "inputs.list");
            if (!inputs)
                throw std::runtime_error("mk_collie: failed to create inputs.list");
            for (const auto &entry : model.channels)
                inputs << (final_directory / (entry.first + ".root")).string() << "\n";
            if (!inputs)
                throw std::runtime_error("mk_collie: failed to write inputs.list");
            inputs.close();

            std::filesystem::rename(temporary_directory, final_directory, error);
            if (error)
                throw std::runtime_error("mk_collie: failed to publish output directory: " +
                                         error.message());
        }
        catch (...)
        {
            std::error_code cleanup_error;
            std::filesystem::remove_all(temporary_directory, cleanup_error);
            throw;
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        gROOT->SetBatch(kTRUE);
        const CliOptions options = parse_args(argc, argv);
        cli::require_distinct_output_path(
            "mk_collie", options.output_directory, "distribution", options.distribution_path);
        cli::require_distinct_output_path(
            "mk_collie", options.output_directory, "manifest", options.manifest_path);
        ExportModel model = build_model(options, read_manifest(options.manifest_path));
        publish_model(options, model);
        std::cout << "mk_collie: wrote " << model.channels.size() << " channels to "
                  << cli::normalised_path(options.output_directory).string() << "\n";
        return 0;
    }
    catch (const HelpRequested &)
    {
        return 0;
    }
    catch (const std::exception &error)
    {
        const std::string message = error.what();
        if (message.rfind("mk_collie: ", 0) == 0)
            std::cerr << message << "\n";
        else
            std::cerr << "mk_collie: " << message << "\n";
        return 1;
    }
}
