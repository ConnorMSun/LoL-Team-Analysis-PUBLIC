if(NOT DEFINED LOLCTL OR NOT DEFINED TEST_DATABASE OR NOT DEFINED DRAFT_FILE)
    message(FATAL_ERROR "lolctl workflow test is missing a required path")
endif()

file(REMOVE "${TEST_DATABASE}" "${TEST_DATABASE}-shm" "${TEST_DATABASE}-wal")
set(NORMALIZED_DRAFT_FILE "${TEST_DATABASE}.draft.csv")
file(READ "${DRAFT_FILE}" draft_contents)
string(REPLACE "Jinx" "jINX" draft_contents "${draft_contents}")
file(WRITE "${NORMALIZED_DRAFT_FILE}" "${draft_contents}")

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" series add "Blue Whales" "Red Foxes"
        --patch 26.15 --elo 1200,1800,1512.5
    RESULT_VARIABLE series_result
    OUTPUT_VARIABLE series_output
    ERROR_VARIABLE series_error
)
if(NOT series_result EQUAL 0 OR NOT series_output MATCHES "series_id=1")
    message(FATAL_ERROR "could not create CLI test series: ${series_error}${series_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" team track "Blue Whales"
    RESULT_VARIABLE track_result
    OUTPUT_VARIABLE track_output
    ERROR_VARIABLE track_error
)
if(NOT track_result EQUAL 0 OR NOT track_output MATCHES "team_id=1 team=Blue Whales")
    message(FATAL_ERROR "could not track team: ${track_error}${track_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" roster set "Blue Whales"
        "BlueTop,BlueJungle,BlueMid,BlueBot,BlueSupport"
    RESULT_VARIABLE roster_result
    OUTPUT_VARIABLE roster_output
    ERROR_VARIABLE roster_error
)
if(NOT roster_result EQUAL 0 OR NOT roster_output MATCHES "Mid +BlueMid" OR
   NOT roster_output MATCHES "Support +BlueSupport")
    message(FATAL_ERROR "could not set tracked roster: ${roster_error}${roster_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game add 1 "${NORMALIZED_DRAFT_FILE}"
        --blue a --winner a
    RESULT_VARIABLE game_result
    OUTPUT_VARIABLE game_output
    ERROR_VARIABLE game_error
)
if(NOT game_result EQUAL 0 OR NOT game_output MATCHES "game_id=1 game_number=1")
    message(FATAL_ERROR "streamlined game import failed: ${game_error}${game_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game roles 1
        --blue "Kai'Sa,Poppy,Aurora,Jinx,Nautilus"
        --red "Renekton,Sejuani,Orianna,Azir,Gnar"
    RESULT_VARIABLE roles_result
    OUTPUT_VARIABLE roles_output
    ERROR_VARIABLE roles_error
)
if(NOT roles_result EQUAL 0 OR NOT roles_output MATCHES "roles_saved=1")
    message(FATAL_ERROR "role assignment failed: ${roles_error}${roles_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game lineup 1
    RESULT_VARIABLE lineup_result
    OUTPUT_VARIABLE lineup_output
    ERROR_VARIABLE lineup_error
)
if(NOT lineup_result EQUAL 0 OR NOT lineup_output MATCHES "Side +Team +Role +Player" OR
   NOT lineup_output MATCHES "Blue +Blue Whales +Mid +BlueMid" OR
   lineup_output MATCHES "Red +Red Foxes")
    message(FATAL_ERROR
        "tracked roster was not snapshotted asymmetrically: ${lineup_error}${lineup_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" roster set "Blue Whales"
        "BlueTop,BlueJungle,FutureMid,BlueBot,BlueSupport"
    RESULT_VARIABLE roster_update_result
    OUTPUT_VARIABLE roster_update_output
    ERROR_VARIABLE roster_update_error
)
if(NOT roster_update_result EQUAL 0 OR NOT roster_update_output MATCHES "Mid +FutureMid")
    message(FATAL_ERROR
        "could not update default roster: ${roster_update_error}${roster_update_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game lineup 1
    RESULT_VARIABLE historical_lineup_result
    OUTPUT_VARIABLE historical_lineup_output
    ERROR_VARIABLE historical_lineup_error
)
if(NOT historical_lineup_result EQUAL 0 OR
   NOT historical_lineup_output MATCHES "Mid +BlueMid" OR
   historical_lineup_output MATCHES "FutureMid")
    message(FATAL_ERROR
        "roster update rewrote historical lineup: ${historical_lineup_error}${historical_lineup_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" series list
    RESULT_VARIABLE series_list_result
    OUTPUT_VARIABLE series_list_output
    ERROR_VARIABLE series_list_error
)
if(NOT series_list_result EQUAL 0 OR
   NOT series_list_output MATCHES "ID +Team A +Team B +Patch +ELO range +ELO avg +Games +Score A-B" OR
   NOT series_list_output MATCHES "1 +Blue Whales +Red Foxes +26\\.15 +1200-1800 +1512\\.5 +1 +1-0")
    message(FATAL_ERROR "series list was not readable: ${series_list_error}${series_list_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" series show 1
    RESULT_VARIABLE series_show_result
    OUTPUT_VARIABLE series_show_output
    ERROR_VARIABLE series_show_error
)
if(NOT series_show_result EQUAL 0 OR NOT series_show_output MATCHES "Series 1" OR
   NOT series_show_output MATCHES "ID +Game +Blue +Red +Winner" OR
   NOT series_show_output MATCHES "1 +1 +Blue Whales +Red Foxes +Blue Whales")
    message(FATAL_ERROR "series show was not readable: ${series_show_error}${series_show_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game show 1
    RESULT_VARIABLE game_show_result
    OUTPUT_VARIABLE game_show_output
    ERROR_VARIABLE game_show_error
)
if(NOT game_show_result EQUAL 0 OR NOT game_show_output MATCHES "Side +Team +Result" OR
   NOT game_show_output MATCHES "Blue +Blue Whales +Win" OR
   NOT game_show_output MATCHES "# +Action +Side +Champion +Role +Player" OR
   NOT game_show_output MATCHES "Pick +Blue +Jinx +bot" OR
   NOT game_show_output MATCHES "Pick +Blue +Aurora +mid +BlueMid" OR
   NOT game_show_output MATCHES "Ban +Blue +Ambessa +-" )
    message(FATAL_ERROR "game show was not readable: ${game_show_error}${game_show_output}")
