#ifndef AMARANTIN_APP_CLI_PATHS_HH
#define AMARANTIN_APP_CLI_PATHS_HH

#include <filesystem>
#include <stdexcept>
#include <string>

namespace cli
{
    inline std::filesystem::path normalised_path(const std::string &path)
    {
        std::error_code error;
        std::filesystem::path resolved = std::filesystem::weakly_canonical(path, error);
        if (!error)
            return resolved;

        error.clear();
        resolved = std::filesystem::absolute(path, error);
        if (error)
            throw std::runtime_error("failed to resolve path: " + path);
        return resolved.lexically_normal();
    }

    inline bool paths_alias(const std::string &first, const std::string &second)
    {
        std::error_code error;
        if (std::filesystem::equivalent(first, second, error))
            return true;
        return normalised_path(first) == normalised_path(second);
    }

    inline void require_distinct_output_path(const char *command,
                                             const std::string &output_path,
                                             const char *input_label,
                                             const std::string &input_path)
    {
        if (paths_alias(output_path, input_path))
        {
            throw std::runtime_error(std::string(command) + ": " + input_label +
                                     " and output paths must differ");
        }
    }
}

#endif // AMARANTIN_APP_CLI_PATHS_HH
