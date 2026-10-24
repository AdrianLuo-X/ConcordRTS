include_guard(GLOBAL)
# These are the original full-product clauses, not a weaker core build policy.
function(concord_core_fixture_options target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Missing required core fixture: ${target}")
    endif()
    if(NOT CONCORD_MSVC_FRONTEND)
        return()
    endif()
    if(target STREQUAL "concord_game_world_deterministic_hash_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive- /wd4189 /wd4996
    )
    elseif(target STREQUAL "concord_map_data_smoke")
        concord_target_compile_options(${target} PRIVATE /utf-8 /FS)
    elseif(target STREQUAL "concord_multiplayer_gameplay_command_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    elseif(target STREQUAL "concord_multiplayer_match_setup_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    elseif(target STREQUAL "concord_multiplayer_snapshot_transfer_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    elseif(target STREQUAL "concord_multiplayer_compatibility_fingerprint_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    elseif(target STREQUAL "concord_multiplayer_lockstep_smoke")
        concord_target_compile_options(
        ${target}
        PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    elseif(target STREQUAL "concord_runtime_atomic_file_smoke")
        concord_target_compile_options(${target} PRIVATE /utf-8 /FS)
    elseif(target STREQUAL "concord_lan_transport_smoke")
        concord_target_compile_options(
        ${target} PRIVATE /utf-8 /FS /W4 /WX /permissive-
    )
    else()
        message(FATAL_ERROR "Unknown core fixture compiler policy: ${target}")
    endif()
endfunction()
function(concord_runtime_abi_definition target)
    target_compile_definitions(${target} PRIVATE CONCORD_RULES_ABI_REVISION=${CONCORD_RULES_ABI_REVISION})
endfunction()
function(concord_runtime_compile_options target)
    if(CONCORD_MSVC_FRONTEND)
concord_target_compile_options(
        ${target} PRIVATE
        /utf-8
        /FS
        /bigobj
        $<$<CONFIG:Debug>:/O2>
        $<$<CONFIG:Debug>:/Ob2>
    )
    endif()
endfunction()
function(concord_runtime_crt_profile target)
if(CONCORD_MSVC_ABI)
    # CRT choice and STL iterator layout belong to the ABI, not the driver.
    set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
endif()
endfunction()
