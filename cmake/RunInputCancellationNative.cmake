cmake_minimum_required(VERSION 3.20)
foreach(required GAME_EXECUTABLE FIXTURE SOURCE_ROOT OUTPUT_PARENT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
string(TIMESTAMP stamp "%Y%m%d-%H%M%S" UTC)
string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef unique)
set(run "${OUTPUT_PARENT}/${stamp}-${unique}")
if(EXISTS "${run}")
    message(FATAL_ERROR "Native run directory must be new")
endif()
file(MAKE_DIRECTORY "${run}/cwd" "${run}/temp" "${run}/output")
configure_file("${FIXTURE}" "${run}/private.ini" COPYONLY)
file(SHA256 "${FIXTURE}" fixture_sha)
file(SHA256 "${run}/private.ini" private_sha)
if(NOT fixture_sha STREQUAL private_sha)
    message(FATAL_ERROR "Complete private fixture differs")
endif()
file(SHA256 "${GAME_EXECUTABLE}" executable_sha)
file(WRITE "${run}/argv.txt" "${GAME_EXECUTABLE}\n--command-smoke-test\n--config\n${run}/private.ini\n--resource-root\n${SOURCE_ROOT}/RedAlert2\n--local-data-root\n${SOURCE_ROOT}/local_data\n--map\n${SOURCE_ROOT}/local_data/resources/mix_exports/multimd/dryheat.map\n")
string(TIMESTAMP started "%Y-%m-%dT%H:%M:%SZ" UTC)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "TEMP=${run}/temp" "TMP=${run}/temp"
    "CONCORD_COMMAND_SMOKE_INPUT_CANCELLATION_ONLY=1"
    "CONCORD_INPUT_CANCELLATION_OUTPUT=${run}/output"
    "${GAME_EXECUTABLE}" --command-smoke-test
    --config "${run}/private.ini"
    --resource-root "${SOURCE_ROOT}/RedAlert2"
    --local-data-root "${SOURCE_ROOT}/local_data"
    --map "${SOURCE_ROOT}/local_data/resources/mix_exports/multimd/dryheat.map"
    WORKING_DIRECTORY "${run}/cwd"
    RESULT_VARIABLE native_result
    OUTPUT_FILE "${run}/native.stdout.log"
    ERROR_FILE "${run}/native.stderr.log"
    TIMEOUT 7200)
string(TIMESTAMP finished "%Y-%m-%dT%H:%M:%SZ" UTC)
file(SHA256 "${run}/native.stdout.log" stdout_sha)
file(SHA256 "${run}/native.stderr.log" stderr_sha)
file(SHA256 "${run}/argv.txt" argv_sha)
if("${native_result}" MATCHES "^-?[0-9]+$")
    set(actual "${native_result}")
else()
    set(actual null)
endif()
string(REPLACE "\\" "\\\\" escaped_result "${native_result}")
string(REPLACE "\"" "\\\"" escaped_result "${escaped_result}")
string(REPLACE "\n" "\\n" escaped_result "${escaped_result}")
file(WRITE "${run}/native-result.json" "{\n  \"schema\":\"W012RegisteredNativeResult/v1\",\n  \"startedUtc\":\"${started}\",\n  \"finishedUtc\":\"${finished}\",\n  \"executable\":\"${GAME_EXECUTABLE}\",\n  \"executableSha256\":\"${executable_sha}\",\n  \"cwd\":\"${run}/cwd\",\n  \"privateConfig\":\"${run}/private.ini\",\n  \"fixtureSha256\":\"${fixture_sha}\",\n  \"temp\":\"${run}/temp\",\n  \"output\":\"${run}/output\",\n  \"argvSha256\":\"${argv_sha}\",\n  \"actualExit\":${actual},\n  \"nativeResultRaw\":\"${escaped_result}\",\n  \"stdoutSha256\":\"${stdout_sha}\",\n  \"stderrSha256\":\"${stderr_sha}\"\n}\n")
file(READ "${run}/native.stdout.log" native_stdout)
file(READ "${run}/native.stderr.log" native_stderr)
message(STATUS "W012 native result: ${run}/native-result.json")
message("${native_stdout}")
if(NOT "${native_stderr}" STREQUAL "")
    message("${native_stderr}")
endif()
if(NOT "${native_result}" STREQUAL "0")
    message(FATAL_ERROR "Actual native matrix failed: ${native_result}; complete raw evidence preserved")
endif()
