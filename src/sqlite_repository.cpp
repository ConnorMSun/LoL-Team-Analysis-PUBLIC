#include "lol/infrastructure/sqlite_repository.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <compare>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

namespace lol {
namespace {

void throw_sqlite(sqlite3* database, const std::string& context) {
    throw StorageError(context + ": " + sqlite3_errmsg(database));
}

void execute(sqlite3* database, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(database, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error == nullptr ? sqlite3_errmsg(database) : error;
        sqlite3_free(error);
        throw StorageError(message);
    }
}

class Statement {
  public:
    Statement(sqlite3* database, const std::string& sql) : database_(database) {
        if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK) {
            throw_sqlite(database, "could not prepare SQL statement");
        }
    }

    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const int index, const std::string& value) {
        if (sqlite3_bind_text(statement_, index, value.c_str(), -1, SQLITE_TRANSIENT) !=
            SQLITE_OK) {
            throw_sqlite(database_, "could not bind text parameter");
        }
    }

    void bind(const int index, const int value) {
        if (sqlite3_bind_int(statement_, index, value) != SQLITE_OK) {
            throw_sqlite(database_, "could not bind integer parameter");
        }
    }

    void bind(const int index, const Id value) {
        if (sqlite3_bind_int64(statement_, index, value) != SQLITE_OK) {
            throw_sqlite(database_, "could not bind id parameter");
        }
    }

    void bind(const int index, const double value) {
        if (sqlite3_bind_double(statement_, index, value) != SQLITE_OK) {
            throw_sqlite(database_, "could not bind numeric parameter");
        }
    }

    void bind_optional(const int index, const std::optional<std::string>& value) {
        if (value) {
            bind(index, *value);
        } else if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw_sqlite(database_, "could not bind null parameter");
        }
    }

    void bind_null(const int index) {
        if (sqlite3_bind_null(statement_, index) != SQLITE_OK) {
            throw_sqlite(database_, "could not bind null parameter");
        }
    }

    [[nodiscard]] bool row() {
        const int result = sqlite3_step(statement_);
        if (result == SQLITE_ROW) {
            return true;
        }
        if (result == SQLITE_DONE) {
            return false;
        }
        throw_sqlite(database_, "SQL query failed");
        return false;
    }

    void done() {
        if (sqlite3_step(statement_) != SQLITE_DONE) {
            throw_sqlite(database_, "SQL command failed");
        }
    }

    [[nodiscard]] Id id(const int column) const { return sqlite3_column_int64(statement_, column); }
    [[nodiscard]] int integer(const int column) const {
        return sqlite3_column_int(statement_, column);
    }
    [[nodiscard]] double number(const int column) const {
        return sqlite3_column_double(statement_, column);
    }
    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        return value == nullptr ? std::string{} : reinterpret_cast<const char*>(value);
    }
    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) {
            return std::nullopt;
        }
        return text(column);
    }

  private:
    sqlite3* database_{};
    sqlite3_stmt* statement_{};
};

