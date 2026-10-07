#include "lol/domain/model.hpp"

#include <algorithm>
#include <cctype>

namespace lol {

std::string to_string(const Side side) { return side == Side::blue ? "blue" : "red"; }

std::string to_string(const DraftActionType type) {
    return type == DraftActionType::ban ? "ban" : "pick";
}

std::string to_string(const Role role) {
    switch (role) {
    case Role::top:
        return "top";
    case Role::jungle:
        return "jungle";
    case Role::mid:
        return "mid";
    case Role::bot:
        return "bot";
    case Role::support:
        return "support";
    }
    throw ValidationError("unknown role");
}

std::string champion_key(const std::string& champion) {
    std::string key;
    key.reserve(champion.size());
    for (const char raw_character : champion) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalnum(character) != 0) {
            key.push_back(static_cast<char>(std::tolower(character)));
        }
    }
    return key;
}

std::string normalized_champion_name(const std::string& champion) {
    std::string result = trimmed_name(champion);

    bool begins_word = true;
    for (char& raw_character : result) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (std::isalpha(character) != 0) {
            raw_character =
                static_cast<char>(begins_word ? std::toupper(character) : std::tolower(character));
            begins_word = false;
        } else if (std::isdigit(character) != 0) {
            begins_word = false;
        } else {
            begins_word = true;
        }
    }
    if (champion_key(result) == "leblanc") {
        return "LeBlanc";
    }
    if (result == "Jarvan Iv") {
        return "Jarvan IV";
    }
    return result;
}

std::string trimmed_name(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string identity_key(const std::string& value) {
    auto key = trimmed_name(value);
    std::ranges::transform(key, key.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return key;
}

} // namespace lol
