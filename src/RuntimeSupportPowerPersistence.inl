bool runtimeHasSupportFlightPose(const WorldObjectInstance& object) {
    return object.alive && object.airborne && (object.mission=="ParadropDrop"
        || object.mission=="SupportFlightEast" || object.mission=="SupportFlightWest");
}

void writeRuntimeSupportFlightPoses(const RuntimeWindowState& state, std::ostream& output) {
    const auto precision=output.precision(); output<<std::setprecision(17);
    const auto& objects=state.world.getObjects();
    output<<"\n[SupportFlightPoses]\nVersion=1\nCount="
        <<std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose)<<"\n";
    int index=0;
    for (const auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
        output<<index++<<"="<<object.instanceId<<"|"<<object.renderX<<"|"<<object.renderY
            <<"|"<<object.renderLevel<<"|"<<object.movementAnimationDistance<<"\n";
    }
    output.precision(precision);
}

bool restoreRuntimeSupportFlightPoses(const IniFile& ini,
    std::vector<WorldObjectInstance>& objects, std::string& error) {
    const auto reject=[&]() {error="invalid saved support aircraft/paratrooper pose";return false;};
    if (!ini.getSection("SupportFlightPoses")) {
        // Earlier saves retain integer world coordinates but no sub-cell
        // pose. Resume those flights from that last known position.
        for (auto& object:objects) if (runtimeHasSupportFlightPose(object)) {
            object.hasRenderPosition=object.renderMoveActive=true;
            object.renderX=object.x; object.renderY=object.y; object.renderLevel=object.level;
        }
        return true;
    }
    int version=0,count=-1;
    const auto expected=std::count_if(objects.begin(),objects.end(),runtimeHasSupportFlightPose);
    if (!parseRuntimeInt(ini.getValue("SupportFlightPoses","Version",""),version) || version!=1
        || !parseRuntimeInt(ini.getValue("SupportFlightPoses","Count",""),count)
        || count!=expected) return reject();
    std::unordered_set<int> restored;
    for (int i=0;i<count;++i) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("SupportFlightPoses",std::to_string(i),""));
        int id=-1; double x=0,y=0,z=0,distance=0;
        if (fields.size()!=5 || !parseRuntimeInt(fields[0],id) || !restored.insert(id).second
            || !parseRuntimeFiniteDouble(fields[1],x) || !parseRuntimeFiniteDouble(fields[2],y)
            || !parseRuntimeFiniteDouble(fields[3],z) || !parseRuntimeFiniteDouble(fields[4],distance)
            || distance<0.0 || distance>1e9) return reject();
        const auto object=std::find_if(objects.begin(),objects.end(),[&](const auto& candidate){return candidate.instanceId==id;});
        if (object==objects.end() || !runtimeHasSupportFlightPose(*object)
            || std::abs(x-object->x)>0.500001 || std::abs(y-object->y)>0.500001
            || std::abs(z-object->level)>0.500001 || object->flightLevel!=object->level) return reject();
        object->hasRenderPosition=object->renderMoveActive=true;
        object->renderX=x; object->renderY=y; object->renderLevel=z;
        object->movementAnimationDistance=distance;
    }
    return true;
}
