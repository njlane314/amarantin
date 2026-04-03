#include "SignalStrengthFit.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RooAbsData.h"
#include "RooAbsPdf.h"
#include "RooAbsReal.h"
#include "RooArgList.h"
#include "RooArgSet.h"
#include "RooDataSet.h"
#include "RooFit.h"
#include "RooFitResult.h"
#include "RooMinimizer.h"
#include "RooRealVar.h"
#include "RooStats/HistFactory/HistFactoryNavigation.h"
#include "RooStats/HistFactory/MakeModelAndMeasurementsFast.h"
#include "RooStats/ModelConfig.h"
#include "TH1D.h"
#include "TIterator.h"
#include "TMatrixDSym.h"

namespace
{
    constexpr double kTemplateFloor = 1e-9;
    constexpr double kShiftTolerance = 1e-12;

    struct VarState
    {
        RooRealVar *var = nullptr;
        double value = 0.0;
        bool constant = false;
    };

    double positive_error_lo(double value)
    {
        return (value < 0.0) ? -value : value;
    }

    double positive_error_hi(double value)
    {
        return (value > 0.0) ? value : -value;
    }

    bool has_detector_templates(const fit::Process &process)
    {
        return process.detector_template_count > 0 &&
               !process.detector_templates.empty();
    }

    bool has_detector_shift_sources(const fit::Process &process)
    {
        return process.detector_source_count > 0 &&
               !process.detector_shift_vectors.empty();
    }

    bool has_genie_knob_shift_sources(const fit::Process &process)
    {
        return process.genie_knob_source_count > 0 &&
               !process.genie_knob_shift_vectors.empty();
    }

    bool has_detector_envelope(const fit::Process &process)
    {
        return !process.detector_down.empty() &&
               !process.detector_up.empty();
    }

    bool has_total_envelope(const fit::Process &process)
    {
        return !process.total_down.empty() &&
               !process.total_up.empty();
    }

    int family_mode_count(const fit::Family &family)
    {
        if (family.eigen_rank > 0 && !family.eigenmodes.empty())
            return family.eigen_rank;
        if (!family.sigma.empty())
            return 1;
        return 0;
    }

    double family_mode_value(const fit::Family &family,
                             int mode_index,
                             int bin)
    {
        if (mode_index < 0)
            throw std::runtime_error("fit: mode_index must not be negative");

        if (family.eigen_rank > 0 && !family.eigenmodes.empty())
        {
            if (mode_index >= family.eigen_rank)
                throw std::runtime_error("fit: family mode_index out of range");
            const std::size_t index =
                static_cast<std::size_t>(bin * family.eigen_rank + mode_index);
            if (index >= family.eigenmodes.size())
                throw std::runtime_error("fit: family eigenmode payload is truncated");
            return family.eigenmodes[index];
        }

        if (!family.sigma.empty())
        {
            if (mode_index != 0)
                throw std::runtime_error("fit: sigma-only family exposes only one mode");
            if (static_cast<std::size_t>(bin) >= family.sigma.size())
                throw std::runtime_error("fit: family sigma payload is truncated");
            return family.sigma[static_cast<std::size_t>(bin)];
        }

        throw std::runtime_error("fit: family has no usable fit payload");
    }

    std::string mode_name(const char *label,
                          const fit::Family &family,
                          int mode_index)
    {
        std::string name(label);
        if (!family.branch_name.empty())
        {
            name += ":";
            name += family.branch_name;
        }
        name += ":mode";
        name += std::to_string(mode_index);
        return name;
    }

    std::string detector_source_name(const fit::Process &process, int source_index)
    {
        if (source_index >= 0 &&
            static_cast<std::size_t>(source_index) < process.detector_source_labels.size() &&
            !process.detector_source_labels[static_cast<std::size_t>(source_index)].empty())
        {
            return "detector:" + process.detector_source_labels[static_cast<std::size_t>(source_index)];
        }

        if (source_index >= 0 &&
            static_cast<std::size_t>(source_index) < process.detector_sample_keys.size() &&
            !process.detector_sample_keys[static_cast<std::size_t>(source_index)].empty())
        {
            return "detector:" + process.detector_sample_keys[static_cast<std::size_t>(source_index)];
        }

        return "detector:" + process.name + ":source" + std::to_string(source_index);
    }

