#include "lol/application/analytics_service.hpp"
#include "lol/application/assignment_service.hpp"
#include "lol/application/roster_service.hpp"
#include "lol/application/series_service.hpp"
#include "lol/infrastructure/sqlite_repository.hpp"
#include "lol/infrastructure/workspace_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Options = std::map<std::string, std::vector<std::string>>;
using TableRow = std::vector<std::string>;

struct CommandLine {
    std::optional<std::filesystem::path> database;
    std::vector<std::string> arguments;
};

void print_overview(std::ostream& output) {
    output << "LoL Team Analysis " << LOL_ANALYTICS_VERSION << R"text(

Commands:
  init            Initialize or migrate the active database.
  db use PATH     Save a database selection for this directory.
  db show         Show the active database and how it was selected.
  db clear        Clear the nearest saved database selection.
  team track      Mark a team for player and roster tracking.
  team list       List tracked teams and roster status.
  roster set      Set a tracked team's default five-player roster.
  roster show     Show a tracked team's role-to-player defaults.
  series list     List recorded series and their current scores.
  series show     Show one series and all of its games.
  series add      Create a series and record teams, patch, and ELO.
  game show       Show one game's result, ordered draft, and final roles.
  game add        Import the next game and its ordered draft.
  game roles      Assign final roles after flex picks are resolved.
  game lineup     Show or override a snapshotted game lineup.
  stats           Query records and champion associations.
  help [TOPIC]    Show all commands or detailed help for a topic.

Global options:
  --db PATH       Override database selection for one command.
  --help, -h      Show contextual help without opening a database.
  --version       Print the installed version.

Database selection: --db PATH, then the nearest saved workspace, then analytics.sqlite.
Run 'lolctl help init|db|team|roster|series|game|stats' for syntax, options, and examples.
)text";
}

void print_team_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl [--db PATH] team track TEAM
  lolctl [--db PATH] team list

  track  Enable roster and player tracking for TEAM. Opposing teams do not need to be tracked.
  list   Show tracked teams and whether each has a complete default roster.

Examples:
  lolctl team track NCK
  lolctl team list
)text";
}

void print_roster_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl [--db PATH] roster set TEAM "TOP,JUNGLE,MID,BOT,SUPPORT"
  lolctl [--db PATH] roster show TEAM

The roster is the default for future games and is snapshotted when a game is added. Updating it does
not rewrite historical game lineups. Player names may contain spaces when the complete comma-separated
lineup is quoted.

Examples:
  lolctl roster set NCK "Alice,Bob,Carol,Dana,Evan"
  lolctl roster show NCK
)text";
}

void print_init_help(std::ostream& output) {
    output << R"text(Usage: lolctl [--db PATH] init

Initializes a new SQLite database or applies missing migrations to an existing database. Existing
analytics data is not deleted. Initialization is optional because normal commands initialize the
selected database automatically.

Examples:
  lolctl init
  lolctl --db tournament.sqlite init
)text";
}

void print_db_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl db use PATH
  lolctl db show
  lolctl db clear

  use    Validate or initialize PATH and save it in .lolctl for this directory and descendants.
  show   Print the resolved database, selection source, and workspace config path.
  clear  Remove the nearest .lolctl selection without deleting its SQLite database.

Selection precedence is an explicit --db option, the nearest workspace selection, then
analytics.sqlite in the current directory.

Examples:
  lolctl db use tournament.sqlite
  lolctl db show
  lolctl --db comparison.sqlite db show
  lolctl db clear
)text";
}

void print_series_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl [--db PATH] series list
  lolctl [--db PATH] series show SERIES_ID
  lolctl [--db PATH] series add TEAM_A TEAM_B --patch PATCH --elo LOW,HIGH,AVERAGE

  list  Show every series with teams, patch, ELO, game count, and current score.
  show  Show one series followed by its games, sides, and winners.
  add   Create a series between two distinct teams. ELO values describe the lowest player, highest
        player, and team average in that order. The average must fall within the supplied range.

Examples:
  lolctl series list
  lolctl series show 1
  lolctl series add NCK SKY --patch 26.15 --elo 1200,1800,1512.5
)text";
}

