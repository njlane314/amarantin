#include <algorithm>
#include <iostream>
#include <string>

#include "EventListIO.hh"
#include "TTree.h"

namespace
{
    void print_sample_tree(TTree *tree, const std::string &sample_name)
    {
        if (!tree) return;

        int run = -1;
        int sub = -1;
        int evt = -1;
        int selected = 0;
        int selection_pass = 0;
        int num_tracks = -1;
        int num_showers = -1;
        float topological_score = -999.f;
        float reco_neutrino_vertex_x = -999.f;
        float reco_neutrino_vertex_y = -999.f;
        float reco_neutrino_vertex_z = -999.f;

        tree->SetBranchAddress("run", &run);
        tree->SetBranchAddress("sub", &sub);
        tree->SetBranchAddress("evt", &evt);
        tree->SetBranchAddress("selected", &selected);

        if (tree->GetBranch("selection_pass"))
            tree->SetBranchAddress("selection_pass", &selection_pass);
        if (tree->GetBranch("num_tracks"))
            tree->SetBranchAddress("num_tracks", &num_tracks);
        if (tree->GetBranch("num_showers"))
            tree->SetBranchAddress("num_showers", &num_showers);
        if (tree->GetBranch("topological_score"))
            tree->SetBranchAddress("topological_score", &topological_score);
        if (tree->GetBranch("reco_neutrino_vertex_x"))
            tree->SetBranchAddress("reco_neutrino_vertex_x", &reco_neutrino_vertex_x);
        if (tree->GetBranch("reco_neutrino_vertex_y"))
            tree->SetBranchAddress("reco_neutrino_vertex_y", &reco_neutrino_vertex_y);
        if (tree->GetBranch("reco_neutrino_vertex_z"))
            tree->SetBranchAddress("reco_neutrino_vertex_z", &reco_neutrino_vertex_z);

        std::cout << "\nSample: " << sample_name
                  << "  selected_entries=" << tree->GetEntries() << "\n";

        const Long64_t nprint = std::min<Long64_t>(tree->GetEntries(), 10);
        for (Long64_t i = 0; i < nprint; ++i)
        {
            tree->GetEntry(i);
            std::cout << "  [" << i << "]"
                      << " run=" << run
                      << " sub=" << sub
                      << " evt=" << evt
                      << " selected=" << selected
                      << " selection_pass=" << selection_pass
                      << " num_tracks=" << num_tracks
                      << " num_showers=" << num_showers
                      << " topo=" << topological_score
                      << " vtx=("
                      << reco_neutrino_vertex_x << ", "
                      << reco_neutrino_vertex_y << ", "
                      << reco_neutrino_vertex_z << ")\n";
        }
    }
}

void print_eventlist(const char *read_path)
{
    EventListIO eventlist(read_path, EventListIO::Mode::kRead);

    for (const auto &sample_name : eventlist.sample_keys())
    {
        TTree *selected = eventlist.selected_tree(sample_name);
        print_sample_tree(selected, sample_name);
    }
}
