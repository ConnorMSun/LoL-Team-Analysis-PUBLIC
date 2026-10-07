#include "lol/application/series_service.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace lol {
namespace {

bool blank(const std::string& value) {
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char character) { return std::isspace(character) != 0; });
}

void validate_draft(const std::vector<DraftAction>& draft, const std::set<std::string>& fearless) {
    if (draft.size() != standard_draft_order.size()) {
        throw ValidationError("a completed draft must contain exactly 20 ordered actions");
    }

    std::set<std::string> champions;
    for (std::size_t index = 0; index < draft.size(); ++index) {
        const auto& action = draft[index];
        const auto expected_sequence = static_cast<int>(index + 1);
        if (action.sequence != expected_sequence) {
            throw ValidationError("draft action sequences must be contiguous from 1 through 20");
        }
        const auto expected = standard_draft_order[index];
        if (action.type != expected.type || action.side != expected.side) {
            std::ostringstream message;
            message << "draft action " << action.sequence
                    << " does not follow the standard tournament order";
            throw ValidationError(message.str());
        }
        const auto key = champion_key(action.champion);
        if (key.empty()) {
            throw ValidationError("draft champions cannot be blank");
        }
        if (!champions.insert(key).second) {
            throw ValidationError("a champion cannot occur more than once in a game draft: " +
                                  action.champion);
        }
        if (fearless.contains(key)) {
            throw ValidationError("champion is unavailable due to Full Fearless: " +
                                  action.champion);
        }
        if (action.type == DraftActionType::ban && (action.player || action.role)) {
            throw ValidationError("ban actions cannot have a player or role assignment");
        }
    }
}

} // namespace

Id SeriesService::create_series(const SeriesDefinition& definition) {
    if (blank(definition.team_a) || blank(definition.team_b)) {
        throw ValidationError("both series team names are required");
    }
    if (definition.team_a == definition.team_b) {
        throw ValidationError("a series requires two distinct teams");
    }
    if (definition.elo_low < 0 || definition.elo_high < definition.elo_low ||
        !std::isfinite(definition.elo_average) || definition.elo_average < definition.elo_low ||
        definition.elo_average > definition.elo_high) {
        throw ValidationError("ELO must satisfy 0 <= low <= average <= high");
    }
    if (blank(definition.patch)) {
        throw ValidationError("a series patch is required");
    }
    return repository_.insert_series(definition);
}

Id SeriesService::add_game(const GameDefinition& definition) {
    const auto series = repository_.find_series(definition.series_id);
    if (!series) {
        throw ValidationError("series does not exist");
    }
    const bool teams_match =
        (definition.blue_team == series->team_a && definition.red_team == series->team_b) ||
        (definition.blue_team == series->team_b && definition.red_team == series->team_a);
    if (!teams_match) {
        throw ValidationError("the blue and red teams must be the two teams in the series");
    }
    if (definition.winner_team != definition.blue_team &&
        definition.winner_team != definition.red_team) {
        throw ValidationError("the winner must be the blue or red team");
    }

    const auto existing_games = repository_.games_in_series(definition.series_id);
    const auto next_game_number = static_cast<int>(existing_games.size() + 1);
    if (definition.game_number != next_game_number) {
        throw ValidationError("games must be ingested in series order; expected game " +
                              std::to_string(next_game_number));
    }

    std::set<std::string> fearless;
    for (const auto& game : existing_games) {
        for (const auto& action : game.draft) {
            if (action.type == DraftActionType::pick) {
                fearless.insert(champion_key(action.champion));
            }
        }
    }
    validate_draft(definition.draft, fearless);
    return repository_.insert_game(definition);
}

} // namespace lol
