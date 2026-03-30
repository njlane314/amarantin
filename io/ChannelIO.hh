#ifndef CHANNEL_IO_HH
#define CHANNEL_IO_HH

#include <string>
#include <vector>

#include "DistributionIO.hh"

class TFile;

class ChannelIO
{
public:
    enum class Mode { kRead, kWrite, kUpdate };

    enum class ProcessKind
    {
        kData,
        kSignal,
        kBackground
    };

    struct Metadata
    {
        std::string distribution_path;
        int build_version = 1;
    };

    struct Spec
    {
        std::string channel_key;
        std::string branch_expr;
        std::string selection_expr;
        int nbins = 0;
        double xmin = 0.0;
        double xmax = 0.0;
    };

    using Family = DistributionIO::Family;

    struct Process
    {
        std::string name;
        ProcessKind kind = ProcessKind::kBackground;
        std::vector<std::string> source_keys;
        std::vector<std::string> detector_sample_keys;

        std::vector<double> nominal;
        std::vector<double> sumw2;

        std::vector<double> detector_down;
        std::vector<double> detector_up;
        std::vector<double> detector_templates;
        int detector_template_count = 0;

        Family genie;
        Family flux;
        Family reint;

        std::vector<double> total_down;
        std::vector<double> total_up;
    };

    struct Channel
    {
        Spec spec;
        std::vector<double> data;
        std::vector<Process> processes;

        const Process *find_process(const std::string &name) const;
        Process *find_process(const std::string &name);
    };

    explicit ChannelIO(const std::string &path, Mode mode = Mode::kRead);
    ~ChannelIO();

    ChannelIO(const ChannelIO &) = delete;
    ChannelIO &operator=(const ChannelIO &) = delete;

    const std::string &path() const { return path_; }
    Mode mode() const { return mode_; }

    Metadata metadata() const;
    void write_metadata(const Metadata &metadata);
    void flush();

    std::vector<std::string> channel_keys() const;
    std::vector<std::string> process_names(const std::string &channel_key) const;
    bool has(const std::string &channel_key) const;
    Channel read(const std::string &channel_key) const;
    void write(const std::string &channel_key, const Channel &channel);

    static const char *process_kind_name(ProcessKind kind);
    static ProcessKind process_kind_from(const std::string &name);

private:
    void require_open_() const;

    std::string path_;
    Mode mode_;
    TFile *file_ = nullptr;
};

#endif // CHANNEL_IO_HH
