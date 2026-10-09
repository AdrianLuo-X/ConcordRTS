// Include after RuntimeMixedLocalComponentCertificateContract.inl. Reuses the
// real stock decoder and complete authority writer; no direct game execution.
#pragma once
#include <tuple>
namespace RuntimePsychicNoSensorContractDetail {
using Snapshot = SpecialMechanismRenderSnapshot;
auto fields(const Snapshot& s) {
    // Every declared field, including all display-only endpoints, is compared.
    return std::tie(s.kind,s.instanceId,s.objectId,s.targetObjectId,s.superWeaponId,
        s.sourceCell.x,s.sourceCell.y,s.targetCell.x,s.targetCell.y,s.cell.x,s.cell.y,s.cell2.x,s.cell2.y,
        s.sourceX,s.sourceY,s.targetX,s.targetY,s.sourceLevel,s.targetLevel,
        s.sourceScreenOffsetX,s.sourceScreenOffsetY,s.sourceNativeFixedX,s.sourceNativeFixedY,
        s.hasBeamEndpoints,s.missilePitch,s.missileFacing,s.radius,s.intensity,s.maximumIntensity,
        s.progress,s.requiredProgress,s.expireTick,s.owner,s.color,s.phase,s.resourceId);
}
bool equal(const std::vector<Snapshot>& a,const std::vector<Snapshot>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(fields(a[i])!=fields(b[i]))return false;
    return true;
}
struct Observation {
    std::size_t calls=0;
    RuntimePsychicNoSensorStats values;
    static void observe(const RuntimePsychicNoSensorStats& s,void* raw) noexcept {
        auto& o=*static_cast<Observation*>(raw);++o.calls;
        o.values.originalCalls+=s.originalCalls;o.values.readOnlyCalls+=s.readOnlyCalls;
        o.values.sensorScanEntries+=s.sensorScanEntries;o.values.sensors+=s.sensors;
        o.values.indexConstructions+=s.indexConstructions;o.values.indexScanEntries+=s.indexScanEntries;
        o.values.indexInsertionAttempts+=s.indexInsertionAttempts;o.values.uniqueIndexEntries+=s.uniqueIndexEntries;
        o.values.noSensorOmissions+=s.noSensorOmissions;
    }
};
WorldObjectInstance object(int id,WorldObjectKind kind,const char* owner,const char* type,int x,int y) {
    WorldObjectInstance o;o.instanceId=id;o.kind=kind;o.owner=owner;o.type=type;
    o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.alive=true;o.health=100;o.poweredOperational=true;
    return o;
}
bool primitive(std::string& error) {
    const auto fail=[&](const std::string& message){error="psychic no-sensor primitive: "+message;return false;};
    IniFile ini;RulesDatabase rules;
    if(!ini.loadFromString("[BuildingTypes]\n0=SENSOR\n1=NOSENSOR\n[SENSOR]\nStrength=1000\nPsychicDetectionRadius=5\n[NOSENSOR]\nStrength=1000\n")
        || !rules.loadFromIni(ini))return fail("real rule parser");
    std::size_t comparisons=0,callbackComparisons=0,positiveCases=0,noSensorCases=0,omissions=0;
    for(int variant=0;variant<24;++variant) {
        std::vector<WorldObjectInstance> objects{
            object(1,WorldObjectKind::Structure,"A","SENSOR",10,10),
            object(2,WorldObjectKind::Unit,"B","TANK",13,14),
            object(3,WorldObjectKind::Unit,"B","TANK",16,10),
            object(4,WorldObjectKind::Unit,"A","TANK",11,10)};
        std::vector<RuntimePsychicIntent> intents{{3,-1,{10,10},true},{2,4,{},true},{4,-1,{20,20},false}};
        std::string observer="A";bool rejectEligible=false;
        switch(variant) {
        case 1:observer.clear();break;
        case 2:objects[0].alive=false;break;
        case 3:objects[0].health=0;break;
        case 4:objects[0].poweredOperational=false;break;
        case 5:objects[0].kind=WorldObjectKind::Unit;break;
        case 6:objects[0].owner="B";break;
        case 7:objects[0].type="NOSENSOR";break;
        case 8:{auto dead=objects[1];dead.alive=false;objects.insert(objects.begin(),dead);break;}
        case 9:{auto dead=objects[1];dead.alive=false;objects.push_back(dead);break;}
        case 10:objects[1].instanceId=-7;intents[1].objectId=-7;break;
        case 11:objects[0].instanceId=-3;break;
        case 12:{auto dead=objects[3];dead.alive=false;objects.insert(objects.begin(),dead);break;}
        case 13:{auto dead=objects[3];dead.alive=false;objects.push_back(dead);break;}
        case 14:for(int n=0;n<10000;++n){auto dead=object(5+n,WorldObjectKind::Unit,"B","TANK",2,2);
            dead.alive=false;dead.health=0;objects.push_back(std::move(dead));}break;
        case 15:objects[1].x=14;objects[1].y=14;break;
        case 16:objects[1].health=0;break;
        case 17:objects[1].alive=false;break;
        case 18:objects[1].dockedToObjectId=1;break;
        case 19:objects[1].owner.clear();break;
        case 20:objects[1].owner="A";break;
        case 21:rejectEligible=true;break;
        case 22:intents[1].targetObjectId=999;break;
        case 23:intents[1].targetObjectId=-1;intents[1].destination={13,14};break;
        default:break;
        }
        std::array<std::vector<Snapshot>,3> results;
        std::array<std::vector<std::pair<std::string,std::string>>,3> allianceCalls;
        std::array<std::vector<int>,3> eligibleCalls;std::array<Observation,3> stats;
        for(int route=0;route<3;++route) {
            RuntimePsychicNoSensorReferenceScope reference(route==1);
            RuntimePsychicNoSensorObserverScope observe(&Observation::observe,&stats[route]);
            results[route]=runtimePsychicDetectionSnapshots(objects,rules,intents,observer,
                [&](const auto& a,const auto& b){allianceCalls[route].emplace_back(a,b);return false;},
                [&](const auto& actor){eligibleCalls[route].push_back(actor.instanceId);return !rejectEligible;},route!=0);
            if(variant==1) {if(stats[route].calls)return fail("original empty-observer early return");continue;}
            const auto& v=stats[route].values;
            if(stats[route].calls!=1 || v.sensorScanEntries!=objects.size())return fail("complete original sensor traversal");
            if(route<2 && (v.indexConstructions!=1 || v.indexScanEntries!=objects.size()
                || v.indexInsertionAttempts!=objects.size() || v.originalCalls!=1 || v.readOnlyCalls))
                return fail("two genuine old/reference ID maps");
            if(route==2) {
                if(v.readOnlyCalls!=1 || v.originalCalls)return fail("read-only default policy");
                if(v.sensors==0) {
                    if(v.indexConstructions || v.indexScanEntries || v.indexInsertionAttempts || v.uniqueIndexEntries
                        || v.noSensorOmissions!=1)return fail("no sensor must construct and scan ZERO ID indexes");
                    ++noSensorCases;omissions+=v.noSensorOmissions;
                } else if(v.indexConstructions!=1 || v.indexScanEntries!=objects.size()
                    || v.indexInsertionAttempts!=objects.size() || v.uniqueIndexEntries!=stats[0].values.uniqueIndexEntries)
                    return fail("positive sensors retain complete first-ID map");
            }
        }
        for(int route=1;route<3;++route) {
            if(!equal(results[0],results[route]))return fail("all snapshot fields/order variant="+std::to_string(variant));
            if(allianceCalls[0]!=allianceCalls[route] || eligibleCalls[0]!=eligibleCalls[route])
                return fail("original alliance/eligibility callback order variant="+std::to_string(variant));
            ++comparisons;++callbackComparisons;
        }
        positiveCases+=!results[0].empty();
        if((variant==0 || variant==9 || variant==10 || variant==11 || variant==13 || variant==14)
            && results[0].size()!=1)return fail("positive boundary/first-live/negative/large-history oracle");
        if((variant==8 || variant==12 || variant==22) && !results[0].empty())
            return fail("first-dead duplicate or missing target must veto");
    }
    // The generic API still captures each original ID BEFORE alliance callbacks.
    // A callback can edit a previously visited ID; do not silently broaden the
    // new read-only promise to the old public/default API.
    std::array<std::vector<Snapshot>,2> legacy;std::array<std::vector<int>,2> legacyCalls;
    for(int route=0;route<2;++route) {
        auto actor=object(2,WorldObjectKind::Unit,"B","TANK",13,14);
        std::vector<WorldObjectInstance> objects{actor,
            object(1,WorldObjectKind::Structure,"C","SENSOR",10,10),
            object(4,WorldObjectKind::Unit,"A","TANK",11,10)};
        RuntimePsychicNoSensorReferenceScope reference(route!=0);
        legacy[route]=runtimePsychicDetectionSnapshots(objects,rules,{{2,4,{},true}},"A",
            [&](const auto&,const auto& other){legacyCalls[route].push_back(objects.front().instanceId);
                if(other=="C"){objects.front().instanceId=1002;return true;}return false;});
        if(legacy[route].size()!=1 || legacy[route].front().objectId!=1002)return fail("legacy interleaved ID capture");
    }
    if(!equal(legacy[0],legacy[1]) || legacyCalls[0]!=legacyCalls[1]
        || comparisons!=48 || callbackComparisons!=48 || noSensorCases!=6 || omissions!=6 || positiveCases!=6)
        return fail("primitive/legacy coverage totals");
    std::cout<<"Psychic no-sensor primitive: cases=24 routeCalls=72 snapshotComparisons=48 callbackComparisons=48 legacyMutableCallbackRoutes=2 legacyComparisons=1 noSensorOmissions=6 positiveCases="
        <<positiveCases<<" firstID/deadDuplicate/negativeID/targetDead/targetMissing/alliance/eligibility/completeSnapshotFields/order/callbackOrder=exact passed=yes\n";
    return true;
}
bool actual(const RuntimeConfig& config,std::string& error) {
    using RuntimePlannerPassabilityContractDetail::Authority;
    using RuntimePlannerPassabilityContractDetail::capture;
    const auto fail=[&](const std::string& message){error="psychic no-sensor actual: "+message;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)return fail("real stock resources");
    const auto* stockSensor=assets->rules.findBuildingType("NAPSIS");
    if(!stockSensor || stockSensor->psychicDetectionRadius<=0)return fail("stock NAPSIS positive sensor-rule readback");
    std::size_t refreshCalls=0,snapshotComparisons=0,fullWriterComparisons=0,defaultOmissions=0;
    std::size_t oldIndexConstructions=0,oldIndexScanEntries=0,defaultSourceEntries=0;
    for(int history:{0,1000,10000}) {
        auto state=std::make_unique<RuntimeWindowState>();std::vector<int> ids;
        if(!RuntimeMixedLocalComponentContractDetail::prepareNative(*state,*assets,config,240,history,false,ids,error))return false;
        RuntimeSnapshotActiveStateScope active(*state);
        if(ids.size()!=48 || state->world.getObjects().size()!=static_cast<std::size_t>(57+history)
            || state->economy.owner!="Americans")return fail("real producer/world/owner identity");
        std::size_t structures=0,sensors=0,alive=0;
        for(const auto& actor:state->world.getObjects()) {
            if(!actor.alive)continue;++alive;
            if(actor.type=="NAPSIS")return fail("no synthetic NAPSIS injected into native world");
            if(actor.kind!=WorldObjectKind::Structure)continue;++structures;
            const auto* type=state->rules.findBuildingType(actor.type);
            if(!type)return fail("real stock building-type lookup");
            sensors+=type->psychicDetectionRadius>0;
        }
        if(structures!=9 || alive!=57 || sensors)return fail("all nine actual stock building sensor radii must be zero");
        Authority before;if(!capture(*state,before,error))return false;
        std::array<std::vector<Snapshot>,3> snapshots;
        for(int route=0;route<3;++route) {
            RuntimePsychicNoSensorReferenceScope reference(route!=2);
            Observation observation;
            {RuntimePsychicNoSensorObserverScope observe(&Observation::observe,&observation);
                refreshRuntimeSpecialMechanismRenderSnapshots(*state);}
            snapshots[route]=state->specialMechanismRenderSnapshots;
            Authority after;if(!capture(*state,after,error))return false;
            if(before!=after)return fail("render-only query changed complete World/system/serialized writer");
            ++fullWriterComparisons;++refreshCalls;
            const auto& v=observation.values;
            if(observation.calls!=1 || v.sensorScanEntries!=state->world.getObjects().size() || v.sensors)
                return fail("actual production caller did not execute whole sensor scan exactly once");
            if(route<2) {
                if(v.originalCalls!=1 || v.indexConstructions!=1
                    || v.indexScanEntries!=state->world.getObjects().size()
                    || v.uniqueIndexEntries!=state->world.getObjects().size())return fail("actual old full ID-map branch");
                oldIndexConstructions+=v.indexConstructions;oldIndexScanEntries+=v.indexScanEntries;
            } else {
                if(v.readOnlyCalls!=1 || v.indexConstructions || v.indexScanEntries || v.indexInsertionAttempts
                    || v.uniqueIndexEntries || v.noSensorOmissions!=1)return fail("actual no-sensor ID-index work not omitted");
                defaultOmissions+=v.noSensorOmissions;defaultSourceEntries+=v.sensorScanEntries;
            }
        }
        for(int route=1;route<3;++route) {
            if(!equal(snapshots[0],snapshots[route]))return fail("all actual combined special render snapshot fields/order");
            ++snapshotComparisons;
        }
    }
    if(refreshCalls!=9 || snapshotComparisons!=6 || fullWriterComparisons!=9 || defaultOmissions!=3
        || oldIndexConstructions!=6 || oldIndexScanEntries!=22342 || defaultSourceEntries!=11171)
        return fail("actual coverage/cardinality totals");
    std::cout<<"Psychic no-sensor actual: nativeCases=3 nativeActors=144 historyObjects=11000 refreshCalls=9 snapshotComparisons=6 completedFullWriterComparisons=9 actualBuildingReadbacks=27 actualWorldSourceEntries=11171 originalIndexConstructions=6 originalIndexScanEntries=22342 defaultNoSensorOmissions=3 defaultIndexConstructions=0 defaultIndexScanEntries=0 defaultIndexInsertionAttempts=0 defaultUniqueIndexEntries=0 stockNAPSISRadius="
        <<stockSensor->psychicDetectionRadius<<" Native48/H0/H1000/H10000/stockZeroSensorWorld/completeSpecialSnapshotFields/order/fullWriter=exact passed=yes\n";
    return true;
}
}
bool runRuntimePsychicDetectionNoSensorContract(const RuntimeConfig& config,std::string& error) {
    return RuntimePsychicNoSensorContractDetail::primitive(error)
        && RuntimePsychicNoSensorContractDetail::actual(config,error);
}