    std::string detector_template_name(const fit::Process &process, int template_index)
    {
        return detector_source_name(process, template_index);
    }

    std::string genie_knob_source_name(const fit::Process &process, int source_index)
    {
        if (source_index >= 0 &&
            static_cast<std::size_t>(source_index) < process.genie_knob_source_labels.size() &&
            !process.genie_knob_source_labels[static_cast<std::size_t>(source_index)].empty())
        {
            return "genie_knob:" + process.genie_knob_source_labels[static_cast<std::size_t>(source_index)];
        }

        return "genie_knob:" + process.name + ":source" + std::to_string(source_index);
    }

    std::string detector_group_name(const fit::Process &process)
    {
        std::string name = "detector";
        bool first = true;
        for (const auto &key : process.detector_sample_keys)
        {
            if (key.empty())
                continue;
            name += first ? ":" : "+";
            name += key;
            first = false;
        }
        if (first)
        {
            name += ":";
            name += process.name;
        }
        return name;
    }

    bool process_has_family_payload(const fit::Process &process)
    {
        return family_mode_count(process.genie) > 0 ||
               family_mode_count(process.flux) > 0 ||
               family_mode_count(process.reint) > 0;
    }

    bool process_has_detector_payload(const fit::Process &process)
    {
        return has_detector_shift_sources(process) ||
               has_detector_templates(process) ||
               has_detector_envelope(process);
    }

    bool process_has_genie_knob_payload(const fit::Process &process)
    {
        return has_genie_knob_shift_sources(process);
    }

    bool channel_name_is_valid(const std::string &channel_key)
    {
        return !channel_key.empty() &&
               std::isdigit(static_cast<unsigned char>(channel_key.front())) == 0 &&
               channel_key.find(',') == std::string::npos;
    }

    void validate_family(const fit::Family &family,
                         int nbins,
                         const std::string &label)
    {
        if (!family.sigma.empty() &&
            static_cast<int>(family.sigma.size()) != nbins)
        {
            throw std::runtime_error("fit: " + label + " sigma size does not match channel bin count");
        }

        if (family.eigen_rank < 0)
            throw std::runtime_error("fit: " + label + " eigen_rank must not be negative");

        if (!family.eigenmodes.empty())
        {
            const std::size_t expected =
                static_cast<std::size_t>(family.eigen_rank * nbins);
            if (family.eigen_rank == 0 || family.eigenmodes.size() != expected)
            {
                throw std::runtime_error("fit: " + label + " eigenmode payload is truncated");
            }
        }
    }

