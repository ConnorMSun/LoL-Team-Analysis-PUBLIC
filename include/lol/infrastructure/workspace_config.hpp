#pragma once

#include <filesystem>
#include <optional>

namespace lol {

enum class DatabaseSource { explicit_option, workspace, default_database };

struct DatabaseSelection {
    std::filesystem::path database_path;
    DatabaseSource source{};
    std::optional<std::filesystem::path> config_path;
};

class WorkspaceConfig {
  public:
    [[nodiscard]] static DatabaseSelection
    resolve(const std::optional<std::filesystem::path>& explicit_database,
            const std::filesystem::path& working_directory = std::filesystem::current_path());

    static std::filesystem::path
    save(const std::filesystem::path& database_path,
         const std::filesystem::path& working_directory = std::filesystem::current_path());

    [[nodiscard]] static std::optional<std::filesystem::path>
    clear(const std::filesystem::path& working_directory = std::filesystem::current_path());
};

[[nodiscard]] const char* to_string(DatabaseSource source) noexcept;

} // namespace lol