void print_game_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl [--db PATH] game show GAME_ID
  lolctl [--db PATH] game add SERIES_ID DRAFT_FILE --blue a|b --winner a|b
  lolctl [--db PATH] game roles GAME_ID
      [--blue TOP,JUNGLE,MID,BOT,SUPPORT] [--red TOP,JUNGLE,MID,BOT,SUPPORT]
  lolctl [--db PATH] game lineup GAME_ID
  lolctl [--db PATH] game lineup GAME_ID TEAM [--top PLAYER] [--jungle PLAYER]
      [--mid PLAYER] [--bot PLAYER] [--support PLAYER]

game show:
  Displays the resolved teams and result, then the complete chronological draft. Final roles appear
  beside assigned picks; bans display '-' and picks without assignments display 'unassigned'.

game add:
  Imports the series' next game. DRAFT_FILE contains exactly 20 champion names in draft order, one
  per line. Game number, red team, action types, and acting sides are inferred. Full Fearless and
  standard draft legality are validated before the transaction is committed.

game roles:
  Records final role assignments separately from the draft. Without --blue or --red, the command
  shows that side's picks and resolved team name, then prompts for champions in
  top,jungle,mid,bot,support order. Providing both options makes the operation non-interactive.
  Re-running it atomically replaces prior assignments.

game lineup:
  With only GAME_ID, show snapshotted player lineups. Supplying TEAM resets that side to its current
  roster, with any supplied role overrides applied. New games snapshot complete tracked rosters
  automatically; use overrides for substitutions or role swaps.

Examples:
  lolctl game show 1
  lolctl game add 1 drafts/game1.csv --blue a --winner a
  lolctl game roles 1
  lolctl game roles 1 --blue "Kennen,Vi,Aurora,Jinx,Nautilus" \
                       --red "Renekton,Sejuani,Orianna,Zeri,Rell"
  lolctl game lineup 1
  lolctl game lineup 1 NCK --mid Substitute
)text";
}

void print_stats_help(std::ostream& output) {
    output << R"text(Usage:
  lolctl [--db PATH] stats TEAM [OPTIONS]

Options:
  --pick CHAMPION[@ORDINAL]  Require TEAM to have picked a champion, optionally at overall pick N.
  --player PLAYER            Restrict results to one tracked player on TEAM. With --pick, that
                             player must have played the focal champion.
  --bans CHAMPION,...        Require listed bans; prefix a name with ! to require it was not banned.
  --role ROLE                Require the picked champion's final top/jungle/mid/bot/support role.
  --patch PATCH              Restrict results to one patch.
  --opponent TEAM            Restrict results to one opposing team.
  --side blue|red            Restrict TEAM to one side.

Without --role, matching remains role-agnostic and association rows are grouped by any stored final
roles. Only games without assignments display `unknown`. With --role, opposing associations are
limited to the opponent assigned to that same role. Role requires --pick. Player with --pick also
requires a final role assignment and a player lineup so the champion-to-player link is explicit.

Examples:
  lolctl stats NCK
  lolctl stats NCK --player "Mid Player"
  lolctl stats NCK --player "Mid Player" --pick Aurora@1
  lolctl stats NCK --pick Aurora@1 --bans Poppy,Ashe
  lolctl stats NCK --bans 'Poppy,Ashe,!Jinx'
  lolctl stats NCK --pick Aurora@1 --role mid --patch 26.15 --opponent SKY --side blue
)text";
}

void print_help_topic(const std::string& topic, std::ostream& output) {
    if (topic == "init") {
        print_init_help(output);
    } else if (topic == "db") {
        print_db_help(output);
    } else if (topic == "team") {
        print_team_help(output);
    } else if (topic == "roster") {
        print_roster_help(output);
    } else if (topic == "series") {
        print_series_help(output);
    } else if (topic == "game") {
        print_game_help(output);
    } else if (topic == "stats") {
        print_stats_help(output);
    } else if (topic == "all") {
        print_overview(output);
    } else {
        throw std::invalid_argument("unknown help topic: " + topic);
    }
}

bool contains_help_option(const std::vector<std::string>& arguments) {
    return std::ranges::any_of(arguments, [](const std::string& argument) {
        return argument == "--help" || argument == "-h";
    });
}

