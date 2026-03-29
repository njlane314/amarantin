void print_sample(const char *read_path = nullptr)
{
    macro_utils::run_macro("print_sample", [&]() {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("print_sample: read_path is required");

        SampleIO sample;
        sample.read(read_path);

        std::cout << "origin=" << SampleIO::origin_name(sample.origin_)
                  << " beam=" << SampleIO::beam_name(sample.beam_)
                  << " polarity=" << SampleIO::polarity_name(sample.polarity_)
                  << " partitions=" << sample.partitions_.size()
                  << " normalised_pot_sum=" << sample.normalised_pot_sum_
                  << "\n";
    });
}
