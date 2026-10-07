#include "lol/application/assignment_service.hpp"

#include <array>
#include <set>
#include <string>

namespace lol {

void AssignmentService::replace_assignments(const GameAssignments& assignments) {
    const auto game = repository_.find_game(assignments.game_id);
    if (!game) {
        throw ValidationError("game does not exist");
    }
    if (assignments.assignments.size() != 10) {
        throw ValidationError("role assignment requires all ten picked champions");
    }

    std::set<std::pair<Side, std::string>> available_picks;
    for (const auto& action : game->draft) {
        if (action.type == DraftActionType::pick) {
            available_picks.emplace(action.side, champion_key(action.champion));
        }
    }

    for (const auto side : {Side::blue, Side::red}) {
        std::set<Role> roles;
        std::set<std::string> champions;
        std::set<std::string> players;
        int side_count = 0;
        for (const auto& assignment : assignments.assignments) {
            if (assignment.side != side) {
                continue;
            }
            ++side_count;
            const auto key = champion_key(assignment.champion);
            if (!available_picks.contains({side, key})) {
                throw ValidationError("role assignment champion was not picked by " +
                                      to_string(side) + " side: " + assignment.champion);
            }
            if (!roles.insert(assignment.role).second) {
                throw ValidationError("each role must be assigned once per side");
            }
            if (!champions.insert(key).second) {
                throw ValidationError("each picked champion must be assigned once per side");
            }
            if (assignment.player && !players.insert(champion_key(*assignment.player)).second) {
                throw ValidationError("a player cannot receive two assignments in one game");
            }
        }
        if (side_count != 5 || roles.size() != 5) {
            throw ValidationError("each side requires top, jungle, mid, bot, and support");
        }
    }

    repository_.replace_assignments(assignments);
}

} // namespace lol