bool handle_informational_command(const std::vector<std::string>& arguments) {
    if (arguments.empty() || arguments == std::vector<std::string>{"--help"} ||
        arguments == std::vector<std::string>{"-h"} ||
        arguments == std::vector<std::string>{"help"}) {
        print_overview(std::cout);
        return true;
    }
    if (arguments == std::vector<std::string>{"--version"}) {
        std::cout << "lolctl " << LOL_ANALYTICS_VERSION << '\n';
        return true;
    }
    if (arguments[0] == "help") {
        if (arguments.size() != 2) {
            throw std::invalid_argument(
                "help accepts one topic: init, db, team, roster, series, game, or stats");
        }
        print_help_topic(arguments[1], std::cout);
        return true;
    }
    if (contains_help_option(arguments)) {
        print_help_topic(arguments[0], std::cout);
        return true;
    }
    return false;
}

CommandLine parse_command_line(const int argc, char** argv) {
    CommandLine result;
    bool database_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--db") {
            if (database_seen || index + 1 >= argc) {
                throw std::invalid_argument("--db requires one database path");
            }
            database_seen = true;
            result.database = std::filesystem::path(argv[++index]);
        } else {
            result.arguments.push_back(argument);
        }
    }
    return result;
}

Options parse_options(const std::vector<std::string>& arguments, const std::size_t first) {
    Options options;
    for (std::size_t index = first; index < arguments.size(); index += 2) {
        const auto& key = arguments[index];
        if (!key.starts_with("--") || index + 1 >= arguments.size()) {
            throw std::invalid_argument("options must use --name value pairs");
        }
        options[key].push_back(arguments[index + 1]);
    }
    return options;
}

void validate_options(const Options& options,
                      const std::initializer_list<std::string_view> allowed_options) {
    const std::set<std::string_view> allowed(allowed_options);
    for (const auto& [option, values] : options) {
        static_cast<void>(values);
        if (!allowed.contains(option)) {
            throw std::invalid_argument("unknown option: " + option);
        }
    }
}

const std::string& required(const Options& options, const std::string& key) {
    const auto iterator = options.find(key);
    if (iterator == options.end() || iterator->second.empty()) {
        throw std::invalid_argument("missing required option " + key);
    }
    return iterator->second.back();
}

std::optional<std::string> optional(const Options& options, const std::string& key) {
    const auto iterator = options.find(key);
    if (iterator == options.end() || iterator->second.empty()) {
        return std::nullopt;
    }
    return iterator->second.back();
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split_list(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(std::move(item));
        if (item.empty()) {
            throw std::invalid_argument("comma-separated lists cannot contain empty values");
        }
        result.push_back(std::move(item));
    }
    return result;
}

int parse_int(const std::string& value, const std::string& label) {
    int result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument(label + " must be an integer");
    }
    return result;
}

lol::Id parse_id(const std::string& value, const std::string& label) {
    lol::Id result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::invalid_argument(label + " must be an integer id");
    }
    return result;
}

double parse_double(const std::string& value, const std::string& label) {
    std::size_t consumed{};
    const double result = std::stod(value, &consumed);
    if (consumed != value.size()) {
        throw std::invalid_argument(label + " must be numeric");
    }
    return result;
}

lol::Side parse_side(const std::string& value) {
    if (value == "blue") {
        return lol::Side::blue;
    }
    if (value == "red") {
        return lol::Side::red;
    }
    throw std::invalid_argument("side must be blue or red");
}

lol::Role parse_role(const std::string& value) {
    if (value == "top") {
        return lol::Role::top;
    }
    if (value == "jungle") {
        return lol::Role::jungle;
    }
    if (value == "mid") {
        return lol::Role::mid;
    }
    if (value == "bot" || value == "adc") {
        return lol::Role::bot;
    }
    if (value == "support" || value == "sup") {
        return lol::Role::support;
    }
    throw std::invalid_argument("role must be top, jungle, mid, bot, or support");
}

std::string resolve_team_slot(const lol::Series& series, const std::string& value,
                              const std::string& option) {
    if (value == "a" || value == "A") {
        return series.team_a;
    }
    if (value == "b" || value == "B") {
        return series.team_b;
    }
    throw std::invalid_argument(option + " must reference series team a or b");
}

std::vector<lol::DraftAction> read_draft(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::invalid_argument("could not open draft file: " + path);
    }
    std::vector<lol::DraftAction> draft;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        line = trim(std::move(line));
        if (line.empty() || line.starts_with('#')) {
            continue;
        }
        if (draft.size() == lol::standard_draft_order.size()) {
            throw std::invalid_argument("a draft file must contain exactly 20 champion names");
        }
        const auto index = draft.size();
        const auto slot = lol::standard_draft_order[index];
        draft.push_back(lol::DraftAction{static_cast<int>(index + 1), slot.type, slot.side,
                                         std::move(line), std::nullopt, std::nullopt});
    }
    if (draft.size() != lol::standard_draft_order.size()) {
        throw std::invalid_argument("a draft file must contain exactly 20 champion names; found " +
                                    std::to_string(draft.size()));
    }
    return draft;
}

