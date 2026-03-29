#ifndef MACRO_UTILS_HH
#define MACRO_UTILS_HH

#if defined(__CLING__)
R__ADD_LIBRARY_PATH(build/lib)
R__LOAD_LIBRARY(libIO.so)
#endif

#include <exception>
#include <iostream>
#include <utility>

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

#endif // MACRO_UTILS_HH
