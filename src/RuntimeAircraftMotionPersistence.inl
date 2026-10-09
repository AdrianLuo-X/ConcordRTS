std::string serializeRuntimeActiveAircraftFlights(const RuntimeWindowState& state) {
    RuntimeAircraftFlights active;
    for(const auto& object:state.world.getObjects()) {
        if(!object.alive||!object.airborne||!runtimeFixedWingProfile(state,object))continue;
        const auto found=state.aircraftFlights.find(object.instanceId);
        if(found!=state.aircraftFlights.end()&&object.x==found->second.x/runtimeAircraftCellQ16
            &&object.y==found->second.y/runtimeAircraftCellQ16)active.emplace(found->first,found->second);
    }
    return serializeRuntimeAircraftFlights(active);
}

void writeRuntimeAircraftFlights(const RuntimeWindowState& state,std::ostream& output) {
    output<<"[AircraftFlights]\nSchema=1\nDataHex="
        <<iniHexEncode(serializeRuntimeActiveAircraftFlights(state))<<"\n\n";
}

bool readRuntimeAircraftFlights(RuntimeWindowState& state,const IniFile& ini,std::string& error) {
    RuntimeAircraftFlights staged;
    if(ini.getSection("AircraftFlights")) {
        std::string payload;
        if(ini.getValue("AircraftFlights","Schema","")!="1"
            ||!iniHexDecode(ini.getValue("AircraftFlights","DataHex",""),payload,16U*1024U*1024U)
            ||!deserializeRuntimeAircraftFlights(payload,staged)) {
            error="invalid continuous aircraft flight section";return false;
        }
        for(const auto& [id,f]:staged) {
            const auto* object=findObjectById(state,id);
            if(!object||!object->alive||!object->airborne||!runtimeFixedWingProfile(state,*object)
                ||f.lastTick>state.tick||object->x!=f.x/runtimeAircraftCellQ16
                ||object->y!=f.y/runtimeAircraftCellQ16) {
                error="continuous aircraft flight does not match saved world";return false;
            }
            if(f.z>=0&&f.dockFlightStartZ<0
                &&f.z!=std::int64_t(object->flightLevel)*runtimeAircraftCellQ16) {
                error="continuous aircraft cruise height does not match saved world";return false;
            }
            if(f.landingHostId>=0) {
                // A killed/captured host can be retained until the next
                // mission pass cancels its approach. Its concrete pad still
                // has to be the geometry named by this saved descent.
                const auto* host=findRuntimeObjectByIdIncludingInactive(state,f.landingHostId);
                const auto* building=host?findRuntimeBuildingTypeById(state.rules,host->type):nullptr;
                if(!host||host->kind!=WorldObjectKind::Structure||!building
                    ||f.landingDockIndex>=building->numberOfDocks) {
                    error="continuous aircraft landing host does not match saved world";return false;
                }
                const auto dock=runtimeAircraftDockSpawn(state,*host,f.landingDockIndex);
                if(f.dockFlightTargetZ!=std::int64_t(dock.level)*runtimeAircraftCellQ16
                    ||f.x!=std::llround(dock.renderX*runtimeAircraftCellQ16)
                    ||f.y!=std::llround(dock.renderY*runtimeAircraftCellQ16)
                    ||(host->alive&&runtimeAircraftDockAssignmentCompatible(state,*object,*host)
                        &&(object->dockedToObjectId!=host->instanceId||object->dockIndex!=f.landingDockIndex))) {
                    error="continuous aircraft landing pad does not match saved world";return false;
                }
            }
        }
    }
    state.aircraftFlights=std::move(staged);
    for(auto& object:state.world.accessObjects())applyRuntimeFixedWingPose(state,object);
    return true;
}
