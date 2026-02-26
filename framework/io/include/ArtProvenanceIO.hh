#ifndef ART_PROVENANCE_IO_HH
#define ART_PROVENANCE_IO_HH

#include <string>
#include <utility>
#include <vector>

class ArtProvenanceIO
{
public:
    static std::vector<std::string> read_sample_list(const std::string &path);

    void scan_subruns(const std::vector<std::string> &files);

    const std::vector<std::pair<int, int>> &run_subruns() const { return run_subruns_; }
    double subrun_pot_sum() const { return subrun_pot_sum_; }
    long long n_entries() const { return n_entries_; }

private:
    std::vector<std::pair<int, int>> run_subruns_;
    double subrun_pot_sum_ = 0.0;
    long long n_entries_ = 0;
};

#endif // ART_PROVENANCE_IO_HH
