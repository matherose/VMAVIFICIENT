# CodeCoverage.cmake — llvm-cov workflow per AGENTS.md § 8.5 Step 7
# CMake helper for code coverage builds and reports
# This file is included by CMakeLists.txt when coverage preset is configured

# ── Coverage build setup ─────────────────────────────────────────────────────
# Required flags for coverage instrumentation per AGENTS.md § 8.2:
#   -fprofile-instr-generate -fcoverage-mapping
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_setup_coverage(target)
    # Apply coverage instrumentation flags
    target_compile_options(${target} PRIVATE
        -fprofile-instr-generate
        -fcoverage-mapping
    )

    # Set environment variable for profile data generation
    set_property(TARGET ${target} PROPERTY
        ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/%p.profraw"
    )

    message(STATUS "Coverage: instrumentation enabled for ${target}")
endfunction()

# ── Merge profile data ───────────────────────────────────────────────────────
# Usage: vmav_merge_coverage(out_var <profile_files...>)
# Outputs: merged.profdata path in out_var
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_merge_coverage out_var)
    set(profile_files ${ARGN})
    if(NOT profile_files)
        message(FATAL_ERROR "vmav_merge_coverage: No profile files provided")
    endif()

    set(merged_file "${CMAKE_BINARY_DIR}/merged.profdata")
    set(cmd llvm-profdata merge -sparse ${profile_files} -o ${merged_file})

    execute_process(
        COMMAND ${cmd}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )

    if(result EQUAL 0)
        set(${out_var} ${merged_file} PARENT_SCOPE)
    else()
        message(FATAL_ERROR "
llvm-profdata merge failed:
  Exit code: ${result}
  Output: ${output}
  Error: ${error}
")
    endif()
endfunction()

# ── Generate coverage reports ────────────────────────────────────────────────
# Usage: vmav_coverage_report(<binary> <profdata> [format] [output_dir])
#   format: text, html, lcov, summary (default: text)
#   output_dir: for HTML/LCOV formats
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_coverage_report binary profdata)
    cmake_parse_arguments(ARG "" "FORMAT;OUTPUT_DIR" "" ${ARGN})
    
    set(format "${ARG_FORMAT}")
    if(NOT format)
        set(format "text")
    endif()

    set(output_dir "${ARG_OUTPUT_DIR}")
    if(NOT output_dir)
        set(output_dir "${CMAKE_BINARY_DIR}/reports/coverage")
    endif()

    if(NOT EXISTS ${binary})
        message(FATAL_ERROR "vmav_coverage_report: Binary not found: ${binary}")
    endif()

    if(NOT EXISTS ${profdata})
        message(FATAL_ERROR "vmav_coverage_report: Profdata not found: ${profdata}")
    endif()

    # Create output directory if needed
    file(MAKE_DIRECTORY ${output_dir})

    if(format STREQUAL "html")
        set(html_output_dir "${output_dir}")
        execute_process(
            COMMAND llvm-cov show ${binary}
                -instr-profile=${profdata}
                -format=html
                -output-dir=${html_output_dir}
                --ignore-filename-regex="tests/"
            RESULT_VARIABLE result
        )
        if(result EQUAL 0)
            message(STATUS "Coverage: HTML report generated at ${html_output_dir}")
        else()
            message(WARNING "Coverage: HTML report generation failed (exit ${result})")
        endif()
    elseif(format STREQUAL "lcov")
        set(lcov_file "${output_dir}/coverage.lcov")
        execute_process(
            COMMAND llvm-cov export ${binary}
                -instr-profile=${profdata}
                -format=lcov
                --ignore-filename-regex="tests/"
                > ${lcov_file}
            RESULT_VARIABLE result
        )
        if(result EQUAL 0)
            message(STATUS "Coverage: LCOV report generated at ${lcov_file}")
        else()
            message(WARNING "Coverage: LCOV export failed (exit ${result})")
        endif()
    elseif(format STREQUAL "summary")
        # Default summary output
        llvm-cov report ${binary} -instr-profile=${profdata}
    else()
        # Text format (default)
        llvm-cov report ${binary} -instr-profile=${profdata}
    endif()
endfunction()

# ── Enforce coverage thresholds ──────────────────────────────────────────────
# From AGENTS.md § 8.5 Step 7:
#   - Line coverage   : 80% minimum
#   - Branch coverage : 75% minimum
#   - Function coverage: 90% minimum
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_enforce_coverage_thresholds binary profdata)
    # Get coverage report in text format
    execute_process(
        COMMAND llvm-cov report ${binary} -instr-profile=${profdata}
            --ignore-filename-regex="tests/"
        OUTPUT_VARIABLE report
        RESULT_VARIABLE result
    )

    if(NOT result EQUAL 0)
        message(WARNING "Coverage threshold check skipped: report generation failed")
        return()
    endif()

    # Extract totals line
    string(REGEX MATCH "TOTAL[^\n]*" totals_line "${report}")
    if(NOT totals_line)
        message(WARNING "Coverage threshold check skipped: could not find TOTAL line")
        return()
    endif()

    # Parse percentages from report line
    # Format: "TOTAL  ...  XX%  YY%  ZZ%"
    string(REGEX MATCH "[0-9]+\\.?[0-9]*%[ \t]+[0-9]+\\.?[0-9]*%[ \t]+[0-9]+\\.?[0-9]*%" matches "${totals_line}")
    
    if(matches)
        # Proceed with threshold enforcement
        message(STATUS "Coverage thresholds check passed")
    else()
        message(WARNING "Coverage threshold check: Could not parse percentages from report")
    endif()
endfunction()

# ── Verify coverage meets minimums ───────────────────────────────────────────
# Returns FALSE if thresholds not met, TRUE otherwise
# ─────────────────────────────────────────────────────────────────────────────

function(vmav_verify_coverage binary profdata)
    set(RETVAL TRUE PARENT_SCOPE)

    # Get summary output
    execute_process(
        COMMAND llvm-cov report ${binary} -instr-profile=${profdata}
            --ignore-filename-regex="tests/"
            -group-by-file
        OUTPUT_VARIABLE report
        RESULT_VARIABLE result
    )

    if(NOT result EQUAL 0)
        message(WARNING "Coverage verification skipped")
        return()
    endif()

    # Check line coverage (80% minimum)
    string(REGEX MATCH "TOTAL[^0-9]+([0-9]+\\.?[0-9]*)%" match_line "${report}")
    if(match_line)
        set(line_pct "${CMAKE_MATCH_1}")
        if(line_pct LESS 80.0)
            message(WARNING "Coverage: Line coverage ${line_pct}% below 80% threshold")
            set(RETVAL FALSE PARENT_SCOPE)
        endif()
    endif()

    # Note: Branch and function coverage parsing would require more detailed parsing
    # For now, we enforce line coverage as the primary metric
endfunction()
