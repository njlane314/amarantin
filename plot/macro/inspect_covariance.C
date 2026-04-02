#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "TFile.h"
#include "TKey.h"
#include "TMatrixT.h"

namespace
{
    std::string format_double(double value)
    {
        std::ostringstream out;
        out << std::fixed << std::setprecision(6) << value;
        return out.str();
    }

    template <class MatrixType>
    void print_matrix_summary(const std::string &name,
                              const MatrixType &matrix,
                              const char *type_name)
    {
        double trace = 0.0;
        double max_diag = 0.0;
        const int n = std::min(matrix.GetNrows(), matrix.GetNcols());
        for (int i = 0; i < n; ++i)
        {
            const double value = static_cast<double>(matrix(i, i));
            trace += value;
            max_diag = (i == 0) ? value : std::max(max_diag, value);
        }

        std::cout << "matrix=" << name
                  << " type=" << type_name
                  << " rows=" << matrix.GetNrows()
                  << " cols=" << matrix.GetNcols()
                  << " trace=" << format_double(trace)
                  << " max_diag=" << format_double(max_diag)
                  << "\n";
    }
}

void inspect_covariance(const char *path = "output.frac_cov.root",
                        const char *matrix_name = nullptr)
{
    macro_utils::run_macro("inspect_covariance", [&]() {
        if (!path || !*path)
            throw std::runtime_error("inspect_covariance: path is required");

        TFile input(path, "READ");
        if (input.IsZombie())
            throw std::runtime_error("inspect_covariance: failed to open file");

        std::vector<std::string> matrix_names;
        if (matrix_name && *matrix_name)
        {
            matrix_names.push_back(matrix_name);
        }
        else
        {
            TIter next_key(input.GetListOfKeys());
            while (auto *key = dynamic_cast<TKey *>(next_key()))
                matrix_names.emplace_back(key->GetName());
            std::sort(matrix_names.begin(), matrix_names.end());
        }

        bool found_any = false;
        for (const auto &name : matrix_names)
        {
            TObject *object = input.Get(name.c_str());
            if (!object)
                continue;

            if (auto *matrix_f = dynamic_cast<TMatrixT<float> *>(object))
            {
                print_matrix_summary(name, *matrix_f, "float");
                found_any = true;
                continue;
            }
            if (auto *matrix_d = dynamic_cast<TMatrixT<double> *>(object))
            {
                print_matrix_summary(name, *matrix_d, "double");
                found_any = true;
                continue;
            }
        }

        if (!found_any)
            throw std::runtime_error("inspect_covariance: no TMatrixT payloads found");
    });
}