    void validate_problem(const fit::Problem &problem)
    {
        if (problem.channels.empty())
            throw std::runtime_error("fit: at least one fit channel is required");
        if (problem.poi_name.empty())
            throw std::runtime_error("fit: poi_name must not be empty");
        if (problem.mu_lower > problem.mu_upper)
            throw std::runtime_error("fit: mu bounds are inverted");
        if (problem.mu_start < problem.mu_lower || problem.mu_start > problem.mu_upper)
            throw std::runtime_error("fit: mu_start lies outside the configured bounds");

        bool have_signal = false;

        for (const auto &channel : problem.channels)
        {
            if (!channel_name_is_valid(channel.spec.channel_key))
            {
                throw std::runtime_error(
                    "fit: channel names must be non-empty, must not start with a digit, and must not contain ','");
            }
            if (channel.spec.nbins <= 0)
                throw std::runtime_error("fit: channel nbins must be positive");
            if (!(channel.spec.xmax > channel.spec.xmin))
                throw std::runtime_error("fit: channel histogram range is invalid");
            if (static_cast<int>(channel.data.size()) != channel.spec.nbins)
                throw std::runtime_error("fit: observed data size does not match channel bin count");
            if (channel.processes.empty())
                throw std::runtime_error("fit: every channel must contain at least one process");

            for (double value : channel.data)
            {
                if (!std::isfinite(value) || value < 0.0)
                    throw std::runtime_error("fit: observed data bins must be finite and non-negative");
            }

            for (const auto &process : channel.processes)
            {
                if (process.name.empty())
                    throw std::runtime_error("fit: process name must not be empty");
                if (process.kind == fit::ProcessKind::kData)
                    throw std::runtime_error("fit: data rows must not be stored as fit processes");
                if (static_cast<int>(process.nominal.size()) != channel.spec.nbins)
                    throw std::runtime_error("fit: process nominal size does not match channel bin count");
                if (!process.sumw2.empty() &&
                    static_cast<int>(process.sumw2.size()) != channel.spec.nbins)
                {
                    throw std::runtime_error("fit: process sumw2 size does not match channel bin count");
                }
                if (!process.detector_shift_vectors.empty())
                {
                    const std::size_t expected =
                        static_cast<std::size_t>(process.detector_source_count * channel.spec.nbins);
                    if (process.detector_source_count <= 0 ||
                        process.detector_shift_vectors.size() != expected)
                    {
                        throw std::runtime_error("fit: detector shift payload is truncated");
                    }
                }
                if (!process.genie_knob_shift_vectors.empty())
                {
                    const std::size_t expected =
                        static_cast<std::size_t>(process.genie_knob_source_count * channel.spec.nbins);
                    if (process.genie_knob_source_count <= 0 ||
                        process.genie_knob_shift_vectors.size() != expected)
                    {
                        throw std::runtime_error("fit: GENIE knob shift payload is truncated");
                    }
                }
                if (!process.detector_templates.empty())
                {
                    const std::size_t expected =
                        static_cast<std::size_t>(process.detector_template_count * channel.spec.nbins);
                    if (process.detector_template_count <= 0 ||
                        process.detector_templates.size() != expected)
                    {
                        throw std::runtime_error("fit: detector template payload is truncated");
                    }
                }
                if ((!process.detector_down.empty() || !process.detector_up.empty()) &&
                    (static_cast<int>(process.detector_down.size()) != channel.spec.nbins ||
                     static_cast<int>(process.detector_up.size()) != channel.spec.nbins))
                {
                    throw std::runtime_error("fit: detector envelope size does not match channel bin count");
                }
                if ((!process.total_down.empty() || !process.total_up.empty()) &&
                    (static_cast<int>(process.total_down.size()) != channel.spec.nbins ||
                     static_cast<int>(process.total_up.size()) != channel.spec.nbins))
                {
                    throw std::runtime_error("fit: total envelope size does not match channel bin count");
                }

                validate_family(process.genie, channel.spec.nbins, "genie");
                validate_family(process.flux, channel.spec.nbins, "flux");
                validate_family(process.reint, channel.spec.nbins, "reint");

                for (double value : process.nominal)
                {
                    if (!std::isfinite(value))
                        throw std::runtime_error("fit: process nominal bins must be finite");
                }

                if (process.kind == fit::ProcessKind::kSignal)
                    have_signal = true;
            }
        }

        if (!have_signal)
            throw std::runtime_error("fit: at least one signal process is required");
    }

    double floor_template_bin(double value)
    {
        if (!std::isfinite(value))
            throw std::runtime_error("fit: non-finite template bin");
        return std::max(kTemplateFloor, value);
    }

    std::unique_ptr<TH1D> make_hist(const std::string &name,
                                    const fit::Channel &channel,
                                    const std::vector<double> &values,
                                    const std::vector<double> *sumw2 = nullptr,
                                    bool floor_bins = true)
    {
        auto hist = std::make_unique<TH1D>(name.c_str(),
                                           name.c_str(),
                                           channel.spec.nbins,
                                           channel.spec.xmin,
                                           channel.spec.xmax);
        hist->SetDirectory(nullptr);

        for (int bin = 0; bin < channel.spec.nbins; ++bin)
        {
            const std::size_t index = static_cast<std::size_t>(bin);
            const double content = floor_bins ? floor_template_bin(values.at(index))
                                              : values.at(index);
            hist->SetBinContent(bin + 1, content);

            double error = 0.0;
            if (sumw2 && sumw2->size() == values.size())
                error = std::sqrt(std::max(0.0, sumw2->at(index)));
            hist->SetBinError(bin + 1, error);
        }

        return hist;
    }

