// Isolated diagnostic World, prepared before the authority snapshot.
void prepareRuntimeMapGpuFixtureScene(RuntimeWindowState& state) {
    const auto require=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
        // Same original CANEWY06 + MTNK occlusion fixture as RuntimeBehindMarkerSmoke;
        // this isolated state is prepared before the immutable authority snapshot.
        state.world.accessObjects().clear();state.world.accessTerrainObjects().clear();state.world.accessTerrainCells().clear();state.world.accessSmudges().clear();
        for(int y=64;y<=96;++y)for(int x=64;x<=96;++x){TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.tileIndex=0;state.world.accessTerrainCells().push_back(cell);}
        state.mapFactsLoaded=false;state.economy.owner="Americans";state.triggers.bootstrapComplete=true;state.tick=12;
        WorldObjectInstance building;building.instanceId=1;building.kind=WorldObjectKind::Structure;building.type="CANEWY06";building.owner="Americans";
        const auto* rules=state.rules.findBuildingType(building.type);require(rules!=nullptr,"original CANEWY06 building rules");
        building.alive=true;building.health=rules->strength;building.x=building.y=80;building.cell=runtimeCellKey(80,80);building.mission="Guard";
        WorldObjectInstance unit;unit.instanceId=2;unit.kind=WorldObjectKind::Unit;unit.type="MTNK";unit.owner="Americans";unit.alive=true;unit.health=100;
        unit.x=79;unit.y=80;unit.cell=runtimeCellKey(79,80);unit.mission="Guard";state.world.accessObjects().push_back(building);state.world.accessObjects().push_back(unit);
        if(std::getenv("CONCORD_S41_LIGHT_SCENE")){const auto* lampRules=state.rules.findBuildingType("INGALITE");require(lampRules&&lampRules->lightIntensity!=0&&lampRules->lightVisibility>0,"original INGALITE light rules required");
            WorldObjectInstance lamp;lamp.instanceId=3;lamp.kind=WorldObjectKind::Structure;lamp.type="INGALITE";lamp.owner="Neutral";lamp.alive=true;lamp.health=std::max(1,lampRules->strength);lamp.poweredOperational=true;lamp.x=lamp.y=80;lamp.cell=runtimeCellKey(80,80);lamp.mission="Guard";state.world.accessObjects().push_back(lamp);}
        if(std::getenv("CONCORD_S41_VXL_SCENE")){auto front=unit;front.instanceId=4;front.x=82;front.y=83;front.cell=runtimeCellKey(82,83);front.facing=64;front.turretFacing=128;state.world.accessObjects().push_back(front);
            auto rotor=unit;rotor.instanceId=5;rotor.type="SHAD";rotor.x=84;rotor.y=84;rotor.cell=runtimeCellKey(84,84);rotor.airborne=true;rotor.flightLevel=1;rotor.facing=64;rotor.turretFacing=-1;const auto* rotorRules=state.rules.findUnitType("vehicle",rotor.type);require(rotorRules!=nullptr,"original SHAD VXL fixed scene rules");rotor.health=rotorRules->strength;state.world.accessObjects().push_back(rotor);}
        // Match original object-created/load setup for this replaced isolated World.
        state.cloakObservedHealthByObject.clear();
        for(auto& object:state.world.accessObjects()){initializeRuntimeObjectCloakState(state,object);state.cloakObservedHealthByObject[object.instanceId]=object.health;}
        state.cloakStateInitialized=true;state.selectedObjectIds={1};
        if(std::getenv("CONCORD_S41_VXL_SCENE"))state.selectedObjectIds={4};
        ensureRuntimeSpecialMechanismsInitialized(state);
        require(state.specialMechanismsInitialized,"original special mechanism bootstrap completed before immutable timing snapshot");
        // WM_PAINT consumes original particle/gate snapshots; establish their authority
        // clocks during isolated World preparation, before the first-paint hash.
        ensureRuntimeParticlesInitialized(state);ensureRuntimeGatesInitialized(state);ensureRuntimeSuperWeaponsInitialized(state);
        require(state.superWeaponsInitialized,"original snapshot producer super-weapon bootstrap before first-paint authority");
        require(state.particlesInitialized&&state.particles.currentTick()==state.tick&&state.gatesInitialized,
            "original particle/gate bootstrap completed before immutable timing snapshot");
        state.config.seeHiddenObjects=std::getenv("CONCORD_MAIN_FRAME_BEHIND_OFF")==nullptr;
        state.visionEnabled=true;updateRuntimeVisionState(state);
        (void)state.vision.revealArea(state.economy.owner,80,80,8);
        require(state.vision.hasObserver(state.economy.owner)&&state.vision.isCellVisibleTo(state.economy.owner,80,80)
            &&state.vision.isCellVisibleTo(state.economy.owner,79,80),"explicit original reconnaissance input makes the BEHIND fixture visible");
        RuntimeVisionGridView view;require(state.vision.gridViewFor(state.economy.owner,view),"fixture authority grid exists");
        const auto cell=std::find_if(state.world.getTerrainCells().begin(),state.world.getTerrainCells().end(),[](const TerrainCell& value){return value.x==80&&value.y==80;});
        require(cell!=state.world.getTerrainCells().end()&&RuntimeVisionSystem::visibilityForGridView(view,
            cell->cellIndex512%512,cell->cellIndex512/512)==RuntimeCellVisibility::Visible,"terrain pixel owner index agrees with original visible cell authority");
        centerRuntimeCameraOnCell(state,{0,0,800,600},{80,80});
}
