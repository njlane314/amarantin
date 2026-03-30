{
    const TString root_dir = gSystem->pwd();
    const TString include_dir = root_dir + "/io/include";
    const TString book_include_dir = root_dir + "/book/include";
    const TString ana_include_dir = root_dir + "/ana/include";
    const TString macro_dir = root_dir + "/macro";
    const TString plot_include_dir = root_dir + "/plot/include";
    const TString plot_macro_dir = root_dir + "/plot/macro";
    const TString io_macro_dir = root_dir + "/io/macro";
    const TString syst_include_dir = root_dir + "/syst/include";
    const TString lib_dir = root_dir + "/build/lib";
    const TString lib_path = lib_dir + "/libIO.so";
    const TString book_lib_path = lib_dir + "/libBook.so";
    const TString ana_lib_path = lib_dir + "/libAna.so";
    const TString plot_lib_path = lib_dir + "/libPlot.so";
    const TString syst_lib_path = lib_dir + "/libSyst.so";

    gInterpreter->AddIncludePath(include_dir.Data());
    gInterpreter->AddIncludePath(book_include_dir.Data());
    gInterpreter->AddIncludePath(ana_include_dir.Data());
    gInterpreter->AddIncludePath(macro_dir.Data());
    gInterpreter->AddIncludePath(plot_include_dir.Data());
    gInterpreter->AddIncludePath(plot_macro_dir.Data());
    gInterpreter->AddIncludePath(io_macro_dir.Data());
    gInterpreter->AddIncludePath(syst_include_dir.Data());
    gSystem->AddDynamicPath(lib_dir.Data());
    gSystem->Load(lib_path.Data());
    gSystem->Load(book_lib_path.Data());
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
        #include "SampleBook.hh"
        #include "DatasetIO.hh"
        #include "EventListIO.hh"
        #include "EventListBuilder.hh"
        #include "EventListSelection.hh"
        #include "EventDisplay.hh"
        #include "EventListPlotting.hh"
        #include "Systematics.hh"
        #include "SystematicsCacheBuilder.hh"
        #include "SnapshotService.hh"

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
