{
    TString root_dir = gSystem->pwd();
    if (const char *env = gSystem->Getenv("AMARANTIN_ROOT_DIR"))
    {
        if (*env)
            root_dir = env;
    }

    TString build_dir = root_dir + "/build";
    if (const char *env = gSystem->Getenv("AMARANTIN_BUILD_DIR"))
    {
        if (*env)
        {
            build_dir = env;
            if (!build_dir.BeginsWith("/"))
                build_dir = root_dir + "/" + build_dir;
        }
    }

    TString io_dir = root_dir;
    io_dir += "/io";
    TString ana_dir = root_dir;
    ana_dir += "/ana";
    TString ana_macro_dir = root_dir;
    ana_macro_dir += "/ana/macro";
    TString macro_dir = root_dir;
    macro_dir += "/macro";
    TString plot_dir = root_dir;
    plot_dir += "/plot";
    TString plot_macro_dir = root_dir;
    plot_macro_dir += "/plot/macro";
    TString io_macro_dir = root_dir;
    io_macro_dir += "/io/macro";
    TString syst_dir = root_dir;
    syst_dir += "/syst";
    TString lib_dir = build_dir;
    lib_dir += "/lib";
    TString lib_path = lib_dir;
    lib_path += "/libIO.so";
    TString ana_lib_path = lib_dir;
    ana_lib_path += "/libAna.so";
    TString plot_lib_path = lib_dir;
    plot_lib_path += "/libPlot.so";
    TString syst_lib_path = lib_dir;
    syst_lib_path += "/libSyst.so";

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
        #include "ChannelIO.hh"
        #include "DatasetIO.hh"
        #include "DistributionIO.hh"
        #include "EventListIO.hh"
        #include "EventListBuild.hh"
        #include "EfficiencyPlot.hh"
        #include "EventDisplay.hh"
        #include "EventListPlotting.hh"
        #include "PlotChannels.hh"
        #include "PlotDescriptors.hh"
        #include "Plotter.hh"
        #include "PlottingHelper.hh"
        #include "StackedHist.hh"
        #include "Systematics.hh"
        #include "Snapshot.hh"
        #include "UnstackedHist.hh"

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
