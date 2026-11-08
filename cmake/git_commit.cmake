# Writes the short git hash of the source to OUTPUT for the 关于 page. Run with cmake -P at every build.
# COMMIT comes from the build (e.g. Nix, which builds without .git) and wins over git.
if (NOT COMMIT AND GIT)
    execute_process(COMMAND "${GIT}" -C "${SOURCE_DIR}" rev-parse --short=7 HEAD
            OUTPUT_VARIABLE COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE failed)
    if (failed)
        set(COMMIT "")
    endif ()
endif ()
set(text "#pragma once\n\n#define B_LAUNCHER_COMMIT \"${COMMIT}\"\n")
if (EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" written)
    if ("${written}" STREQUAL "${text}")
        return()
    endif ()
endif ()
file(WRITE "${OUTPUT}" "${text}")
