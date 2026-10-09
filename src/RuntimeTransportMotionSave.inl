// Optional accepted transport presentation. Existing intent/cargo records retain all permissions.
void writeRuntimeTransportTransferPresentation(const RuntimeWindowState& state, std::ostream& output,
    bool multiplayer = false) {
    std::vector<int> ids;
    for (const auto& [id, move] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (move.transportTransferHostId >= 0 && move.transportTransferKind != RuntimeTransportTransferKind::None
            && (move.kind == RuntimeBehaviorKind::Move || move.kind == RuntimeBehaviorKind::Attack)
            && object && object->alive && !object->airborne && object->renderMoveActive
            && move.renderMoveTicksRemaining > 0 && move.renderMoveTicksRemaining == move.moveCooldownTicks)
            ids.push_back(id);
    }
    if (ids.empty()) return;
    std::sort(ids.begin(), ids.end());
    output << "\n[" << (multiplayer ? "MultiplayerTransportTransferPresentation" : "TransportTransferPresentation")
        << "]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t n = 0; n < ids.size(); ++n) {
        const auto& move=state.behaviors.at(ids[n]);
        output << n << '=' << ids[n] << '|' << move.transportTransferHostId << '|'
            << (move.transportTransferKind==RuntimeTransportTransferKind::Boarding?"boarding":"egress") << '\n';
    }
}

// The factory movement proof is a private scratch state built before the normal intent stage.
// Copy the real saved intent records into that scratch only; the regular loader still validates all authority.
bool loadRuntimeTransportTransferProofIntents(RuntimeWindowState& state,
    const IniFile& ini, std::string& errorMessage) {
    const auto* marker=ini.getSection("TransportTransferPresentation");
    if (!marker) return true;
    int count=0;
    const auto fail=[&](){errorMessage="invalid transport presentation proof intent";return false;};
    if (!parseRuntimeInt(ini.getValue("TransportIntents","Count","0"),count)
        || count<0 || count>100000) return fail();
    std::map<int,int> intents;
    for(int n=0;n<count;++n) {
        const auto fields=splitRuntimeSaveFields(ini.getValue("TransportIntents",std::to_string(n),""));
        int id=-1,host=-1;
        if(fields.size()!=2 || !parseRuntimeInt(fields[0],id) || !parseRuntimeInt(fields[1],host)
            || id<0 || host<0 || id==host || !intents.emplace(id,host).second) return fail();
    }
    for(const auto& [id,host]:intents) state.pendingTransportTargets[id]=host;
    return true;
}

bool loadRuntimeTransportTransferPresentation(RuntimeWindowState& state,
    const IniFile& ini, std::string& errorMessage, bool multiplayer = false) {
    const char* sectionName=multiplayer && ini.getSection("MultiplayerTransportTransferPresentation")
        ? "MultiplayerTransportTransferPresentation" : "TransportTransferPresentation";
    struct Record {int id,host;RuntimeTransportTransferKind kind;};
    std::vector<Record> records;
    const auto fail=[&](){errorMessage="invalid transport transfer presentation";return false;};
    if (ini.getSection(sectionName)) {
        int schema=0,count=0;
        if(!parseRuntimeInt(ini.getValue(sectionName,"Schema",""),schema)||schema!=1
            ||!parseRuntimeInt(ini.getValue(sectionName,"Count",""),count)
            ||count<0||std::size_t(count)>state.world.getObjects().size()) return fail();
        const auto* section=ini.getSection(sectionName);
        if(section->size()!=std::size_t(count)+2) return fail();
        std::unordered_set<std::string> keys;
        for(const auto& item:*section) {
            const auto key=toLowerAsciiLocal(trimAsciiLocal(item.first));int index=-1;
            if(!keys.insert(key).second || (key!="schema"&&key!="count"
                &&(!parseRuntimeInt(key,index)||index<0||index>=count||key!=std::to_string(index)))) return fail();
        }
        std::unordered_set<int> seen;
        for(int n=0;n<count;++n) {
            const auto fields=splitRuntimeSaveFields(ini.getValue(sectionName,std::to_string(n),""));
            int id=-1,host=-1;
            if(fields.size()!=3||!parseRuntimeInt(fields[0],id)||!parseRuntimeInt(fields[1],host)
                ||id<0||host<0||id==host||!seen.insert(id).second) return fail();
            RuntimeTransportTransferKind kind=RuntimeTransportTransferKind::None;
            if(fields[2]=="boarding")kind=RuntimeTransportTransferKind::Boarding;
            else if(fields[2]=="egress")kind=RuntimeTransportTransferKind::Egress;
            else return fail();
            const auto* object=findObjectById(state,id);const auto found=state.behaviors.find(id);
            if(!object||!object->alive||object->health<=0||object->airborne
                ||(object->kind!=WorldObjectKind::Infantry&&object->kind!=WorldObjectKind::Unit)
                ||found==state.behaviors.end()
                ||(found->second.kind!=RuntimeBehaviorKind::Move&&found->second.kind!=RuntimeBehaviorKind::Attack)
                ||found->second.moveCooldownTicks<=0) return fail();
            const auto& move=found->second;
            const auto from=move.moveSegmentFrom,logical=runtimeCellPointForObject(*object);
            if(from.x<0||from.y<0||from.x>=512||from.y>=512) return fail();
            if(kind==RuntimeTransportTransferKind::Boarding) {
                if(move.kind!=RuntimeBehaviorKind::Move||logical!=from
                    ||(object->kind==WorldObjectKind::Infantry&&!RuntimeSubcellSystemState::isActiveSubCell(object->subCell))
                    ||runtimeTransportAuthorizedBoardingHostId(state,*object,move.moveTarget)!=host
                    ||from==move.moveTarget||std::abs(from.x-move.moveTarget.x)>1||std::abs(from.y-move.moveTarget.y)>1) return fail();
            } else if(from==logical||std::abs(from.x-logical.x)>1||std::abs(from.y-logical.y)>1) return fail();
            // Egress is immutable accepted presentation. Its old host may already be dead or have changed owner.
            records.push_back({id,host,kind});
        }
    }
    for(auto& [id,move]:state.behaviors) {
        move.transportTransferHostId=-1;move.transportTransferKind=RuntimeTransportTransferKind::None;
    }
    for(const auto& record:records) {
        auto& move=state.behaviors.at(record.id);
        move.transportTransferHostId=record.host;move.transportTransferKind=record.kind;
    }
    return true;
}

