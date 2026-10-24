# Only targets actually created by the selected core capability.
foreach(_core_target IN LISTS CONCORD_CORE_FIXTURE_TARGETS)
    concord_core_fixture_options(${_core_target})
endforeach()
concord_runtime_abi_definition(concord_production_core_hash_smoke)
concord_runtime_compile_options(concord_production_core_hash_smoke)
concord_runtime_crt_profile(concord_production_core_hash_smoke)
if(CONCORD_MSVC_FRONTEND)
    # Retain the full Runtime's two late, private /wd4702 clauses exactly.
    # They are diagnostic policy, not a dependency on their old GUI consumers.
    concord_target_compile_options(concord_production_core_hash_smoke PRIVATE /wd4702 /wd4702)
endif()
if(WIN32)
    target_link_libraries(concord_lan_transport_smoke PRIVATE ws2_32)
endif()
concord_finalize_compiler_contract()
