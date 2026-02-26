#include "ArtProvenanceIO.hh"
#include "RootUtils.hh"

#include <algorithm>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <TChain.h>
#include <TDirectory.h>
#include <TFile.h>
#include <TObject.h>
#include <TTree.h>

ArtProvenanceIO::ArtProvenanceIO(const std::string &input_path)
{
    input_files_ = read_sample_list(input_path);
    if (!input_files_.empty()) scan_subruns(input_files_);
}

ArtProvenanceIO::ArtProvenanceIO(std::vector<std::string> files)
    : input_files_(std::move(files))
{
    if (!input_files_.empty()) scan_subruns(input_files_);
}

void ArtProvenanceIO::write(TDirectory *d) const
{
    if (!d) throw std::runtime_error("ArtProvenanceIO::write: null directory");
    d->cd();

    rootu::write_param<double>(d, "pot_sum", subrun_pot_sum());
    rootu::write_param<long long>(d, "entries", n_events());

    {
        TTree files_t("root_files", "");
        std::string root_file;
        files_t.Branch("root_file", &root_file);
        for (const auto &path : sample_files())
        {
            root_file = path;
            files_t.Fill();
        }
        files_t.Write("root_files", TObject::kOverwrite);
    }

    {
        TTree rs("run_subrun", "");
        Int_t run = 0;
        Int_t subrun = 0;
        rs.Branch("run", &run, "run/I");
        rs.Branch("subrun", &subrun, "subrun/I");
        for (const auto &pair : run_subruns())
        {
            run = pair.first;
            subrun = pair.second;
            rs.Fill();
        }
        rs.Write("run_subrun", TObject::kOverwrite);
    }
}

ArtProvenanceIO ArtProvenanceIO::read(TDirectory *d)
{
    if (!d) throw std::runtime_error("ArtProvenanceIO::read: null directory");

    ArtProvenanceIO out;
    out.pot_sum_ = rootu::read_param<double>(d, "pot_sum");
    out.n_events_ = rootu::read_param<long long>(d, "entries");

    {
        auto *t = dynamic_cast<TTree *>(d->Get("root_files"));
        if (!t) throw std::runtime_error("ArtProvenanceIO: missing root_files tree in part/" + std::string(d->GetName()));

        std::string *root_file = nullptr;
        t->SetBranchAddress("root_file", &root_file);

        const Long64_t n = t->GetEntries();
        out.input_files_.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            if (!root_file) throw std::runtime_error("ArtProvenanceIO: root_files missing root_file in part/" + std::string(d->GetName()));
            out.input_files_.push_back(*root_file);
        }
    }

    {
        auto *t = dynamic_cast<TTree *>(d->Get("run_subrun"));
        if (!t) throw std::runtime_error("ArtProvenanceIO: missing run_subrun tree in part/" + std::string(d->GetName()));

        Int_t run = 0;
        Int_t subrun = 0;
        t->SetBranchAddress("run", &run);
        t->SetBranchAddress("subrun", &subrun);

        const Long64_t n = t->GetEntries();
        out.run_subruns_.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            out.run_subruns_.emplace_back(static_cast<int>(run), static_cast<int>(subrun));
        }
    }

    return out;
}

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

    pot_sum_ = 0.0;
    n_events_ = 0;

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
        throw std::runtime_error("SubRun tree missing required branches (run, subrun, pot).");

    Int_t run = 0;
    Int_t subrun = 0;
    Double_t pot = 0.0;

    chain.SetBranchAddress("run", &run);
    chain.SetBranchAddress("subRun", &subrun);
    chain.SetBranchAddress("pot", &pot);

    const Long64_t n = chain.GetEntries();
    n_events_ = static_cast<long long>(n);

    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(static_cast<size_t>(n));

    for (Long64_t i = 0; i < n; ++i)
    {
        chain.GetEntry(i);
        pot_sum_ += static_cast<double>(pot);
        pairs.emplace_back(static_cast<int>(run), static_cast<int>(subrun));
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
