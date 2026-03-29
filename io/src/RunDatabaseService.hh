#ifndef RUN_DATABASE_SERVICE_HH
#define RUN_DATABASE_SERVICE_HH

#include <string>
#include <utility>
#include <vector>

struct RunInfoSums
{
    long long n_pairs_loaded = 0;
    double tortgt_sum = 0.0;
    double tor101_sum = 0.0;
    double tor860_sum = 0.0;
    double tor875_sum = 0.0;
    long long EA9CNT_sum = 0;
    long long E1DCNT_sum = 0;
    long long EXTTrig_sum = 0;
    long long Gate1Trig_sum = 0;
    long long Gate2Trig_sum = 0;
};

class sqlite3;
struct sqlite3_stmt;

class RunDatabaseService
{
public:
    explicit RunDatabaseService(std::string path);
    ~RunDatabaseService();

    RunInfoSums sum_run_info(const std::vector<std::pair<int, int>> &pairs) const;

private:
    void exec(const std::string &sql) const;
    void prepare(const std::string &sql, sqlite3_stmt **stmt) const;

    std::string db_path_;
    sqlite3 *db_ = nullptr;
};

#endif // RUN_DATABASE_SERVICE_HH
