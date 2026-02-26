#include <exception>
#include <iostream>
#include <string>

#include "SampleIO.hh"

namespace
{
    const char *arg_or_default(int argc, char **argv, int index, const char *fallback)
    {
        return (index < argc && argv[index] && std::string(argv[index]).size() > 0)
                   ? argv[index]
                   : fallback;
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc < 3)
        {
            std::cerr << "usage: mk_sample <output.root> <input.list> [db_path] [origin] [variation] [beam] [polarity]\n";
            return 2;
        }

        const char *output_path = argv[1];
        const char *list_path = argv[2];
        const char *db_path = arg_or_default(argc, argv, 3, "");
        const char *origin = arg_or_default(argc, argv, 4, "data");
        const char *variation = arg_or_default(argc, argv, 5, "nominal");
        const char *beam = arg_or_default(argc, argv, 6, "numi");
        const char *polarity = arg_or_default(argc, argv, 7, "fhc");

        SampleIO sample;
        sample.build(list_path, origin, variation, beam, polarity, db_path);
        sample.write(output_path);

        std::cout << "mk_sample: wrote " << output_path << " from " << list_path << "\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "mk_sample: " << e.what() << "\n";
        return 1;
    }
}