    std::vector<double> hist_contents(const TH1 &hist, int expected_bins)
    {
        std::vector<double> out(static_cast<std::size_t>(expected_bins), 0.0);
        for (int bin = 0; bin < expected_bins; ++bin)
            out[static_cast<std::size_t>(bin)] = hist.GetBinContent(bin + 1);
        return out;
    }

    bool has_nonzero_shift(const std::vector<double> &shift)
    {
        for (double value : shift)
        {
            if (std::fabs(value) > kShiftTolerance)
                return true;
        }
        return false;
    }

    bool differs_from_nominal(const std::vector<double> &nominal,
                              const std::vector<double> &other)
    {
        if (nominal.size() != other.size())
            return true;
        for (std::size_t i = 0; i < nominal.size(); ++i)
        {
            if (std::fabs(nominal[i] - other[i]) > kShiftTolerance)
                return true;
        }
        return false;
    }

    void add_histo_sys(RooStats::HistFactory::Sample &sample,
                       const std::string &name,
                       std::unique_ptr<TH1D> low,
                       std::unique_ptr<TH1D> high)
    {
        RooStats::HistFactory::HistoSys syst;
        syst.SetName(name);
        syst.SetHistoLow(low.release());
        syst.SetHistoHigh(high.release());
        sample.AddHistoSys(syst);
    }

    void add_symmetric_shift_systematic(RooStats::HistFactory::Sample &sample,
                                        const fit::Channel &channel,
                                        const fit::Process &process,
                                        const std::string &name,
                                        const std::vector<double> &shift)
    {
        if (!has_nonzero_shift(shift))
            return;

        std::vector<double> low(process.nominal.size(), 0.0);
        std::vector<double> high(process.nominal.size(), 0.0);
        for (std::size_t bin = 0; bin < process.nominal.size(); ++bin)
        {
            low[bin] = process.nominal[bin] - shift[bin];
            high[bin] = process.nominal[bin] + shift[bin];
        }

        add_histo_sys(
            sample,
            name,
            make_hist(process.name + "_" + channel.spec.channel_key + "_" + name + "_down",
                      channel,
                      low),
            make_hist(process.name + "_" + channel.spec.channel_key + "_" + name + "_up",
                      channel,
                      high));
    }

    void add_envelope_systematic(RooStats::HistFactory::Sample &sample,
                                 const fit::Channel &channel,
                                 const fit::Process &process,
                                 const std::string &name,
                                 const std::vector<double> &down,
                                 const std::vector<double> &up)
    {
        if (!differs_from_nominal(process.nominal, down) &&
            !differs_from_nominal(process.nominal, up))
        {
            return;
        }

        add_histo_sys(
            sample,
            name,
            make_hist(process.name + "_" + channel.spec.channel_key + "_" + name + "_down",
                      channel,
                      down),
            make_hist(process.name + "_" + channel.spec.channel_key + "_" + name + "_up",
                      channel,
                      up));
    }

    std::vector<double> source_major_slice(const std::vector<double> &payload,
                                           int row_index,
                                           int nbins)
    {
        std::vector<double> out(static_cast<std::size_t>(nbins), 0.0);
        for (int bin = 0; bin < nbins; ++bin)
        {
            out[static_cast<std::size_t>(bin)] =
                payload[static_cast<std::size_t>(row_index * nbins + bin)];
        }
        return out;
    }

