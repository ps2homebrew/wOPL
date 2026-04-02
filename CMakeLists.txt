#
# Copyright (C) 2025 André "Wolf3s" Guilherme <andregui17@outlook.com>
# Copyright (C) 2025-Present wOPL devs
# Licenced under Academic Free License version 3.0
# Review w-Open-PS2-Loader README & LICENSE files for further details.
#

cmake_minimum_required(VERSION 3.16)
project(WOPL LANGUAGES NONE)

# Wrapper CMake build that delegates to the canonical Makefile-based PS2SDK build.
# This keeps one source of truth for build logic while offering CMake entry points.

find_program(WOPL_MAKE NAMES make gmake mingw32-make)
if(NOT WOPL_MAKE)
    message(FATAL_ERROR "No make program found. Install make/gmake/mingw32-make.")
endif()

set(WOPL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(WOPL_COMMON_ARGS "")

if(DEFINED LOCALVERSION AND NOT LOCALVERSION STREQUAL "")
    list(APPEND WOPL_COMMON_ARGS "LOCALVERSION=${LOCALVERSION}")
endif()

function(wopl_add_make_target target_name make_target)
    add_custom_target(${target_name}
        COMMAND "${WOPL_MAKE}" ${WOPL_COMMON_ARGS} ${make_target}
        WORKING_DIRECTORY "${WOPL_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running: make ${make_target}"
        VERBATIM
    )
endfunction()

add_custom_target(wopl ALL
    COMMAND "${WOPL_MAKE}" ${WOPL_COMMON_ARGS} all
    WORKING_DIRECTORY "${WOPL_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Running: make all"
    VERBATIM
)

wopl_add_make_target(release release)
wopl_add_make_target(clean clean)
wopl_add_make_target(rebuild rebuild)
wopl_add_make_target(debug debug)
wopl_add_make_target(iopcore_debug iopcore_debug)
wopl_add_make_target(eesio_debug eesio_debug)
wopl_add_make_target(ingame_debug ingame_debug)
wopl_add_make_target(deci2_debug deci2_debug)
wopl_add_make_target(debug_ppctty debug_ppctty)
wopl_add_make_target(iopcore_ppctty_debug iopcore_ppctty_debug)
wopl_add_make_target(ingame_ppctty_debug ingame_ppctty_debug)

message(STATUS "wOPL CMake wrapper configured.")
message(STATUS "Use: cmake --build <build-dir> (default target: wopl)")
message(STATUS "Requires PS2SDK in environment when invoking make targets.")
