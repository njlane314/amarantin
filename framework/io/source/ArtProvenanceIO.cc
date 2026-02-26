#include "ArtProvenanceIO.hh"

#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TChain.h>
#include <TFile.h>
#include <TTree.h>

std::vector<std::string> ArtProvenanceIO::read_sample_list(const std::string &path)
{
    std::ifstream in(path);
    if (!in) throw std::runtime_error("ArtProvenanceIO: failed to open sample list: " + path);

    std::vector<std::string> out;
    std::string line;
    while (std::getline(in, line))
    {
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c) { return c == '\r' || c == '\n'; }),
                   line.end());
        if (line.empty()) continue;
        out.push_back(line);
    }

    return out;
}

void ArtProvenanceIO::scan_subruns(const std::vector<std::string> &files)
{
    if (files.empty()) throw std::runtime_error("ArtProvenanceIO: no input files provided for subrun scan.");

    run_subruns_.clear();
    subrun_pot_sum_ = 0.0;
    n_entries_ = 0;

    const std::vector<std::string> candidates = {"nuselection/SubRun", "SubRun"};
    std::string tree_path;

    for (const auto &f : files)
    {
        std::unique_ptr<TFile> file(TFile::Open(f.c_str(), "READ"));
        if (!file || file->IsZombie())
            throw std::runtime_error("Failed to open input ROOT file: " + f);

        for (const auto &name : candidates)
        {
            if (dynamic_cast<TTree *>(file->Get(name.c_str())))
            {
                tree_path = name;
                break;
            }
        }

        if (!tree_path.empty()) break;
    }

    if (tree_path.empty())
        throw std::runtime_error("No input files contained a SubRun tree.");

    TChain chain(tree_path.c_str());
    for (const auto &f : files)
        chain.Add(f.c_str());

    if (!chain.GetBranch("run") || !chain.GetBranch("subRun") || !chain.GetBranch("pot"))
        throw std::runtime_error("SubRun tree missing required branches (run, subRun, pot).");

    Int_t run = 0;
    Int_t subRun = 0;
    Double_t pot = 0.0;

    chain.SetBranchAddress("run", &run);
    chain.SetBranchAddress("subRun", &subRun);
    chain.SetBranchAddress("pot", &pot);

    const Long64_t n = chain.GetEntries();
    n_entries_ = static_cast<long long>(n);

    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(static_cast<size_t>(n));

    for (Long64_t i = 0; i < n; ++i)
    {
        chain.GetEntry(i);
        subrun_pot_sum_ += static_cast<double>(pot);
        pairs.emplace_back(static_cast<int>(run), static_cast<int>(subRun));
    }

    std::sort(pairs.begin(), pairs.end(),
              [](const std::pair<int, int> &a, const std::pair<int, int> &b)
              {
                  if (a.first != b.first) return a.first < b.first;
                  return a.second < b.second;
              });

    pairs.erase(std::unique(pairs.begin(), pairs.end(),
                            [](const std::pair<int, int> &a, const std::pair<int, int> &b)
                            {
                                return a.first == b.first && a.second == b.second;
                            }),
                pairs.end());

    run_subruns_ = std::move(pairs);
}
