#pragma once

#include "lol/domain/model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lol {

class Repository {
  public:
    virtual ~Repository() = default;

    virtual Id insert_series(const SeriesDefinition& definition) = 0;
    [[nodiscard]] virtual std::vector<Series> list_series() const = 0;
    [[nodiscard]] virtual std::optional<Series> find_series(Id id) const = 0;
    [[nodiscard]] virtual std::optional<StoredGame> find_game(Id id) const = 0;
    [[nodiscard]] virtual std::vector<StoredGame> games_in_series(Id series_id) const = 0;
    [[nodiscard]] virtual std::vector<RoleAssignment> assignments_for_game(Id game_id) const = 0;
    virtual Id track_team(const std::string& team) = 0;
    [[nodiscard]] virtual std::vector<TrackedTeam> tracked_teams() const = 0;
    [[nodiscard]] virtual std::optional<TrackedTeam>
    find_tracked_team(const std::string& team) const = 0;
    virtual void replace_roster(const TeamRoster& roster) = 0;
    [[nodiscard]] virtual std::optional<TeamRoster>
    roster_for_team(const std::string& team) const = 0;
    virtual void replace_game_lineup(const GameLineup& lineup) = 0;
    [[nodiscard]] virtual std::vector<GameLineup> lineups_for_game(Id game_id) const = 0;
    virtual Id insert_game(const GameDefinition& definition) = 0;
    virtual void replace_assignments(const GameAssignments& assignments) = 0;
    [[nodiscard]] virtual AnalysisResult analyze(const AnalysisFilter& filter) const = 0;
};

} // namespace lol
