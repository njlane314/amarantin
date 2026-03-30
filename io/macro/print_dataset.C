namespace
{
    void print_provenance(const DatasetIO::Provenance &p, std::ostream &os, int indent = 4)
    {
        const std::string pad(static_cast<size_t>(indent), ' ');
        os << pad << "scale=" << p.scale
           << "  shard=" << (p.shard.empty() ? "-" : p.shard)
           << "  sample_list=" << (p.sample_list_path.empty() ? "-" : p.sample_list_path)
           << "  pot_sum=" << p.pot_sum
           << "  entries=" << p.n_entries
           << "  inputs=" << p.input_files.size()
           << "  run_subruns=" << p.run_subruns.size()
           << "  generated_exposures=" << p.generated_exposures.size()
           << "\n";
    }

    void print_sample(const DatasetIO::Sample &s, std::ostream &os, size_t i)
    {
        os << "[" << i << "] "
           << "sample=" << (s.sample.empty() ? "-" : s.sample)
           << "origin=" << DatasetIO::Sample::origin_name(s.origin)
           << "  variation=" << DatasetIO::Sample::variation_name(s.variation)
           << "  beam=" << DatasetIO::Sample::beam_name(s.beam)
           << "  polarity=" << DatasetIO::Sample::polarity_name(s.polarity)
           << "  mode=" << (s.normalisation_mode.empty() ? "-" : s.normalisation_mode)
           << "  norm=" << s.normalisation
           << "  pot(subrun)=" << s.subrun_pot_sum
           << "  pot(db_tortgt)=" << s.db_tortgt_pot_sum
           << "  run_subrun_norms=" << s.run_subrun_normalisations.size()
           << "  root_files=" << s.root_files.size()
           << "  prov=" << s.provenance_list.size()
           << "\n";

        for (const auto &p : s.provenance_list)
            print_provenance(p, os);
    }
} // namespace

void print_dataset(const char *read_path = nullptr)
{
    macro_utils::run_macro("print_dataset", [&]() {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("print_dataset: read_path is required");

        const DatasetIO ds(read_path);

        const auto samples = ds.samples();
        std::cout << "  samples: " << samples.size() << "\n";

        for (size_t i = 0; i < samples.size(); ++i)
            print_sample(samples[i], std::cout, i);
    });
}
