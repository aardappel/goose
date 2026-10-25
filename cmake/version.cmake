# Writes OUT, a header defining GOOSE_VERSION: the version given (the
# GOOSE_VERSION cache variable, which a release sets to its tag), or else
# what `git describe` says of the source tree SRC, or "unknown" outside a
# git checkout. Run on every build; the header is rewritten only when the
# version changes, so only src/version.cpp is compiled again.
if(NOT VERSION)
    execute_process(COMMAND git describe --tags --always --dirty
                    WORKING_DIRECTORY "${SRC}"
                    OUTPUT_VARIABLE VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
                    RESULT_VARIABLE result ERROR_QUIET)
    if(NOT result EQUAL 0 OR NOT VERSION)
        set(VERSION "unknown")
    endif()
endif()
set(text "#define GOOSE_VERSION \"${VERSION}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL text)
    file(WRITE "${OUT}" "${text}")
endif()
