#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>



static std::string base_name(const char *argv0)
{
    return std::filesystem::path(argv0).filename().string();
}

int main(int argc, char** argv)
{
    const auto cmd = base(argv[0]);

    try 
    {
        if (cmd == "data") return update_database(argc, argv);
        if (cmd == "list") return skim_eventlist(argc, argv);
        if (cmd == "plot") return create_plotfigure(argc, argv);

        std::fprintf(stderr, "unknown mode: %.*s\n", (int)cmd.size(), cmd.data());
        return 2;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%.*s: %s\n", (int)cmd.size(), cmd.data(), e.what());
        return 1;
    }
}