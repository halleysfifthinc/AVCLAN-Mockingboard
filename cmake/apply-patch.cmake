# Idempotent patch driver for FetchContent PATCH_COMMAND steps.
#
# Usage: cmake -DGIT_EXECUTABLE=<git> -DPATCH_FILE=<patch> -P apply-patch.cmake
# (run with CWD = the populated source directory, as PATCH_COMMAND does).
#
# FetchContent re-runs PATCH_COMMAND after its update step on reconfigure, so a
# bare `git apply` fails the second time around. Skip if the patch is already
# applied (the reverse-apply dry run succeeds); otherwise apply it for real.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check --ignore-whitespace
        "${PATCH_FILE}"
    RESULT_VARIABLE _already_applied
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _already_applied EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
        COMMAND_ERROR_IS_FATAL ANY)
endif()
