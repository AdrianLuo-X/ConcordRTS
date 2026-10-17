# Stable source ownership with distinct consumer ABI/diagnostic profiles.
# Included after the four existing consumer targets, in the existing root scope.
include_guard(GLOBAL)

function(concord_add_profiled_leaf target source owner profile)
    if(NOT TARGET "${owner}")
        message(FATAL_ERROR "Leaf owner target is missing: ${owner}")
    endif()
    add_library("${target}" STATIC "${source}")
    target_include_directories("${target}" PRIVATE "${PROJECT_SOURCE_DIR}/src")
    target_compile_features("${target}" PRIVATE cxx_std_20)
    # PRIVATE only: neither flags nor definitions flow back into consumers.
    # Resolve after all includes so final late /wd4702, ABI stamps and /RTC1
    # stay identical to the original owner, in their original option order.
    target_compile_options("${target}" PRIVATE
        "$<TARGET_GENEX_EVAL:${owner},$<TARGET_PROPERTY:${owner},COMPILE_OPTIONS>>")
    target_compile_definitions("${target}" PRIVATE
        "$<TARGET_GENEX_EVAL:${owner},$<TARGET_PROPERTY:${owner},COMPILE_DEFINITIONS>>")
    if(CONCORD_MSVC_ABI)
        if(profile STREQUAL "Runtime")
            set_property(TARGET "${target}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
        elseif(profile STREQUAL "Tools" OR profile STREQUAL "Contract")
            set_property(TARGET "${target}" PROPERTY MSVC_RUNTIME_LIBRARY
                "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        else()
            message(FATAL_ERROR "Unknown leaf compilation profile: ${profile}")
        endif()
    endif()
    set_property(TARGET "${target}" PROPERTY CONCORD_LEAF_OWNER "${owner}")
    set_property(TARGET "${target}" PROPERTY CONCORD_LEAF_PROFILE "${profile}")
endfunction()

concord_add_profiled_leaf(concord_csf_runtime_leaf src/assets/text/CsfCodec.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_csf_contract_leaf src/assets/text/CsfCodec.cpp concord_csf_codec_contract Contract)
concord_add_profiled_leaf(concord_ini_runtime_leaf src/core/io/IniFile.cpp concord_runtime Runtime)
concord_add_profiled_leaf(concord_ini_tools_leaf src/core/io/IniFile.cpp concord_rules_dump Tools)
concord_add_profiled_leaf(concord_ini_contract_leaf src/core/io/IniFile.cpp concord_runtime_match_score_contract Contract)

target_link_libraries(concord_runtime PRIVATE concord_csf_runtime_leaf concord_ini_runtime_leaf)
target_link_libraries(concord_rules_dump PRIVATE concord_ini_tools_leaf)
target_link_libraries(concord_csf_codec_contract PRIVATE concord_csf_contract_leaf)
target_link_libraries(concord_runtime_match_score_contract PRIVATE concord_ini_contract_leaf)

set(_concord_leaf_report "configuration=$<CONFIG>\n")
foreach(_concord_leaf IN ITEMS concord_csf_runtime_leaf concord_csf_contract_leaf
        concord_ini_runtime_leaf concord_ini_tools_leaf concord_ini_contract_leaf)
    get_target_property(_concord_owner "${_concord_leaf}" CONCORD_LEAF_OWNER)
    get_target_property(_concord_profile "${_concord_leaf}" CONCORD_LEAF_PROFILE)
    string(APPEND _concord_leaf_report
        "\ntarget=${_concord_leaf}\nowner=${_concord_owner}\nprofile=${_concord_profile}\nCRT=$<TARGET_GENEX_EVAL:${_concord_leaf},$<TARGET_PROPERTY:${_concord_leaf},MSVC_RUNTIME_LIBRARY>>\noptions=$<JOIN:$<TARGET_GENEX_EVAL:${_concord_leaf},$<TARGET_PROPERTY:${_concord_leaf},COMPILE_OPTIONS>>, >\ndefinitions=$<JOIN:$<TARGET_GENEX_EVAL:${_concord_leaf},$<TARGET_PROPERTY:${_concord_leaf},COMPILE_DEFINITIONS>>, >\n")
endforeach()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/leaf-module-profiles-$<CONFIG>.txt"
    CONTENT "${_concord_leaf_report}")