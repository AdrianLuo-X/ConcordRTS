// New regression only; existing Dense cases, counts and assertions stay intact.
#pragma once
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <tuple>

namespace RuntimeSavedPsychicWorldContractDetail {
bool fail(std::string& error,const std::string& reason) {
    error="saved psychic world: "+reason;return false;
}
RuntimePsychicObjectSnapshot object(int id) {
    RuntimePsychicObjectSnapshot value;
    value.objectId=id;value.owner=id==1?"Yuri":"Allies";
    value.typeId=id==1?"CAOS":"MTNK";
    value.kind=RuntimePsychicObjectKind::Vehicle;value.present=value.alive=true;
    return value;
}
RuntimePsychicStatusReconcileInput input(std::size_t n) {
    RuntimePsychicStatusReconcileInput value;value.currentTick=1;
    value.objects.reserve(n);
    for(std::size_t i=0;i<n;++i)value.objects.push_back(object(static_cast<int>(i+1)));
    return value;
}
std::string saved(const RuntimePsychicStatusSystemState& state) {
    std::ostringstream out;state.save(out);return out.str();
}
bool seeded(RuntimePsychicStatusSystemState& state,std::string& error) {
    RuntimePsychicStatusHitRequest hit;hit.currentTick=0;hit.hitEventId=1;hit.durationTicks=100;
    hit.source=object(1);hit.target=object(2);
    hit.warhead.warheadId="PsychGas";hit.warhead.warheadExists=hit.warhead.psychedelic=true;
    return state.applyHit(hit).accepted ? true : fail(error,"real original applyHit prerequisite");
}
bool actions(const std::vector<RuntimePsychicStatusAction>& a,const std::vector<RuntimePsychicStatusAction>& b) {
    if(a.size()!=b.size())return false;
    const auto fields=[](const RuntimePsychicStatusAction& s){return std::tie(s.kind,s.endReason,s.tick,s.statusId,
        s.hitEventId,s.sourceObjectId,s.targetObjectId,s.sourceOwner,s.targetOwner,s.warheadId,s.startedTick,
        s.expiresTick,s.attackOriginalAllies,s.sourceValidatedAtLaunch);};
    for(std::size_t i=0;i<a.size();++i)if(fields(a[i])!=fields(b[i]))return false;
    return true;
}
bool reports(const RuntimePsychicStatusReconcileReport& a,const RuntimePsychicStatusReconcileReport& b) {
    return a.inputRejected==b.inputRejected && a.actionLimitReached==b.actionLimitReached
        && a.retained==b.retained && a.ended==b.ended
        && a.sourceIdentityChangedStatusIds==b.sourceIdentityChangedStatusIds && actions(a.actions,b.actions);
}
bool unchanged(const RuntimePsychicStatusSystemState& state,std::uint64_t hash,const std::string& text) {
    return state.stateHash()==hash && saved(state)==text && state.limits().maxInputRecords==4096;
}
bool sameLimits(const RuntimePsychicStatusLimits& a,const RuntimePsychicStatusLimits& b) {
    return std::tie(a.maxStatuses,a.maxHitEvents,a.maxActionsPerOperation,a.maxInputRecords,a.maxTextLength,a.maxRecursionDepth,a.maxDurationTicks)
        ==std::tie(b.maxStatuses,b.maxHitEvents,b.maxActionsPerOperation,b.maxInputRecords,b.maxTextLength,b.maxRecursionDepth,b.maxDurationTicks);
}
} // namespace RuntimeSavedPsychicWorldContractDetail

