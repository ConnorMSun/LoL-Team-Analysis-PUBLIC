#include "lol/application/analytics_service.hpp"

#include <cctype>
#include <set>

namespace lol {
namespace {

bool only_whitespace(const std::string& value) {
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isspace(character) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace

AnalysisResult AnalyticsService::analyze(const AnalysisFilter& filter) const {
    if (filter.perspective_team.empty() || only_whitespace(filter.perspective_team)) {
        throw ValidationError("an analytics query requires a perspective team");
    }
    if (filter.pick_ordinal && (*filter.pick_ordinal < 1 || *filter.pick_ordinal > 10)) {
        throw ValidationError("pick ordinal must be between 1 and 10");
    }
    if (filter.pick_ordinal && !filter.champion) {
        throw ValidationError("pick ordinal requires a champion filter");
    }
    if (filter.role && !filter.champion) {
        throw ValidationError("role requires a champion filter");
    }
    if (filter.champion && champion_key(*filter.champion).empty()) {
        throw ValidationError("champion filter cannot be blank");
    }
    if (filter.player && identity_key(*filter.player).empty()) {
        throw ValidationError("player filter cannot be blank");
    }
    std::set<std::string> required_ban_keys;
    for (const auto& ban : filter.required_bans) {
        if (champion_key(ban).empty()) {
            throw ValidationError("required bans cannot be blank");
        }
        required_ban_keys.insert(champion_key(ban));
    }
    for (const auto& ban : filter.excluded_bans) {
        const auto key = champion_key(ban);
        if (key.empty()) {
            throw ValidationError("excluded bans cannot be blank");
        }
        if (required_ban_keys.contains(key)) {
            throw ValidationError("a champion cannot be both required and excluded from bans: " +
                                  ban);
        }
    }
    return repository_.analyze(filter);
}

} // namespace lol
