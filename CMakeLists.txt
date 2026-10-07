cmake_minimum_required(VERSION 3.24)

project(lol_team_analysis VERSION 0.4.2 LANGUAGES CXX)

option(LOL_ANALYTICS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(SQLite3 REQUIRED)

add_library(lol_analytics
    src/assignment_service.cpp
    src/analytics_service.cpp
    src/model.cpp
    src/roster_service.cpp
    src/series_service.cpp
    src/sqlite_repository.cpp
    src/workspace_config.cpp
)
add_library(lol::analytics ALIAS lol_analytics)

target_compile_features(lol_analytics PUBLIC cxx_std_20)
target_include_directories(lol_analytics PUBLIC include)
target_link_libraries(lol_analytics PRIVATE SQLite::SQLite3)

if(MSVC)
    target_compile_options(lol_analytics PRIVATE /W4)
    if(LOL_ANALYTICS_WARNINGS_AS_ERRORS)
        target_compile_options(lol_analytics PRIVATE /WX)
    endif()
else()
    target_compile_options(lol_analytics PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(LOL_ANALYTICS_WARNINGS_AS_ERRORS)
        target_compile_options(lol_analytics PRIVATE -Werror)
    endif()
endif()

add_executable(lolctl src/main.cpp)
target_compile_features(lolctl PRIVATE cxx_std_20)
target_link_libraries(lolctl PRIVATE lol::analytics)
target_compile_definitions(lolctl PRIVATE LOL_ANALYTICS_VERSION="${PROJECT_VERSION}")

include(GNUInstallDirs)
install(TARGETS lolctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CTest)
if(BUILD_TESTING)
    add_executable(lol_analytics_tests tests/integration_tests.cpp)
    target_compile_features(lol_analytics_tests PRIVATE cxx_std_20)
    target_link_libraries(lol_analytics_tests PRIVATE lol::analytics)
    add_test(NAME lol_analytics_tests COMMAND lol_analytics_tests)
    add_test(
        NAME lolctl_workflow
        COMMAND ${CMAKE_COMMAND}
            -DLOLCTL=$<TARGET_FILE:lolctl>
            -DTEST_DATABASE=${CMAKE_CURRENT_BINARY_DIR}/lolctl-team-slot-test.sqlite
            -DDRAFT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/examples/format.csv
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_team_slot_test.cmake
    )
    add_test(
        NAME lolctl_database_selection
        COMMAND ${CMAKE_COMMAND}
            -DLOLCTL=$<TARGET_FILE:lolctl>
            -DWORKSPACE_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/lolctl-workspace-test
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_database_selection_test.cmake
    )
    add_test(
        NAME lolctl_help
        COMMAND ${CMAKE_COMMAND}
            -DLOLCTL=$<TARGET_FILE:lolctl>
            -DHELP_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/lolctl-help-test
            -DEXPECTED_VERSION=${PROJECT_VERSION}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_help_test.cmake
    )
endif()