bool runRuntimeSavedPsychicWorldPrimitiveContract(std::string& error) {
    namespace D=RuntimeSavedPsychicWorldContractDetail;
    std::size_t boundaries=0,parity=0,malformed=0,budgets=0,largeActive=0,customLimits=0;
    // Include both the generic operation cap and the actual loader hard cap.
    for(std::size_t n:{0U,4096U,4097U,10057U,100000U,100001U}) {
        const auto value=D::input(n);RuntimePsychicStatusSystemState original,world;
        const auto hash=world.stateHash();const auto text=D::saved(world);
        const auto old=original.reconcile(value);const auto actual=world.reconcileSavedWorld(value,n);
        if(old.inputRejected!=(n>4096) || actual.inputRejected!=(n>100000)
            || actual.actionLimitReached || actual.ended || actual.retained || !actual.actions.empty()
            || !D::unchanged(world,hash,text))return D::fail(error,"exact count boundary="+std::to_string(n));
        ++boundaries;
    }
    // All original source/target meanings, including dead source attribution,
    // run through the SAME kernel under every validation-flag combination.
    for(int variant=0;variant<8;++variant)for(int flags=0;flags<4;++flags) {
        auto value=D::input(4);value.validateSources=(flags&1)!=0;value.validateTargets=(flags&2)!=0;
        if(variant==1)value.objects[1].alive=false;
        if(variant==2)value.objects[0].alive=false;
        if(variant==3)value.objects.erase(value.objects.begin());
        if(variant==4)value.objects.erase(value.objects.begin()+1);
        if(variant==5)value.objects[0].typeId="CHANGED";
        if(variant==6)value.objects[1].typeId="CHANGED";
        if(variant==7)value.objects[1].airborne=true;
        RuntimePsychicStatusSystemState original,world;
        if(!D::seeded(original,error)||!D::seeded(world,error))return false;
        const auto old=original.reconcile(value);const auto actual=world.reconcileSavedWorld(value,value.objects.size());
        if(!D::reports(old,actual) || original.stateHash()!=world.stateHash() || D::saved(original)!=D::saved(world)
            || world.limits().maxInputRecords!=4096)return D::fail(error,"original full action/status/hit report parity");
        ++parity;
    }
    // A malformed unrelated record AFTER 4096 still receives original exact
    // snapshot validation. No live-only, relevant-only or first-prefix filter.
    for(int variant=0;variant<12;++variant) {
        auto value=D::input(4097);auto& row=value.objects.back();
        switch(variant) {
        case 0:row.objectId=0;break;case 1:row.objectId=-1;break;
        case 2:row.objectId=1;break;case 3:row.kind=static_cast<RuntimePsychicObjectKind>(255);break;
        case 4:row.owner.clear();break;case 5:row.typeId.clear();break;
        case 6:row.owner.assign(129,'a');break;case 7:row.typeId.assign(129,'b');break;
        case 8:row.owner=std::string("a\0b",3);break;case 9:row.typeId=std::string("a\0b",3);break;
        case 10:row.objectId=std::numeric_limits<int>::min();break;
        case 11:row.present=false;row.owner.assign(129,'c');break;
        }
        RuntimePsychicStatusSystemState state;if(!D::seeded(state,error))return false;
        const auto hash=state.stateHash();const auto text=D::saved(state);
        const auto actual=state.reconcileSavedWorld(value,value.objects.size());
        if(!actual.inputRejected || !actual.actions.empty() || !D::unchanged(state,hash,text))
            return D::fail(error,"malformed complete-world record not rejected atomically");
        ++malformed;
    }
    for(std::size_t budget:std::array<std::size_t,4>{0U,4096U,100001U,std::numeric_limits<std::size_t>::max()}) {
        auto value=D::input(4097);RuntimePsychicStatusSystemState state;if(!D::seeded(state,error))return false;
        const auto hash=state.stateHash();const auto text=D::saved(state);
        if(!state.reconcileSavedWorld(value,budget).inputRejected || !D::unchanged(state,hash,text))
            return D::fail(error,"mismatched/overflow operation budget");
        ++budgets;
    }
    // Active attributed statuses and receipts survive a full H10000 input;
    // the original persistent limits, serialized bytes, and hash remain exact.
    for(int absent=0;absent<2;++absent) {
        auto value=D::input(10057);value.objects[0].alive=false;
        if(absent){value.objects.back().present=false;value.objects.back().owner.clear();value.objects.back().typeId.clear();}
        RuntimePsychicStatusSystemState state;if(!D::seeded(state,error))return false;
        const auto hash=state.stateHash();const auto text=D::saved(state);
        const auto result=state.reconcileSavedWorld(value,value.objects.size());
        if(result.inputRejected || result.actionLimitReached || result.ended || result.retained!=1
            || !result.actions.empty() || !result.sourceIdentityChangedStatusIds.empty() || !D::unchanged(state,hash,text))
            return D::fail(error,"large active/dead source attribution or absent snapshot semantics");
        ++largeActive;
    }
    for(int variant=0;variant<2;++variant) {
        RuntimePsychicStatusLimits limits;limits.maxStatuses=limits.maxHitEvents=variant?2:1;
        limits.maxActionsPerOperation=1;limits.maxInputRecords=64;
        RuntimePsychicStatusSystemState original(limits),world(limits);
        if(!D::seeded(original,error)||!D::seeded(world,error))return false;
        if(variant) {
            RuntimePsychicStatusHitRequest hit;hit.currentTick=0;hit.hitEventId=2;hit.durationTicks=100;
            hit.source=D::object(1);hit.target=D::object(3);
            hit.warhead.warheadId="PsychGas";hit.warhead.warheadExists=hit.warhead.psychedelic=true;
            if(!original.applyHit(hit).accepted||!world.applyHit(hit).accepted)return D::fail(error,"action-cap two status prerequisite");
        }
        auto smallInput=D::input(4),largeInput=D::input(10057);
        if(variant){smallInput.objects[1].alive=smallInput.objects[2].alive=largeInput.objects[1].alive=largeInput.objects[2].alive=false;}
        const auto beforeHash=world.stateHash();const auto beforeText=D::saved(world);
        const auto a=original.reconcile(smallInput),b=world.reconcileSavedWorld(largeInput,largeInput.objects.size());
        if(!D::reports(a,b)||a.actionLimitReached!=(variant!=0)||original.stateHash()!=world.stateHash()
            ||D::saved(original)!=D::saved(world)||!D::sameLimits(world.limits(),limits)
            ||world.stateHash()!=beforeHash||D::saved(world)!=beforeText)
            return D::fail(error,"independent status/hit/action/text/recursion/duration limits changed");
        ++customLimits;
    }
    if(boundaries!=6||parity!=32||malformed!=12||budgets!=4||largeActive!=2||customLimits!=2)return D::fail(error,"primitive count closure");
    std::cout<<"Saved psychic complete-world regression: countBoundaries=6 originalReportPairs=32 malformedTailRecords=12 invalidBudgets=4 largeActiveStatusCases=2 customLimitPairs=2 genericInputCap=4096 savedWorldCap=100000 allRows/duplicates/negativeIDs/type/owner/missing/deadSource/deadTarget/actionOrder/statuses/hitEvents/textLimits/persistentHash=exact passed=yes\n";
    return true;
}

