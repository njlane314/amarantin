#include <iostream>
#include <stdexcept>
#include <string>

#include "SampleIO.hh"

void process_sample(const char *read_path)
{
    try
    {
        if (!read_path || std::string(read_path).empty())
            throw std::runtime_error("process_sample: read_path is required");

        const SampleIO sample = SampleIO::read(read_path);

        std::cout << "origin=" << SampleIO::origin_name(sample.origin_)
                  << " beam=" << SampleIO::beam_name(sample.beam_)
                  << " polarity=" << SampleIO::polarity_name(sample.polarity_)
                  << " partitions=" << sample.partitions_.size()
                  << " normalised_pot_sum=" << sample.normalised_pot_sum_
                  << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "process_sample: " << e.what() << "\n";
        throw;
    }
}
