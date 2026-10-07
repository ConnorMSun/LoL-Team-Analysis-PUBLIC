#pragma once

#include "lol/application/repository.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lol {

class RosterService {
  public:
    explicit RosterService(Repository& repository) : repository_(repository) {}

    Id track_team(const std::string& team);
    [[nodiscard]] std::vector<TrackedTeam> tracked_teams() const;
    void replace_roster(const std::string& team, const std::vector<RolePlayer>& assignments);
    [[nodiscard]] std::optional<TeamRoster> roster(const std::string& team) const;
    void snapshot_available_lineups(Id game_id);
    void set_game_lineup(Id game_id, const std::string& team,
                         const std::vector<RolePlayer>& overrides);
    [[nodiscard]] std::vector<GameLineup> game_lineups(Id game_id) const;

  private:
    Repository& repository_;
};

} // namespace lol
