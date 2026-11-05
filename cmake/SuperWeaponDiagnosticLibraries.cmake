# H010: production mechanisms and explicit legacy diagnostics have separate TUs.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

foreach(_super_profile IN ITEMS runtime contract)
    if(_super_profile STREQUAL "runtime")
        set(_super_owner concord_runtime)
        set(_super_abi Runtime)
    else()
        set(_super_owner concord_superweapon_smoke)
        set(_super_abi Contract)
    endif()
    concord_add_profiled_leaf(concord_superweapon_core_${_super_profile}_leaf
        src/simulation/superweapons/SuperWeaponSystem.cpp ${_super_owner} ${_super_abi})
    target_sources(concord_superweapon_core_${_super_profile}_leaf PRIVATE
        src/simulation/superweapons/RuntimeChronosphereRelocation.cpp)
    concord_add_profiled_leaf(concord_superweapon_diagnostics_${_super_profile}_leaf
        src/simulation/superweapons/diagnostics/SuperWeaponSystemSmoke.cpp ${_super_owner} ${_super_abi})
    target_link_libraries(concord_superweapon_diagnostics_${_super_profile}_leaf PRIVATE
        concord_superweapon_core_${_super_profile}_leaf)
endforeach()

# These real consumers retain their original Rules/Ini/FactoryDelivery owners.
# Portable production callers select the core archive without the diagnostic TU.
target_link_libraries(concord_runtime PRIVATE concord_superweapon_diagnostics_runtime_leaf)
target_link_libraries(concord_superweapon_smoke PRIVATE concord_superweapon_diagnostics_contract_leaf)

# H011: original standard providers, with no duplicate Runtime rules producer.
concord_add_profiled_leaf(concord_superweapon_rules_contract_leaf
    src/data/rules/RulesDatabase.cpp concord_superweapon_smoke Contract)
target_sources(concord_superweapon_rules_contract_leaf PRIVATE src/data/rules/ProductionRulesLoader.cpp)
target_link_libraries(concord_superweapon_rules_contract_leaf PRIVATE concord_ini_contract_leaf)
foreach(_super_profile IN ITEMS runtime contract)
    if(_super_profile STREQUAL "runtime")
        set(_super_owner concord_runtime)
        set(_super_abi Runtime)
    else()
        set(_super_owner concord_superweapon_smoke)
        set(_super_abi Contract)
    endif()
    concord_add_profiled_leaf(concord_superweapon_delivery_${_super_profile}_leaf
        src/simulation/production/RuntimeFactoryDeliveryState.cpp ${_super_owner} ${_super_abi})
endforeach()
target_link_libraries(concord_superweapon_core_runtime_leaf PRIVATE
    concord_world_core_runtime_leaf concord_superweapon_delivery_runtime_leaf)
target_link_libraries(concord_superweapon_core_contract_leaf PRIVATE
    concord_superweapon_rules_contract_leaf concord_superweapon_delivery_contract_leaf)
add_subdirectory(tests/infrastructure/superweapon_core)
