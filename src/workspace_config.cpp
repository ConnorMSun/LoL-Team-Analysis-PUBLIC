#include "lol/infrastructure/workspace_config.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lol {
namespace {

constexpr auto config_filename = ".lolctl";

std::filesystem::path absolute_normalized(const std::filesystem::path& path,
                                          const std::filesystem::path& base) {
    return (path.is_absolute() ? path : base / path).lexically_normal();
}

std::optional<std::filesystem::path> find_config(std::filesystem::path directory) {
    directory = std::filesystem::absolute(directory).lexically_normal();
    while (true) {
        const auto candidate = directory / config_filename;
        if (std::filesystem::exists(candidate)) {
            if (!std::filesystem::is_regular_file(candidate)) {
                throw std::runtime_error(candidate.string() + " is not a regular config file");
            }
            return candidate;
        }
        const auto parent = directory.parent_path();
        if (parent == directory || parent.empty()) {
            return std::nullopt;
        }
        directory = parent;
    }
}

std::filesystem::path read_database(const std::filesystem::path& config_path) {
    std::ifstream input(config_path);
    if (!input) {
        throw std::runtime_error("could not read workspace config: " + config_path.string());
    }

    std::optional<std::string> database;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line.starts_with('#') || line == "version=1") {
            continue;
        }
        constexpr std::string_view prefix = "database=";
        if (line.starts_with(prefix)) {
            database = line.substr(prefix.size());
            continue;
        }
        throw std::runtime_error("unsupported workspace config entry: " + line);
    }
    if (!database || database->empty()) {
        throw std::runtime_error("workspace config does not select a database: " +
                                 config_path.string());
    }
    return absolute_normalized(*database, config_path.parent_path());
}

} // namespace

DatabaseSelection
WorkspaceConfig::resolve(const std::optional<std::filesystem::path>& explicit_database,
                         const std::filesystem::path& working_directory) {
    const auto absolute_working_directory =
        std::filesystem::absolute(working_directory).lexically_normal();
    if (explicit_database) {
        return {absolute_normalized(*explicit_database, absolute_working_directory),
                DatabaseSource::explicit_option, std::nullopt};
    }
    if (const auto config_path = find_config(absolute_working_directory)) {
        return {read_database(*config_path), DatabaseSource::workspace, config_path};
    }
    return {absolute_working_directory / "analytics.sqlite", DatabaseSource::default_database,
            std::nullopt};
}

std::filesystem::path WorkspaceConfig::save(const std::filesystem::path& database_path,
                                            const std::filesystem::path& working_directory) {
    if (database_path.empty() ||
        database_path.string().find_first_of("\r\n") != std::string::npos) {
        throw std::invalid_argument("database path cannot be empty or contain a newline");
    }
    const auto directory = std::filesystem::absolute(working_directory).lexically_normal();
    const auto config_path = directory / config_filename;
    const auto temporary_path = directory / ".lolctl.tmp";
    const auto stored_path = database_path.lexically_normal().generic_string();

    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("could not write workspace config: " +
                                     temporary_path.string());
        }
        output << "version=1\n";
        output << "database=" << stored_path << '\n';
        if (!output) {
            throw std::runtime_error("could not finish workspace config: " +
                                     temporary_path.string());
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary_path, config_path, error);
    if (error) {
        std::filesystem::remove(config_path, error);
        error.clear();
        std::filesystem::rename(temporary_path, config_path, error);
    }
    if (error) {
        std::filesystem::remove(temporary_path);
        throw std::runtime_error("could not activate workspace config: " + error.message());
    }
    return config_path;
}

std::optional<std::filesystem::path>
WorkspaceConfig::clear(const std::filesystem::path& working_directory) {
    const auto config_path = find_config(working_directory);
    if (!config_path) {
        return std::nullopt;
    }
    if (!std::filesystem::remove(*config_path)) {
        throw std::runtime_error("could not remove workspace config: " + config_path->string());
    }
    return config_path;
}

const char* to_string(const DatabaseSource source) noexcept {
    switch (source) {
    case DatabaseSource::explicit_option:
        return "--db";
    case DatabaseSource::workspace:
        return "workspace";
    case DatabaseSource::default_database:
        return "default";
    }
    return "unknown";
}

} // namespace lol