std::vector<std::string> picks_for_side(const lol::StoredGame& game, const lol::Side side) {
    std::vector<std::string> picks;
    for (const auto& action : game.draft) {
        if (action.type == lol::DraftActionType::pick && action.side == side) {
            picks.push_back(action.champion);
        }
    }
    return picks;
}

std::string join(const std::vector<std::string>& values) {
    std::ostringstream result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            result << ", ";
        }
        result << values[index];
    }
    return result.str();
}

std::vector<std::string> obtain_lineup(const std::optional<std::string>& supplied,
                                       const std::string& label,
                                       const std::vector<std::string>& available_picks) {
    if (supplied) {
        return split_list(*supplied);
    }
    std::cout << label << " picks: " << join(available_picks) << '\n';
    std::cout << "Enter top,jungle,mid,bot,support: " << std::flush;
    std::string input;
    if (!std::getline(std::cin, input)) {
        throw std::invalid_argument("role assignment input ended unexpectedly");
    }
    return split_list(input);
}

void append_assignments(std::vector<lol::RoleAssignment>& assignments, const lol::Side side,
                        const std::vector<std::string>& lineup) {
    constexpr std::array roles{lol::Role::top, lol::Role::jungle, lol::Role::mid, lol::Role::bot,
                               lol::Role::support};
    if (lineup.size() != roles.size()) {
        throw std::invalid_argument(
            "a role lineup requires five champions in top,jungle,mid,bot,support order");
    }
    for (std::size_t index = 0; index < roles.size(); ++index) {
        assignments.push_back({side, roles[index], lineup[index], std::nullopt});
    }
}

std::vector<lol::RolePlayer> role_players(const std::vector<std::string>& lineup) {
    constexpr std::array roles{lol::Role::top, lol::Role::jungle, lol::Role::mid, lol::Role::bot,
                               lol::Role::support};
    if (lineup.size() != roles.size()) {
        throw std::invalid_argument(
            "a roster requires five players in top,jungle,mid,bot,support order");
    }
    std::vector<lol::RolePlayer> assignments;
    assignments.reserve(roles.size());
    for (std::size_t index = 0; index < roles.size(); ++index) {
        assignments.push_back({roles[index], lineup[index]});
    }
    return assignments;
}

std::vector<lol::RolePlayer> lineup_overrides(const Options& options) {
    struct RoleOption {
        std::string_view name;
        lol::Role role;
    };
    constexpr std::array<RoleOption, 5> role_options{{
        {"--top", lol::Role::top},
        {"--jungle", lol::Role::jungle},
        {"--mid", lol::Role::mid},
        {"--bot", lol::Role::bot},
        {"--support", lol::Role::support},
    }};
    std::vector<lol::RolePlayer> result;
    for (const auto& [name, role] : role_options) {
        if (const auto player = optional(options, std::string(name))) {
            result.push_back({role, *player});
        }
    }
    return result;
}

void parse_pick_filter(const std::string& value, lol::AnalysisFilter& filter) {
    const auto separator = value.rfind('@');
    if (separator == std::string::npos) {
        filter.champion = value;
        return;
    }
    filter.champion = value.substr(0, separator);
    filter.pick_ordinal = parse_int(value.substr(separator + 1), "pick ordinal");
}

void parse_ban_filters(const std::string& value, lol::AnalysisFilter& filter) {
    for (auto ban : split_list(value)) {
        if (ban.starts_with('!')) {
            ban.erase(0, 1);
            ban = trim(std::move(ban));
            if (ban.empty() || ban.starts_with('!')) {
                throw std::invalid_argument("! in --bans must prefix exactly one champion name");
            }
            filter.excluded_bans.push_back(std::move(ban));
        } else {
            filter.required_bans.push_back(std::move(ban));
        }
    }
}

std::string percentage(const double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value * 100.0 << '%';
    return output.str();
}

