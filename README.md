# LoL Team Analysis

[![CI](https://github.com/ConnorMSun/LoL-Team-Analysis/actions/workflows/ci.yml/badge.svg)](https://github.com/ConnorMSun/LoL-Team-Analysis/actions/workflows/ci.yml)

LoL Team Analysis is a C++20 command-line tool for recording League of Legends drafts and exploring
team, player, champion, and matchup trends. It understands tournament draft order, Full Fearless
rules, flex picks, roster changes, and side swaps.

Current version: **0.4.2**

## What it can answer

- How often does a team win with Aurora first pick?
- Which champions perform best with or against that pick?
- How does a player's record change by champion or role?
- What happened when Poppy and Ashe were banned but Jinx was left open?
- Which champions are unavailable later in a Full Fearless series?

Drafts stay role-agnostic when imported. Final roles and player lineups are attached after flex picks
are resolved. This preserves general draft data while supporting lane and player-specific analysis.

## Install

You need CMake 3.24+, a C++20 compiler, and SQLite 3 development files.

```sh
./scripts/install.sh --add-to-path
```

Open a new terminal and run `lolctl` from any directory. The default install location is
`~/.local/bin`. See [Amendment 0004](docs/amendments/0004-cli-installation/README.md) for other
options.

## A typical workflow

Create a series. ELO values are `lowest,highest,average`:

```sh
lolctl series add NCK SKY --patch 26.15 --elo 1200,1800,1512.5
```

Import the next game from a 20-line champion list. Team `a` is blue and won; red and the game number
are inferred:

```sh
lolctl game add 1 examples/format.csv --blue a --winner a
```

Once the draft is settled, assign champions in `top,jungle,mid,bot,support` order:

```sh
lolctl game roles 1
```

The command is interactive, or both sides can be supplied directly:

```sh
lolctl game roles 1 \
  --blue "Kai'Sa,Poppy,Aurora,Jinx,Nautilus" \
  --red "Renekton,Sejuani,Orianna,Azir,Gnar"
```

Review the saved data:

```sh
lolctl series list
lolctl series show 1
lolctl game show 1
```

## Player tracking

Player data is optional. Track the team you analyze without entering every opponent roster:

```sh
lolctl team track NCK
lolctl roster set NCK "TopPlayer,JunglePlayer,MidPlayer,BotPlayer,SupportPlayer"
```

Complete rosters are copied into new games so later roster edits do not rewrite history. Override
only the roles that changed:

```sh
lolctl game lineup 1 NCK --mid Substitute
```

## Query the data

Filters can be combined freely:

```sh
lolctl stats NCK --pick Aurora@1
lolctl stats NCK --pick Aurora@1 --role mid --opponent SKY
lolctl stats NCK --player MidPlayer --pick Aurora
lolctl stats NCK --bans 'Poppy,Ashe,!Jinx'
```

In a ban list, `!Jinx` means Jinx was not banned by either side. Quote expressions containing `!` so
your shell passes them through unchanged.

Results include the matching record and champion associations:

```text
Summary
Matched games  Wins  Losses  Win rate
-------------  ----  ------  --------
            4     3       1     75.0%

Champion associations
Relationship  Champion  Role     Games  Wins  Losses  Win rate
------------  --------  -------  -----  ----  ------  --------
with          Nautilus  support      3     2       1     66.7%
against       Orianna   mid          2     2       0    100.0%
```

Champion matching ignores case and punctuation. Stored names use consistent display casing.

## Databases and help

Commands use the nearest saved workspace database, then fall back to `analytics.sqlite` in the
current directory:

```sh
lolctl db use tournament.sqlite
lolctl db show
lolctl db clear
```

Use `--db PATH` for a one-command override. Existing databases are migrated in place and are never
deleted by `init`.

```sh
lolctl help
lolctl help game
lolctl stats --help
```

The complete command reference is in [docs/CLI.md](docs/CLI.md).

## Development

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The development preset enables warnings-as-errors. CI runs the same build and test flow on Ubuntu
and macOS.

The code has three layers:

- `domain`: draft rules and shared data types
- `application`: series, roster, assignment, and analytics use cases
- `infrastructure`: SQLite persistence and workspace configuration

The application layer talks to a `Repository` interface rather than SQLite. Draft actions, final
roles, and player lineups are separate facts, so correcting one does not rewrite the others.

Version-by-version design notes live in the [amendment index](docs/amendments/README.md).

## License

Released under the [MIT License](LICENSE).
