# lolctl command reference

This document describes the current user-facing CLI. The same information is available from the
installed application:

```sh
lolctl help
```

## Global behavior

```text
lolctl [--db PATH] COMMAND
```

Global options:

- `--db PATH`: override database selection for one command
- `--help`, `-h`: show contextual help without opening a database
- `--version`: print the installed version

Database precedence is an explicit `--db`, the nearest `.lolctl` workspace selection, then
`analytics.sqlite` in the current directory.

## Help

```sh
lolctl help
lolctl help init
lolctl help db
lolctl help series
lolctl help game
lolctl help stats
```

Contextual forms are equivalent:

```sh
lolctl game --help
lolctl game roles --help
lolctl stats -h
```

Running `lolctl` without arguments prints the complete command catalog. Help and version commands do
not initialize, migrate, or otherwise modify a database.

## Initialize a database

```sh
lolctl init
lolctl --db tournament.sqlite init
```

Initialization creates a missing database or applies missing migrations. It does not delete existing
data. Explicit initialization is optional because commands initialize their selected database on
first use.

## Select a workspace database

```sh
lolctl db use tournament.sqlite
lolctl db show
lolctl db clear
```

- `db use` validates or initializes the database and saves the selection in `.lolctl`.
- `db show` prints the resolved path and source.
- `db clear` removes the saved selection without deleting the SQLite database.

The saved selection applies in the current directory and its descendants.

## Track a team and roster

Player tracking is opt-in. Track only the teams whose individual players matter to your analysis:

```sh
lolctl team track NCK
lolctl team list
lolctl roster set NCK "TopPlayer,JunglePlayer,MidPlayer,BotPlayer,SupportPlayer"
lolctl roster show NCK
```

The roster order is `top,jungle,mid,bot,support`. Setting a roster creates or reuses global player
identities automatically. Opposing teams do not need to be tracked, and their player fields may
remain unknown without affecting champion, role, team, or matchup analytics.

A roster is a default for future games, not a mutable pointer from historical games. Updating it
does not change lineup snapshots already attached to games.

## List and inspect series

```sh
lolctl series list
lolctl series show SERIES_ID
```

`series list` gives one aligned row per series with its teams, patch, ELO range and average, game
count, and Team A–Team B score. `series show` repeats that summary for one series and follows it with
every game, including side assignments and winner.

## Add a series

```sh
lolctl series add TEAM_A TEAM_B --patch PATCH --elo LOW,HIGH,AVERAGE
```

Example:

```sh
lolctl series add NCK SKY --patch 26.15 --elo 1200,1800,1512.5
```

The ELO values represent the lowest player, highest player, and team average. The command prints the
new series ID.

## Add a game

```sh
lolctl game add SERIES_ID DRAFT_FILE --blue a|b --winner a|b
```

Example:

```sh
lolctl game add 1 examples/format.csv --blue a --winner a
```

The draft file contains exactly 20 champion names in chronological order, one per line. The command
infers the game number, red team, action types, and acting sides. It validates standard draft order,
duplicate champions, and Full Fearless before committing the game transaction.

Champion capitalization is normalized at storage time. For example, `aURorA` is stored and shown as
`Aurora`; punctuation in names is preserved.

## Inspect a game

```sh
lolctl game show GAME_ID
```

The game view shows its series and patch, a two-row side/result summary, and all 20 draft actions in
chronological order. Assigned roles appear beside picks; bans use `-`, and picks without final role
assignments use `unassigned`.

When a tracked lineup exists, the draft also displays the player assigned to each picked champion.
Untracked players display `-`.

## Assign final roles

Guided assignment:

```sh
lolctl game roles GAME_ID
```

The application displays the resolved team name and five picks for each side, then requests them in
`top,jungle,mid,bot,support` order.

Non-interactive assignment:

```sh
lolctl game roles 1 \
  --blue "Kennen,Vi,Aurora,Jinx,Nautilus" \
  --red "Renekton,Sejuani,Orianna,Zeri,Rell"
```

Assignments are separate from the original draft and may be corrected by running the command again.
Each update is validated and replaced atomically.

## Inspect or override a game lineup

New games automatically snapshot any complete roster belonging to a tracked participating team:

```sh
lolctl game lineup GAME_ID
```

For an existing game without a snapshot, or to reset one side to its current roster:

```sh
lolctl game lineup GAME_ID TEAM
```

Supply only the roles that differ for a substitution or role swap:

```sh
lolctl game lineup 4 NCK --mid Substitute
lolctl game lineup 5 NCK --top MidPlayer --mid TopPlayer
```

Supported overrides are `--top`, `--jungle`, `--mid`, `--bot`, and `--support`. The resulting lineup
must contain five distinct players. Changes affect only the selected game.

## Query statistics

```sh
lolctl stats TEAM [OPTIONS]
```

Options:

- `--pick CHAMPION[@ORDINAL]`: require a champion pick, optionally at overall pick position N
- `--player PLAYER`: require a tracked player on the perspective team; with `--pick`, require that
  player to have played the focal champion
- `--bans CHAMPION,...`: require listed champions among the game's bans; prefix a champion with `!`
  to require that it was not banned by either side
- `--role ROLE`: require the focal pick's final `top`, `jungle`, `mid`, `bot`, or `support` role
- `--patch PATCH`: restrict the series patch
- `--opponent TEAM`: restrict the opposing team
- `--side blue|red`: restrict the perspective team's side

Examples:

```sh
lolctl stats NCK
lolctl stats NCK --player MidPlayer
lolctl stats NCK --player MidPlayer --pick Aurora@1
lolctl stats NCK --pick Aurora@1 --bans Poppy,Ashe
lolctl stats NCK --bans 'Poppy,Ashe,!Jinx'
lolctl stats NCK --pick Aurora@1 --role mid --patch 26.15 --opponent SKY --side blue
```

Positive and negative bans compose in the same list. For example, `Poppy,Ashe,!Jinx` requires Poppy
and Ashe to have been banned and Jinx not to have been banned. Jinx may still have been picked or
left untouched. A champion cannot appear in both forms. Use single quotes around expressions
containing `!` so the shell passes the modifier to `lolctl` unchanged.

General queries do not require roles, but association rows use and group by stored final assignments
when available. `unknown` therefore means that the matching game has no role assignment for that
champion. When `--role` is supplied, games require completed role assignments and opposing
associations are limited to the champion assigned to the same role.

Statistics use content-sized columns: labels are left-aligned, counts and percentages are
right-aligned, and no terminal-dependent tab spacing is used.
