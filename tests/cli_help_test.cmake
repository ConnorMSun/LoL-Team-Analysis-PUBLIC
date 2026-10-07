if(NOT DEFINED LOLCTL OR NOT DEFINED HELP_DIRECTORY OR NOT DEFINED EXPECTED_VERSION)
    message(FATAL_ERROR "lolctl help test is missing a required value")
endif()

file(REMOVE_RECURSE "${HELP_DIRECTORY}")
file(MAKE_DIRECTORY "${HELP_DIRECTORY}")

execute_process(
    COMMAND "${LOLCTL}"
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE overview_result
    OUTPUT_VARIABLE overview_output
    ERROR_VARIABLE overview_error
)
foreach(command_label "init" "db use PATH" "db show" "db clear" "team track" "team list"
                      "roster set" "roster show" "series list" "series show" "series add"
                      "game show" "game add" "game roles" "game lineup" "stats" "help")
    if(NOT overview_output MATCHES "${command_label}")
        message(FATAL_ERROR "overview omitted ${command_label}: ${overview_output}")
    endif()
endforeach()
if(NOT overview_result EQUAL 0 OR EXISTS "${HELP_DIRECTORY}/analytics.sqlite")
    message(FATAL_ERROR "overview failed or created a database: ${overview_error}${overview_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" game roles --help
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE game_help_result
    OUTPUT_VARIABLE game_help_output
    ERROR_VARIABLE game_help_error
)
if(NOT game_help_result EQUAL 0 OR NOT game_help_output MATCHES "top,jungle,mid,bot,support" OR
   NOT game_help_output MATCHES "--blue" OR NOT game_help_output MATCHES "--red" OR
   EXISTS "${HELP_DIRECTORY}/analytics.sqlite")
    message(FATAL_ERROR "contextual game help failed: ${game_help_error}${game_help_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" help stats
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE stats_help_result
    OUTPUT_VARIABLE stats_help_output
    ERROR_VARIABLE stats_help_error
)
if(NOT stats_help_result EQUAL 0 OR NOT stats_help_output MATCHES "--pick CHAMPION")
    message(FATAL_ERROR "stats topic help failed: ${stats_help_error}${stats_help_output}")
endif()
if(NOT stats_help_output MATCHES "--player PLAYER" OR NOT stats_help_output MATCHES "!Jinx")
    message(FATAL_ERROR "stats help omitted player or negative-ban filtering: ${stats_help_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" help roster
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE roster_help_result
    OUTPUT_VARIABLE roster_help_output
    ERROR_VARIABLE roster_help_error
)
if(NOT roster_help_result EQUAL 0 OR NOT roster_help_output MATCHES "TOP,JUNGLE,MID,BOT,SUPPORT")
    message(FATAL_ERROR "roster topic help failed: ${roster_help_error}${roster_help_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --version
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE version_result
    OUTPUT_VARIABLE version_output
)
if(NOT version_result EQUAL 0 OR NOT version_output MATCHES "lolctl ${EXPECTED_VERSION}")
    message(FATAL_ERROR "version output failed: ${version_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" unknown
    WORKING_DIRECTORY "${HELP_DIRECTORY}"
    RESULT_VARIABLE unknown_result
    ERROR_VARIABLE unknown_error
)
if(unknown_result EQUAL 0 OR NOT unknown_error MATCHES "lolctl help")
    message(FATAL_ERROR "unknown command did not provide a help hint: ${unknown_error}")
endif()
