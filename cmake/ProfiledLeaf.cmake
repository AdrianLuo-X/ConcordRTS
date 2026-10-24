# Shared S11 compiler/ABI factory; original function body unchanged.
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
