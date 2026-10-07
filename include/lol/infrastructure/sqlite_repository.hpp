#pragma once

#include "lol/application/repository.hpp"

#include <string>

struct sqlite3;

namespace lol {

class SqliteRepository final : public Repository {
  public:
    explicit SqliteRepository(const std::string& database_path);
    ~SqliteRepository() override;

    SqliteRepository(const SqliteRepository&) = delete;
    SqliteRepository& operator=(const SqliteRepository&) = delete;
    SqliteRepository(SqliteRepository&&) = delete;
    SqliteRepository& operator=(SqliteRepository&&) = delete;

    Id insert_series(const SeriesDefinition& definition) override;
    [[nodiscard]] std::vector<Series> list_series() const override;
    [[nodiscard]] std::optional<Series> find_series(Id id) const override;
    [[nodiscard]] std::optional<StoredGame> find_game(Id id) const override;
    [[nodiscard]] std::vector<StoredGame> games_in_series(Id series_id) const override;
    [[nodiscard]] std::vector<RoleAssignment> assignments_for_game(Id game_id) const override;
    Id track_team(const std::string& team) override;
    [[nodiscard]] std::vector<TrackedTeam> tracked_teams() const override;
    [[nodiscard]] std::optional<TrackedTeam>
    find_tracked_team(const std::string& team) const override;
    void replace_roster(const TeamRoster& roster) override;
    [[nodiscard]] std::optional<TeamRoster> roster_for_team(const std::string& team) const override;
    void replace_game_lineup(const GameLineup& lineup) override;
    [[nodiscard]] std::vector<GameLineup> lineups_for_game(Id game_id) const override;
    Id insert_game(const GameDefinition& definition) override;
    void replace_assignments(const GameAssignments& assignments) override;
    [[nodiscard]] AnalysisResult analyze(const AnalysisFilter& filter) const override;

  private:
    sqlite3* database_{};
    void migrate();
};

} // namespace lol