endif()
foreach(inspect_output IN ITEMS series_list_output series_show_output game_show_output)
    string(FIND "${${inspect_output}}" "\t" inspect_tab_position)
    if(NOT inspect_tab_position EQUAL -1)
        message(FATAL_ERROR "${inspect_output} contains terminal-dependent tabs")
    endif()
endforeach()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --player BlueMid --pick Aurora@1 --role mid
    RESULT_VARIABLE player_stats_result
    OUTPUT_VARIABLE player_stats_output
    ERROR_VARIABLE player_stats_error
)
if(NOT player_stats_result EQUAL 0 OR
   NOT player_stats_output MATCHES "1 +1 +0 +100\\.0%" OR
   NOT player_stats_output MATCHES "against +Orianna +mid")
    message(FATAL_ERROR
        "player/champion analytics failed: ${player_stats_error}${player_stats_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game lineup 1 "Blue Whales"
        --mid Substitute
    RESULT_VARIABLE substitute_result
    OUTPUT_VARIABLE substitute_output
    ERROR_VARIABLE substitute_error
)
if(NOT substitute_result EQUAL 0 OR NOT substitute_output MATCHES "Mid +Substitute")
    message(FATAL_ERROR "lineup substitution failed: ${substitute_error}${substitute_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --player BlueMid --pick Aurora@1
    RESULT_VARIABLE old_player_result
    OUTPUT_VARIABLE old_player_output
    ERROR_VARIABLE old_player_error
)
if(NOT old_player_result EQUAL 0 OR NOT old_player_output MATCHES "0 +0 +0 +0\\.0%")
    message(FATAL_ERROR
        "substitution did not decouple old player: ${old_player_error}${old_player_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --player Substitute --pick Aurora@1
    RESULT_VARIABLE substitute_stats_result
    OUTPUT_VARIABLE substitute_stats_output
    ERROR_VARIABLE substitute_stats_error
)
if(NOT substitute_stats_result EQUAL 0 OR
   NOT substitute_stats_output MATCHES "1 +1 +0 +100\\.0%")
    message(FATAL_ERROR
        "substitute player was not linked to Aurora: ${substitute_stats_error}${substitute_stats_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --pick Aurora@1 --bans Ambessa,Vi --role mid
    RESULT_VARIABLE analysis_result
    OUTPUT_VARIABLE analysis_output
    ERROR_VARIABLE analysis_error
)
if(NOT analysis_result EQUAL 0 OR NOT analysis_output MATCHES "1 +1 +0 +100\\.0%")
    message(FATAL_ERROR "role-aware stats failed: ${analysis_error}${analysis_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --pick Aurora@1 --bans "Ambessa,!Teemo"
    RESULT_VARIABLE excluded_ban_result
    OUTPUT_VARIABLE excluded_ban_output
    ERROR_VARIABLE excluded_ban_error
)
if(NOT excluded_ban_result EQUAL 0 OR
   NOT excluded_ban_output MATCHES "1 +1 +0 +100\\.0%")
    message(FATAL_ERROR
        "mixed required/excluded bans failed: ${excluded_ban_error}${excluded_ban_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales" --bans "!Ambessa"
    RESULT_VARIABLE present_exclusion_result
    OUTPUT_VARIABLE present_exclusion_output
    ERROR_VARIABLE present_exclusion_error
)
if(NOT present_exclusion_result EQUAL 0 OR
   NOT present_exclusion_output MATCHES "0 +0 +0 +0\\.0%")
    message(FATAL_ERROR
        "present excluded ban was not rejected: ${present_exclusion_error}${present_exclusion_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales"
        --bans "Ambessa,!Ambessa"
    RESULT_VARIABLE contradictory_ban_result
    ERROR_VARIABLE contradictory_ban_error
)
if(contradictory_ban_result EQUAL 0 OR
   NOT contradictory_ban_error MATCHES "both required and excluded")
    message(FATAL_ERROR "contradictory bans were accepted: ${contradictory_ban_error}")