    void add_process_systematics(RooStats::HistFactory::Sample &sample,
                                 const fit::Channel &channel,
                                 const fit::Process &process)
    {
        const int nbins = channel.spec.nbins;

        const std::pair<const char *, const fit::Family *> families[] = {
            {"genie", &process.genie},
            {"flux", &process.flux},
            {"reint", &process.reint},
        };

        for (const auto &entry : families)
        {
            const int mode_count = family_mode_count(*entry.second);
            for (int mode = 0; mode < mode_count; ++mode)
            {
                std::vector<double> shift(static_cast<std::size_t>(nbins), 0.0);
                for (int bin = 0; bin < nbins; ++bin)
                    shift[static_cast<std::size_t>(bin)] =
                        family_mode_value(*entry.second, mode, bin);
                add_symmetric_shift_systematic(sample,
                                               channel,
                                               process,
                                               mode_name(entry.first, *entry.second, mode),
                                               shift);
            }
        }

        if (has_genie_knob_shift_sources(process))
        {
            for (int row = 0; row < process.genie_knob_source_count; ++row)
            {
                add_symmetric_shift_systematic(sample,
                                               channel,
                                               process,
                                               genie_knob_source_name(process, row),
                                               source_major_slice(process.genie_knob_shift_vectors,
                                                                  row,
                                                                  nbins));
            }
        }

        if (has_detector_shift_sources(process))
        {
            for (int row = 0; row < process.detector_source_count; ++row)
            {
                add_symmetric_shift_systematic(sample,
                                               channel,
                                               process,
                                               detector_source_name(process, row),
                                               source_major_slice(process.detector_shift_vectors,
                                                                  row,
                                                                  nbins));
            }
        }
        else if (has_detector_templates(process))
        {
            for (int row = 0; row < process.detector_template_count; ++row)
            {
                const std::vector<double> varied =
                    source_major_slice(process.detector_templates, row, nbins);
                std::vector<double> shift(static_cast<std::size_t>(nbins), 0.0);
                for (int bin = 0; bin < nbins; ++bin)
                {
                    shift[static_cast<std::size_t>(bin)] =
                        varied[static_cast<std::size_t>(bin)] -
                        process.nominal[static_cast<std::size_t>(bin)];
                }
                add_symmetric_shift_systematic(sample,
                                               channel,
                                               process,
                                               detector_template_name(process, row),
                                               shift);
            }
        }
        else if (has_detector_envelope(process))
        {
            add_envelope_systematic(sample,
                                    channel,
                                    process,
                                    detector_group_name(process),
                                    process.detector_down,
                                    process.detector_up);
        }

        if (!process_has_family_payload(process) &&
            !process_has_genie_knob_payload(process) &&
            !process_has_detector_payload(process) &&
            has_total_envelope(process))
        {
            add_envelope_systematic(sample,
                                    channel,
                                    process,
                                    "total:" + process.name,
                                    process.total_down,
                                    process.total_up);
        }
    }

    bool process_has_mc_stat(const fit::Process &process)
    {
        if (process.sumw2.empty())
            return false;
        for (double value : process.sumw2)
        {
            if (value > 0.0)
                return true;
        }
        return false;
    }

    RooStats::HistFactory::Measurement build_measurement(const fit::Problem &problem)
    {
        const std::string measurement_name =
            problem.measurement_name.empty() ? std::string("measurement")
                                             : problem.measurement_name;
        RooStats::HistFactory::Measurement measurement(measurement_name.c_str(),
                                                       measurement_name.c_str());
        measurement.SetPOI(problem.poi_name);
        measurement.SetLumi(problem.lumi);
        measurement.SetLumiRelErr(problem.lumi_rel_error);

        for (const auto &param : problem.constant_params)
            measurement.AddConstantParam(param);

        for (const auto &channel : problem.channels)
        {
            RooStats::HistFactory::Channel hf_channel(channel.spec.channel_key);
            hf_channel.SetData(
                make_hist(channel.spec.channel_key + "_data",
                          channel,
                          channel.data,
                          nullptr,
                          false)
                    .release());
            hf_channel.SetStatErrorConfig(channel.stat_rel_threshold,
                                          channel.stat_constraint);

            for (const auto &process : channel.processes)
            {
                RooStats::HistFactory::Sample sample(process.name);
                sample.SetNormalizeByTheory(false);
                sample.SetHisto(
                    make_hist(process.name + "_" + channel.spec.channel_key + "_nominal",
                              channel,
                              process.nominal,
                              process.sumw2.empty() ? nullptr : &process.sumw2)
                        .release());

                if (process.kind == fit::ProcessKind::kSignal)
                {
                    sample.AddNormFactor(problem.poi_name,
                                         problem.mu_start,
                                         problem.mu_lower,
                                         problem.mu_upper);
                }

                if (process_has_mc_stat(process))
                    sample.ActivateStatError();

                add_process_systematics(sample, channel, process);
                hf_channel.AddSample(sample);
            }

            if (!hf_channel.CheckHistograms())
                throw std::runtime_error("fit: HistFactory channel histogram validation failed");

            measurement.AddChannel(hf_channel);
        }

        return measurement;
    }

