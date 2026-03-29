{
    const TString root_dir = gSystem->pwd();
    const TString include_dir = root_dir + "/io/include";
    const TString macro_dir = root_dir + "/macro";
    const TString lib_dir = root_dir + "/build/lib";
    const TString lib_path = lib_dir + "/libIO.so";

    gInterpreter->AddIncludePath(include_dir.Data());
    gInterpreter->AddIncludePath(macro_dir.Data());
    gSystem->AddDynamicPath(lib_dir.Data());
    gSystem->Load(lib_path.Data());

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
        #include "DatasetIO.hh"
        #include "EventListIO.hh"
        #include "EventListPlotting.hh"

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
