#
# Copyright (C) 2025 André "Wolf3s" Guilherme <andregui17@outlook.com>
# Copyright (C) 2025-Present uOPL devs
# Licenced under Academic Free License version 3.0
# Review w-Open-PS2-Loader README & LICENSE files for further details.
#

cmake_minimum_required(VERSION 3.16)
project(uOPL LANGUAGES NONE)

# Wrapper CMake build that delegates to the canonical Makefile-based PS2SDK build.
# This keeps one source of truth for build logic while offering CMake entry points.

find_program(UOPL_MAKE NAMES make gmake mingw32-make)
if(NOT UOPL_MAKE)
    message(FATAL_ERROR "No make program found. Install make/gmake/mingw32-make.")
endif()

set(UOPL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(UOPL_COMMON_ARGS "")

if(DEFINED LOCALVERSION AND NOT LOCALVERSION STREQUAL "")
    list(APPEND UOPL_COMMON_ARGS "LOCALVERSION=${LOCALVERSION}")
endif()

function(uopl_add_make_target target_name make_target)
    add_custom_target(${target_name}
        COMMAND "${UOPL_MAKE}" ${UOPL_COMMON_ARGS} ${make_target}
        WORKING_DIRECTORY "${UOPL_SOURCE_DIR}"
        USES_TERMINAL
        COMMENT "Running: make ${make_target}"
        VERBATIM
    )
endfunction()

add_custom_target(uopl ALL
    COMMAND "${UOPL_MAKE}" ${UOPL_COMMON_ARGS} all
    WORKING_DIRECTORY "${UOPL_SOURCE_DIR}"
    USES_TERMINAL
    COMMENT "Running: make all"
    VERBATIM
)

uopl_add_make_target(release release)
uopl_add_make_target(clean clean)
uopl_add_make_target(rebuild rebuild)
uopl_add_make_target(debug debug)
uopl_add_make_target(iopcore_debug iopcore_debug)
uopl_add_make_target(eesio_debug eesio_debug)
uopl_add_make_target(ingame_debug ingame_debug)
uopl_add_make_target(deci2_debug deci2_debug)
uopl_add_make_target(debug_ppctty debug_ppctty)
uopl_add_make_target(iopcore_ppctty_debug iopcore_ppctty_debug)
uopl_add_make_target(ingame_ppctty_debug ingame_ppctty_debug)

message(STATUS "uOPL CMake wrapper configured.")
message(STATUS "Use: cmake --build <build-dir> (default target: uopl)")
message(STATUS "Requires PS2SDK in environment when invoking make targets.")