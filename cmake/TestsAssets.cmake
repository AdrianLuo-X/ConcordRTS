if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/neutral.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/cache.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/langmd/ra2ts_l.bik"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/Binkw32.dll")
    add_test(
        NAME runtime_main_menu_ui_smoke_test
        COMMAND concord_runtime
            --main-menu-ui-smoke-test
            "${CMAKE_BINARY_DIR}/runtime_main_menu_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/mpyscrnl.shp")
    add_test(
        NAME runtime_skirmish_ui_smoke_test
        COMMAND concord_runtime
            --skirmish-ui-smoke-test
            "${CMAKE_BINARY_DIR}/runtime_skirmish_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )

    add_test(
        NAME runtime_loading_prewarm_smoke_test
        COMMAND concord_runtime
            --loading-smoke-test
            --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    add_test(
        NAME runtime_loading_first_frame_positions_smoke_test
        COMMAND concord_runtime --loading-smoke-test --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(runtime_loading_first_frame_positions_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_LOADING_PRESENTATION_ONLY=1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
    add_test(
        NAME runtime_loading_frame_stability_smoke_test
        COMMAND concord_runtime --loading-smoke-test --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(runtime_loading_frame_stability_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_LOADING_FRAME_STABILITY_ONLY=1;CONCORD_LOADING_FRAME_STABILITY_OUTPUT=${CMAKE_BINARY_DIR}/issue21_loading_frame_stability"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        TIMEOUT 300 RUN_SERIAL TRUE
    )
endif()

add_test(
    NAME performance_plan
    COMMAND concord_rules_dump
        --performance-plan
        --workers 4
)

function(add_runtime_theater_texture_smoke_test test_name map_path)
    if (WIN32 AND EXISTS "${map_path}")
        add_test(
            NAME ${test_name}
            COMMAND concord_runtime
                --render-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${map_path}"
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES ENVIRONMENT
                "CONCORD_RENDER_SMOKE_TERRAIN_COVERAGE=yes"
        )
    endif()
endfunction()

add_runtime_theater_texture_smoke_test(
    runtime_theater_temperate_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_snow_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xxmas.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_urban_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/hillbtwn.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_newurban_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_desert_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
)
add_runtime_theater_texture_smoke_test(
    runtime_theater_lunar_texture_smoke_test
    "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/sov06lmd.map"
)

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xmp06mw.map")
    add_test(
        NAME runtime_smudge_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xmp06mw.map"
    )
    set_tests_properties(
        runtime_smudge_render_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_SMUDGES=yes;CONCORD_RENDER_SMOKE_TERRAIN_COVERAGE=yes;CONCORD_RENDER_SMOKE_CELL=33,51;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_smudge_render.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
    add_test(
        NAME runtime_smoke_test
        COMMAND concord_runtime
            --smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_skirmish_build_dryheat_smoke_test
        COMMAND concord_runtime
            --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_computer_ai_smoke_test
        COMMAND concord_runtime
            --computer-ai-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )

    add_test(
        NAME runtime_unholy_alliance_tech_tree_smoke_test
        COMMAND concord_runtime
            --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_unholy_alliance_tech_tree_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_SKIRMISH_UNHOLY_START_ONLY=1"
    )

    add_test(NAME runtime_skirmish_modes_technology_smoke_test
        COMMAND concord_runtime --skirmish-build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_skirmish_modes_technology_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_SKIRMISH_MODES_ONLY=1"
        TIMEOUT 300
    )

    add_test(NAME runtime_vehicle_arrival_integration_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_vehicle_arrival_integration_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_ARRIVAL_ONLY=1"
    )

    add_test(NAME runtime_building_ready_sidebar_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
    )
    set_tests_properties(runtime_building_ready_sidebar_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_BUILDING_READY_ONLY=1"
        TIMEOUT 180
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    add_test(
        NAME runtime_campaign_database_smoke_test
        COMMAND concord_runtime
            --campaign-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

add_test(
    NAME runtime_campaign_unsupported_coverage_must_fail
    COMMAND concord_runtime
        --campaign-coverage-map-test
        "${CMAKE_SOURCE_DIR}/tests/fixtures/campaign_unsupported_coverage.map"
)
set_tests_properties(
    runtime_campaign_unsupported_coverage_must_fail
    PROPERTIES WILL_FAIL TRUE
)
foreach(CAMPAIGN_COVERAGE_KIND IN ITEMS event action script)
    add_test(
        NAME runtime_campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}_must_fail
        COMMAND concord_runtime
            --campaign-coverage-map-test
            "${CMAKE_SOURCE_DIR}/tests/fixtures/campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}.map"
    )
    set_tests_properties(
        runtime_campaign_unsupported_${CAMPAIGN_COVERAGE_KIND}_must_fail
        PROPERTIES WILL_FAIL TRUE
    )
endforeach()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2/local.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/language/ra2.csf"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/Maps01.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/Maps02.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies01.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/Game/movies02.mix"
    AND EXISTS "${CMAKE_SOURCE_DIR}/RedAlert2/GameMD/movmd03.mix")
    add_test(
        NAME runtime_campaign_runtime_smoke_test
        COMMAND concord_runtime
            --campaign-runtime-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )

    add_test(
        NAME runtime_campaign_ui_smoke_test
        COMMAND concord_runtime
            --campaign-ui-smoke-test
            "${CMAKE_BINARY_DIR}/campaign_ui_smoke.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )

    add_test(
        NAME runtime_campaign_ra2_yr_catalog_acceptance_test
        COMMAND concord_runtime
            --campaign-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
    set_tests_properties(
        runtime_campaign_ra2_yr_catalog_acceptance_test
        PROPERTIES PASS_REGULAR_EXPRESSION
            "campaigns=5 missions=40 available=40"
    )
endif()

if (WIN32
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd/obs_alli.pcx"
    AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/expandmd01/mpyscrnlbar10.pcx")
    add_test(
        NAME runtime_internet_menu_skin_smoke_test
        COMMAND concord_runtime
            --internet-menu-skin-smoke-test
            "${CMAKE_BINARY_DIR}/internet_menu_skin_allied.bmp"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
    add_test(
        NAME runtime_resource_field_2peaks_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(
        runtime_resource_field_2peaks_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_RESOURCE_FIELD=yes;CONCORD_RENDER_SMOKE_CELL=75,64;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_resource_field_2peaks.bmp"
    )

    add_test(
        NAME runtime_bridge_repair_2peaks_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map"
    )
    set_tests_properties(
        runtime_bridge_repair_2peaks_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_BRIDGE_REPAIR=yes;CONCORD_COMMAND_SMOKE_BRIDGE_SAVE=${CMAKE_BINARY_DIR}/runtime_bridge_repair_2peaks_save.ini;CONCORD_COMMAND_SMOKE_BRIDGE_BMP=${CMAKE_BINARY_DIR}/runtime_bridge_repair_2peaks.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xshrapnel.map")
    add_test(
        NAME runtime_low_bridge_repair_xshrapnel_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/xshrapnel.map"
    )
    set_tests_properties(
        runtime_low_bridge_repair_xshrapnel_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_LOW_BRIDGE_REPAIR=yes"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/bridgegap.map")
    add_test(
        NAME runtime_high_bridge_bridgegap_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/bridgegap.map"
    )
    set_tests_properties(
        runtime_high_bridge_bridgegap_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_BRIDGE_VISUAL=yes;CONCORD_RENDER_SMOKE_BRIDGE_KIND=high;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_high_bridge_bridgegap.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all04dmd.map")
    add_test(
        NAME runtime_low_bridge_all04dmd_visual_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all04dmd.map"
    )
    set_tests_properties(
        runtime_low_bridge_all04dmd_visual_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_BRIDGE_VISUAL=yes;CONCORD_RENDER_SMOKE_BRIDGE_KIND=low;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_low_bridge_all04dmd.bmp"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
    add_test(
        NAME runtime_world_all01umd_smoke_test
        COMMAND concord_runtime
            --smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_render_all01umd_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_special_mechanism_golden_smoke_test
        COMMAND concord_runtime
            --mechanism-golden-smoke-test
                "${CMAKE_BINARY_DIR}/special_mechanism_golden"
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_defense_turret_directional_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_defense_turret_directional_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GTGCAN@0,GTGCAN@32,GTGCAN@64,GTGCAN@96,GTGCAN@128,GTGCAN@160,GTGCAN@192,GTGCAN@224,NALASR@0,NALASR@32,NALASR@64,NALASR@96,NALASR@128,NALASR@160,NALASR@192,NALASR@224;CONCORD_RENDER_SMOKE_COLUMNS=4;CONCORD_RENDER_SMOKE_SPACING_X=3;CONCORD_RENDER_SMOKE_SPACING_Y=3;CONCORD_RENDER_SMOKE_EXPECT_STRUCTURE_TURRETS=16;CONCORD_RENDER_SMOKE_EXPECT_STRUCTURE_TURRET_CUBOIDS=16;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_POINTS=0;CONCORD_RENDER_SMOKE_MIN_STRUCTURE_TURRET_WIDTH=64;CONCORD_RENDER_SMOKE_MIN_STRUCTURE_TURRET_HEIGHT=78;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_WIDTH=68;CONCORD_RENDER_SMOKE_MAX_STRUCTURE_TURRET_HEIGHT=82;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_defense_turret_directions.bmp"
    )

    add_test(
        NAME runtime_building_owner_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_building_owner_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GAGAP@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_BUILDING_REMAP_RULES_INI=${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini;CONCORD_BUILDING_REMAP_AUDIT_CSV=${CMAKE_BINARY_DIR}/runtime_building_owner_remap.csv;CONCORD_BUILDING_REMAP_PREVIEW_BMP=${CMAKE_BINARY_DIR}/runtime_gap_generator_states.bmp"
    )

    add_test(
        NAME runtime_tesla_yellow_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_tesla_yellow_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=TESLA@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Yellow;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_tesla_yellow_remap.bmp"
    )

    add_test(
        NAME runtime_battle_bunker_yellow_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_battle_bunker_yellow_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=NABNKR@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_EXPECT_REMAP_PIXELS=793;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_battle_bunker_yellow_remap.bmp"
    )

    add_test(
        NAME runtime_kirov_gold_remap_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_kirov_gold_remap_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=ZEP@64;CONCORD_RENDER_SMOKE_OWNER=RenderSmokeProbe;CONCORD_RENDER_SMOKE_REMAP=Gold;CONCORD_RENDER_SMOKE_COMPARE_REMAP=DarkRed;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_kirov_gold_remap.bmp"
    )

    add_test(
        NAME runtime_aircraft_building_clearance_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_aircraft_building_clearance_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=CACHIG05@64,ZEP@64;CONCORD_RENDER_SMOKE_COLUMNS=1;CONCORD_RENDER_SMOKE_SPACING_X=0;CONCORD_RENDER_SMOKE_SPACING_Y=0;CONCORD_RENDER_SMOKE_AIRBORNE=yes;CONCORD_RENDER_SMOKE_FLIGHT_LEVEL_OFFSET=4;CONCORD_RENDER_SMOKE_EXPECT_AIRBORNE_FOREGROUND=1;CONCORD_RENDER_SMOKE_EXPECT_BUILDING_CLEARANCE=1;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_aircraft_building_clearance.bmp"
    )

    add_test(
        NAME runtime_docked_aircraft_foreground_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_docked_aircraft_foreground_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_OBJECTS=GAAIRC@64,ORCA@64,ORCA@64,GALITE@64,ORCA@64,ORCA@64;CONCORD_RENDER_SMOKE_COLUMNS=3;CONCORD_RENDER_SMOKE_SPACING_X=1;CONCORD_RENDER_SMOKE_SPACING_Y=1;CONCORD_RENDER_SMOKE_DOCKED_TO_STRUCTURE=yes;CONCORD_RENDER_SMOKE_EXPECT_DOCKED_AIRCRAFT_FOREGROUND=4;CONCORD_RENDER_SMOKE_EXPECT_VOXELS=4;CONCORD_RENDER_SMOKE_BMP=${CMAKE_BINARY_DIR}/runtime_docked_aircraft_foreground.bmp"
    )

    add_test(
        NAME runtime_interaction_all01umd_smoke_test
        COMMAND concord_runtime
            --interaction-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_fullscreen_render_smoke_test
        COMMAND concord_runtime
            --interaction-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_fullscreen_render_smoke_test
        PROPERTIES
            ENVIRONMENT "CONCORD_FULLSCREEN_RENDER_BENCHMARK=1;CONCORD_RENDER_DETAIL_STATS=0"
            TIMEOUT 300
    )

    add_test(
        NAME runtime_live_frame_all01umd_smoke_test
        COMMAND concord_runtime
            --config "${CMAKE_SOURCE_DIR}/tests/fixtures/runtime_live_performance.ini"
            --skip-main-menu
            --skip-skirmish-menu
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_live_frame_all01umd_smoke_test
        PROPERTIES
            ENVIRONMENT "CONCORD_LIVE_BENCHMARK=1;CONCORD_LIVE_BENCHMARK_TICKS=600;CONCORD_RENDER_DETAIL_STATS=0"
            TIMEOUT 300
    )

    add_test(
        NAME runtime_result_presentation_smoke_test
        COMMAND concord_runtime
            --result-presentation-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_command_all01umd_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    add_test(
        NAME runtime_factory_exit_clearance_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_factory_exit_clearance_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FACTORY_EXIT_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_air_navigation_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_air_navigation_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_NAVIGATION_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_harvester_stability_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    add_test(
        NAME runtime_desolator_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_desolator_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_DESOLATOR_ONLY=1;CONCORD_DESOLATOR_BMP=${CMAKE_BINARY_DIR}/desolator_radiation.bmp"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_vehicle_damage_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_vehicle_damage_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_DAMAGE_ONLY=1;CONCORD_VEHICLE_DAMAGE_SMOKE_BMP=${CMAKE_BINARY_DIR}/vehicle_damage_smoke.bmp"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_squid_grapple_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_squid_grapple_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SQUID_GRAPPLE_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_mastermind_overload_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mastermind_overload_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MASTERMIND_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_floating_disk_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_floating_disk_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FLOATING_DISK_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_disk_drain_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_disk_drain_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_DISK_DRAIN_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_guard_rotation_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_guard_rotation_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_GUARD_ROTATION_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_ai_building_targets_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_ai_building_targets_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AI_BUILDING_TARGETS_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_type_immune_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_type_immune_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TYPE_IMMUNE_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_terrain_movement_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_terrain_movement_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TERRAIN_MOVEMENT_ONLY=1"
        TIMEOUT 300
    )
    add_test(
        NAME runtime_move_to_shroud_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_move_to_shroud_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MOVE_TO_SHROUD_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_fire_in_transport_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_fire_in_transport_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_FIRE_IN_TRANSPORT_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_air_range_bonus_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_air_range_bonus_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AIR_RANGE_BONUS_ONLY=1"
        TIMEOUT 180
    )
    add_test(
        NAME runtime_slave_miner_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_slave_miner_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SLAVE_MINER_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_slave_miner_lifecycle_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_slave_miner_lifecycle_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SLAVE_MINER_LIFECYCLE_ONLY=1"
        TIMEOUT 600
    )
    add_test(
        NAME runtime_mcv_survival_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mcv_survival_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MCV_SURVIVAL_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_mcv_repack_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_mcv_repack_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MCV_REPACK_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_virus_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_virus_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VIRUS_ONLY=1"
        TIMEOUT 120
    )
    add_test(
        NAME runtime_virus_friendly_gas_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_virus_friendly_gas_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VIRUS_FRIENDLY_GAS_ONLY=1"
        TIMEOUT 240
    )
    add_test(
        NAME runtime_cloning_batch_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_cloning_batch_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_CLONING_BATCH_ONLY=1"
        TIMEOUT 600
    )
    set_tests_properties(
        runtime_harvester_stability_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_HARVESTER_STABILITY_ONLY=1"
    )
    add_test(
        NAME runtime_neutral_garrison_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_neutral_garrison_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_NEUTRAL_GARRISON_ONLY=1;CONCORD_GARRISON_PIPS_BMP=${CMAKE_BINARY_DIR}/garrison_capacity_pips.bmp"
    )
    add_test(
        NAME runtime_garrison_render_smoke_test
        COMMAND concord_runtime
            --render-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_garrison_render_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_GARRISON_RENDER_TEST=1;CONCORD_GARRISON_RENDER_PREVIEW_BMP=${CMAKE_BINARY_DIR}/runtime_garrison_states.bmp"
    )
    add_test(
        NAME runtime_amphibious_transport_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_amphibious_transport_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_AMPHIBIOUS_TRANSPORT_ONLY=1"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_tank_bunker_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_tank_bunker_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_TANK_BUNKER_ONLY=1;CONCORD_TANK_BUNKER_OUTPUT=${CMAKE_BINARY_DIR}/tank_bunker"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_bioreactor_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_bioreactor_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_BIOREACTOR_ONLY=1;CONCORD_BIOREACTOR_OUTPUT=${CMAKE_BINARY_DIR}/bioreactor"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_destroyer_aircraft_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_destroyer_aircraft_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_DESTROYER_ONLY=1"
    )
    foreach(silo_theater IN ITEMS temperate urban)
        if(silo_theater STREQUAL "temperate")
            set(silo_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
        else()
            set(silo_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        endif()
        add_test(NAME runtime_nuclear_silo_${silo_theater}_smoke_test
            COMMAND concord_runtime --command-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${silo_map}")
        set_tests_properties(runtime_nuclear_silo_${silo_theater}_smoke_test PROPERTIES
            ENVIRONMENT "CONCORD_COMMAND_SMOKE_NUCLEAR_SILO_ONLY=1;CONCORD_NUCLEAR_SILO_OUTPUT=${CMAKE_BINARY_DIR}/nuclear_silo/${silo_theater}"
            RUN_SERIAL TRUE)
    endforeach()
    foreach(crate_theater IN ITEMS temperate urban)
        if(crate_theater STREQUAL "temperate")
            set(crate_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/2peaks.map")
        else()
            set(crate_map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        endif()
        add_test(NAME runtime_crates_${crate_theater}_smoke_test
            COMMAND concord_runtime --command-smoke-test
                --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
                --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
                --map "${crate_map}")
        set_tests_properties(runtime_crates_${crate_theater}_smoke_test PROPERTIES
            ENVIRONMENT "CONCORD_COMMAND_SMOKE_CRATES_ONLY=1;CONCORD_CRATE_OUTPUT=${CMAKE_BINARY_DIR}/crates/${crate_theater}"
            RUN_SERIAL TRUE)
    endforeach()
    add_test(
        NAME runtime_vehicle_body_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_vehicle_body_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_VEHICLE_BODY_ONLY=1;CONCORD_VEHICLE_BODY_OUTPUT=${CMAKE_BINARY_DIR}/vehicle_dynamics"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_support_powers_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_support_powers_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_SUPPORT_POWERS_ONLY=1;CONCORD_SUPPORT_POWERS_OUTPUT=${CMAKE_BINARY_DIR}/support_powers"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_missile_volley_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/maps/concord_flat_lake.map"
    )
    set_tests_properties(runtime_missile_volley_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILE_VOLLEY_ONLY=1;CONCORD_MISSILE_VOLLEY_OUTPUT=${CMAKE_BINARY_DIR}/missile_volley"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_missile_flight_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_missile_flight_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILE_FLIGHT_ONLY=1;CONCORD_MISSILE_FLIGHT_OUTPUT=${CMAKE_BINARY_DIR}/missile_flight"
        RUN_SERIAL TRUE
    )
    add_test(
        NAME runtime_spawned_missile_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_spawned_missile_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_MISSILES_ONLY=1"
    )
    add_test(
        NAME runtime_projectile_muzzle_smoke_test
        COMMAND concord_runtime --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(runtime_projectile_muzzle_smoke_test PROPERTIES
        ENVIRONMENT "CONCORD_COMMAND_SMOKE_MUZZLE_ONLY=1"
    )
    add_test(
        NAME runtime_electric_and_bomb_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_electric_and_bomb_smoke_test
        PROPERTIES ENVIRONMENT "CONCORD_COMMAND_SMOKE_ELECTRIC_ATTACK_ONLY=1"
    )
    set_tests_properties(
        runtime_render_all01umd_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_RENDER_SMOKE_EXPECT_DAMAGE_FIRE_DRAWN=1;CONCORD_RENDER_SMOKE_MAX_DAMAGE_FIRE_MISSING=0"
    )

    add_test(
        NAME runtime_ifv_multifunction_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_ifv_multifunction_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_IFV_ONLY=yes;CONCORD_COMMAND_SMOKE_IFV_SAVE=${CMAKE_BINARY_DIR}/runtime_ifv_multifunction_save.ini;CONCORD_RENDER_USE_JAVA_VEHICLE_PNG=no"
    )

    add_test(
        NAME runtime_aircraft_return_to_dock_smoke_test
        COMMAND concord_runtime
            --command-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
    set_tests_properties(
        runtime_aircraft_return_to_dock_smoke_test
        PROPERTIES ENVIRONMENT
            "CONCORD_COMMAND_SMOKE_AIRCRAFT_RETURN_ONLY=yes"
    )

    add_test(
        NAME runtime_build_all01umd_smoke_test
        COMMAND concord_runtime
            --build-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )

    add_test(
        NAME runtime_trigger_all01umd_smoke_test
        COMMAND concord_runtime
            --trigger-smoke-test
            --resource-root "${CMAKE_SOURCE_DIR}/RedAlert2"
            --local-data-root "${CMAKE_SOURCE_DIR}/local_data"
            --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd.mix")
    add_test(
        NAME encrypted_mix_localmd_extract_ltnk
        COMMAND concord_rules_dump
            --mix "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/ra2md/localmd.mix"
            --extract-mix-file ltnk.vxl
            --output "${CMAKE_BINARY_DIR}/encrypted_mix_ltnk_probe.vxl"
    )
endif()

if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini")
    add_test(
        NAME combat_sandbox_mtnk_htnk
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 600
            --distance 8
    )

    add_test(
        NAME combat_sandbox_e1_e2
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker E1
            --defender E2
            --ticks 300
            --distance 8
    )

    add_test(
        NAME combat_sandbox_sref_htnk
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --combat-sandbox
            --attacker SREF
            --defender HTNK
            --ticks 600
            --distance 8
    )

    add_test(
        NAME economy_sandbox_allied
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --economy-sandbox
            --house Americans
            --mcv AMCV
            --credits 10000
            --ticks 600
    )

    add_test(
        NAME ai_trigger_sandbox_synthetic
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --ai-trigger-sandbox
            --ai-house Russians
            --enemy-house Americans
            --mcv SMCV
            --credits 10000
            --ticks 900
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        add_test(
            NAME ai_trigger_sandbox_all01umd
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
                --ai-trigger-sandbox
                --ai-house Russians
                --enemy-house Americans
                --mcv SMCV
                --credits 10000
                --ticks 900
        )
    endif()

    add_test(
        NAME mod_compat_report_fixture
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat_rules.ini"
            --art "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat_art.ini"
            --map "${CMAKE_SOURCE_DIR}/tests/fixtures/mod_compat.map"
            --mod-compat-report
    )

    add_test(
        NAME mod_compat_build_limit_vanilla
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/tests/fixtures/build_limit_rules.ini"
            --mod-compat-report
    )
    set_tests_properties(
        mod_compat_build_limit_vanilla
        PROPERTIES
            PASS_REGULAR_EXPRESSION "Ares hits: 0"
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/artmd.ini" AND EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
        add_test(
            NAME engine_status_report
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --art "${CMAKE_SOURCE_DIR}/local_data/artmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
                --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
                --engine-status
        )
    endif()

    add_test(
        NAME tech_tree_report_allied
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --tech-tree-report
            --house Americans
    )

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map")
        add_test(
            NAME victory_sandbox_all01umd
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/mapsmd03/all01umd.map"
                --victory-sandbox
                --house Americans
        )
    endif()

    if (EXISTS "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map")
        add_test(
            NAME pathfinding_sandbox_dryheat
            COMMAND concord_rules_dump
                --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
                --map "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports/multimd/dryheat.map"
                --pathfinding-sandbox
        )
    endif()

    add_test(
        NAME audio_catalog_report
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
            --audio-report
    )

    add_test(
        NAME audio_decode_smoke
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --asset-root "${CMAKE_SOURCE_DIR}/local_data/resources/mix_exports"
            --output "${CMAKE_BINARY_DIR}/audio_decode_smoke_cache"
            --audio-decode-smoke
    )

    add_test(
        NAME replay_sandbox_lockstep
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --replay-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 240
            --distance 8
            --seed 12345
    )

    add_test(
        NAME replay_sandbox_desync_detection
        COMMAND concord_rules_dump
            --rules "${CMAKE_SOURCE_DIR}/local_data/rulesmd.ini"
            --replay-sandbox
            --attacker MTNK
            --defender HTNK
            --ticks 120
            --distance 8
            --seed 12345
            --desync-at 40
    )
endif()