    std::vector<VarState> capture_var_states(const RooArgSet &parameters)
    {
        std::vector<VarState> states;
        std::unique_ptr<TIterator> it{parameters.createIterator()};
        for (TObject *obj = it->Next(); obj != nullptr; obj = it->Next())
        {
            auto *var = dynamic_cast<RooRealVar *>(obj);
            if (!var)
                continue;
            states.push_back(VarState{var, var->getVal(), var->isConstant()});
        }
        return states;
    }

    void restore_var_states(const std::vector<VarState> &states)
    {
        for (const auto &state : states)
        {
            if (!state.var)
                continue;
            state.var->setVal(state.value);
            state.var->setConstant(state.constant);
        }
    }

    void set_all_constant(const RooArgSet &parameters, bool constant)
    {
        std::unique_ptr<TIterator> it{parameters.createIterator()};
        for (TObject *obj = it->Next(); obj != nullptr; obj = it->Next())
        {
            auto *var = dynamic_cast<RooRealVar *>(obj);
            if (var)
                var->setConstant(constant);
        }
    }

    std::unique_ptr<RooFitResult> run_fit(RooAbsPdf &pdf,
                                          RooAbsData &data,
                                          const RooArgSet &global_observables,
                                          const RooArgSet &poi_set,
                                          const fit::FitOptions &options)
    {
        using namespace RooFit;

        auto nll = pdf.createNLL(data,
                                 GlobalObservables(global_observables),
                                 Offset(true));
        if (!nll)
            throw std::runtime_error("fit: failed to create the likelihood");

        RooMinimizer minimizer(*nll);
        minimizer.setMaxIterations(options.max_iterations);
        minimizer.setMaxFunctionCalls(options.max_function_calls);
        minimizer.setStrategy(options.strategy);
        minimizer.setPrintLevel(options.print_level);
        minimizer.setEps(options.tolerance);
        minimizer.optimizeConst(2);

        minimizer.minimize("Minuit2", "Migrad");
        if (options.run_hesse)
            minimizer.hesse();
        minimizer.minos(poi_set);

        std::unique_ptr<RooFitResult> result(minimizer.save());
        if (!result)
            throw std::runtime_error("fit: MINOS returned no RooFitResult");
        return result;
    }

    std::vector<double> flatten_covariance(const RooFitResult &result)
    {
        const TMatrixDSym covariance = result.covarianceMatrix();
        std::vector<double> out;
        out.reserve(static_cast<std::size_t>(covariance.GetNrows() * covariance.GetNcols()));
        for (int row = 0; row < covariance.GetNrows(); ++row)
        {
            for (int col = 0; col < covariance.GetNcols(); ++col)
                out.push_back(covariance(row, col));
        }
        return out;
    }

    void fill_parameter_values(const RooFitResult &result, fit::Result &out)
    {
        RooArgList parameters = result.floatParsFinal();
        out.parameter_names.reserve(static_cast<std::size_t>(parameters.getSize()));
        out.parameter_values.reserve(static_cast<std::size_t>(parameters.getSize()));
        for (int i = 0; i < parameters.getSize(); ++i)
        {
            auto *var = dynamic_cast<RooRealVar *>(parameters.at(i));
            if (!var)
                continue;
            out.parameter_names.push_back(var->GetName());
            out.parameter_values.push_back(var->getVal());
        }
        out.covariance = flatten_covariance(result);
    }

