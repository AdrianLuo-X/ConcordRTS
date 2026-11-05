# H036: real original standard vision/building owners, one producer per profile.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")

# A later real portable-core owner can register the same production sources.
# No dummy concord_runtime or source copies are introduced.
function(concord_register_vision_phase_libraries runtime_owner contract_owner)
    foreach(_vision_profile IN ITEMS runtime contract)
        if(_vision_profile STREQUAL "runtime")
            set(_vision_owner "${runtime_owner}")
            set(_vision_abi Runtime)
            set(_vision_world concord_world_core_runtime_leaf)
        else()
            set(_vision_owner "${contract_owner}")
            set(_vision_abi Contract)
            set(_vision_world concord_world_core_hash_contract_leaf)
        endif()
        concord_add_profiled_leaf(concord_special_building_${_vision_profile}_leaf
            src/simulation/buildings/RuntimeSpecialBuildingSystem.cpp ${_vision_owner} ${_vision_abi})
        target_link_libraries(concord_special_building_${_vision_profile}_leaf PRIVATE ${_vision_world})
        concord_add_profiled_leaf(concord_vision_${_vision_profile}_leaf
            src/simulation/vision/RuntimeVision.cpp ${_vision_owner} ${_vision_abi})
        target_link_libraries(concord_vision_${_vision_profile}_leaf PRIVATE
            concord_special_building_${_vision_profile}_leaf ${_vision_world})
        concord_add_profiled_leaf(concord_vision_phase_${_vision_profile}_leaf
            src/simulation/vision/RuntimeVisionPhase.cpp ${_vision_owner} ${_vision_abi})
        target_link_libraries(concord_vision_phase_${_vision_profile}_leaf PRIVATE
            concord_vision_${_vision_profile}_leaf concord_trigger_scheduler_${_vision_profile}_leaf)
    endforeach()
endfunction()

if(TARGET concord_runtime AND TARGET concord_radar_cell_values_value_contract)
    concord_register_vision_phase_libraries(concord_runtime concord_radar_cell_values_value_contract)
    target_link_libraries(concord_runtime PRIVATE concord_vision_runtime_leaf
        concord_special_building_runtime_leaf concord_vision_phase_runtime_leaf)
    add_subdirectory(tests/infrastructure/vision_phase)
    add_test(NAME vision_phase_runtime_contract_test COMMAND concord_vision_phase_runtime_contract)
    add_test(NAME vision_phase_contract_contract_test COMMAND concord_vision_phase_contract_contract)
    set_tests_properties(vision_phase_runtime_contract_test vision_phase_contract_contract_test
        PROPERTIES TIMEOUT 180 LABELS "vision-phase;standard-contract;no-assets")
endif()