class Transaction {
  public:
    explicit Transaction(sqlite3* database) : database_(database) {
        execute(database_, "BEGIN IMMEDIATE");
    }
    ~Transaction() {
        if (!committed_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    void commit() {
        execute(database_, "COMMIT");
        committed_ = true;
    }

  private:
    sqlite3* database_{};
    bool committed_{};
};

Side parse_side(const std::string& side) { return side == "blue" ? Side::blue : Side::red; }

DraftActionType parse_action_type(const std::string& type) {
    return type == "ban" ? DraftActionType::ban : DraftActionType::pick;
}

Role parse_role(const std::string& role) {
    if (role == "top") {
        return Role::top;
    }
    if (role == "jungle") {
        return Role::jungle;
    }
    if (role == "mid") {
        return Role::mid;
    }
    if (role == "bot") {
        return Role::bot;
    }
    return Role::support;
}

using Parameter = std::variant<std::string, int>;

void bind_parameters(Statement& statement, const std::vector<Parameter>& parameters) {
    int index = 1;
    for (const auto& parameter : parameters) {
        std::visit([&](const auto& value) { statement.bind(index, value); }, parameter);
        ++index;
    }
}

struct AggregateKey {
    Relationship relationship{};
    std::string champion_key;
    std::string role;

    auto operator<=>(const AggregateKey&) const = default;
};

Id ensure_player(sqlite3* database, const std::string& player) {
    const auto display_name = trimmed_name(player);
    Statement insert(database, R"sql(
        INSERT INTO players(display_name, player_key) VALUES (?, ?)
        ON CONFLICT(player_key) DO UPDATE SET display_name = excluded.display_name
    )sql");
    insert.bind(1, display_name);
    insert.bind(2, identity_key(display_name));
    insert.done();

    Statement select(database, "SELECT id FROM players WHERE player_key = ?");
    select.bind(1, identity_key(display_name));
    if (!select.row()) {
        throw StorageError("could not resolve stored player");
    }
    return select.id(0);
}

} // namespace

SqliteRepository::SqliteRepository(const std::string& database_path) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    if (sqlite3_open_v2(database_path.c_str(), &database_, flags, nullptr) != SQLITE_OK) {
        const std::string message =
            database_ == nullptr ? "unknown SQLite error" : sqlite3_errmsg(database_);
        sqlite3_close(database_);
        database_ = nullptr;
        throw StorageError("could not open database: " + message);
    }
    sqlite3_busy_timeout(database_, 5'000);
    try {
        migrate();
    } catch (...) {
        sqlite3_close(database_);
        database_ = nullptr;
        throw;
    }
}

SqliteRepository::~SqliteRepository() { sqlite3_close(database_); }

void SqliteRepository::migrate() {
    execute(database_, "PRAGMA foreign_keys = ON");
    execute(database_, R"sql(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY,
            applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS series_records (
            id INTEGER PRIMARY KEY,
            team_a TEXT NOT NULL,
            team_b TEXT NOT NULL,
            elo_low INTEGER NOT NULL CHECK (elo_low >= 0),
            elo_high INTEGER NOT NULL CHECK (elo_high >= elo_low),
            elo_average REAL NOT NULL CHECK (elo_average >= elo_low AND elo_average <= elo_high),
            patch TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            CHECK (team_a <> team_b)
        );

        CREATE TABLE IF NOT EXISTS games (
            id INTEGER PRIMARY KEY,
            series_id INTEGER NOT NULL REFERENCES series_records(id) ON DELETE CASCADE,
            game_number INTEGER NOT NULL CHECK (game_number > 0),
            blue_team TEXT NOT NULL,
            red_team TEXT NOT NULL,
            winner_team TEXT NOT NULL,
            created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
            UNIQUE (series_id, game_number),
            CHECK (blue_team <> red_team),
            CHECK (winner_team = blue_team OR winner_team = red_team)
        );

        CREATE TABLE IF NOT EXISTS draft_actions (
            id INTEGER PRIMARY KEY,
            game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            sequence INTEGER NOT NULL CHECK (sequence BETWEEN 1 AND 20),
            action_type TEXT NOT NULL CHECK (action_type IN ('ban', 'pick')),
            side TEXT NOT NULL CHECK (side IN ('blue', 'red')),
            champion TEXT NOT NULL,
            champion_key TEXT NOT NULL,
            pick_ordinal INTEGER,
            player TEXT,
            role TEXT,
            UNIQUE (game_id, sequence),
            UNIQUE (game_id, champion_key),
            CHECK ((action_type = 'pick' AND pick_ordinal IS NOT NULL) OR
                   (action_type = 'ban' AND pick_ordinal IS NULL))
        );

        CREATE TABLE IF NOT EXISTS game_assignments (
            id INTEGER PRIMARY KEY,
            game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            side TEXT NOT NULL CHECK (side IN ('blue', 'red')),
            role TEXT NOT NULL CHECK (role IN ('top', 'jungle', 'mid', 'bot', 'support')),
            champion TEXT NOT NULL,
            champion_key TEXT NOT NULL,
            player TEXT,
            UNIQUE (game_id, side, role),
            UNIQUE (game_id, side, champion_key)
        );

        CREATE TABLE IF NOT EXISTS tracked_teams (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL,
            name_key TEXT NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS players (
            id INTEGER PRIMARY KEY,
            display_name TEXT NOT NULL,
            player_key TEXT NOT NULL UNIQUE
        );

        CREATE TABLE IF NOT EXISTS team_rosters (
            team_id INTEGER NOT NULL REFERENCES tracked_teams(id) ON DELETE CASCADE,
            role TEXT NOT NULL CHECK (role IN ('top', 'jungle', 'mid', 'bot', 'support')),
            player_id INTEGER NOT NULL REFERENCES players(id),
            PRIMARY KEY (team_id, role),
            UNIQUE (team_id, player_id)
        );

        CREATE TABLE IF NOT EXISTS game_lineups (
            game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            side TEXT NOT NULL CHECK (side IN ('blue', 'red')),
            role TEXT NOT NULL CHECK (role IN ('top', 'jungle', 'mid', 'bot', 'support')),
            player_id INTEGER NOT NULL REFERENCES players(id),
            PRIMARY KEY (game_id, side, role),
            UNIQUE (game_id, side, player_id)
        );

        CREATE INDEX IF NOT EXISTS idx_series_patch ON series_records(patch);
        CREATE INDEX IF NOT EXISTS idx_games_series ON games(series_id, game_number);
        CREATE INDEX IF NOT EXISTS idx_games_blue ON games(blue_team);
        CREATE INDEX IF NOT EXISTS idx_games_red ON games(red_team);
        CREATE INDEX IF NOT EXISTS idx_games_winner ON games(winner_team);
        CREATE INDEX IF NOT EXISTS idx_draft_champion ON draft_actions(champion_key, action_type, pick_ordinal);
        CREATE INDEX IF NOT EXISTS idx_draft_game_type_side ON draft_actions(game_id, action_type, side);
        CREATE INDEX IF NOT EXISTS idx_assignment_champion_role
            ON game_assignments(champion_key, role, side);
        CREATE INDEX IF NOT EXISTS idx_game_lineup_player
            ON game_lineups(player_id, game_id, side, role);

        INSERT OR IGNORE INTO schema_migrations(version) VALUES (1);
        INSERT OR IGNORE INTO schema_migrations(version) VALUES (2);
        INSERT OR IGNORE INTO schema_migrations(version) VALUES (5);
    )sql");

    const auto apply_champion_name_migration = [&](const int version) {
        bool applied = false;
        {
            Statement migration(database_, "SELECT 1 FROM schema_migrations WHERE version = ?");
            migration.bind(1, version);
            applied = migration.row();
        }
        if (applied) {
            return;
        }

        Transaction transaction(database_);
        for (const std::string_view table : {"draft_actions", "game_assignments"}) {
            std::vector<std::pair<Id, std::string>> champions;
            {
                Statement select(database_,
                                 "SELECT id, champion FROM " + std::string(table) + " ORDER BY id");
                while (select.row()) {
                    champions.emplace_back(select.id(0), select.text(1));
                }
            }
            for (const auto& [id, champion] : champions) {
                const auto normalized = normalized_champion_name(champion);
                Statement update(database_, "UPDATE " + std::string(table) +
                                                " SET champion = ?, champion_key = ? WHERE id = ?");
                update.bind(1, normalized);
                update.bind(2, champion_key(normalized));
                update.bind(3, id);
                update.done();
            }
        }
        Statement record(database_, "INSERT INTO schema_migrations(version) VALUES (?)");
        record.bind(1, version);
        record.done();
        transaction.commit();
    };
    apply_champion_name_migration(3);
    apply_champion_name_migration(4);
}

Id SqliteRepository::insert_series(const SeriesDefinition& definition) {
    Statement statement(database_, R"sql(
        INSERT INTO series_records(team_a, team_b, elo_low, elo_high, elo_average, patch)
        VALUES (?, ?, ?, ?, ?, ?)
    )sql");
    statement.bind(1, definition.team_a);
    statement.bind(2, definition.team_b);
    statement.bind(3, definition.elo_low);
    statement.bind(4, definition.elo_high);
    statement.bind(5, definition.elo_average);
    statement.bind(6, definition.patch);
    statement.done();
    return sqlite3_last_insert_rowid(database_);
}

std::vector<Series> SqliteRepository::list_series() const {
    Statement statement(database_, R"sql(
        SELECT id, team_a, team_b, elo_low, elo_high, elo_average, patch
        FROM series_records ORDER BY id
    )sql");
    std::vector<Series> result;
    while (statement.row()) {
        result.push_back(
            Series{SeriesDefinition{statement.text(1), statement.text(2), statement.integer(3),
                                    statement.integer(4), statement.number(5), statement.text(6)},
                   statement.id(0)});
    }
    return result;
}

std::optional<Series> SqliteRepository::find_series(const Id id) const {
    Statement statement(database_, R"sql(
        SELECT id, team_a, team_b, elo_low, elo_high, elo_average, patch
        FROM series_records WHERE id = ?
    )sql");
    statement.bind(1, id);
    if (!statement.row()) {
        return std::nullopt;
    }
    return Series{SeriesDefinition{statement.text(1), statement.text(2), statement.integer(3),
                                   statement.integer(4), statement.number(5), statement.text(6)},
                  statement.id(0)};
}

std::optional<StoredGame> SqliteRepository::find_game(const Id id) const {
    Statement game_row(database_, R"sql(
        SELECT id, series_id, game_number, blue_team, red_team, winner_team
        FROM games WHERE id = ?
    )sql");
    game_row.bind(1, id);
    if (!game_row.row()) {
        return std::nullopt;
    }

    StoredGame game;
    game.id = game_row.id(0);
    game.series_id = game_row.id(1);
    game.game_number = game_row.integer(2);
    game.blue_team = game_row.text(3);
    game.red_team = game_row.text(4);
    game.winner_team = game_row.text(5);

    Statement actions(database_, R"sql(
        SELECT sequence, action_type, side, champion, player, role
        FROM draft_actions WHERE game_id = ? ORDER BY sequence
    )sql");
    actions.bind(1, game.id);
    while (actions.row()) {
        game.draft.push_back(DraftAction{actions.integer(0), parse_action_type(actions.text(1)),
                                         parse_side(actions.text(2)), actions.text(3),
                                         actions.optional_text(4), actions.optional_text(5)});
    }
    return game;
}

std::vector<StoredGame> SqliteRepository::games_in_series(const Id series_id) const {
    Statement games(database_, R"sql(
        SELECT id, series_id, game_number, blue_team, red_team, winner_team
        FROM games WHERE series_id = ? ORDER BY game_number
    )sql");
    games.bind(1, series_id);
    std::vector<StoredGame> result;
    while (games.row()) {
        StoredGame game;
        game.id = games.id(0);
        game.series_id = games.id(1);
        game.game_number = games.integer(2);
        game.blue_team = games.text(3);
        game.red_team = games.text(4);
        game.winner_team = games.text(5);

        Statement actions(database_, R"sql(
            SELECT sequence, action_type, side, champion, player, role
            FROM draft_actions WHERE game_id = ? ORDER BY sequence
        )sql");
        actions.bind(1, game.id);
        while (actions.row()) {
            game.draft.push_back(DraftAction{actions.integer(0), parse_action_type(actions.text(1)),
                                             parse_side(actions.text(2)), actions.text(3),
                                             actions.optional_text(4), actions.optional_text(5)});
        }
        result.push_back(std::move(game));
    }
    return result;
}

std::vector<RoleAssignment> SqliteRepository::assignments_for_game(const Id game_id) const {
    Statement statement(database_, R"sql(
        SELECT assignment.side, assignment.role, assignment.champion,
               COALESCE(player.display_name, assignment.player)
        FROM game_assignments assignment
        LEFT JOIN game_lineups lineup
          ON lineup.game_id = assignment.game_id
         AND lineup.side = assignment.side
         AND lineup.role = assignment.role
        LEFT JOIN players player ON player.id = lineup.player_id
        WHERE assignment.game_id = ?
        ORDER BY CASE assignment.role
            WHEN 'top' THEN 1
            WHEN 'jungle' THEN 2
            WHEN 'mid' THEN 3
            WHEN 'bot' THEN 4
            ELSE 5
        END, assignment.side
    )sql");
    statement.bind(1, game_id);
    std::vector<RoleAssignment> result;
    while (statement.row()) {
        result.push_back(RoleAssignment{parse_side(statement.text(0)),
                                        parse_role(statement.text(1)), statement.text(2),
                                        statement.optional_text(3)});
    }
    return result;
}

Id SqliteRepository::track_team(const std::string& team) {
    const auto name = trimmed_name(team);
    Statement insert(database_, R"sql(
        INSERT INTO tracked_teams(name, name_key) VALUES (?, ?)
        ON CONFLICT(name_key) DO UPDATE SET name = excluded.name
    )sql");
    insert.bind(1, name);
    insert.bind(2, identity_key(name));
    insert.done();

    Statement select(database_, "SELECT id FROM tracked_teams WHERE name_key = ?");
    select.bind(1, identity_key(name));
    if (!select.row()) {
        throw StorageError("could not resolve tracked team");
    }
    return select.id(0);
}

std::vector<TrackedTeam> SqliteRepository::tracked_teams() const {
    Statement statement(database_, "SELECT id, name FROM tracked_teams ORDER BY name_key");
    std::vector<TrackedTeam> result;
    while (statement.row()) {
        result.push_back({statement.id(0), statement.text(1)});
    }
    return result;
}

std::optional<TrackedTeam> SqliteRepository::find_tracked_team(const std::string& team) const {
    Statement statement(database_, "SELECT id, name FROM tracked_teams WHERE name_key = ?");
    statement.bind(1, identity_key(team));
    if (!statement.row()) {
        return std::nullopt;
    }
    return TrackedTeam{statement.id(0), statement.text(1)};
}

void SqliteRepository::replace_roster(const TeamRoster& roster) {
    Transaction transaction(database_);
    Statement remove(database_, "DELETE FROM team_rosters WHERE team_id = ?");
    remove.bind(1, roster.team.id);
    remove.done();
    for (const auto& assignment : roster.assignments) {
        const auto player_id = ensure_player(database_, assignment.player);
        Statement insert(database_, R"sql(
            INSERT INTO team_rosters(team_id, role, player_id) VALUES (?, ?, ?)
        )sql");
        insert.bind(1, roster.team.id);
        insert.bind(2, to_string(assignment.role));
        insert.bind(3, player_id);
        insert.done();
    }
    transaction.commit();
}

std::optional<TeamRoster> SqliteRepository::roster_for_team(const std::string& team) const {
    const auto tracked = find_tracked_team(team);
    if (!tracked) {
        return std::nullopt;
    }
    Statement statement(database_, R"sql(
        SELECT roster.role, player.display_name
        FROM team_rosters roster
        JOIN players player ON player.id = roster.player_id
        WHERE roster.team_id = ?
        ORDER BY CASE roster.role
            WHEN 'top' THEN 1
            WHEN 'jungle' THEN 2
            WHEN 'mid' THEN 3
            WHEN 'bot' THEN 4
            ELSE 5
        END
    )sql");
    statement.bind(1, tracked->id);
    TeamRoster result{*tracked, {}};
    while (statement.row()) {
        result.assignments.push_back({parse_role(statement.text(0)), statement.text(1)});
    }
    return result;
}

void SqliteRepository::replace_game_lineup(const GameLineup& lineup) {
    Transaction transaction(database_);
    Statement remove(database_, "DELETE FROM game_lineups WHERE game_id = ? AND side = ?");
    remove.bind(1, lineup.game_id);
    remove.bind(2, to_string(lineup.side));
    remove.done();
    for (const auto& assignment : lineup.assignments) {
        const auto player_id = ensure_player(database_, assignment.player);
        Statement insert(database_, R"sql(
            INSERT INTO game_lineups(game_id, side, role, player_id) VALUES (?, ?, ?, ?)
        )sql");
        insert.bind(1, lineup.game_id);
        insert.bind(2, to_string(lineup.side));
        insert.bind(3, to_string(assignment.role));
        insert.bind(4, player_id);
        insert.done();
    }
    transaction.commit();
}

std::vector<GameLineup> SqliteRepository::lineups_for_game(const Id game_id) const {
    Statement statement(database_, R"sql(
        SELECT lineup.side, lineup.role, player.display_name,
               CASE lineup.side WHEN 'blue' THEN game.blue_team ELSE game.red_team END
        FROM game_lineups lineup
        JOIN players player ON player.id = lineup.player_id
        JOIN games game ON game.id = lineup.game_id
        WHERE lineup.game_id = ?
        ORDER BY lineup.side, CASE lineup.role
            WHEN 'top' THEN 1
            WHEN 'jungle' THEN 2
            WHEN 'mid' THEN 3
            WHEN 'bot' THEN 4
            ELSE 5
        END
    )sql");
    statement.bind(1, game_id);
    std::vector<GameLineup> result;
    while (statement.row()) {
        const auto side = parse_side(statement.text(0));
        if (result.empty() || result.back().side != side) {
            result.push_back(GameLineup{game_id, side, statement.text(3), {}});
        }
        result.back().assignments.push_back({parse_role(statement.text(1)), statement.text(2)});
    }
    return result;
}

Id SqliteRepository::insert_game(const GameDefinition& definition) {
    Transaction transaction(database_);
    Statement game(database_, R"sql(
        INSERT INTO games(series_id, game_number, blue_team, red_team, winner_team)
        VALUES (?, ?, ?, ?, ?)
    )sql");
    game.bind(1, definition.series_id);
    game.bind(2, definition.game_number);
    game.bind(3, definition.blue_team);
    game.bind(4, definition.red_team);
    game.bind(5, definition.winner_team);
    game.done();
    const Id game_id = sqlite3_last_insert_rowid(database_);

    int pick_ordinal = 0;
    for (const auto& item : definition.draft) {
        Statement action(database_, R"sql(
            INSERT INTO draft_actions(
                game_id, sequence, action_type, side, champion, champion_key,
                pick_ordinal, player, role
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        )sql");
        action.bind(1, game_id);
        action.bind(2, item.sequence);
        action.bind(3, to_string(item.type));
        action.bind(4, to_string(item.side));
        const auto champion = normalized_champion_name(item.champion);
        action.bind(5, champion);
        action.bind(6, champion_key(champion));
        if (item.type == DraftActionType::pick) {
            ++pick_ordinal;
            action.bind(7, pick_ordinal);
        } else {
            action.bind_null(7);
        }
        action.bind_optional(8, item.player);
        action.bind_optional(9, item.role);
        action.done();
    }
    transaction.commit();
    return game_id;
}

void SqliteRepository::replace_assignments(const GameAssignments& assignments) {
    Transaction transaction(database_);
    Statement remove(database_, "DELETE FROM game_assignments WHERE game_id = ?");
    remove.bind(1, assignments.game_id);
    remove.done();

    for (const auto& item : assignments.assignments) {
        Statement insert(database_, R"sql(
            INSERT INTO game_assignments(game_id, side, role, champion, champion_key, player)
            VALUES (?, ?, ?, ?, ?, ?)
        )sql");
        insert.bind(1, assignments.game_id);
        insert.bind(2, to_string(item.side));
        insert.bind(3, to_string(item.role));
        const auto champion = normalized_champion_name(item.champion);
        insert.bind(4, champion);
        insert.bind(5, champion_key(champion));
        insert.bind_optional(6, item.player);
        insert.done();
    }
    transaction.commit();
}

AnalysisResult SqliteRepository::analyze(const AnalysisFilter& filter) const {
    std::ostringstream sql;
    sql << R"sql(
        SELECT g.id, g.winner_team
        FROM games g
        JOIN series_records s ON s.id = g.series_id
        WHERE (g.blue_team = ? OR g.red_team = ?)
    )sql";
    std::vector<Parameter> parameters{filter.perspective_team, filter.perspective_team};

    if (filter.champion) {
        sql << R"sql(
            AND EXISTS (
                SELECT 1 FROM draft_actions focal
                WHERE focal.game_id = g.id
                  AND focal.action_type = 'pick'
                  AND focal.champion_key = ?
                  AND CASE focal.side WHEN 'blue' THEN g.blue_team ELSE g.red_team END = ?
        )sql";
        parameters.emplace_back(champion_key(*filter.champion));
        parameters.emplace_back(filter.perspective_team);
        if (filter.pick_ordinal) {
            sql << " AND focal.pick_ordinal = ?\n";
            parameters.emplace_back(*filter.pick_ordinal);
        }
        if (filter.role) {
            sql << R"sql(
                AND EXISTS (
                    SELECT 1 FROM game_assignments focal_assignment
                    WHERE focal_assignment.game_id = focal.game_id
                      AND focal_assignment.side = focal.side
                      AND focal_assignment.champion_key = focal.champion_key
                      AND focal_assignment.role = ?
                )
            )sql";
            parameters.emplace_back(to_string(*filter.role));
        }
        if (filter.player) {
            sql << R"sql(
                AND EXISTS (
                    SELECT 1
                    FROM game_assignments focal_role
                    JOIN game_lineups focal_lineup
                      ON focal_lineup.game_id = focal_role.game_id
                     AND focal_lineup.side = focal_role.side
                     AND focal_lineup.role = focal_role.role
                    JOIN players focal_player ON focal_player.id = focal_lineup.player_id
                    WHERE focal_role.game_id = focal.game_id
                      AND focal_role.side = focal.side
                      AND focal_role.champion_key = focal.champion_key
                      AND focal_player.player_key = ?
                )
            )sql";
            parameters.emplace_back(identity_key(*filter.player));
        }
        sql << ")\n";
    }
    if (filter.player && !filter.champion) {
        sql << R"sql(
            AND EXISTS (
                SELECT 1
                FROM game_lineups participant_lineup
                JOIN players participant ON participant.id = participant_lineup.player_id
                WHERE participant_lineup.game_id = g.id
                  AND CASE participant_lineup.side
                      WHEN 'blue' THEN g.blue_team ELSE g.red_team END = ?
                  AND participant.player_key = ?
            )
        )sql";
        parameters.emplace_back(filter.perspective_team);
        parameters.emplace_back(identity_key(*filter.player));
    }
    if (filter.patch) {
        sql << " AND s.patch = ?\n";
        parameters.emplace_back(*filter.patch);
    }
    if (filter.opponent) {
        sql << " AND CASE WHEN g.blue_team = ? THEN g.red_team ELSE g.blue_team END = ?\n";
        parameters.emplace_back(filter.perspective_team);
        parameters.emplace_back(*filter.opponent);
    }
    if (filter.side == Side::blue) {
        sql << " AND g.blue_team = ?\n";
        parameters.emplace_back(filter.perspective_team);
    } else if (filter.side == Side::red) {
        sql << " AND g.red_team = ?\n";
        parameters.emplace_back(filter.perspective_team);
    }
    for (const auto& ban : filter.required_bans) {
        sql << R"sql(
            AND EXISTS (
                SELECT 1 FROM draft_actions required_ban
                WHERE required_ban.game_id = g.id
                  AND required_ban.action_type = 'ban'
                  AND required_ban.champion_key = ?
            )
        )sql";
        parameters.emplace_back(champion_key(ban));
    }
    for (const auto& ban : filter.excluded_bans) {
        sql << R"sql(
            AND NOT EXISTS (
                SELECT 1 FROM draft_actions excluded_ban
                WHERE excluded_ban.game_id = g.id
                  AND excluded_ban.action_type = 'ban'
                  AND excluded_ban.champion_key = ?
            )
        )sql";
        parameters.emplace_back(champion_key(ban));
    }
    sql << " ORDER BY g.id\n";

    Statement matched(database_, sql.str());
    bind_parameters(matched, parameters);
    std::vector<std::pair<Id, bool>> matched_games;
    AnalysisResult result;
    while (matched.row()) {
        const bool won = matched.text(1) == filter.perspective_team;
        matched_games.emplace_back(matched.id(0), won);
        ++result.matched_games;
        won ? ++result.wins : ++result.losses;
    }

    std::map<AggregateKey, ChampionAssociation> associations;
    for (const auto& [game_id, won] : matched_games) {
        Statement picks(database_, R"sql(
            SELECT da.champion, da.champion_key,
                   CASE da.side WHEN 'blue' THEN g.blue_team ELSE g.red_team END AS pick_team,
                   assignment.role
            FROM draft_actions da
            JOIN games g ON g.id = da.game_id
            LEFT JOIN game_assignments assignment
                ON assignment.game_id = da.game_id
               AND assignment.side = da.side
               AND assignment.champion_key = da.champion_key
            WHERE da.game_id = ? AND da.action_type = 'pick'
        )sql");
        picks.bind(1, game_id);
        while (picks.row()) {
            const bool allied = picks.text(2) == filter.perspective_team;
            const auto key = picks.text(1);
            if (allied && filter.champion && key == champion_key(*filter.champion)) {
                continue;
            }
            const auto stored_role = picks.optional_text(3);
            if (!allied && filter.role &&
                (!stored_role || *stored_role != to_string(*filter.role))) {
                continue;
            }
            const auto association_role = stored_role;
            const AggregateKey aggregate_key{allied ? Relationship::with : Relationship::against,
                                             key, association_role.value_or("")};
            auto [iterator, inserted] = associations.try_emplace(
                aggregate_key,
                ChampionAssociation{picks.text(0), aggregate_key.relationship,
                                    association_role
                                        ? std::optional<Role>{parse_role(*association_role)}
                                        : std::nullopt,
                                    0, 0, 0});
            auto& row = iterator->second;
            ++row.games;
            won ? ++row.wins : ++row.losses;
        }
    }

    result.associations.reserve(associations.size());
    for (auto& [key, association] : associations) {
        static_cast<void>(key);
        result.associations.push_back(std::move(association));
    }
    std::ranges::sort(result.associations,
                      [](const ChampionAssociation& left, const ChampionAssociation& right) {
                          if (left.relationship != right.relationship) {
                              return left.relationship < right.relationship;
                          }
                          if (left.games != right.games) {
                              return left.games > right.games;
                          }
                          return left.champion < right.champion;
                      });
    return result;
}

} // namespace lol
