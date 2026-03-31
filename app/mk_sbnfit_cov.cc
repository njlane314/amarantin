#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DistributionIO.hh"

#include <TFile.h>
#include <TH1D.h>
#include <TMatrixT.h>
#include <TTree.h>

namespace
{
    struct CliOptions
    {
        std::string input_path;
        std::string sample_key;
        std::string output_path;
        std::string cache_key;
        std::string manifest_path;
        std::string matrix_name = "frac_covariance";
        std::string nominal_name = "nominal_prediction";

        bool stacked_mode() const
        {
            return !manifest_path.empty();
        }
    };

    struct MatrixComponent
    {
        std::string label;
        std::vector<double> absolute;
        bool diagonal_only = false;
    };

    struct ManifestRow
    {
        std::string label;
        std::string sample_key;
        std::string cache_key;
        int line_number = 0;
    };

    struct LoadedEntry
    {
        std::string label;
        std::string sample_key;
        std::string cache_key;
        DistributionIO::Spectrum spectrum;
        int bin_offset = 0;
    };

    enum class ShiftLaneKind
    {
        kDetector,
        kGenieKnobs
    };

    enum class FamilyKind
    {
        kGenie,
        kFlux,
        kReint
    };

    void add_outer_product_in_place(std::vector<double> &target,
                                    const std::vector<double> &delta,
                                    int nbins);

    bool family_has_exact_universes(const DistributionIO::Family &family,
                                    int nbins);

    void print_usage(std::ostream &os)
    {
        os << "usage: mk_sbnfit_cov [--cache-key <key>] [--matrix-name <name>] "
              "[--nominal-name <name>] <input.dists.root> <sample-key> <output.root>\n"
              "   or: mk_sbnfit_cov [--manifest <export.manifest>] "
              "[--matrix-name <name>] [--nominal-name <name>] "
              "<input.dists.root> <output.root>\n";
    }

    [[noreturn]] void print_usage_and_throw()
    {
        print_usage(std::cerr);
        throw std::runtime_error("mk_sbnfit_cov: invalid arguments");
    }

