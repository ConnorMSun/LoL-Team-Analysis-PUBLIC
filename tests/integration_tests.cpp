#include "lol/application/analytics_service.hpp"
#include "lol/application/series_service.hpp"
#include "lol/infrastructure/sqlite_repository.hpp"

#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using lol::Side;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable>
void require_validation_error(Callable&& callable, const std::string& message) {
    try {
        callable();
    } catch (const lol::ValidationError&) {
        return;
    }
    throw std::runtime_error(message);
}

std::vector<lol::DraftAction> draft(const std::array<std::string, 20>& champions) {
    std::vector<lol::DraftAction> result;
    for (std::size_t index = 0; index < champions.size(); ++index) {
        const auto slot = lol::standard_draft_order[index];
        result.push_back(lol::DraftAction{static_cast<int>(index + 1), slot.type, slot.side,
                                          champions[index], std::nullopt, std::nullopt});
    }
    return result;
}

constexpr std::array<std::string, 20> game_one_champions{
    "Ambessa", "Vi",      "Yone",     "Ashe",  "Rumble",   "Maokai", "Aurora",
    "Sejuani", "Orianna", "Nautilus", "Jinx",  "Renekton", "Ezreal", "Rakan",
    "Leona",   "Varus",   "Azir",     "Poppy", "Kai'Sa",   "Gnar"};

constexpr std::array<std::string, 20> game_two_legal{
    "Skarner",  "Corki",   "Tristana", "Kalista", "Neeko",    "Draven", "Jayce",
    "Nocturne", "Syndra",  "Braum",    "Xayah",   "Ornn",     "Lucian", "Milio",
    "Zeri",     "Alistar", "Viktor",   "Gragas",  "Aphelios", "Kennen"};

void run() {
    require(lol::normalized_champion_name("jarvan iv") == "Jarvan IV",
            "Roman-numeral champion capitalization should be preserved");
    require(lol::normalized_champion_name("LEBLANC") == "LeBlanc",
            "internal champion capitalization should be canonical");

    lol::SqliteRepository repository(":memory:");
    lol::SeriesService series(repository);
    lol::AnalyticsService analytics(repository);

    require_validation_error(
        [&] { series.create_series({"Blue Whales", "Red Foxes", 1200, 1800, 1900.0, "26.15"}); },
        "invalid ELO average should fail");

    const auto series_id =
        series.create_series({"Blue Whales", "Red Foxes", 1200, 1800, 1512.5, "26.15"});
    auto first_game_draft = draft(game_one_champions);
    first_game_draft[10].champion = "jINX";
    const auto game_id = series.add_game(
        {series_id, 1, "Blue Whales", "Red Foxes", "Blue Whales", first_game_draft});
    require(game_id > 0, "game should be persisted");
    const auto stored_game = repository.find_game(game_id);
    require(stored_game && stored_game->draft[10].champion == "Jinx",
            "stored champion names should use normalized capitalization");

    auto repeated_champion = draft(game_two_legal);
    repeated_champion[6].champion = "AURORA";
    require_validation_error(
        [&] {
            series.add_game(
                {series_id, 2, "Red Foxes", "Blue Whales", "Red Foxes", repeated_champion});
        },
        "a prior pick should be rejected case-insensitively by Full Fearless");

    const auto second_game_id = series.add_game(
        {series_id, 2, "Red Foxes", "Blue Whales", "Red Foxes", draft(game_two_legal)});
    require(second_game_id > game_id, "second game should be persisted");

    lol::AnalysisFilter filter;
    filter.perspective_team = "Blue Whales";
    filter.champion = "aurora";
    filter.pick_ordinal = 1;
    filter.required_bans = {"Ambessa", "Vi"};
    filter.patch = "26.15";
    filter.side = Side::blue;
    const auto result = analytics.analyze(filter);
    require(result.matched_games == 1, "Aurora first-pick query should match one game");
    require(result.wins == 1 && result.losses == 0, "matched game outcome should be a win");
    require(result.associations.size() == 9,
            "query should return four allied and five opposing picks");

    bool found_jinx = false;
    bool found_sejuani = false;
    for (const auto& row : result.associations) {
        if (row.champion == "Jinx" && row.relationship == lol::Relationship::with) {
            found_jinx = row.wins == 1;
        }
        if (row.champion == "Sejuani" && row.relationship == lol::Relationship::against) {
            found_sejuani = row.wins == 1;
        }
    }
    require(found_jinx, "allied pick association should be classified as with");
    require(found_sejuani, "opposing pick association should be classified as against");

    filter.excluded_bans = {"Teemo"};
    require(analytics.analyze(filter).matched_games == 1,
            "an absent excluded ban should preserve the match");
    filter.required_bans = {"Vi"};
    filter.excluded_bans = {"Ambessa"};
    require(analytics.analyze(filter).matched_games == 0,
            "a present excluded ban should reject the game");
    filter.required_bans = {"Ambessa", "Vi"};
    require_validation_error([&] { static_cast<void>(analytics.analyze(filter)); },
                             "contradictory required and excluded bans should fail");
    filter.excluded_bans.clear();
    filter.required_bans.push_back("Teemo");
    require(analytics.analyze(filter).matched_games == 0,
            "all specified required bans should be present for a match");
}

} // namespace

int main() {
    try {
        run();
        std::cout << "all integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
