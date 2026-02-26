#include <iostream>
#include <stdexcept>
#include <string>

#include "SampleIO.hh"

void make_sample(const char *output_path,
                 const char *input_paths,
                 const char *db_path = "",
                 const char *origin = "unknown",
                 const char *variation = "unknown",
                 const char *beam = "unknown",
                 const char *polarity = "unknown")
{
    try
    {
        if (!output_path || std::string(output_path).empty())
            throw std::runtime_error("make_sample: output_path is required");
        if (!input_paths || std::string(input_paths).empty())
            throw std::runtime_error("make_sample: input_paths is required");

        const auto options = SampleIO::BuildOptions::from_strings(origin ? origin : "",
                                                                   variation ? variation : "",
                                                                   beam ? beam : "",
                                                                   polarity ? polarity : "",
                                                                   db_path ? db_path : "");

        SampleIO::build_and_write_from_spec(output_path, input_paths, options);
        std::cout << "make_sample: wrote SampleIO to " << output_path << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "make_sample: " << e.what() << "\n";
        throw;
    }
}
