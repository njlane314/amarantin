#ifndef AMARANTIN_APP_CLI_PATHS_HH
#define AMARANTIN_APP_CLI_PATHS_HH

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <unistd.h>

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

    inline std::string unused_temporary_output_path(const std::string &output_path)
    {
        const std::string base_path =
            output_path + ".tmp." + std::to_string(getpid());

        for (std::size_t suffix = 0;; ++suffix)
        {
            const std::string candidate =
                suffix == 0 ? base_path : base_path + "." + std::to_string(suffix);
            std::error_code error;
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(candidate, error);
            if ((!error && status.type() == std::filesystem::file_type::not_found) ||
                error == std::errc::no_such_file_or_directory)
            {
                return candidate;
            }
            if (error)
            {
                throw std::runtime_error(
                    "failed to inspect temporary output path: " + candidate +
                    ": " + error.message());
            }
        }
    }

    template <class WriteTemporaryFile>
    inline void write_file_atomically(const std::string &output_path,
                                      const char *publish_error_prefix,
                                      WriteTemporaryFile write_temporary_file)
    {
        const std::string temporary_path = unused_temporary_output_path(output_path);
        try
        {
            write_temporary_file(temporary_path);
            if (std::rename(temporary_path.c_str(), output_path.c_str()) != 0)
            {
                const int error_number = errno;
                throw std::runtime_error(
                    std::string(publish_error_prefix) + ": " + std::strerror(error_number));
            }
        }
        catch (...)
        {
            std::remove(temporary_path.c_str());
            throw;
        }
    }
}

#endif // AMARANTIN_APP_CLI_PATHS_HH