void print_table(const TableRow& headings, const std::vector<TableRow>& rows,
                 const std::set<std::size_t>& right_aligned_columns) {
    std::vector<std::size_t> widths;
    widths.reserve(headings.size());
    for (const auto& heading : headings) {
        widths.push_back(heading.size());
    }
    for (const auto& row : rows) {
        if (row.size() != headings.size()) {
            throw std::logic_error("table row does not match its headings");
        }
        for (std::size_t column = 0; column < row.size(); ++column) {
            widths[column] = std::max(widths[column], row[column].size());
        }
    }

    const auto print_row = [&](const TableRow& row) {
        for (std::size_t column = 0; column < row.size(); ++column) {
            if (column != 0) {
                std::cout << "  ";
            }
            std::cout << (right_aligned_columns.contains(column) ? std::right : std::left)
                      << std::setw(static_cast<int>(widths[column])) << row[column];
        }
        std::cout << '\n';
    };

    print_row(headings);
    TableRow separators;
    separators.reserve(widths.size());
    for (const auto width : widths) {
        separators.emplace_back(width, '-');
    }
    print_row(separators);
    for (const auto& row : rows) {
        print_row(row);
    }
    std::cout << std::left;
}

std::string display_label(std::string value) {
    if (!value.empty()) {
        const auto character = static_cast<unsigned char>(value.front());
        value.front() = static_cast<char>(std::toupper(character));
    }
    return value;
}

std::string concise_number(const double value) {
    std::ostringstream output;
    output << value;
    return output.str();
}

void print_tracked_teams(const lol::RosterService& service) {
    const auto teams = service.tracked_teams();
    if (teams.empty()) {
        std::cout << "No teams tracked.\n";
        return;
    }
    std::vector<TableRow> rows;
    rows.reserve(teams.size());
    for (const auto& team : teams) {
        const auto roster = service.roster(team.name);
        const auto size = roster ? roster->assignments.size() : 0;
        rows.push_back({team.name, size == 5 ? "ready" : "not set"});
    }
    std::cout << "Tracked teams\n";
    print_table({"Team", "Roster"}, rows, {});
}

void print_roster(const lol::TeamRoster& roster) {
    std::cout << "Roster: " << roster.team.name << '\n';
    if (roster.assignments.empty()) {
        std::cout << "No default roster set.\n";
        return;
    }
    std::vector<TableRow> rows;
    rows.reserve(roster.assignments.size());
    for (const auto& assignment : roster.assignments) {
        rows.push_back({display_label(lol::to_string(assignment.role)), assignment.player});
    }
    print_table({"Role", "Player"}, rows, {});
}

void print_lineups(const std::vector<lol::GameLineup>& lineups) {
    if (lineups.empty()) {
        std::cout << "No player lineups recorded for this game.\n";
        return;
    }
    std::vector<TableRow> rows;
    for (const auto& lineup : lineups) {
        for (const auto& assignment : lineup.assignments) {
            rows.push_back({display_label(lol::to_string(lineup.side)), lineup.team,
                            display_label(lol::to_string(assignment.role)), assignment.player});
        }
    }
    print_table({"Side", "Team", "Role", "Player"}, rows, {});
}

TableRow series_row(const lol::Series& series, const std::vector<lol::StoredGame>& games) {
    const auto team_a_wins = static_cast<int>(std::ranges::count_if(
        games, [&](const lol::StoredGame& game) { return game.winner_team == series.team_a; }));
    const auto team_b_wins = static_cast<int>(games.size()) - team_a_wins;
    return {std::to_string(series.id),
            series.team_a,
            series.team_b,
            series.patch,
            std::to_string(series.elo_low) + "-" + std::to_string(series.elo_high),
            concise_number(series.elo_average),
            std::to_string(games.size()),
            std::to_string(team_a_wins) + "-" + std::to_string(team_b_wins)};
}

void print_series_list(const lol::Repository& repository) {
    const auto series_records = repository.list_series();
    if (series_records.empty()) {
        std::cout << "No series recorded.\n";
        return;
    }

    std::vector<TableRow> rows;
    rows.reserve(series_records.size());
    for (const auto& series : series_records) {
        rows.push_back(series_row(series, repository.games_in_series(series.id)));
    }
    std::cout << "Series\n";
    print_table({"ID", "Team A", "Team B", "Patch", "ELO range", "ELO avg", "Games", "Score A-B"},
                rows, {0, 5, 6, 7});
}

