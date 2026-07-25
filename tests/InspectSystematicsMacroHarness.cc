#include <utility>

namespace macro_utils
{
    template <class F>
    void run_macro(const char *, F &&fn)
    {
        std::forward<F>(fn)();
    }
}

#include "../plot/macro/inspect_systematics.C"
