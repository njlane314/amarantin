{
    const TString root_dir = gSystem->pwd();
    const TString io_dir = root_dir + "/io";
    const TString ana_dir = root_dir + "/ana";
    const TString ana_macro_dir = root_dir + "/ana/macro";
    const TString macro_dir = root_dir + "/macro";
    const TString plot_dir = root_dir + "/plot";
    const TString plot_macro_dir = root_dir + "/plot/macro";
    const TString io_macro_dir = root_dir + "/io/macro";
    const TString syst_dir = root_dir + "/syst";
    const TString lib_dir = root_dir + "/build/lib";
    const TString lib_path = lib_dir + "/libIO.so";
    const TString ana_lib_path = lib_dir + "/libAna.so";
    const TString plot_lib_path = lib_dir + "/libPlot.so";
    const TString syst_lib_path = lib_dir + "/libSyst.so";

    gInterpreter->AddIncludePath(io_dir.Data());
    gInterpreter->AddIncludePath(ana_dir.Data());
    gInterpreter->AddIncludePath(ana_macro_dir.Data());
    gInterpreter->AddIncludePath(macro_dir.Data());
    gInterpreter->AddIncludePath(plot_dir.Data());
    gInterpreter->AddIncludePath(plot_macro_dir.Data());
    gInterpreter->AddIncludePath(io_macro_dir.Data());
    gInterpreter->AddIncludePath(syst_dir.Data());
    gSystem->AddDynamicPath(lib_dir.Data());
    gSystem->Load(lib_path.Data());
    gSystem->Load(ana_lib_path.Data());
    gSystem->Load(plot_lib_path.Data());
    gSystem->Load(syst_lib_path.Data());

    gInterpreter->Declare(R"cpp(
        #include <algorithm>
        #include <exception>
        #include <iostream>
        #include <stdexcept>
        #include <string>
        #include <utility>

        #include "TTree.h"
        #include "TH1D.h"
        #include "TCanvas.h"

        #include "SampleIO.hh"
        #include "SampleDef.hh"
        #include "DatasetIO.hh"
        #include "DistributionIO.hh"
        #include "EventListIO.hh"
        #include "EventListBuild.hh"
        #include "EfficiencyPlot.hh"
        #include "EventDisplay.hh"
        #include "EventListPlotting.hh"
        #include "Systematics.hh"
        #include "Snapshot.hh"

        namespace macro_utils
        {
            template <class F>
            void run_macro(const char *name, F &&fn)
            {
                try
                {
                    std::forward<F>(fn)();
                }
                catch (const std::exception &e)
                {
                    std::cerr << name << ": " << e.what() << "\n";
                }
                catch (...)
                {
                    std::cerr << name << ": unknown exception\n";
                }
            }
        }
    )cpp");
}
