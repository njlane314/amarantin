#include <iostream>
#include <stdexcept>
#include <string>

#include "SampleIO.hh"

void make_sample(const char *output_path,
                 const char *input_paths,
                 const char *origin = "data",
                 const char *variation = "nominal",
                 const char *beam = "numi",
                 const char *polarity = "fhc",
                 const char *run_db_path = nullptr)
{
    try
    {
        if (!output_path || std::string(output_path).empty())
            throw std::runtime_error("make_sample: output_path is required");
        if (!input_paths || std::string(input_paths).empty())
            throw std::runtime_error("make_sample: input_paths is required");

        SampleIO sample;
        sample.build(input_paths,
                     origin ? origin : "",
                     variation ? variation : "",
                     beam ? beam : "",
                     polarity ? polarity : "",
                     run_db_path ? run_db_path : "");
        sample.write(output_path);
        std::cout << "make_sample: wrote SampleIO to " << output_path << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "make_sample: " << e.what() << "\n";
        throw;
    }
}