endif()
if(NOT analysis_output MATCHES "against +Orianna +mid")
    message(FATAL_ERROR "mid-lane opponent was not reported: ${analysis_output}")
endif()
if(analysis_output MATCHES "against +Sejuani")
    message(FATAL_ERROR "role filter included a non-mid opponent: ${analysis_output}")
endif()
if(NOT analysis_output MATCHES "Matched games +Wins +Losses +Win rate" OR
   NOT analysis_output MATCHES "Relationship +Champion +Role +Games +Wins +Losses +Win rate")
    message(FATAL_ERROR "stats table headings are missing or misaligned: ${analysis_output}")
endif()
string(FIND "${analysis_output}" "\t" tab_position)
if(NOT tab_position EQUAL -1)
    message(FATAL_ERROR "stats output still contains alignment-dependent tabs: ${analysis_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" stats "Blue Whales" --pick Aurora@1
    RESULT_VARIABLE general_result
    OUTPUT_VARIABLE general_output
    ERROR_VARIABLE general_error
)
if(NOT general_result EQUAL 0 OR NOT general_output MATCHES "against +Sejuani +jungle" OR
   NOT general_output MATCHES "with +Jinx +bot")
    message(FATAL_ERROR
        "general stats did not expose normalized names and stored roles: ${general_error}${general_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" series add Alpha Beta
        --patch 26.15 --elo 1200,1800,1512.5
    RESULT_VARIABLE swapped_series_result
    OUTPUT_VARIABLE swapped_series_output
    ERROR_VARIABLE swapped_series_error
)
if(NOT swapped_series_result EQUAL 0 OR NOT swapped_series_output MATCHES "series_id=2")
    message(FATAL_ERROR
        "could not create side-swap test series: ${swapped_series_error}${swapped_series_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game add 2 "${NORMALIZED_DRAFT_FILE}"
        --blue b --winner b
    RESULT_VARIABLE swapped_game_result
    OUTPUT_VARIABLE swapped_game_output
    ERROR_VARIABLE swapped_game_error
)
if(NOT swapped_game_result EQUAL 0 OR NOT swapped_game_output MATCHES "game_id=2")
    message(FATAL_ERROR
        "could not create side-swap test game: ${swapped_game_error}${swapped_game_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db "${TEST_DATABASE}" game roles 2
        --blue "Kai'Sa,Poppy,Aurora,Jinx,Nautilus"
        --red "Renekton,Sejuani,Orianna,Azir,Gnar"
    RESULT_VARIABLE swapped_roles_result
    OUTPUT_VARIABLE swapped_roles_output
    ERROR_VARIABLE swapped_roles_error
)
if(NOT swapped_roles_result EQUAL 0 OR NOT swapped_roles_output MATCHES "roles_saved=2")
    message(FATAL_ERROR
        "roles followed team A/B instead of game side: ${swapped_roles_error}${swapped_roles_output}")
endif()