void print_series(const lol::Series& series, const std::vector<lol::StoredGame>& games) {
    std::cout << "Series " << series.id << '\n';
    print_table({"ID", "Team A", "Team B", "Patch", "ELO range", "ELO avg", "Games", "Score A-B"},
                {series_row(series, games)}, {0, 5, 6, 7});

    std::cout << "\nGames\n";
    if (games.empty()) {
        std::cout << "No games recorded.\n";
        return;
    }
    std::vector<TableRow> rows;
    rows.reserve(games.size());
    for (const auto& game : games) {
        rows.push_back({std::to_string(game.id), std::to_string(game.game_number), game.blue_team,
                        game.red_team, game.winner_team});
    }
    print_table({"ID", "Game", "Blue", "Red", "Winner"}, rows, {0, 1});
}

void print_game(const lol::Series& series, const lol::StoredGame& game,
                const std::vector<lol::RoleAssignment>& assignments) {
    std::cout << "Game " << game.id << '\n';
    print_table({"Series", "Game", "Patch"},
                {{std::to_string(series.id), std::to_string(game.game_number), series.patch}},
                {0, 1});

    std::cout << "\nResult\n";
    print_table({"Side", "Team", "Result"},
                {{"Blue", game.blue_team, game.winner_team == game.blue_team ? "Win" : "Loss"},
                 {"Red", game.red_team, game.winner_team == game.red_team ? "Win" : "Loss"}},
                {});

    std::map<std::pair<lol::Side, std::string>, lol::RoleAssignment> assigned_roles;
    for (const auto& assignment : assignments) {
        assigned_roles.emplace(std::pair{assignment.side, lol::champion_key(assignment.champion)},
                               assignment);
    }

    std::cout << "\nDraft\n";
    std::vector<TableRow> rows;
    rows.reserve(game.draft.size());
    for (const auto& action : game.draft) {
        std::string role = "-";
        std::string player = "-";
        if (action.type == lol::DraftActionType::pick) {
            const auto assignment =
                assigned_roles.find({action.side, lol::champion_key(action.champion)});
            role = assignment == assigned_roles.end() ? "unassigned"
                                                      : lol::to_string(assignment->second.role);
            if (assignment != assigned_roles.end() && assignment->second.player) {
                player = *assignment->second.player;
            }
        }
        rows.push_back({std::to_string(action.sequence), display_label(lol::to_string(action.type)),
                        display_label(lol::to_string(action.side)), action.champion,
                        std::move(role), std::move(player)});
    }
    print_table({"#", "Action", "Side", "Champion", "Role", "Player"}, rows, {0});
}

