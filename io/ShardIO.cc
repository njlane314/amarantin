#include "ShardIO.hh"
#include "bits/RootUtils.hh"

#include <algorithm>
#include <fstream>
#include <map>
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

namespace
{
    std::string find_subrun_tree_path(TFile &file,
                                      const std::vector<std::string> &candidates)
    {
        for (const auto &name : candidates)
        {
            if (dynamic_cast<TTree *>(file.Get(name.c_str())))
                return name;
        }
        return "";
    }
}

ShardIO::ShardIO(const std::string &list_path,
                 const std::string &shard)
{
    list_path_ = list_path;
    shard_ = shard;
    files_ = read_list(list_path);
    if (!files_.empty()) scan(files_);
}

void ShardIO::write(TDirectory *d) const
{
    if (!d) throw std::runtime_error("ShardIO::write: null directory");
    d->cd();

    utils::write_named(d, "sample_list_path", list_path_);
    utils::write_named(d, "shard", shard_);
    utils::write_param<double>(d, "pot_sum", subrun_pot_sum());
    utils::write_param<long long>(d, "entries", entries());

    {
        TTree files_t("root_files", "");
        std::string root_file;
        files_t.Branch("root_file", &root_file);
        for (const auto &path : files())
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
        Double_t generated_exposure = 0.0;
        rs.Branch("run", &run, "run/I");
        rs.Branch("subrun", &subrun, "subrun/I");
        rs.Branch("generated_exposure", &generated_exposure, "generated_exposure/D");
        if (!generated_exposures_.empty())
        {
            for (const auto &entry : generated_exposures_)
            {
                run = entry.run;
                subrun = entry.subrun;
                generated_exposure = entry.generated_exposure;
                rs.Fill();
            }
        }
        else
        {
            for (const auto &pair : subruns())
            {
                run = pair.first;
                subrun = pair.second;
                generated_exposure = 0.0;
                rs.Fill();
            }
        }
        rs.Write("run_subrun", TObject::kOverwrite);
    }
}

ShardIO ShardIO::read(TDirectory *d)
{
    if (!d) throw std::runtime_error("ShardIO::read: null directory");

    ShardIO out;
    out.list_path_ = utils::read_named_or(d, "sample_list_path");
    out.shard_ = utils::read_named_or(d, "shard");
    out.pot_sum_ = utils::read_param<double>(d, "pot_sum");
    out.entries_ = utils::read_param<long long>(d, "entries");

    {
        auto *t = dynamic_cast<TTree *>(d->Get("root_files"));
        if (!t) throw std::runtime_error("ShardIO: missing root_files tree in part/" + std::string(d->GetName()));

        std::string *root_file = nullptr;
        t->SetBranchAddress("root_file", &root_file);

        const Long64_t n = t->GetEntries();
        out.files_.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            if (!root_file) throw std::runtime_error("ShardIO: root_files missing root_file in part/" + std::string(d->GetName()));
            out.files_.push_back(*root_file);
        }
    }

    {
        auto *t = dynamic_cast<TTree *>(d->Get("run_subrun"));
        if (!t) throw std::runtime_error("ShardIO: missing run_subrun tree in part/" + std::string(d->GetName()));

        Int_t run = 0;
        Int_t subrun = 0;
        Double_t generated_exposure = 0.0;
        t->SetBranchAddress("run", &run);
        t->SetBranchAddress("subrun", &subrun);
        const bool have_generated_exposure = t->GetBranch("generated_exposure") != nullptr;
        if (have_generated_exposure)
            t->SetBranchAddress("generated_exposure", &generated_exposure);

        const Long64_t n = t->GetEntries();
        out.generated_exposures_.reserve(static_cast<size_t>(n));
        out.subruns_.reserve(static_cast<size_t>(n));
        for (Long64_t i = 0; i < n; ++i)
        {
            t->GetEntry(i);
            out.subruns_.emplace_back(static_cast<int>(run), static_cast<int>(subrun));
            RunSubrunExposure entry;
            entry.run = static_cast<int>(run);
            entry.subrun = static_cast<int>(subrun);
            entry.generated_exposure = have_generated_exposure ? static_cast<double>(generated_exposure) : 0.0;
            out.generated_exposures_.push_back(entry);
        }
    }

    return out;
}

std::vector<std::string> ShardIO::read_list(const std::string &path)
{
    std::ifstream in(path);
    if (!in) throw std::runtime_error("ShardIO: failed to open sample list: " + path);

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

void ShardIO::scan(const std::vector<std::string> &files)
{
    if (files.empty()) throw std::runtime_error("ShardIO: no input files provided for subrun scan.");

    files_ = files;
    subruns_.clear();
    generated_exposures_.clear();

    pot_sum_ = 0.0;
    entries_ = 0;

    const std::vector<std::string> candidates = {"nuselection/SubRun", "SubRun"};
    std::string tree_path;

    for (const auto &f : files)
    {
        std::unique_ptr<TFile> file(TFile::Open(f.c_str(), "READ"));
        if (!file || file->IsZombie())
            throw std::runtime_error("ShardIO: failed to open input ROOT file: " + f);

        const std::string file_tree_path = find_subrun_tree_path(*file, candidates);
        if (file_tree_path.empty())
        {
            throw std::runtime_error("ShardIO: input ROOT file is missing a SubRun tree: " + f);
        }

        if (tree_path.empty())
        {
            tree_path = file_tree_path;
            continue;
        }

        if (file_tree_path != tree_path)
        {
            throw std::runtime_error("ShardIO: mixed SubRun tree layouts in sample list: expected " +
                                     tree_path + " but found " + file_tree_path + " in " + f);
        }
    }

    if (tree_path.empty())
        throw std::runtime_error("ShardIO: no input files contained a SubRun tree.");

    TChain chain(tree_path.c_str());
    for (const auto &f : files)
        chain.Add(f.c_str());

    if (!chain.GetBranch("run") || !chain.GetBranch("subRun") || !chain.GetBranch("pot"))
        throw std::runtime_error("ShardIO: SubRun tree missing required branches (run, subRun, pot).");

    Int_t run = 0;
    Int_t subrun = 0;
    Double_t pot = 0.0;

    chain.SetBranchAddress("run", &run);
    chain.SetBranchAddress("subRun", &subrun);
    chain.SetBranchAddress("pot", &pot);

    const Long64_t n = chain.GetEntries();
    entries_ = static_cast<long long>(n);

    std::map<std::pair<int, int>, double> exposures;

    for (Long64_t i = 0; i < n; ++i)
    {
        chain.GetEntry(i);
        pot_sum_ += static_cast<double>(pot);
        exposures[std::make_pair(static_cast<int>(run), static_cast<int>(subrun))] += static_cast<double>(pot);
    }

    subruns_.reserve(exposures.size());
    generated_exposures_.reserve(exposures.size());
    for (const auto &entry : exposures)
    {
        subruns_.push_back(entry.first);
        generated_exposures_.push_back(
            RunSubrunExposure{entry.first.first, entry.first.second, entry.second});
    }
}
