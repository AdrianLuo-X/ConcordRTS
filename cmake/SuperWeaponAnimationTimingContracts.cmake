# H017: resource/animation policy contracts, independent of host and drawing.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/ProfiledLeaf.cmake")
concord_add_profiled_leaf(concord_auxiliary_shp_bytes_contract_leaf
    src/assets/sprites/RuntimeAuxiliaryShpBytes.cpp concord_radar_cell_values_value_contract Contract)
concord_add_profiled_leaf(concord_art_animation_sequence_contract_leaf
    src/assets/animation/RuntimeArtAnimationSequence.cpp concord_radar_cell_values_value_contract Contract)
concord_add_profiled_leaf(concord_superweapon_animation_timing_contract_leaf
    src/simulation/superweapons/RuntimeSuperWeaponAnimationTiming.cpp concord_radar_cell_values_value_contract Contract)
target_link_libraries(concord_superweapon_animation_timing_contract_leaf PRIVATE
    concord_auxiliary_shp_bytes_contract_leaf concord_art_animation_sequence_contract_leaf)
add_subdirectory(tests/simulation/superweapons/animation_timing)
foreach(_animation_profile IN ITEMS runtime contract)
    foreach(_animation_case IN ITEMS auxiliary_bytes animation_metadata)
        add_test(NAME ${_animation_case}_${_animation_profile}_contract_test
            COMMAND concord_${_animation_case}_${_animation_profile}_contract)
        set_tests_properties(${_animation_case}_${_animation_profile}_contract_test
            PROPERTIES TIMEOUT 120 LABELS "superweapon-animation;standard-contract;no-assets")
    endforeach()
endforeach()
