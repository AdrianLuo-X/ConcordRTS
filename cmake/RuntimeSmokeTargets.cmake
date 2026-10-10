add_executable(concord_missile_acceleration_smoke tests/gameplay/RuntimeMissileAccelerationSmoke.cpp)
target_include_directories(concord_missile_acceleration_smoke PRIVATE src)
target_compile_features(concord_missile_acceleration_smoke PRIVATE cxx_std_20)
add_executable(concord_vehicle_startup_motion_smoke tests/gameplay/RuntimeVehicleStartupMotionSmoke.cpp)
target_include_directories(concord_vehicle_startup_motion_smoke PRIVATE src)
target_compile_features(concord_vehicle_startup_motion_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_missile_acceleration_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_vehicle_startup_motion_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_aircraft_flight_codec_smoke tests/gameplay/RuntimeAircraftFlightCodecSmoke.cpp)
target_include_directories(concord_aircraft_flight_codec_smoke PRIVATE src)
target_compile_features(concord_aircraft_flight_codec_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_aircraft_flight_codec_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()
add_executable(concord_subcell_requested_index_smoke tests/gameplay/RuntimeSubcellRequestedIndexSmoke.cpp
    src/simulation/navigation/occupancy/RuntimeSubcellSystem.cpp)
target_include_directories(concord_subcell_requested_index_smoke PRIVATE src)
target_compile_features(concord_subcell_requested_index_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_subcell_requested_index_smoke PRIVATE /utf-8 /W4 /O2 /Ob2 /FS)
endif()
add_executable(concord_mod_history_noop_smoke tests/infrastructure/RuntimeModExtensionsHistoryNoopSmoke.cpp
    src/world/model/GameWorld.cpp src/core/io/IniFile.cpp src/data/maps/MapDatabase.cpp src/data/rules/RulesDatabase.cpp src/data/art/TheaterDatabase.cpp)
target_include_directories(concord_mod_history_noop_smoke PRIVATE src)
target_compile_features(concord_mod_history_noop_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_mod_history_noop_smoke PRIVATE /utf-8 /W4 /O2 /Ob2 /FS)
endif()

add_executable(concord_history_owner_census_smoke tests/infrastructure/RuntimeHistoryOwnerCensusSmoke.cpp)
target_include_directories(concord_history_owner_census_smoke PRIVATE src)
target_compile_features(concord_history_owner_census_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_history_owner_census_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_presentation_work_budget_smoke tests/rendering/RuntimePresentationWorkBudgetSmoke.cpp)
target_include_directories(concord_presentation_work_budget_smoke PRIVATE src)
target_compile_features(concord_presentation_work_budget_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_presentation_work_budget_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_presentation_budget_remainder_smoke tests/rendering/RuntimePresentationBudgetRemainderSmoke.cpp)
target_include_directories(concord_presentation_budget_remainder_smoke PRIVATE src)
target_compile_features(concord_presentation_budget_remainder_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_presentation_budget_remainder_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_shadow_opaque_span_smoke tests/rendering/RuntimeShadowOpaqueSpanSmoke.cpp)
target_include_directories(concord_shadow_opaque_span_smoke PRIVATE src)
target_compile_features(concord_shadow_opaque_span_smoke PRIVATE cxx_std_20)
add_executable(concord_water_wake_history_smoke tests/rendering/RuntimeWaterWakeHistoryMaintenanceSmoke.cpp
    src/world/model/GameWorld.cpp src/core/io/IniFile.cpp src/data/maps/MapDatabase.cpp src/data/rules/RulesDatabase.cpp src/data/art/TheaterDatabase.cpp)
target_include_directories(concord_water_wake_history_smoke PRIVATE src)
target_compile_features(concord_water_wake_history_smoke PRIVATE cxx_std_20)
add_executable(concord_simulation_query_reuse_smoke tests/infrastructure/RuntimeSimulationQueryReuseSmoke.cpp
    src/world/model/GameWorld.cpp src/core/io/IniFile.cpp src/data/maps/MapDatabase.cpp src/data/rules/RulesDatabase.cpp src/data/art/TheaterDatabase.cpp)
target_include_directories(concord_simulation_query_reuse_smoke PRIVATE src)
target_compile_features(concord_simulation_query_reuse_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_shadow_opaque_span_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_water_wake_history_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
    target_compile_options(concord_simulation_query_reuse_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_frame_diagnostics_smoke tests/infrastructure/RuntimeFrameDiagnosticsSmoke.cpp)
target_include_directories(concord_frame_diagnostics_smoke PRIVATE src)
target_compile_features(concord_frame_diagnostics_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_frame_diagnostics_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_game_speed_timing_smoke tests/gameplay/RuntimeGameSpeedTimingSmoke.cpp)
target_include_directories(concord_game_speed_timing_smoke PRIVATE src)
target_compile_features(concord_game_speed_timing_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_game_speed_timing_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_building_depth_smoke tests/rendering/RuntimeBuildingDepthSmoke.cpp)
target_include_directories(concord_building_depth_smoke PRIVATE src)
target_compile_features(concord_building_depth_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_building_depth_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_naval_scene_depth_smoke tests/rendering/RuntimeNavalSceneDepthSmoke.cpp)
target_include_directories(concord_naval_scene_depth_smoke PRIVATE src)
target_compile_features(concord_naval_scene_depth_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_naval_scene_depth_smoke PRIVATE /utf-8 /W4 /O2 /Ob2)
endif()

add_executable(concord_lighting_lookup_raster_smoke
    tests/rendering/RuntimeLightingLookupRasterSmoke.cpp src/render/lighting/RuntimeLightingSystem.cpp src/core/io/IniFile.cpp)
target_include_directories(concord_lighting_lookup_raster_smoke PRIVATE src)
target_compile_features(concord_lighting_lookup_raster_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(concord_lighting_lookup_raster_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(concord_lighting_lookup_raster_smoke PRIVATE /utf-8 /FS /O2 /Ob2)
endif()

if(MSVC)
    add_executable(concord_body_tint_context_smoke tests/rendering/RuntimeBodyTintContextSmoke.cpp)
    target_include_directories(concord_body_tint_context_smoke PRIVATE src)
    target_compile_features(concord_body_tint_context_smoke PRIVATE cxx_std_20)
    target_compile_options(concord_body_tint_context_smoke PRIVATE /utf-8 /FS)
endif()

add_executable(concord_movement_classification_smoke tests/gameplay/RuntimeMovementClassificationSmoke.cpp)
target_include_directories(concord_movement_classification_smoke PRIVATE src)
target_compile_features(concord_movement_classification_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(concord_movement_classification_smoke PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(concord_movement_classification_smoke PRIVATE /utf-8 /W4 /WX /O2 /Ob2)
endif()

add_executable(concord_voxel_debris_smoke tests/rendering/RuntimeVoxelDebrisSmoke.cpp)
target_include_directories(concord_voxel_debris_smoke PRIVATE src)
target_compile_features(concord_voxel_debris_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_voxel_debris_smoke PRIVATE /utf-8 /W4 /WX /O2 /Ob2)
endif()

add_executable(concord_internet_ranked_match_smoke
    tests/internet/RuntimeInternetRankedMatchSmoke.cpp src/network/internet/RuntimeInternetRankedMatch.cpp)
target_include_directories(concord_internet_ranked_match_smoke PRIVATE src)
target_compile_features(concord_internet_ranked_match_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_internet_ranked_match_smoke PRIVATE /utf-8 /W4 /WX /O2)
endif()

add_executable(concord_random_map_smoke tests/maps/RuntimeRandomMapSmoke.cpp
    src/data/maps/RuntimeRandomMap.cpp src/core/io/RuntimeAtomicFile.cpp src/runtime/campaign/RuntimeCampaignDataSource.cpp
    src/core/io/IniFile.cpp src/data/maps/MapDatabase.cpp src/data/art/TheaterDatabase.cpp src/data/rules/RulesDatabase.cpp
    src/simulation/production/RuntimeFactoryDeliveryState.cpp src/assets/archive/MixArchive.cpp src/assets/archive/WestwoodCrypto.cpp)
target_include_directories(concord_random_map_smoke PRIVATE src)
target_compile_features(concord_random_map_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_random_map_smoke PRIVATE /utf-8 /FS /O2)
endif()

add_executable(concord_vehicle_arrival_motion_smoke tests/gameplay/RuntimeVehicleArrivalMotionSmoke.cpp)
target_include_directories(concord_vehicle_arrival_motion_smoke PRIVATE src)
target_compile_features(concord_vehicle_arrival_motion_smoke PRIVATE cxx_std_20)

add_executable(concord_building_economy_smoke
    tests/gameplay/RuntimeBuildingEconomySmoke.cpp src/data/rules/RulesDatabase.cpp
    src/core/io/IniFile.cpp src/simulation/production/RuntimeFactoryDeliveryState.cpp)
target_include_directories(concord_building_economy_smoke PRIVATE src)
target_compile_features(concord_building_economy_smoke PRIVATE cxx_std_20)

add_executable(concord_superweapon_smoke tests/gameplay/SuperWeaponSmoke.cpp
    src/simulation/superweapons/SuperWeaponSystem.cpp src/data/rules/RulesDatabase.cpp src/core/io/IniFile.cpp
    src/simulation/production/RuntimeFactoryDeliveryState.cpp)
target_include_directories(concord_superweapon_smoke PRIVATE src)
target_compile_features(concord_superweapon_smoke PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(concord_superweapon_smoke PRIVATE /utf-8 /FS)
endif()

if (MSVC)
    # /RTC1 cannot be combined with the optimized playable Debug runtime.
    # Add it back only to the non-optimized command-line tool below.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(concord_multiplayer_resume_save_smoke
    tests/multiplayer/RuntimeMultiplayerResumeSaveSmoke.cpp src/network/snapshots/RuntimeMultiplayerResumeSave.cpp
    src/network/protocol/RuntimeMultiplayerMatchSetup.cpp src/network/snapshots/RuntimeMultiplayerSnapshotTransfer.cpp src/core/io/IniFile.cpp)
target_include_directories(concord_multiplayer_resume_save_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_multiplayer_resume_save_smoke PRIVATE /utf-8 /FS /O2)
endif()

add_executable(concord_combat_details_smoke tests/gameplay/RuntimeCombatDetailsSmoke.cpp src/core/io/IniFile.cpp)
target_include_directories(concord_combat_details_smoke PRIVATE src)
add_executable(concord_settings_hotkey_smoke tests/infrastructure/RuntimeSettingsHotkeySmoke.cpp
    src/ui/settings/RuntimeSettings.cpp src/core/io/RuntimeAtomicFile.cpp)
target_include_directories(concord_settings_hotkey_smoke PRIVATE src)
if(MSVC)
    target_compile_options(concord_settings_hotkey_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_combat_details_smoke PRIVATE /utf-8 /FS)
    target_compile_options(concord_building_economy_smoke PRIVATE /utf-8 /FS)
endif()

