#ifndef SHARD_IO_HH
#define SHARD_IO_HH

#include <string>
#include <utility>
#include <vector>

class TDirectory;

class ShardIO
{
public:
    struct RunSubrunExposure
    {
        int run = 0;
        int subrun = 0;
        double generated_exposure = 0.0;
    };

public:
    ShardIO() = default;
    explicit ShardIO(const std::string &list_path,
                     const std::string &shard = "");

    void write(TDirectory *d) const;
    static ShardIO read(TDirectory *d);

    void scan(const std::vector<std::string> &files);

    const std::string &list_path() const { return list_path_; }
    const std::string &shard() const { return shard_; }
    const std::vector<std::string> &files() const { return files_; }
    const std::vector<std::pair<int, int>> &subruns() const { return subruns_; }
    const std::vector<RunSubrunExposure> &generated_exposures() const { return generated_exposures_; }

    double subrun_pot_sum() const { return pot_sum_; }
    long long entries() const { return entries_; }

private:
    static std::vector<std::string> read_list(const std::string &path);

    std::string list_path_;
    std::string shard_;
    std::vector<std::string> files_;
    std::vector<std::pair<int, int>> subruns_;
    std::vector<RunSubrunExposure> generated_exposures_;

    double pot_sum_ = 0.0;
    long long entries_ = 0;
};

#endif // SHARD_IO_HH