bool runRuntimeSavedPsychicWorldNegativeLoads(RuntimeWindowState& state,const std::filesystem::path& savedPath,std::string& error) {
    namespace D=RuntimeSavedPsychicWorldContractDetail;
    using Full=RuntimePathSnapshotConditionalQueriesContractDetail::Full;
    using RuntimePathSnapshotConditionalQueriesContractDetail::capture;
    if(state.world.getObjects().size()!=10057 || state.psychicStatuses.limits().maxInputRecords!=4096)
        return D::fail(error,"actual H10000 public-load negative prerequisite");
    std::ifstream input(savedPath,std::ios::binary);
    const std::string original((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    IniFile ini;if(original.empty()||!ini.loadFromString(original)||ini.getValue("Objects","Count","")!="10057")
        return D::fail(error,"actual complete save source");
    const auto first=ini.getValue("Objects","0","");const auto tail=ini.getValue("Objects","10056","");
    const auto firstBar=first.find('|'),tailBar=tail.find('|');
    const auto firstEnd=first.find('|',firstBar+1),tailEnd=tail.find('|',tailBar+1);
    if(firstBar==std::string::npos||tailBar==std::string::npos||firstEnd==std::string::npos||tailEnd==std::string::npos)return D::fail(error,"actual object-ID record grammar");
    Full before;if(!capture(state,before,error))return false;
    for(int variant=0;variant<2;++variant) {
        const std::string needle="\n10056="+tail;
        const auto at=original.find(needle);
        if(at==std::string::npos||original.find(needle,at+needle.size())!=std::string::npos)return D::fail(error,"unique actual saved tail row");
        const std::string id=variant==0?first.substr(firstBar+1,firstEnd-firstBar-1):"-1";
        std::string bad=original;bad.replace(at+7+tailBar+1,tailEnd-tailBar-1,id);
        const auto path=savedPath.parent_path()/(savedPath.stem().string()+"_negative_id_"+std::to_string(variant)+".ini");
        if(std::filesystem::exists(path))return D::fail(error,"fresh negative file already exists");
        {std::ofstream out(path,std::ios::binary);out.write(bad.data(),static_cast<std::streamsize>(bad.size()));
            if(!out.good())return D::fail(error,"actual negative file write");}
        std::cout<<"Saved psychic negative-load fixture: "<<pathString(path)<<'\n';
        std::string rejection;
        if(loadRuntimeState(state,pathString(path),rejection)||rejection.empty())return D::fail(error,"malformed public load accepted");
        Full after;if(!capture(state,after,error))return false;
        if(!(before==after)||state.psychicStatuses.limits().maxInputRecords!=4096)
            return D::fail(error,"public negative load changed complete live authority/writer");
    }
    std::cout<<"Saved psychic public-load atomic rejection: nativeWorldObjects=10057 malformedFiles=2 duplicateIDs=1 negativeIDs=1 completeWriterComparisons=2 sourceFilePreserved=yes liveStateUnchanged=yes passed=yes\n";
    return true;
}
