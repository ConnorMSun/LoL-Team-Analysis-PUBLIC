#include "lol/application/roster_service.hpp"

#include <algorithm>
#include <set>

namespace lol {
namespace {

void validate_complete_assignments(const std::vector<RolePlayer>& assignments,
                                   const std::string& context) {
    if (assignments.size() != 5) {
        throw ValidationError(context + " requires top, jungle, mid, bot, and support");
    }
    std::set<Role> roles;
    std::set<std::string> players;
    for (const auto& assignment : assignments) {
        const auto player_key = identity_key(assignment.player);
        if (player_key.empty()) {
            throw ValidationError("player names cannot be blank");
        }
        if (!roles.insert(assignment.role).second) {
            throw ValidationError("each role must be assigned once");
        }
        if (!players.insert(player_key).second) {
            throw ValidationError("a player cannot fill two roles in one lineup");
        }
    }
}

Side side_for_team(const StoredGame& game, const std::string& team) {
    if (identity_key(game.blue_team) == identity_key(team)) {
        return Side::blue;
    }
    if (identity_key(game.red_team) == identity_key(team)) {
        return Side::red;
    }
    throw ValidationError("team did not play in the selected game");
}

} // namespace

Id RosterService::track_team(const std::string& team) {
    const auto name = trimmed_name(team);
    if (name.empty()) {
        throw ValidationError("a tracked team name is required");
    }
    return repository_.track_team(name);
}

std::vector<TrackedTeam> RosterService::tracked_teams() const {
    return repository_.tracked_teams();
}

void RosterService::replace_roster(const std::string& team,
                                   const std::vector<RolePlayer>& assignments) {
    const auto tracked = repository_.find_tracked_team(team);
    if (!tracked) {
        throw ValidationError("team is not tracked; run 'lolctl team track " + team + "'");
    }
    validate_complete_assignments(assignments, "a roster");
    repository_.replace_roster(TeamRoster{*tracked, assignments});
}

std::optional<TeamRoster> RosterService::roster(const std::string& team) const {
    return repository_.roster_for_team(team);
}

void RosterService::snapshot_available_lineups(const Id game_id) {
    const auto game = repository_.find_game(game_id);
    if (!game) {
        throw ValidationError("game does not exist");
    }
    const auto existing = repository_.lineups_for_game(game_id);
    for (const auto side : {Side::blue, Side::red}) {
        const auto& team = side == Side::blue ? game->blue_team : game->red_team;
        const auto tracked = repository_.find_tracked_team(team);
        const auto roster_record = repository_.roster_for_team(team);
        if (!tracked || !roster_record || roster_record->assignments.size() != 5) {
            continue;
        }
        const bool already_snapshotted = std::ranges::any_of(
            existing, [side](const GameLineup& lineup) { return lineup.side == side; });
        if (!already_snapshotted) {
            repository_.replace_game_lineup(
                GameLineup{game_id, side, team, roster_record->assignments});
        }
    }
}

void RosterService::set_game_lineup(const Id game_id, const std::string& team,
                                    const std::vector<RolePlayer>& overrides) {
    const auto game = repository_.find_game(game_id);
    if (!game) {
        throw ValidationError("game does not exist");
    }
    const auto side = side_for_team(*game, team);
    const auto& canonical_team = side == Side::blue ? game->blue_team : game->red_team;
    if (!repository_.find_tracked_team(canonical_team)) {
        throw ValidationError("team is not tracked; run 'lolctl team track " + canonical_team +
                              "'");
    }

    auto base = repository_.roster_for_team(canonical_team);
    if (!base || base->assignments.size() != 5) {
        throw ValidationError("tracked team needs a complete roster before setting a lineup");
    }
    auto assignments = base->assignments;
    if (!overrides.empty()) {
        const auto lineups = repository_.lineups_for_game(game_id);
        const auto existing = std::ranges::find_if(
            lineups, [side](const GameLineup& lineup) { return lineup.side == side; });
        if (existing != lineups.end()) {
            assignments = existing->assignments;
        }
        std::set<Role> overridden_roles;
        for (const auto& override : overrides) {
            if (identity_key(override.player).empty()) {
                throw ValidationError("player names cannot be blank");
            }
            if (!overridden_roles.insert(override.role).second) {
                throw ValidationError("a lineup role can only be overridden once");
            }
            const auto slot = std::ranges::find_if(assignments, [&](const RolePlayer& assignment) {
                return assignment.role == override.role;
            });
            if (slot == assignments.end()) {
                throw ValidationError("lineup is missing a required role");
            }
            slot->player = trimmed_name(override.player);
        }
    }
    validate_complete_assignments(assignments, "a game lineup");
    repository_.replace_game_lineup(GameLineup{game_id, side, canonical_team, assignments});
}

std::vector<GameLineup> RosterService::game_lineups(const Id game_id) const {
    if (!repository_.find_game(game_id)) {
        throw ValidationError("game does not exist");
    }
    return repository_.lineups_for_game(game_id);
}

} // namespace lol
