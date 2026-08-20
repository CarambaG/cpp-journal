cmake_minimum_required(VERSION 3.16)

project(cpp_journal
    VERSION 1.0.0
    DESCRIPTION "C++17 text journal library"
    LANGUAGES CXX
)

option(JOURNAL_BUILD_TESTS "Build unit tests" ON)

find_package(Threads REQUIRED)

add_library(journal_objects OBJECT
    src/file_logger.cpp
    src/socket_logger.cpp
)

set_target_properties(journal_objects PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(journal_objects PUBLIC cxx_std_17)
target_include_directories(journal_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(journal_objects PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()

add_library(journal_static STATIC
    $<TARGET_OBJECTS:journal_objects>
)
add_library(Journal::static ALIAS journal_static)
target_include_directories(journal_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(journal_static PUBLIC cxx_std_17)
target_link_libraries(journal_static PUBLIC
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

add_library(journal_shared SHARED
    $<TARGET_OBJECTS:journal_objects>
)
add_library(Journal::shared ALIAS journal_shared)
set_target_properties(journal_shared PROPERTIES
    OUTPUT_NAME journal
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(journal_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(journal_shared PUBLIC cxx_std_17)
target_link_libraries(journal_shared PUBLIC
    Threads::Threads
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# On Windows a DLL also produces an import library. Give the real static
# library another name so that both .lib files can coexist in one directory.
if(WIN32)
    set_target_properties(journal_static PROPERTIES OUTPUT_NAME journal_static)
else()
    set_target_properties(journal_static PROPERTIES OUTPUT_NAME journal)
endif()

add_executable(journal_cli
    apps/journal_cli/main.cpp
    apps/journal_cli/input_parser.cpp
    apps/journal_cli/journal_application.cpp
)
target_compile_features(journal_cli PRIVATE cxx_std_17)
target_link_libraries(journal_cli PRIVATE Journal::static)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(journal_cli PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()

if(UNIX)
    add_library(journal_stats_core STATIC
        apps/journal_stats/log_record.cpp
        apps/journal_stats/statistics.cpp
        apps/journal_stats/tcp_statistics_server.cpp
    )
    target_compile_features(journal_stats_core PUBLIC cxx_std_17)
    target_include_directories(journal_stats_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/journal_stats
    )
    target_link_libraries(journal_stats_core PUBLIC
        Journal::static
        Threads::Threads
    )

    add_executable(journal_stats
        apps/journal_stats/main.cpp
    )
    target_compile_features(journal_stats PRIVATE cxx_std_17)
    target_link_libraries(journal_stats PRIVATE journal_stats_core)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(journal_stats_core PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
        target_compile_options(journal_stats PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS journal_static journal_shared journal_cli
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(UNIX)
    install(TARGETS journal_stats
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(JOURNAL_BUILD_TESTS)
    enable_testing()

    add_executable(journal_tests
        tests/file_logger_tests.cpp
        tests/socket_logger_tests.cpp
    )
    target_compile_features(journal_tests PRIVATE cxx_std_17)
    target_link_libraries(journal_tests PRIVATE Journal::static)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(journal_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    endif()

    add_test(NAME journal_tests COMMAND journal_tests)

    add_executable(journal_cli_tests
        tests/journal_cli_tests.cpp
        apps/journal_cli/input_parser.cpp
        apps/journal_cli/journal_application.cpp
    )
    target_compile_features(journal_cli_tests PRIVATE cxx_std_17)
    target_include_directories(journal_cli_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/apps/journal_cli
    )
    target_link_libraries(journal_cli_tests PRIVATE Journal::static)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(journal_cli_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    endif()

    add_test(NAME journal_cli_tests COMMAND journal_cli_tests)

    if(UNIX)
        add_executable(journal_stats_tests
            tests/journal_stats_tests.cpp
        )
        target_compile_features(journal_stats_tests PRIVATE cxx_std_17)
        target_link_libraries(journal_stats_tests PRIVATE
            journal_stats_core
            Threads::Threads
        )

        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(journal_stats_tests PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
            )
        endif()

        add_test(NAME journal_stats_tests COMMAND journal_stats_tests)
    endif()
endif()
