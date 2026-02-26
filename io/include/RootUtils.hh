// RootUtils.hh
#ifndef ROOT_UTILS_HH
#define ROOT_UTILS_HH

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

#include <TDirectory.h>
#include <TKey.h>
#include <TNamed.h>
#include <TParameter.h>

namespace utils
{
    inline std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    inline std::vector<std::string> list_keys(TDirectory *d)
    {
        std::vector<std::string> out;
        if (!d) return out;

        TIter next(d->GetListOfKeys());
        while (TKey *k = (TKey *)next())
            out.emplace_back(k->GetName());

        std::sort(out.begin(), out.end());
        return out;
    }

    inline TDirectory *must_dir(TDirectory *base, const char *name, bool create)
    {
        if (!base) throw std::runtime_error("RootUtils: null base directory");
        TDirectory *d = base->GetDirectory(name);
        if (!d && create) d = base->mkdir(name);
        if (!d) throw std::runtime_error(std::string("RootUtils: missing directory: ") + name);
        return d;
    }

    inline TDirectory *must_subdir(TDirectory *base, const std::string &name, bool create, const char *what)
    {
        if (!base) throw std::runtime_error("RootUtils: null base directory");
        TDirectory *d = base->GetDirectory(name.c_str());
        if (!d && create) d = base->mkdir(name.c_str());
        if (!d) throw std::runtime_error(std::string("RootUtils: missing ") + what + "/" + name);
        return d;
    }

    inline std::string read_named(TDirectory *d, const char *key)
    {
        TObject *obj = d ? d->Get(key) : nullptr;
        auto *n = dynamic_cast<TNamed *>(obj);
        if (!n) throw std::runtime_error(std::string("RootUtils: missing TNamed: ") + key);
        return std::string(n->GetTitle());
    }

    inline void write_named(TDirectory *d, const char *key, const std::string &value)
    {
        if (!d) throw std::runtime_error("RootUtils: null directory in write_named");
        d->cd();
        TNamed(key, value.c_str()).Write(key, TObject::kOverwrite);
    }

    template <class T>
    inline T read_param(TDirectory *d, const char *key)
    {
        TObject *obj = d ? d->Get(key) : nullptr;
        auto *p = dynamic_cast<TParameter<T> *>(obj);
        if (!p) throw std::runtime_error(std::string("RootUtils: missing TParameter: ") + key);
        return p->GetVal();
    }

    template <class T>
    inline void write_param(TDirectory *d, const char *key, const T &value)
    {
        if (!d) throw std::runtime_error("RootUtils: null directory in write_param");
        d->cd();
        TParameter<T>(key, value).Write(key, TObject::kOverwrite);
    }
} 


#endif // ROOT_UTILS_HH