    void fill_nuisance_values(const RooArgSet *nuisances, fit::Result &out)
    {
        if (!nuisances)
            return;

        std::unique_ptr<TIterator> it{nuisances->createIterator()};
        for (TObject *obj = it->Next(); obj != nullptr; obj = it->Next())
        {
            auto *var = dynamic_cast<RooRealVar *>(obj);
            if (!var)
                continue;
            out.nuisance_names.push_back(var->GetName());
            out.nuisance_values.push_back(var->getVal());
        }
    }

    void add_bins_in_place(std::vector<double> &target,
                           const std::vector<double> &source)
    {
        if (target.empty())
        {
            target = source;
            return;
        }

        if (target.size() != source.size())
            throw std::runtime_error("fit: predicted histogram bin counts do not match");

        for (std::size_t i = 0; i < target.size(); ++i)
            target[i] += source[i];
    }

    void fill_channel_predictions(const fit::Problem &problem,
                                  RooStats::ModelConfig &model_config,
                                  fit::Result &out)
    {
        RooStats::HistFactory::HistFactoryNavigation navigation(&model_config);

        out.channels.reserve(problem.channels.size());
        for (const auto &channel : problem.channels)
        {
            fit::ChannelResult channel_result;
            channel_result.channel_key = channel.spec.channel_key;
            channel_result.branch_expr = channel.spec.branch_expr;
            channel_result.selection_expr = channel.spec.selection_expr;
            channel_result.observed_source_keys = channel.data_source_keys;
            channel_result.observed = channel.data;

            std::unique_ptr<TH1> total_hist{
                navigation.GetChannelHist(channel.spec.channel_key)};
            if (!total_hist)
                throw std::runtime_error("fit: failed to evaluate the fitted channel prediction");
            channel_result.predicted_total = hist_contents(*total_hist,
                                                          channel.spec.nbins);

            for (const auto &process : channel.processes)
            {
                std::unique_ptr<TH1> sample_hist{
                    navigation.GetSampleHist(channel.spec.channel_key,
                                             process.name)};
                if (!sample_hist)
                    throw std::runtime_error("fit: failed to evaluate a fitted process prediction");

                const std::vector<double> bins = hist_contents(*sample_hist,
                                                               channel.spec.nbins);
                if (process.kind == fit::ProcessKind::kSignal)
                    add_bins_in_place(channel_result.predicted_signal, bins);
                else if (process.kind == fit::ProcessKind::kBackground)
                    add_bins_in_place(channel_result.predicted_background, bins);
            }

            if (channel_result.predicted_signal.empty())
                channel_result.predicted_signal.assign(static_cast<std::size_t>(channel.spec.nbins), 0.0);
            if (channel_result.predicted_background.empty())
                channel_result.predicted_background.assign(static_cast<std::size_t>(channel.spec.nbins), 0.0);

            out.channels.push_back(std::move(channel_result));
        }

        if (out.channels.size() == 1)
        {
            out.predicted_signal = out.channels.front().predicted_signal;
            out.predicted_background = out.channels.front().predicted_background;
            out.predicted_total = out.channels.front().predicted_total;
        }
    }
}

namespace fit
{
    const Process *Channel::find_process(const std::string &name) const
    {
        for (const auto &process : processes)
        {
            if (process.name == name)
                return &process;
        }
        return nullptr;
    }

    Process *Channel::find_process(const std::string &name)
    {
        for (auto &process : processes)
        {
            if (process.name == name)
                return &process;
        }
        return nullptr;
    }

    Problem make_independent_problem(const Channel &channel,
                                     double mu_start,
                                     double mu_upper)
    {
        return make_independent_problem(std::vector<Channel>{channel},
                                        mu_start,
                                        mu_upper);
    }

    Problem make_independent_problem(const std::vector<Channel> &channels,
                                     double mu_start,
                                     double mu_upper)
    {
        Problem problem;
        problem.channels = channels;
        problem.mu_start = mu_start;
        problem.mu_upper = mu_upper;
        return problem;
    }

