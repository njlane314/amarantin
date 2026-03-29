#include <iostream>
#include <stdexcept>
#include <string>

#include "DatasetIO.hh"

namespace
{
    void print_provenance(const DatasetIO::Provenance &p, std::ostream &os, int indent = 4)
    {
        const std::string pad(static_cast<size_t>(indent), ' ');
        os << pad << "scale=" << p.scale
           << "  pot_sum=" << p.pot_sum
           << "  entries=" << p.n_entries
           << "  inputs=" << p.input_files.size()
           << "  run_subruns=" << p.run_subruns.size()
           << "\n";
    }

    void print_sample(const DatasetIO::Sample &s, std::ostream &os, size_t i)
    {
        os << "[" << i << "] "
           << "origin=" << DatasetIO::Sample::origin_name(s.origin)
           << "  variation=" << DatasetIO::Sample::variation_name(s.variation)
           << "  beam=" << DatasetIO::Sample::beam_name(s.beam)
           << "  polarity=" << DatasetIO::Sample::polarity_name(s.polarity)
           << "  norm=" << s.normalisation
           << "  pot(subrun)=" << s.subrun_pot_sum
           << "  pot(db_tortgt)=" << s.db_tortgt_pot_sum
           << "  root_files=" << s.root_files.size()
           << "  prov=" << s.provenance_list.size()
           << "\n";

        for (const auto &p : s.provenance_list)
            print_provenance(p, os);
    }
} // namespace

void print_dataset(const char *read_path)
{
    try
    {
        const DatasetIO ds(read_path);

        const auto samples = ds.samples();
        std::cout << "  samples: " << samples.size() << "\n";

        for (size_t i = 0; i < samples.size(); ++i)
            print_sample(samples[i], std::cout, i);
    }
    catch (const std::exception &e)
    {
        std::cerr << "print_dataset: " << e.what() << "\n";
        throw;
    }
}
