#ifndef ART_PROVENANCE_IO_HH
#define ART_PROVENANCE_IO_HH

#include <string>
#include <utility>
#include <vector>

class ArtProvenanceIO
{
public:
    ArtProvenanceIO() = default;
    explicit ArtProvenanceIO(const std::string &sample_list_path);

    void scan_subruns(const std::vector<std::string> &files);
    const std::vector<std::string> &sample_files() const { return sample_files_; }

    const std::vector<std::pair<int, int>> &run_subruns() const { return run_subruns_; }
    double subrun_pot_sum() const { return subrun_pot_sum_; }
    long long n_entries() const { return n_entries_; }

private:
    static std::vector<std::string> read_sample_list(const std::string &path);

    std::vector<std::string> sample_files_;
    std::vector<std::pair<int, int>> run_subruns_;
    double subrun_pot_sum_ = 0.0;
    long long n_entries_ = 0;
};

#endif // ART_PROVENANCE_IO_HH