    Result profile_signal_strength(const Problem &problem,
                                   const FitOptions &options)
    {
        validate_problem(problem);

        RooStats::HistFactory::Measurement measurement =
            build_measurement(problem);
        auto workspace =
            RooStats::HistFactory::MakeModelAndMeasurementFast(measurement);
        if (!workspace)
            throw std::runtime_error("fit: HistFactory returned no RooWorkspace");

        auto *model_config =
            dynamic_cast<RooStats::ModelConfig *>(workspace->obj("ModelConfig"));
        if (!model_config)
            throw std::runtime_error("fit: ModelConfig not found in workspace");

        RooAbsPdf *pdf = model_config->GetPdf();
        RooAbsData *data = workspace->data("obsData");
        auto *poi =
            dynamic_cast<RooRealVar *>(model_config->GetParametersOfInterest()->first());
        if (!pdf || !data || !poi)
            throw std::runtime_error("fit: workspace is missing the pdf, data, or POI");

        RooArgSet global_observables;
        if (model_config->GetGlobalObservables())
            global_observables.add(*model_config->GetGlobalObservables());

        RooArgSet poi_set(*poi);
        std::unique_ptr<RooFitResult> fit_total =
            run_fit(*pdf, *data, global_observables, poi_set, options);

        Result out;
        out.minimizer_status = fit_total->status();
        out.edm = fit_total->edm();
        out.objective = fit_total->minNll();
        out.mu_hat = poi->getVal();
        out.mu_err_total_down = positive_error_lo(poi->getErrorLo());
        out.mu_err_total_up = positive_error_hi(poi->getErrorHi());
        out.mu_err_total_down_found =
            std::isfinite(out.mu_err_total_down) && out.mu_err_total_down > 0.0;
        out.mu_err_total_up_found =
            std::isfinite(out.mu_err_total_up) && out.mu_err_total_up > 0.0;
        out.converged = (out.minimizer_status == 0);

        fill_parameter_values(*fit_total, out);
        fill_nuisance_values(model_config->GetNuisanceParameters(), out);

        RooArgSet model_parameters;
        if (model_config->GetObservables())
            pdf->getParameters(model_config->GetObservables(), model_parameters);
        else
            pdf->getParameters(data->get(), model_parameters);
        const std::vector<VarState> full_fit_states =
            capture_var_states(model_parameters);

        if (!options.compute_stat_only_interval)
        {
            out.minimizer_status_stat = -1;
        }
        else if (!model_config->GetNuisanceParameters() ||
                 model_config->GetNuisanceParameters()->getSize() == 0)
        {
            out.minimizer_status_stat = out.minimizer_status;
            out.mu_err_stat_down = out.mu_err_total_down;
            out.mu_err_stat_up = out.mu_err_total_up;
            out.mu_err_stat_down_found = out.mu_err_total_down_found;
            out.mu_err_stat_up_found = out.mu_err_total_up_found;
        }
        else
        {
            set_all_constant(*model_config->GetNuisanceParameters(), true);
            std::unique_ptr<RooFitResult> fit_stat;
            try
            {
                fit_stat = run_fit(*pdf, *data, global_observables, poi_set, options);
            }
            catch (...)
            {
                restore_var_states(full_fit_states);
                throw;
            }

            out.minimizer_status_stat = fit_stat->status();
            out.mu_err_stat_down = positive_error_lo(poi->getErrorLo());
            out.mu_err_stat_up = positive_error_hi(poi->getErrorHi());
            out.mu_err_stat_down_found =
                std::isfinite(out.mu_err_stat_down) && out.mu_err_stat_down > 0.0;
            out.mu_err_stat_up_found =
                std::isfinite(out.mu_err_stat_up) && out.mu_err_stat_up > 0.0;

            restore_var_states(full_fit_states);
        }

        out.mu_err_total_down = positive_error_lo(out.mu_err_total_down);
        out.mu_err_total_up = positive_error_hi(out.mu_err_total_up);
        out.mu_err_stat_down = positive_error_lo(out.mu_err_stat_down);
        out.mu_err_stat_up = positive_error_hi(out.mu_err_stat_up);

        fill_channel_predictions(problem, *model_config, out);
        return out;
    }
}
