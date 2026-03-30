#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ChannelIO.hh"
#include "DistributionIO.hh"

namespace
{
    std::string pick_cache_key(const DistributionIO &dist,
                               const std::string &sample_key,
                               const char *cache_key)
    {
        if (cache_key && *cache_key)
        {
            if (!dist.has(sample_key, cache_key))
                throw std::runtime_error("write_channel: cache_key not found for sample_key");
            return cache_key;
        }

        const auto keys = dist.dist_keys(sample_key);
        if (keys.empty())
            throw std::runtime_error("write_channel: no cached distributions found for sample_key");
        return keys.front();
    }

    std::vector<double> parse_bins_csv(const char *text,
                                       int expected_nbins,
                                       bool allow_zero_data)
    {
        if (!text || !*text)
        {
            if (!allow_zero_data)
                throw std::runtime_error("write_channel: data_bins_csv is required unless allow_zero_data is true");
            return expected_nbins > 0 ? std::vector<double>(static_cast<std::size_t>(expected_nbins), 0.0)
                                      : std::vector<double>{};
        }

        std::vector<double> out;
        std::stringstream ss(text);
        std::string token;
        while (std::getline(ss, token, ','))
        {
            if (token.empty())
                continue;
            out.push_back(std::stod(token));
        }

        if (expected_nbins > 0 && static_cast<int>(out.size()) != expected_nbins)
            throw std::runtime_error("write_channel: data_bins_csv size does not match cached histogram binning");
        return out;
    }

    void require_matching_inputs(const DistributionIO::Entry &signal,
                                 const DistributionIO::Entry &background)
    {
        if (signal.spec.nbins != background.spec.nbins ||
            signal.spec.xmin != background.spec.xmin ||
            signal.spec.xmax != background.spec.xmax)
        {
            throw std::runtime_error("write_channel: signal and background caches do not share binning");
        }

        if (signal.spec.branch_expr != background.spec.branch_expr)
            throw std::runtime_error("write_channel: signal and background caches do not share branch_expr");
        if (signal.spec.selection_expr != background.spec.selection_expr)
            throw std::runtime_error("write_channel: signal and background caches do not share selection_expr");
    }

    std::string resolve_selection_expr(const DistributionIO::Entry &signal,
                                       const DistributionIO::Entry &background,
                                       const char *selection_expr)
    {
        require_matching_inputs(signal, background);

        const std::string cached = signal.spec.selection_expr;
        const std::string requested =
            (selection_expr && *selection_expr) ? std::string(selection_expr) : std::string();
        if (requested.empty())
            return cached;
        if (cached.empty())
            return requested;
        if (requested != cached)
            throw std::runtime_error("write_channel: selection_expr does not match cached distributions");
        return cached;
    }
}

void write_channel(const char *dist_path = "output.dists.root",
                   const char *chan_path = "output.channels.root",
                   const char *signal_sample_key = "beam-s0",
                   const char *background_sample_key = "beam-bkg",
                   const char *channel_key = "muon_region",
                   const char *selection_expr = "__pass_muon__",
                   const char *data_bins_csv = nullptr,
                   const char *signal_cache_key = nullptr,
                   const char *background_cache_key = nullptr,
                   bool allow_zero_data = false)
{
    macro_utils::run_macro("write_channel", [&]() {
        if (!dist_path || !*dist_path)
            throw std::runtime_error("dist_path is required");
        if (!chan_path || !*chan_path)
            throw std::runtime_error("chan_path is required");
        if (!signal_sample_key || !*signal_sample_key)
            throw std::runtime_error("signal_sample_key is required");
        if (!background_sample_key || !*background_sample_key)
            throw std::runtime_error("background_sample_key is required");
        if (!channel_key || !*channel_key)
            throw std::runtime_error("channel_key is required");
        if (!selection_expr || !*selection_expr)
            throw std::runtime_error("selection_expr is required");

        DistributionIO dist(dist_path, DistributionIO::Mode::kRead);
        ChannelIO chio(chan_path, ChannelIO::Mode::kUpdate);
        chio.write_metadata({dist_path, 1});

        const auto sig = dist.read(signal_sample_key,
                                   pick_cache_key(dist, signal_sample_key, signal_cache_key));
        const auto bkg = dist.read(background_sample_key,
                                   pick_cache_key(dist, background_sample_key, background_cache_key));

        ChannelIO::Channel channel;
        channel.spec.channel_key = channel_key;
        channel.spec.branch_expr = sig.spec.branch_expr;
        channel.spec.selection_expr =
            resolve_selection_expr(sig, bkg, selection_expr);
        channel.spec.nbins = sig.spec.nbins;
        channel.spec.xmin = sig.spec.xmin;
        channel.spec.xmax = sig.spec.xmax;
        channel.data = parse_bins_csv(data_bins_csv, sig.spec.nbins, allow_zero_data);

        ChannelIO::Process signal;
        signal.name = "signal";
        signal.kind = ChannelIO::ProcessKind::kSignal;
        signal.source_keys = {sig.spec.sample_key};
        signal.detector_sample_keys = sig.detector_sample_keys;
        signal.nominal = sig.nominal;
        signal.sumw2 = sig.sumw2;
        signal.detector_down = sig.detector_down;
        signal.detector_up = sig.detector_up;
        signal.detector_templates = sig.detector_templates;
        signal.detector_template_count = sig.detector_template_count;
        signal.genie = sig.genie;
        signal.flux = sig.flux;
        signal.reint = sig.reint;
        signal.total_down = sig.total_down;
        signal.total_up = sig.total_up;

        ChannelIO::Process background;
        background.name = "background";
        background.kind = ChannelIO::ProcessKind::kBackground;
        background.source_keys = {bkg.spec.sample_key};
        background.detector_sample_keys = bkg.detector_sample_keys;
        background.nominal = bkg.nominal;
        background.sumw2 = bkg.sumw2;
        background.detector_down = bkg.detector_down;
        background.detector_up = bkg.detector_up;
        background.detector_templates = bkg.detector_templates;
        background.detector_template_count = bkg.detector_template_count;
        background.genie = bkg.genie;
        background.flux = bkg.flux;
        background.reint = bkg.reint;
        background.total_down = bkg.total_down;
        background.total_up = bkg.total_up;

        channel.processes.push_back(std::move(signal));
        channel.processes.push_back(std::move(background));

        chio.write(channel.spec.channel_key, channel);
        chio.flush();
    });
}