    CliOptions parse_args(int argc, char **argv)
    {
        CliOptions options;

        int i = 1;
        for (; i < argc; ++i)
        {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "-h" || arg == "--help")
            {
                print_usage(std::cout);
                throw std::runtime_error("");
            }
            if (arg == "--cache-key")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.cache_key = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--manifest")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.manifest_path = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--matrix-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.matrix_name = argv[i] ? argv[i] : "";
                continue;
            }
            if (arg == "--nominal-name")
            {
                if (++i >= argc)
                    print_usage_and_throw();
                options.nominal_name = argv[i] ? argv[i] : "";
                continue;
            }
            break;
        }

        if (options.stacked_mode())
        {
            if (argc - i != 2)
                print_usage_and_throw();
            options.input_path = argv[i] ? argv[i] : "";
            options.output_path = argv[i + 1] ? argv[i + 1] : "";
        }
        else
        {
            if (argc - i != 3)
                print_usage_and_throw();
            options.input_path = argv[i] ? argv[i] : "";
            options.sample_key = argv[i + 1] ? argv[i + 1] : "";
            options.output_path = argv[i + 2] ? argv[i + 2] : "";
        }

        if (options.matrix_name.empty())
            throw std::runtime_error("mk_sbnfit_cov: matrix-name must not be empty");
        if (options.nominal_name.empty())
            throw std::runtime_error("mk_sbnfit_cov: nominal-name must not be empty");
        return options;
    }

    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const std::string &requested_key)
    {
        if (!requested_key.empty())
        {
            if (!dist.has(sample_key, requested_key))
                throw std::runtime_error("mk_sbnfit_cov: requested cache key is not present for sample");
            return requested_key;
        }

        const std::vector<std::string> keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("mk_sbnfit_cov: no cached distributions found for sample");
        return keys.front();
    }

    std::string trim_copy(const std::string &input)
    {
        const std::string::size_type first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";

        const std::string::size_type last = input.find_last_not_of(" \t\r\n");
        return input.substr(first, last - first + 1);
    }

    std::string strip_comment(const std::string &line)
    {
        const std::string::size_type pos = line.find('#');
        if (pos == std::string::npos)
            return line;
        return line.substr(0, pos);
    }

    std::vector<std::string> split_fields(const std::string &line)
    {
        std::istringstream input(line);
        std::vector<std::string> out;
        std::string field;
        while (input >> field)
            out.push_back(field);
        return out;
    }

    std::string normalise_optional_token(const std::string &token)
    {
        return token == "-" ? std::string() : token;
    }

    std::vector<ManifestRow> read_manifest(const std::string &path)
    {
        std::ifstream input(path);
        if (!input)
            throw std::runtime_error("mk_sbnfit_cov: failed to open manifest: " + path);

        std::vector<ManifestRow> rows;
        std::string line;
        int line_number = 0;
        while (std::getline(input, line))
        {
            ++line_number;
            const std::string trimmed = trim_copy(strip_comment(line));
            if (trimmed.empty())
                continue;

            const std::vector<std::string> fields = split_fields(trimmed);
            if (fields.size() < 2 || fields.size() > 3)
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: expected 2 or 3 fields in manifest at line " +
                    std::to_string(line_number) + " in " + path);
            }

            ManifestRow row;
            row.label = fields[0];
            row.sample_key = fields[1];
            row.cache_key = (fields.size() == 3) ? normalise_optional_token(fields[2])
                                                 : std::string();
            row.line_number = line_number;

            if (row.label.empty())
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: empty label in manifest at line " +
                    std::to_string(line_number));
            }
            if (row.sample_key.empty())
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: empty sample key in manifest at line " +
                    std::to_string(line_number));
            }

            rows.push_back(row);
        }

        if (rows.empty())
            throw std::runtime_error("mk_sbnfit_cov: manifest is empty: " + path);
        return rows;
    }

    std::vector<double> zero_matrix(int nbins)
    {
        return std::vector<double>(static_cast<std::size_t>(nbins * nbins), 0.0);
    }

    void add_matrix_in_place(std::vector<double> &target,
                             const std::vector<double> &source,
                             int nbins,
                             const std::string &context)
    {
        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (target.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: target matrix size is invalid for " + context);
        if (source.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: source matrix size is invalid for " + context);

        for (std::size_t i = 0; i < expected; ++i)
            target[i] += source[i];
    }

    void add_block_matrix_in_place(std::vector<double> &target,
                                   const std::vector<double> &block,
                                   int total_nbins,
                                   int offset,
                                   int block_nbins,
                                   const std::string &context)
    {
        const std::size_t expected = static_cast<std::size_t>(block_nbins * block_nbins);
        if (block.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: block matrix size is invalid for " + context);
        if (target.size() != static_cast<std::size_t>(total_nbins * total_nbins))
            throw std::runtime_error("mk_sbnfit_cov: target stacked matrix size is invalid for " + context);
        if (offset < 0 || offset + block_nbins > total_nbins)
            throw std::runtime_error("mk_sbnfit_cov: block offset is invalid for " + context);

        for (int row = 0; row < block_nbins; ++row)
        {
            for (int col = 0; col < block_nbins; ++col)
            {
                target[static_cast<std::size_t>((offset + row) * total_nbins + (offset + col))] +=
                    block[static_cast<std::size_t>(row * block_nbins + col)];
            }
        }
    }

    std::vector<double> diagonal_matrix_from_sigma(const std::vector<double> &sigma)
    {
        const int nbins = static_cast<int>(sigma.size());
        std::vector<double> out = zero_matrix(nbins);
        for (int bin = 0; bin < nbins; ++bin)
        {
            const double value = sigma[static_cast<std::size_t>(bin)];
            out[static_cast<std::size_t>(bin * nbins + bin)] = value * value;
        }
        return out;
    }

    std::vector<double> covariance_from_shift_vectors(const std::vector<double> &shift_vectors,
                                                      int source_count,
                                                      int nbins)
    {
        if (source_count <= 0 || shift_vectors.empty())
            return {};

        const std::size_t expected = static_cast<std::size_t>(source_count * nbins);
        if (shift_vectors.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: shift payload is truncated");

        std::vector<double> out = zero_matrix(nbins);
        for (int source = 0; source < source_count; ++source)
        {
            for (int row = 0; row < nbins; ++row)
            {
                const double row_shift =
                    shift_vectors[static_cast<std::size_t>(source * nbins + row)];
                for (int col = 0; col < nbins; ++col)
                {
                    const double col_shift =
                        shift_vectors[static_cast<std::size_t>(source * nbins + col)];
                    out[static_cast<std::size_t>(row * nbins + col)] += row_shift * col_shift;
                }
            }
        }
        return out;
    }

    MatrixComponent component_from_shift_sources(const std::string &label,
                                                 const std::vector<double> &shift_vectors,
                                                 int source_count,
                                                 const std::vector<double> &covariance,
                                                 int nbins)
    {
        MatrixComponent component;
        component.label = label;

        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (!covariance.empty() && covariance.size() != expected)
        {
            throw std::runtime_error("mk_sbnfit_cov: " + label + " covariance payload is truncated");
        }
        if (covariance.size() == expected)
        {
            component.absolute = covariance;
            return component;
        }

        if (source_count > 0 && !shift_vectors.empty())
        {
            component.absolute = covariance_from_shift_vectors(
                shift_vectors,
                source_count,
                nbins);
        }

        return component;
    }

    MatrixComponent component_from_detector(const DistributionIO::Spectrum &spectrum)
    {
        return component_from_shift_sources("detector",
                                            spectrum.detector_shift_vectors,
                                            spectrum.detector_source_count,
                                            spectrum.detector_covariance,
                                            spectrum.spec.nbins);
    }

    MatrixComponent component_from_genie_knobs(const DistributionIO::Spectrum &spectrum)
    {
        return component_from_shift_sources("genie_knobs",
                                            spectrum.genie_knob_shift_vectors,
                                            spectrum.genie_knob_source_count,
                                            spectrum.genie_knob_covariance,
                                            spectrum.spec.nbins);
    }

    const DistributionIO::Family &family_for(const DistributionIO::Spectrum &spectrum,
                                             FamilyKind kind)
    {
        switch (kind)
        {
            case FamilyKind::kGenie:
                return spectrum.genie;
            case FamilyKind::kFlux:
                return spectrum.flux;
            case FamilyKind::kReint:
                return spectrum.reint;
        }

        throw std::runtime_error("mk_sbnfit_cov: unknown family kind");
    }

    const char *family_label(FamilyKind kind)
    {
        switch (kind)
        {
            case FamilyKind::kGenie:
                return "genie";
            case FamilyKind::kFlux:
                return "flux";
            case FamilyKind::kReint:
                return "reint";
        }

        return "unknown";
    }

    std::vector<double> covariance_from_family_universes(const DistributionIO::Family &family,
                                                         const std::vector<double> &nominal,
                                                         int nbins,
                                                         const std::string &label)
    {
        if (!family_has_exact_universes(family, nbins))
            return {};
        if (nominal.size() != static_cast<std::size_t>(nbins))
            throw std::runtime_error("mk_sbnfit_cov: nominal payload is incompatible with " + label);

        const int n_variations = static_cast<int>(family.n_variations);
        std::vector<double> covariance = zero_matrix(nbins);
        for (int universe = 0; universe < n_variations; ++universe)
        {
            std::vector<double> delta(static_cast<std::size_t>(nbins), 0.0);
            for (int bin = 0; bin < nbins; ++bin)
            {
                const double universe_value =
                    family.universe_histograms[static_cast<std::size_t>(bin * n_variations + universe)];
                delta[static_cast<std::size_t>(bin)] =
                    universe_value - nominal[static_cast<std::size_t>(bin)];
            }
            add_outer_product_in_place(covariance, delta, nbins);
        }

        for (double &value : covariance)
            value /= static_cast<double>(n_variations);
        return covariance;
    }

    MatrixComponent component_from_family(const DistributionIO::Family &family,
                                          const std::vector<double> &nominal,
                                          int nbins,
                                          const std::string &label)
    {
        MatrixComponent component;
        component.label = label;

        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (!family.covariance.empty() && family.covariance.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: " + label + " covariance payload is truncated");
        if (family.covariance.size() == expected)
        {
            component.absolute = family.covariance;
            return component;
        }
        if (family_has_exact_universes(family, nbins))
        {
            component.absolute =
                covariance_from_family_universes(family, nominal, nbins, label);
            return component;
        }

        if (!family.sigma.empty() &&
            family.sigma.size() != static_cast<std::size_t>(nbins))
        {
            throw std::runtime_error("mk_sbnfit_cov: " + label + " sigma payload is truncated");
        }
        if (family.sigma.size() == static_cast<std::size_t>(nbins))
        {
            component.absolute = diagonal_matrix_from_sigma(family.sigma);
            component.diagonal_only = true;
        }

        return component;
    }

    std::vector<float> fractional_matrix(const std::vector<double> &absolute,
                                         const std::vector<double> &nominal)
    {
        const int nbins = static_cast<int>(nominal.size());
        const std::size_t expected = static_cast<std::size_t>(nbins * nbins);
        if (absolute.size() != expected)
            throw std::runtime_error("mk_sbnfit_cov: absolute covariance size does not match nominal bins");

        std::vector<float> out(expected, 0.0f);
        for (int row = 0; row < nbins; ++row)
        {
            const double row_nominal = nominal[static_cast<std::size_t>(row)];
            for (int col = 0; col < nbins; ++col)
            {
                const double col_nominal = nominal[static_cast<std::size_t>(col)];
                if (row_nominal == 0.0 || col_nominal == 0.0)
                    continue;

                out[static_cast<std::size_t>(row * nbins + col)] =
                    static_cast<float>(absolute[static_cast<std::size_t>(row * nbins + col)] /
                                       (row_nominal * col_nominal));
            }
        }
        return out;
    }

    void fill_matrix(TMatrixT<double> &matrix, const std::vector<double> &values)
    {
        const int nbins = matrix.GetNrows();
        for (int row = 0; row < nbins; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                matrix(row, col) = values[static_cast<std::size_t>(row * nbins + col)];
            }
        }
    }

    void fill_matrix(TMatrixT<float> &matrix, const std::vector<float> &values)
    {
        const int nbins = matrix.GetNrows();
        for (int row = 0; row < nbins; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                matrix(row, col) = values[static_cast<std::size_t>(row * nbins + col)];
            }
        }
    }

    const std::vector<std::string> &shift_source_labels_for(const DistributionIO::Spectrum &spectrum,
                                                            ShiftLaneKind kind)
    {
        switch (kind)
        {
            case ShiftLaneKind::kDetector:
                return spectrum.detector_source_labels;
            case ShiftLaneKind::kGenieKnobs:
                return spectrum.genie_knob_source_labels;
        }

        throw std::runtime_error("mk_sbnfit_cov: unknown shift lane");
    }

    const std::vector<double> &shift_vectors_for(const DistributionIO::Spectrum &spectrum,
                                                 ShiftLaneKind kind)
    {
        switch (kind)
        {
            case ShiftLaneKind::kDetector:
                return spectrum.detector_shift_vectors;
            case ShiftLaneKind::kGenieKnobs:
                return spectrum.genie_knob_shift_vectors;
        }

        throw std::runtime_error("mk_sbnfit_cov: unknown shift lane");
    }

    const std::vector<double> &shift_covariance_for(const DistributionIO::Spectrum &spectrum,
                                                    ShiftLaneKind kind)
    {
        switch (kind)
        {
            case ShiftLaneKind::kDetector:
                return spectrum.detector_covariance;
            case ShiftLaneKind::kGenieKnobs:
                return spectrum.genie_knob_covariance;
        }

        throw std::runtime_error("mk_sbnfit_cov: unknown shift lane");
    }

    int shift_source_count_for(const DistributionIO::Spectrum &spectrum,
                               ShiftLaneKind kind)
    {
        switch (kind)
        {
            case ShiftLaneKind::kDetector:
                return spectrum.detector_source_count;
            case ShiftLaneKind::kGenieKnobs:
                return spectrum.genie_knob_source_count;
        }

        throw std::runtime_error("mk_sbnfit_cov: unknown shift lane");
    }

    bool shift_lane_has_component(const DistributionIO::Spectrum &spectrum,
                                  ShiftLaneKind kind)
    {
        return !shift_vectors_for(spectrum, kind).empty() ||
               !shift_covariance_for(spectrum, kind).empty();
    }

    int find_source_index(const std::vector<std::string> &labels,
                          const std::string &label)
    {
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            if (labels[i] == label)
                return static_cast<int>(i);
        }
        return -1;
    }

    void validate_unique_labels(const std::vector<std::string> &labels,
                                const std::string &context)
    {
        std::set<std::string> seen;
        for (const auto &label : labels)
        {
            if (label.empty())
                throw std::runtime_error("mk_sbnfit_cov: empty source label in " + context);
            if (!seen.insert(label).second)
                throw std::runtime_error("mk_sbnfit_cov: duplicate source label " + label + " in " + context);
        }
    }

    std::vector<double> stacked_shift_for_label(const LoadedEntry &entry,
                                                ShiftLaneKind kind,
                                                int source_index,
                                                int total_nbins)
    {
        const int nbins = entry.spectrum.spec.nbins;
        const int source_count = shift_source_count_for(entry.spectrum, kind);
        const std::vector<double> &shift_vectors = shift_vectors_for(entry.spectrum, kind);
        if (source_index < 0 || source_index >= source_count)
            throw std::runtime_error("mk_sbnfit_cov: source index is out of range");
        if (shift_vectors.size() != static_cast<std::size_t>(source_count * nbins))
            throw std::runtime_error("mk_sbnfit_cov: shift payload is truncated");

        std::vector<double> out(static_cast<std::size_t>(total_nbins), 0.0);
        for (int bin = 0; bin < nbins; ++bin)
        {
            out[static_cast<std::size_t>(entry.bin_offset + bin)] =
                shift_vectors[static_cast<std::size_t>(source_index * nbins + bin)];
        }
        return out;
    }

    void add_outer_product_in_place(std::vector<double> &target,
                                    const std::vector<double> &delta,
                                    int nbins)
    {
        if (target.size() != static_cast<std::size_t>(nbins * nbins) ||
            delta.size() != static_cast<std::size_t>(nbins))
        {
            throw std::runtime_error("mk_sbnfit_cov: outer-product inputs are incompatible");
        }

        for (int row = 0; row < nbins; ++row)
        {
            for (int col = 0; col < nbins; ++col)
            {
                target[static_cast<std::size_t>(row * nbins + col)] +=
                    delta[static_cast<std::size_t>(row)] *
                    delta[static_cast<std::size_t>(col)];
            }
        }
    }

    bool family_has_payload(const DistributionIO::Family &family)
    {
        return !family.covariance.empty() ||
               !family.sigma.empty() ||
               !family.universe_histograms.empty();
    }

    bool family_has_exact_universes(const DistributionIO::Family &family,
                                    int nbins)
    {
        return family.n_variations > 0 &&
               family.universe_histograms.size() ==
                   static_cast<std::size_t>(nbins) *
                       static_cast<std::size_t>(family.n_variations);
    }

    std::vector<double> family_universe_delta(const LoadedEntry &entry,
                                              FamilyKind kind,
                                              int universe_index,
                                              int total_nbins)
    {
        const DistributionIO::Family &family = family_for(entry.spectrum, kind);
        const int nbins = entry.spectrum.spec.nbins;
        if (!family_has_exact_universes(family, nbins))
        {
            throw std::runtime_error("mk_sbnfit_cov: exact family universes are unavailable for stacked export");
        }
        const int n_variations = static_cast<int>(family.n_variations);
        if (universe_index < 0 || universe_index >= n_variations)
            throw std::runtime_error("mk_sbnfit_cov: family universe index is out of range");

        std::vector<double> out(static_cast<std::size_t>(total_nbins), 0.0);
        for (int bin = 0; bin < nbins; ++bin)
        {
            const double universe_value =
                family.universe_histograms[static_cast<std::size_t>(bin * n_variations + universe_index)];
            out[static_cast<std::size_t>(entry.bin_offset + bin)] =
                universe_value - entry.spectrum.nominal[static_cast<std::size_t>(bin)];
        }
        return out;
    }

    std::vector<LoadedEntry> load_entries(const DistributionIO &dist,
                                          const CliOptions &options)
    {
        std::vector<LoadedEntry> entries;

        if (options.stacked_mode())
        {
            const std::vector<ManifestRow> rows = read_manifest(options.manifest_path);
            entries.reserve(rows.size());
            int offset = 0;
            for (const auto &row : rows)
            {
                LoadedEntry entry;
                entry.label = row.label;
                entry.sample_key = row.sample_key;
                entry.cache_key = pick_cache_key(dist, row.sample_key, row.cache_key);
                entry.spectrum = dist.read(row.sample_key, entry.cache_key);
                entry.bin_offset = offset;
                offset += entry.spectrum.spec.nbins;
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        LoadedEntry entry;
        entry.label = options.sample_key;
        entry.sample_key = options.sample_key;
        entry.cache_key = pick_cache_key(dist, options.sample_key, options.cache_key);
        entry.spectrum = dist.read(options.sample_key, entry.cache_key);
        entries.push_back(std::move(entry));
        return entries;
    }

    int stacked_nbins(const std::vector<LoadedEntry> &entries)
    {
        int total = 0;
        for (const auto &entry : entries)
            total += entry.spectrum.spec.nbins;
        return total;
    }

    std::vector<double> stacked_nominal(const std::vector<LoadedEntry> &entries)
    {
        std::vector<double> nominal;
        nominal.reserve(static_cast<std::size_t>(stacked_nbins(entries)));
        for (const auto &entry : entries)
        {
            nominal.insert(nominal.end(),
                           entry.spectrum.nominal.begin(),
                           entry.spectrum.nominal.end());
        }
        return nominal;
    }

    MatrixComponent stacked_shift_component(const std::vector<LoadedEntry> &entries,
                                           ShiftLaneKind kind,
                                           const std::string &label)
    {
        MatrixComponent component;
        component.label = label;

        std::vector<const LoadedEntry *> contributors;
        for (const auto &entry : entries)
        {
            if (shift_lane_has_component(entry.spectrum, kind))
                contributors.push_back(&entry);
        }
        if (contributors.empty())
            return component;

        const int total_nbins = stacked_nbins(entries);
        if (contributors.size() == 1)
        {
            const DistributionIO::Spectrum &spectrum = contributors.front()->spectrum;
            const MatrixComponent local =
                component_from_shift_sources(label,
                                             shift_vectors_for(spectrum, kind),
                                             shift_source_count_for(spectrum, kind),
                                             shift_covariance_for(spectrum, kind),
                                             spectrum.spec.nbins);
            if (local.absolute.empty())
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: contributing " + label +
                    " payload could not be converted into covariance");
            }
            component.absolute = zero_matrix(total_nbins);
            add_block_matrix_in_place(component.absolute,
                                      local.absolute,
                                      total_nbins,
                                      contributors.front()->bin_offset,
                                      spectrum.spec.nbins,
                                      label);
            return component;
        }

        std::vector<std::string> shared_labels;
        for (const auto *entry : contributors)
        {
            const int source_count = shift_source_count_for(entry->spectrum, kind);
            const std::vector<std::string> &labels = shift_source_labels_for(entry->spectrum, kind);
            const std::vector<double> &shifts = shift_vectors_for(entry->spectrum, kind);
            if (source_count <= 0 || shifts.empty())
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: stacked " + label +
                    " export requires explicit source shifts for every contributing spectrum");
            }
            if (labels.size() != static_cast<std::size_t>(source_count))
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: stacked " + label +
                    " export requires one source label per contributing shift source");
            }
            validate_unique_labels(labels, label + " component");
            for (const auto &source_label : labels)
            {
                if (find_source_index(shared_labels, source_label) < 0)
                    shared_labels.push_back(source_label);
            }
        }

        component.absolute = zero_matrix(total_nbins);
        for (const auto &source_label : shared_labels)
        {
            std::vector<double> full_shift(static_cast<std::size_t>(total_nbins), 0.0);
            for (const auto *entry : contributors)
            {
                const std::vector<std::string> &labels = shift_source_labels_for(entry->spectrum, kind);
                const int source_index = find_source_index(labels, source_label);
                if (source_index < 0)
                    continue;

                const std::vector<double> partial =
                    stacked_shift_for_label(*entry, kind, source_index, total_nbins);
                for (int bin = 0; bin < total_nbins; ++bin)
                    full_shift[static_cast<std::size_t>(bin)] += partial[static_cast<std::size_t>(bin)];
            }

            add_outer_product_in_place(component.absolute, full_shift, total_nbins);
        }

        return component;
    }

    MatrixComponent stacked_family_component(const std::vector<LoadedEntry> &entries,
                                            FamilyKind kind)
    {
        MatrixComponent component;
        component.label = family_label(kind);

        std::vector<const LoadedEntry *> contributors;
        for (const auto &entry : entries)
        {
            if (family_has_payload(family_for(entry.spectrum, kind)))
                contributors.push_back(&entry);
        }
        if (contributors.empty())
            return component;

        const int total_nbins = stacked_nbins(entries);
        if (contributors.size() == 1)
        {
            const DistributionIO::Spectrum &spectrum = contributors.front()->spectrum;
            const MatrixComponent local =
                component_from_family(family_for(spectrum, kind),
                                      spectrum.nominal,
                                      spectrum.spec.nbins,
                                      component.label);
            if (local.absolute.empty())
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: contributing " + component.label +
                    " payload could not be converted into covariance");
            }
            component.absolute = zero_matrix(total_nbins);
            add_block_matrix_in_place(component.absolute,
                                      local.absolute,
                                      total_nbins,
                                      contributors.front()->bin_offset,
                                      spectrum.spec.nbins,
                                      component.label);
            component.diagonal_only = local.diagonal_only;
            return component;
        }

        const DistributionIO::Family &reference = family_for(contributors.front()->spectrum, kind);
        if (reference.branch_name.empty())
        {
            throw std::runtime_error(
                "mk_sbnfit_cov: stacked " + component.label +
                " export requires a non-empty family branch name");
        }
        if (!family_has_exact_universes(reference, contributors.front()->spectrum.spec.nbins))
        {
            throw std::runtime_error(
                "mk_sbnfit_cov: stacked " + component.label +
                " export requires retained universes for every contributing spectrum");
        }

        for (const auto *entry : contributors)
        {
            const DistributionIO::Family &family = family_for(entry->spectrum, kind);
            if (family.branch_name != reference.branch_name)
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: stacked " + component.label +
                    " export requires matching family branch names across contributing spectra");
            }
            if (family.n_variations != reference.n_variations)
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: stacked " + component.label +
                    " export requires matching universe counts across contributing spectra");
            }
            if (!family_has_exact_universes(family, entry->spectrum.spec.nbins))
            {
                throw std::runtime_error(
                    "mk_sbnfit_cov: stacked " + component.label +
                    " export requires retained universes for every contributing spectrum");
            }
        }

        component.absolute = zero_matrix(total_nbins);
        const int n_variations = static_cast<int>(reference.n_variations);
        for (int universe = 0; universe < n_variations; ++universe)
        {
            std::vector<double> full_delta(static_cast<std::size_t>(total_nbins), 0.0);
            for (const auto *entry : contributors)
            {
                const std::vector<double> partial =
                    family_universe_delta(*entry, kind, universe, total_nbins);
                for (int bin = 0; bin < total_nbins; ++bin)
                    full_delta[static_cast<std::size_t>(bin)] += partial[static_cast<std::size_t>(bin)];
            }
            add_outer_product_in_place(component.absolute, full_delta, total_nbins);
        }

        for (double &value : component.absolute)
            value /= static_cast<double>(n_variations);
        return component;
    }

    void write_single_nominal_histogram(TFile &output,
                                        const DistributionIO::Spectrum &spectrum,
                                        const CliOptions &options)
    {
        const int nbins = spectrum.spec.nbins;
        TH1D nominal_hist(options.nominal_name.c_str(),
                          options.nominal_name.c_str(),
                          nbins,
                          spectrum.spec.xmin,
                          spectrum.spec.xmax);
        nominal_hist.SetDirectory(nullptr);
        for (int bin = 0; bin < nbins; ++bin)
            nominal_hist.SetBinContent(bin + 1, spectrum.nominal[static_cast<std::size_t>(bin)]);
        nominal_hist.Write(options.nominal_name.c_str());
    }

    void write_stacked_nominal_histogram(TFile &output,
                                         const std::vector<LoadedEntry> &entries,
                                         const CliOptions &options)
    {
        const int total_nbins = stacked_nbins(entries);
        TH1D nominal_hist(options.nominal_name.c_str(),
                          options.nominal_name.c_str(),
                          total_nbins,
                          0.0,
                          static_cast<double>(total_nbins));
        nominal_hist.SetDirectory(nullptr);
        for (const auto &entry : entries)
        {
            for (int bin = 0; bin < entry.spectrum.spec.nbins; ++bin)
            {
                nominal_hist.SetBinContent(entry.bin_offset + bin + 1,
                                           entry.spectrum.nominal[static_cast<std::size_t>(bin)]);
            }
        }
        nominal_hist.Write(options.nominal_name.c_str());
    }

    void write_stack_manifest(TFile &output,
                              const std::vector<LoadedEntry> &entries)
    {
        std::string label;
        std::string sample_key;
        std::string cache_key;
        std::string branch_expr;
        std::string selection_expr;
        Int_t bin_offset = 0;
        Int_t nbins = 0;

        TTree tree("stack_manifest", "stack_manifest");
        tree.Branch("label", &label);
        tree.Branch("sample_key", &sample_key);
        tree.Branch("cache_key", &cache_key);
        tree.Branch("branch_expr", &branch_expr);
        tree.Branch("selection_expr", &selection_expr);
        tree.Branch("bin_offset", &bin_offset);
        tree.Branch("nbins", &nbins);

        for (const auto &entry : entries)
        {
            label = entry.label;
            sample_key = entry.sample_key;
            cache_key = entry.cache_key;
            branch_expr = entry.spectrum.spec.branch_expr;
            selection_expr = entry.spectrum.spec.selection_expr;
            bin_offset = entry.bin_offset;
            nbins = entry.spectrum.spec.nbins;
            tree.Fill();
        }

        tree.Write("stack_manifest");
    }

    void write_component_outputs(TFile &output,
                                 const CliOptions &options,
                                 const std::vector<double> &nominal,
                                 const std::vector<MatrixComponent> &components,
                                 std::vector<std::string> &included_components,
                                 std::vector<std::string> &approximate_components)
    {
        const int nbins = static_cast<int>(nominal.size());
        std::vector<double> total_absolute = zero_matrix(nbins);
        for (const auto &component : components)
        {
            if (component.absolute.empty())
                continue;

            add_matrix_in_place(total_absolute, component.absolute, nbins, component.label);
            included_components.push_back(component.label);
            if (component.diagonal_only)
                approximate_components.push_back(component.label);
        }

        const std::vector<float> total_fractional =
            fractional_matrix(total_absolute, nominal);

        TMatrixT<float> frac_covariance(nbins, nbins);
        fill_matrix(frac_covariance, total_fractional);
        frac_covariance.Write(options.matrix_name.c_str());

        TMatrixT<double> abs_covariance(nbins, nbins);
        fill_matrix(abs_covariance, total_absolute);
        abs_covariance.Write("abs_covariance");

        for (const auto &component : components)
        {
            if (component.absolute.empty())
                continue;

            const std::vector<float> component_fractional =
                fractional_matrix(component.absolute, nominal);

            TMatrixT<double> component_absolute_matrix(nbins, nbins);
            fill_matrix(component_absolute_matrix, component.absolute);
            component_absolute_matrix.Write((component.label + "_covariance").c_str());

            TMatrixT<float> component_fractional_matrix(nbins, nbins);
            fill_matrix(component_fractional_matrix, component_fractional);
            component_fractional_matrix.Write((component.label + "_frac_covariance").c_str());
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        const CliOptions options = parse_args(argc, argv);

        DistributionIO dist(options.input_path, DistributionIO::Mode::kRead);
        const std::vector<LoadedEntry> entries = load_entries(dist, options);

        TFile output(options.output_path.c_str(), "RECREATE");
        if (output.IsZombie())
            throw std::runtime_error("mk_sbnfit_cov: failed to open output ROOT file");

        std::vector<MatrixComponent> components;
        std::vector<double> nominal;
        if (options.stacked_mode())
        {
            nominal = stacked_nominal(entries);
            write_stacked_nominal_histogram(output, entries, options);
            write_stack_manifest(output, entries);

            components.push_back(stacked_shift_component(entries,
                                                         ShiftLaneKind::kDetector,
                                                         "detector"));
            components.push_back(stacked_shift_component(entries,
                                                         ShiftLaneKind::kGenieKnobs,
                                                         "genie_knobs"));
            components.push_back(stacked_family_component(entries, FamilyKind::kGenie));
            components.push_back(stacked_family_component(entries, FamilyKind::kFlux));
            components.push_back(stacked_family_component(entries, FamilyKind::kReint));
        }
        else
        {
            const DistributionIO::Spectrum &spectrum = entries.front().spectrum;
            const int nbins = spectrum.spec.nbins;
            if (nbins <= 0)
                throw std::runtime_error("mk_sbnfit_cov: cached histogram nbins must be positive");
            if (spectrum.nominal.size() != static_cast<std::size_t>(nbins))
                throw std::runtime_error("mk_sbnfit_cov: nominal payload size does not match cached nbins");

            nominal = spectrum.nominal;
            write_single_nominal_histogram(output, spectrum, options);

            components.push_back(component_from_detector(spectrum));
            components.push_back(component_from_genie_knobs(spectrum));
            components.push_back(component_from_family(spectrum.genie, spectrum.nominal, nbins, "genie"));
            components.push_back(component_from_family(spectrum.flux, spectrum.nominal, nbins, "flux"));
            components.push_back(component_from_family(spectrum.reint, spectrum.nominal, nbins, "reint"));
        }

        std::vector<std::string> included_components;
        std::vector<std::string> approximate_components;
        write_component_outputs(output,
                                options,
                                nominal,
                                components,
                                included_components,
                                approximate_components);

        output.Write();
        output.Close();

        if (options.stacked_mode())
        {
            std::cout << "mk_sbnfit_cov: wrote stacked export " << options.output_path
                      << " from " << options.input_path
                      << " manifest " << options.manifest_path
                      << " as " << options.matrix_name
                      << " with " << nominal.size() << " stacked bins\n";
        }
        else
        {
            std::cout << "mk_sbnfit_cov: wrote " << options.output_path
                      << " from " << options.input_path
                      << " sample " << options.sample_key
                      << " cache " << entries.front().cache_key
                      << " as " << options.matrix_name << "\n";
        }

        if (!included_components.empty())
        {
            std::cout << "mk_sbnfit_cov: included components:";
            for (const auto &label : included_components)
                std::cout << " " << label;
            std::cout << "\n";
        }
        else
        {
            std::cout << "mk_sbnfit_cov: no systematic covariance payloads were present; wrote zero covariance matrices\n";
        }
        if (!approximate_components.empty())
        {
            std::cout << "mk_sbnfit_cov: diagonal-only fallback used for:";
            for (const auto &label : approximate_components)
                std::cout << " " << label;
            std::cout << "\n";
        }

        return 0;
    }
    catch (const std::exception &e)
    {
        if (*e.what() != '\0')
            std::cerr << e.what() << "\n";
        return (*e.what() == '\0') ? 0 : 1;
    }
}