bool validateRuntimeTransportTransferPresentation(const RuntimeWindowState& state,std::string& errorMessage) {
    for(const auto& [id,move]:state.behaviors) {
        if(move.transportTransferHostId<0&&move.transportTransferKind==RuntimeTransportTransferKind::None)continue;
        const auto fail=[&](){errorMessage="invalid restored transport transfer curve";return false;};
        const auto* object=findObjectById(state,id);
        if(move.transportTransferHostId<0||move.transportTransferKind==RuntimeTransportTransferKind::None
            ||!object||!object->alive||object->airborne||!object->renderMoveActive
            ||(move.kind!=RuntimeBehaviorKind::Move&&move.kind!=RuntimeBehaviorKind::Attack)
            ||move.renderMoveTicksRemaining<=0||move.renderMoveTicksRemaining!=move.moveCooldownTicks
            ||move.renderMoveTicksTotal<move.renderMoveTicksRemaining||move.renderArrivalDistanceAfter!=-1.0
            ||move.renderFrom!=move.moveSegmentFrom||move.renderFrom==move.renderTo
            ||std::abs(move.renderTo.x-move.renderFrom.x)>1||std::abs(move.renderTo.y-move.renderFrom.y)>1)return fail();
        const double x=move.renderFrom.x+move.renderFromOffsetX,y=move.renderFrom.y+move.renderFromOffsetY;
        const double tx=move.renderTo.x+move.renderToOffsetX,ty=move.renderTo.y+move.renderToOffsetY;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(tx)||!std::isfinite(ty)
            ||x<-.5||y<-.5||tx<-.5||ty<-.5||x>512.5||y>512.5||tx>512.5||ty>512.5
            ||std::hypot(tx-x,ty-y)<=1.0e-9)return fail();
        if(move.transportTransferKind==RuntimeTransportTransferKind::Boarding) {
            if(!runtimeTransportSavedBoardingEndpointAllowed(state,*object,move.renderFrom,move.renderTo,
                    move.renderToLevel,move.renderToOnBridge))return fail();
            const auto* host=findObjectById(state,move.transportTransferHostId);
            const auto offset=worldObjectSubCellOffset(*object),hostOffset=worldObjectSubCellOffset(*host);
            if(std::abs(tx+offset.x-runtimeObjectRenderX(*host)-hostOffset.x)>1.0e-9
                ||std::abs(ty+offset.y-runtimeObjectRenderY(*host)-hostOffset.y)>1.0e-9)return fail();
        } else if(runtimeCellPointForObject(*object)!=move.renderTo
            ||move.renderToOffsetX!=0.0||move.renderToOffsetY!=0.0)return fail();
    }
    return true;
}