void print_analysis(const lol::AnalysisResult& result) {
    std::cout << "Summary\n";
    print_table({"Matched games", "Wins", "Losses", "Win rate"},
                {{std::to_string(result.matched_games), std::to_string(result.wins),
                  std::to_string(result.losses), percentage(result.win_rate())}},
                {0, 1, 2, 3});

    std::cout << "\nChampion associations\n";
    std::vector<TableRow> rows;
    rows.reserve(result.associations.size());
    for (const auto& row : result.associations) {
        rows.push_back({row.relationship == lol::Relationship::with ? "with" : "against",
                        row.champion, row.role ? lol::to_string(*row.role) : "unknown",
                        std::to_string(row.games), std::to_string(row.wins),
                        std::to_string(row.losses), percentage(row.win_rate())});
    }
    print_table({"Relationship", "Champion", "Role", "Games", "Wins", "Losses", "Win rate"}, rows,
                {3, 4, 5, 6});
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto command_line = parse_command_line(argc, argv);
        const auto& arguments = command_line.arguments;
        if (handle_informational_command(arguments)) {
            return 0;
        }

        if (arguments[0] == "db") {
            if (arguments == std::vector<std::string>{"db", "show"}) {
                const auto selection = lol::WorkspaceConfig::resolve(command_line.database);
                std::cout << "database=" << selection.database_path.string() << '\n';
                std::cout << "source=" << lol::to_string(selection.source) << '\n';
                if (selection.config_path) {
                    std::cout << "config=" << selection.config_path->string() << '\n';
                }
                return 0;
            }
            if (arguments.size() == 3 && arguments[1] == "use") {
                if (command_line.database) {
                    throw std::invalid_argument("db use cannot be combined with --db");
                }
                const std::filesystem::path requested_path = arguments[2];
                const auto selected = lol::WorkspaceConfig::resolve(requested_path);
                lol::SqliteRepository selected_repository(selected.database_path.string());
                const auto config_path = lol::WorkspaceConfig::save(requested_path);
                std::cout << "database=" << selected.database_path.string() << '\n';
                std::cout << "config=" << config_path.string() << '\n';
                return 0;
            }
            if (arguments == std::vector<std::string>{"db", "clear"}) {
                if (command_line.database) {
                    throw std::invalid_argument("db clear cannot be combined with --db");
                }
                if (const auto removed = lol::WorkspaceConfig::clear()) {
                    std::cout << "cleared=" << removed->string() << '\n';
                } else {
                    std::cout << "no workspace database selection found\n";
                }
                return 0;
            }
            throw std::invalid_argument("use db use PATH, db show, or db clear");
        }

        const auto database_selection = lol::WorkspaceConfig::resolve(command_line.database);
        lol::SqliteRepository repository(database_selection.database_path.string());
        if (arguments == std::vector<std::string>{"init"}) {
            std::cout << "initialized " << database_selection.database_path.string() << '\n';
            return 0;
        }

        if (arguments == std::vector<std::string>{"team", "list"}) {
            print_tracked_teams(lol::RosterService(repository));
            return 0;
        }

        if (arguments.size() == 3 && arguments[0] == "team" && arguments[1] == "track") {
            lol::RosterService service(repository);
            const auto team_id = service.track_team(arguments[2]);
            std::cout << "team_id=" << team_id << " team=" << lol::trimmed_name(arguments[2])
                      << '\n';
            return 0;
        }

        if (arguments.size() == 4 && arguments[0] == "roster" && arguments[1] == "set") {
            lol::RosterService service(repository);
            service.replace_roster(arguments[2], role_players(split_list(arguments[3])));
            const auto roster = service.roster(arguments[2]);
            if (!roster) {
                throw lol::StorageError("saved roster could not be read");
            }
            print_roster(*roster);
            return 0;
        }

        if (arguments.size() == 3 && arguments[0] == "roster" && arguments[1] == "show") {
            const auto roster = lol::RosterService(repository).roster(arguments[2]);
            if (!roster) {
                throw std::invalid_argument("team is not tracked");
            }
            print_roster(*roster);
            return 0;
        }

        if (arguments == std::vector<std::string>{"series", "list"}) {
            print_series_list(repository);
            return 0;
        }

        if (arguments.size() == 3 && arguments[0] == "series" && arguments[1] == "show") {
            const auto series_id = parse_id(arguments[2], "SERIES_ID");
            const auto series = repository.find_series(series_id);
            if (!series) {
                throw std::invalid_argument("SERIES_ID does not reference an existing series");
            }
            print_series(*series, repository.games_in_series(series_id));
            return 0;
        }

        if (arguments.size() >= 2 && arguments[0] == "series" && arguments[1] == "add") {
            if (arguments.size() < 4) {
                throw std::invalid_argument("series add requires TEAM_A and TEAM_B");
            }
            const auto options = parse_options(arguments, 4);
            validate_options(options, {"--patch", "--elo"});
            const auto elo = split_list(required(options, "--elo"));
            if (elo.size() != 3) {
                throw std::invalid_argument("--elo requires LOW,HIGH,AVERAGE");
            }
            const lol::SeriesDefinition definition{arguments[2],
                                                   arguments[3],
                                                   parse_int(elo[0], "ELO low"),
                                                   parse_int(elo[1], "ELO high"),
                                                   parse_double(elo[2], "ELO average"),
                                                   required(options, "--patch")};
            std::cout << "series_id=" << lol::SeriesService(repository).create_series(definition)
                      << '\n';
            return 0;
        }

        if (arguments.size() == 3 && arguments[0] == "game" && arguments[1] == "show") {
            const auto game_id = parse_id(arguments[2], "GAME_ID");
            const auto game = repository.find_game(game_id);
            if (!game) {
                throw std::invalid_argument("GAME_ID does not reference an existing game");
            }
            const auto series = repository.find_series(game->series_id);
            if (!series) {
                throw lol::StorageError("game references a missing series");
            }
            print_game(*series, *game, repository.assignments_for_game(game_id));
            return 0;
        }

        if (arguments.size() == 3 && arguments[0] == "game" && arguments[1] == "lineup") {
            const auto game_id = parse_id(arguments[2], "GAME_ID");
            std::cout << "Game " << game_id << " lineups\n";
            print_lineups(lol::RosterService(repository).game_lineups(game_id));
            return 0;
        }

        if (arguments.size() >= 4 && arguments[0] == "game" && arguments[1] == "lineup") {
            const auto options = parse_options(arguments, 4);
            validate_options(options, {"--top", "--jungle", "--mid", "--bot", "--support"});
            const auto game_id = parse_id(arguments[2], "GAME_ID");
            lol::RosterService service(repository);
            service.set_game_lineup(game_id, arguments[3], lineup_overrides(options));
            std::cout << "Game " << game_id << " lineups\n";
            print_lineups(service.game_lineups(game_id));
            return 0;
        }

        if (arguments.size() >= 2 && arguments[0] == "game" && arguments[1] == "add") {
            if (arguments.size() < 4) {
                throw std::invalid_argument("game add requires SERIES_ID and DRAFT_FILE");
            }
            const auto options = parse_options(arguments, 4);
            validate_options(options, {"--blue", "--winner"});
            const auto series_id = parse_id(arguments[2], "SERIES_ID");
            const auto series = repository.find_series(series_id);
            if (!series) {
                throw std::invalid_argument("SERIES_ID does not reference an existing series");
            }
            const auto blue_team =
                resolve_team_slot(*series, required(options, "--blue"), "--blue");
            const auto red_team = blue_team == series->team_a ? series->team_b : series->team_a;
            const auto games = repository.games_in_series(series_id);
            const auto game_number = static_cast<int>(games.size() + 1);
            const lol::GameDefinition definition{
                series_id,
                game_number,
                blue_team,
                red_team,
                resolve_team_slot(*series, required(options, "--winner"), "--winner"),
                read_draft(arguments[3])};
            const auto game_id = lol::SeriesService(repository).add_game(definition);
            lol::RosterService(repository).snapshot_available_lineups(game_id);
            std::cout << "game_id=" << game_id << " game_number=" << game_number << '\n';
            return 0;
        }

        if (arguments.size() >= 2 && arguments[0] == "game" && arguments[1] == "roles") {
            if (arguments.size() < 3) {
                throw std::invalid_argument("game roles requires GAME_ID");
            }
            const auto options = parse_options(arguments, 3);
            validate_options(options, {"--blue", "--red"});
            const auto game_id = parse_id(arguments[2], "GAME_ID");
            const auto game = repository.find_game(game_id);
            if (!game) {
                throw std::invalid_argument("GAME_ID does not reference an existing game");
            }
            const auto lineup_blue =
                obtain_lineup(optional(options, "--blue"), "Blue (" + game->blue_team + ")",
                              picks_for_side(*game, lol::Side::blue));
            const auto lineup_red =
                obtain_lineup(optional(options, "--red"), "Red (" + game->red_team + ")",
                              picks_for_side(*game, lol::Side::red));
            lol::GameAssignments game_assignments{game_id, {}};
            append_assignments(game_assignments.assignments, lol::Side::blue, lineup_blue);
            append_assignments(game_assignments.assignments, lol::Side::red, lineup_red);
            lol::AssignmentService(repository).replace_assignments(game_assignments);
            std::cout << "roles_saved=" << game_id << '\n';
            return 0;
        }

        if (arguments[0] == "stats") {
            if (arguments.size() < 2) {
                throw std::invalid_argument("stats requires TEAM");
            }
            const auto options = parse_options(arguments, 2);
            validate_options(options, {"--pick", "--player", "--bans", "--role", "--patch",
                                       "--opponent", "--side"});
            lol::AnalysisFilter filter;
            filter.perspective_team = arguments[1];
            if (const auto pick = optional(options, "--pick")) {
                parse_pick_filter(*pick, filter);
            }
            filter.player = optional(options, "--player");
            if (const auto bans = optional(options, "--bans")) {
                parse_ban_filters(*bans, filter);
            }
            if (const auto role = optional(options, "--role")) {
                filter.role = parse_role(*role);
            }
            filter.patch = optional(options, "--patch");
            filter.opponent = optional(options, "--opponent");
            if (const auto side = optional(options, "--side")) {
                filter.side = parse_side(*side);
            }
            print_analysis(lol::AnalyticsService(repository).analyze(filter));
            return 0;
        }

        throw std::invalid_argument("unknown command");
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        std::cerr << "Run 'lolctl help' to list commands.\n";
        return 1;
    }
}
