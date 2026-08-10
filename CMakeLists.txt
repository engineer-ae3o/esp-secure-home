# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp-secure-home)


# Extra tcommands for static analysis and formatting
find_program(CPPCHECK cppcheck)
find_program(CLANG_FORMAT clang-format)


# Static analysis via cppcheck
if (CPPCHECK)
add_custom_target(cppcheck
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/.cppcheck
    COMMAND ${CPPCHECK} --enable=all
        --inline-suppr
        --cppcheck-build-dir=${CMAKE_BINARY_DIR}/.cppcheck
        --suppressions-list=${CMAKE_SOURCE_DIR}/cppcheck_suppressions.txt
        --error-exitcode=1
        --check-level=exhaustive
        --project=${CMAKE_BINARY_DIR}/compile_commands.json
        --file-filter=${CMAKE_SOURCE_DIR}/main/*
        --file-filter=${CMAKE_SOURCE_DIR}/components/app/src/**
        --output-file=${CMAKE_BINARY_DIR}/cppcheck_report.txt
        --checkers-report=${CMAKE_BINARY_DIR}/cppcheck_checks.txt
    COMMENT "Running static analysis..."
)
else()
message(WARNING "cppcheck not found. Formatting will not be availabe for this target.")
endif()


# Formatting with clang-format
if(CLANG_FORMAT)
add_custom_target(format
    COMMAND git ls-files -- '*.c' '*.h' '*.cpp' '*.hpp' | xargs -r ${CLANG_FORMAT} -i
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting version controlled files...."
)
else()
message(WARNING "clang-format not found. Formatting will not be availabe for this target.")
endif()
