if(NOT DEFINED LOLCTL OR NOT DEFINED WORKSPACE_DIRECTORY)
    message(FATAL_ERROR "lolctl database-selection test is missing a required path")
endif()

file(REMOVE_RECURSE "${WORKSPACE_DIRECTORY}")
file(MAKE_DIRECTORY "${WORKSPACE_DIRECTORY}/nested")
file(REAL_PATH "${WORKSPACE_DIRECTORY}" REAL_WORKSPACE_DIRECTORY)

execute_process(
    COMMAND "${LOLCTL}" db use team.sqlite
    WORKING_DIRECTORY "${WORKSPACE_DIRECTORY}"
    RESULT_VARIABLE use_result
    OUTPUT_VARIABLE use_output
    ERROR_VARIABLE use_error
)
if(NOT use_result EQUAL 0 OR NOT EXISTS "${WORKSPACE_DIRECTORY}/.lolctl" OR
   NOT EXISTS "${WORKSPACE_DIRECTORY}/team.sqlite")
    message(FATAL_ERROR "db use failed: ${use_error}${use_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" db show
    WORKING_DIRECTORY "${WORKSPACE_DIRECTORY}/nested"
    RESULT_VARIABLE show_result
    OUTPUT_VARIABLE show_output
    ERROR_VARIABLE show_error
)
if(NOT show_result EQUAL 0 OR NOT show_output MATCHES "source=workspace" OR
   NOT show_output MATCHES "database=${REAL_WORKSPACE_DIRECTORY}/team.sqlite")
    message(FATAL_ERROR "nested workspace did not inherit selection: ${show_error}${show_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" series add NCK SKY --patch 26.15 --elo 1200,1800,1512.5
    WORKING_DIRECTORY "${WORKSPACE_DIRECTORY}/nested"
    RESULT_VARIABLE series_result
    OUTPUT_VARIABLE series_output
    ERROR_VARIABLE series_error
)
if(NOT series_result EQUAL 0 OR NOT series_output MATCHES "series_id=1")
    message(FATAL_ERROR "saved selection was not used by series add: ${series_error}${series_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" --db override.sqlite db show
    WORKING_DIRECTORY "${WORKSPACE_DIRECTORY}/nested"
    RESULT_VARIABLE override_result
    OUTPUT_VARIABLE override_output
    ERROR_VARIABLE override_error
)
if(NOT override_result EQUAL 0 OR NOT override_output MATCHES "source=--db" OR
   NOT override_output MATCHES "database=${REAL_WORKSPACE_DIRECTORY}/nested/override.sqlite")
    message(FATAL_ERROR "explicit --db did not override workspace: ${override_error}${override_output}")
endif()

execute_process(
    COMMAND "${LOLCTL}" db clear
    WORKING_DIRECTORY "${WORKSPACE_DIRECTORY}/nested"
    RESULT_VARIABLE clear_result
    OUTPUT_VARIABLE clear_output
    ERROR_VARIABLE clear_error
)
if(NOT clear_result EQUAL 0 OR EXISTS "${WORKSPACE_DIRECTORY}/.lolctl")
    message(FATAL_ERROR "db clear failed: ${clear_error}${clear_output}")
endif()
