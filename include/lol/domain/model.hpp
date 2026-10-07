#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lol {

using Id = std::int64_t;

enum class Side { blue, red };
enum class DraftActionType { ban, pick };
enum class Role { top, jungle, mid, bot, support };

struct DraftSlot {
    DraftActionType type;
    Side side;
};

inline constexpr std::array<DraftSlot, 20> standard_draft_order{{
    {DraftActionType::ban, Side::blue},  {DraftActionType::ban, Side::red},
    {DraftActionType::ban, Side::blue},  {DraftActionType::ban, Side::red},
    {DraftActionType::ban, Side::blue},  {DraftActionType::ban, Side::red},
    {DraftActionType::pick, Side::blue}, {DraftActionType::pick, Side::red},
    {DraftActionType::pick, Side::red},  {DraftActionType::pick, Side::blue},
    {DraftActionType::pick, Side::blue}, {DraftActionType::pick, Side::red},
    {DraftActionType::ban, Side::red},   {DraftActionType::ban, Side::blue},
    {DraftActionType::ban, Side::red},   {DraftActionType::ban, Side::blue},
    {DraftActionType::pick, Side::red},  {DraftActionType::pick, Side::blue},
    {DraftActionType::pick, Side::blue}, {DraftActionType::pick, Side::red},
}};

struct SeriesDefinition {
    std::string team_a;
    std::string team_b;
    int elo_low{};
    int elo_high{};
    double elo_average{};
    std::string patch;
};

struct Series : SeriesDefinition {
    Id id{};
};

struct DraftAction {
    int sequence{};
    DraftActionType type{};
    Side side{};
    std::string champion;
    std::optional<std::string> player;
    std::optional<std::string> role;
};

struct GameDefinition {
    Id series_id{};
    int game_number{};
    std::string blue_team;
    std::string red_team;
    std::string winner_team;
    std::vector<DraftAction> draft;
};

struct StoredGame : GameDefinition {
    Id id{};
};

struct RoleAssignment {
    Side side{};
    Role role{};
    std::string champion;
    std::optional<std::string> player;
};

struct GameAssignments {
    Id game_id{};
    std::vector<RoleAssignment> assignments;
};

struct RolePlayer {
    Role role{};
    std::string player;
};

struct TrackedTeam {
    Id id{};
    std::string name;
};

struct TeamRoster {
    TrackedTeam team;
    std::vector<RolePlayer> assignments;
};

struct GameLineup {
    Id game_id{};
    Side side{};
    std::string team;
    std::vector<RolePlayer> assignments;
};

struct AnalysisFilter {
    std::string perspective_team;
    std::optional<std::string> champion;
    std::optional<std::string> player;
    std::optional<int> pick_ordinal;
    std::vector<std::string> required_bans;
    std::vector<std::string> excluded_bans;
    std::optional<std::string> patch;
    std::optional<std::string> opponent;
    std::optional<Side> side;
    std::optional<Role> role;
};

enum class Relationship { with, against };

struct ChampionAssociation {
    std::string champion;
    Relationship relationship{};
    std::optional<Role> role;
    int games{};
    int wins{};
    int losses{};

    [[nodiscard]] double win_rate() const noexcept {
        return games == 0 ? 0.0 : static_cast<double>(wins) / static_cast<double>(games);
    }
};

struct AnalysisResult {
    int matched_games{};
    int wins{};
    int losses{};
    std::vector<ChampionAssociation> associations;

    [[nodiscard]] double win_rate() const noexcept {
        return matched_games == 0 ? 0.0
                                  : static_cast<double>(wins) / static_cast<double>(matched_games);
    }
};

class ValidationError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class StorageError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] std::string to_string(Side side);
[[nodiscard]] std::string to_string(DraftActionType type);
[[nodiscard]] std::string to_string(Role role);
[[nodiscard]] std::string champion_key(const std::string& champion);
[[nodiscard]] std::string normalized_champion_name(const std::string& champion);
[[nodiscard]] std::string identity_key(const std::string& value);
[[nodiscard]] std::string trimmed_name(const std::string& value);

} // namespace lol
