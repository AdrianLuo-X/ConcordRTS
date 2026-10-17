# S12: included in the existing root scope immediately after S11 leaf definitions.
include_guard(GLOBAL)
if(NOT TARGET concord_ini_contract_leaf OR NOT TARGET concord_runtime_match_score_contract)
    message(FATAL_ERROR "S12 requires actual S11 ordinary-contract IniFile leaf and owner")
endif()
get_target_property(_s12_leaf_type concord_ini_contract_leaf TYPE)
get_target_property(_s12_leaf_profile concord_ini_contract_leaf CONCORD_LEAF_PROFILE)
get_target_property(_s12_leaf_owner concord_ini_contract_leaf CONCORD_LEAF_OWNER)
if(NOT _s12_leaf_type STREQUAL "STATIC_LIBRARY"
   OR NOT _s12_leaf_profile STREQUAL "Contract"
   OR NOT _s12_leaf_owner STREQUAL "concord_runtime_match_score_contract")
    message(FATAL_ERROR "S12 IniFile leaf profile/owner does not match ordinary tests")
endif()
add_executable(concord_replay_event_snapshot_contract
    tests/infrastructure/ReplayEventSnapshotContract.cpp
    src/analysis/sandbox/ReplaySandbox.cpp
    src/data/rules/RulesDatabase.cpp)
add_executable(concord_replay_event_snapshot_header_probe
    tests/infrastructure/ReplayEventSnapshotHeaderProbe.cpp)
foreach(_s12_target IN ITEMS concord_replay_event_snapshot_contract concord_replay_event_snapshot_header_probe)
    target_include_directories(${_s12_target} PRIVATE src)
    target_compile_features(${_s12_target} PRIVATE cxx_std_20)
    # Same private ordinary-test options/definitions, including late owner options.
    # No INTERFACE usage requirements flow into unrelated consumers.
    target_compile_options(${_s12_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_OPTIONS>>")
    target_compile_definitions(${_s12_target} PRIVATE
        "$<TARGET_GENEX_EVAL:concord_runtime_match_score_contract,$<TARGET_PROPERTY:concord_runtime_match_score_contract,COMPILE_DEFINITIONS>>")
    # New diagnostic-only compatibility; ordinary ABI/CRT/EH/FP stays unchanged.
    target_compile_definitions(${_s12_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(CONCORD_MSVC_ABI)
        set_property(TARGET ${_s12_target} PROPERTY MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()
endforeach()
target_link_libraries(concord_replay_event_snapshot_contract PRIVATE concord_ini_contract_leaf)
# No direct IniFile.cpp, Shell.cpp, device/network/render/audio implementation.
unset(_s12_target)
unset(_s12_leaf_type)
unset(_s12_leaf_profile)
unset(_s12_leaf_owner)