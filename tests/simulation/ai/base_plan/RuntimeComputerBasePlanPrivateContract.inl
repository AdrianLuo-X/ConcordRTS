namespace h008_private_qualification {
namespace original {
#define runtimeComputerAiBaseTargets reference_runtimeComputerAiBaseTargets
#define runtimeComputerAiBasePlan reference_runtimeComputerAiBasePlan
#include "RuntimeComputerBasePlanOriginal.inl"
#undef runtimeComputerAiBaseTargets
#undef runtimeComputerAiBasePlan
}
std::string exercise(bool reference,int& checks){
    const auto require=[&](bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);};
    auto owned=std::make_unique<IndependentRecoveryHost>();auto& s=*owned;std::string error;
    const bool configured=configureRuntimeDetailSnapshotSmokeWorld(s,false,error);require(configured,"actual baseplan fixture "+error);
    RuntimeSnapshotActiveStateScope active(s);s.config.defaultMapPath="h008-private-base-plan.map";s.config.multiplayerActive=false;s.runtimeSuppressPlayerAudioFeedback=true;
    auto targets=reference?original::reference_runtimeComputerAiBaseTargets:concord::runtime::session_detail::runtimeComputerAiBaseTargets;
    auto plan=reference?original::reference_runtimeComputerAiBasePlan:concord::runtime::session_detail::runtimeComputerAiBasePlan;
    auto goals=reference?original::appendRuntimeComputerAiBasePlanGoals:concord::runtime::session_detail::appendRuntimeComputerAiBasePlanGoals;
    using Role=RuntimeComputerAiStructureRole;using Targets=RuntimeComputerAiBaseTargets;
    require(sizeof(Role)==4&&alignof(Role)==4&&sizeof(Targets)==12&&alignof(Targets)==4,"actual legacy value ABI sizes/alignments");
    require(offsetof(Targets,defenses)==0&&offsetof(Targets,refineries)==4&&offsetof(Targets,slaveRefineries)==8,"actual original target offsets");
    Targets defaults;require(defaults.defenses==0&&defaults.refineries==1&&!defaults.slaveRefineries,"actual target defaults");
    const std::array<Role,9> originalRoles{Role::Power,Role::Refinery,Role::Barracks,Role::VehicleFactory,Role::Shipyard,Role::Radar,Role::Defense,Role::Tech,Role::Superweapon};
    for(std::size_t i=0;i<originalRoles.size();++i)require(static_cast<int>(originalRoles[i])==static_cast<int>(i),"actual original enum values");
    std::ostringstream image;image<<"ABI "<<typeid(Role).name()<<'|'<<typeid(Targets).name()<<'\n';
    const std::array<std::string,4> countries{"Americans","Russians","YuriCountry","UnknownHouse"};
    const std::array<std::string,4> difficulties{" eAsY ","normal","HARD","Brutal"};
    const std::array<std::array<int,3>,3> defense{{{{5,3,1}},{{7,4,2}},{{8,6,0}}}};
    const std::array<int,3> extra{2,1,0},slave{4,3,2};
    auto& ai=s.computerAiByOwner["skirmishplayer1"];ai.initialized=true;ai.owner="SkirmishPlayer1";ai.credits=123456;
    for(int ownerMode=0;ownerMode<2;++ownerMode){
        IniFile ini;const std::string text=std::string(R"H8([General]
AlliedBaseDefenseCounts=5,3,1
SovietBaseDefenseCounts=7,4,2
ThirdBaseDefenseCounts=8,6,0
AIExtraRefineries=2,1,0
AISlaveMinerNumber=4,3,2
HarvesterUnit=H8_HARV,H8_CMIN
[VehicleTypes]
0=SNAPSHOT_TANK
1=H8_HARV
2=H8_CMIN
[BuildingTypes]
0=DETAIL_FACTORY
1=SNAPSHOT_LANDMARK
[SNAPSHOT_TANK]
Strength=200
Cost=100
Speed=6
SpeedType=Track
MovementZone=Normal
[DETAIL_FACTORY]
Strength=1000
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
[SNAPSHOT_LANDMARK]
Strength=1000
[H8_HARV]
Strength=100
Owner=Russians
[H8_CMIN]
Strength=100
Owner=)H8")+(ownerMode?"YuriCountry":"Americans")+'\n';
        const bool parsed=ini.loadFromString(text)&&s.rules.loadFromIni(ini);require(parsed,"actual modified owner/rules fixture");s.rulesLoaded=true;
        for(int side=0;side<4;++side)for(int difficulty=0;difficulty<4;++difficulty){
            ai.country=countries[static_cast<std::size_t>(side)];ai.difficulty=difficulties[static_cast<std::size_t>(difficulty)];const int index=difficulty==0?2:difficulty==1?1:0;
            const bool ordinary=side==1||(ownerMode==0?side==0:side==2);
            const auto t=targets(s,ai);require(t.defenses==(side<3?defense[static_cast<std::size_t>(side)][static_cast<std::size_t>(index)]:0),"actual side and normalized difficulty target");
            require(t.slaveRefineries!=ordinary&&t.refineries==(ordinary?1+extra[static_cast<std::size_t>(index)]:slave[static_cast<std::size_t>(index)]),"actual HarvesterUnit Owner mask controls exception");
            const auto p=plan(ai,t);require(p.size()==static_cast<std::size_t>(7+t.refineries-1+t.defenses),"actual deterministic role quantities");
            require(p[0]==Role::Power&&p[1]==Role::Refinery&&std::count(p.begin(),p.end(),Role::Defense)==t.defenses,"actual opening and defense multiplicity");
            RuntimeComputerAiProfile profile;profile.attackThreshold=6;RuntimeComputerAiMetrics metrics;metrics.structures=20;metrics.combatUnits=9;s.config.superweapons=(difficulty%2)==0;
            std::vector<Role> out{Role::Power,Role::Power};goals(s,ai,profile,metrics,out);
            require(out.size()>=2&&out[0]==Role::Power&&out[1]==Role::Power&&std::count(out.begin(),out.end(),Role::Power)==2,"actual incoming goal prefix retained");
            require((std::find(out.begin(),out.end(),Role::Superweapon)!=out.end())==s.config.superweapons,"actual super gate");
            metrics.structures=7;std::vector<Role> blocked;goals(s,ai,profile,metrics,blocked);require(std::find(blocked.begin(),blocked.end(),Role::Superweapon)==blocked.end(),"actual low structures super gate");
            image<<ownerMode<<':'<<side<<':'<<difficulty<<':'<<t.defenses<<','<<t.refineries<<','<<t.slaveRefineries<<':';for(Role role:p)image<<static_cast<int>(role)<<',';image<<'|';for(Role role:out)image<<static_cast<int>(role)<<',';image<<'|';for(Role role:blocked)image<<static_cast<int>(role)<<',';image<<'\n';
        }
    }
    const std::string priorOwner=ai.owner;const auto first=plan(ai,targets(s,ai));ai.owner="  SKIRMISHPLAYER1 ";require(first==plan(ai,targets(s,ai)),"actual trim/ASCII owner hash stable");ai.owner=priorOwner;
    std::ostringstream saved;const bool wrote=writeRuntimeStateToStream(s,saved,error);require(wrote,"actual complete baseplan save "+error);image<<saved.str();return image.str();
}
int run(){try{int checks=0;const auto before=exercise(true,checks),current=exercise(false,checks);++checks;if(before!=current)throw std::runtime_error("actual ordered target/plan/goals and complete save differ original");std::cout<<"private-base-plan checks="<<checks<<" actual-root-states=64 complete-save=exact ordered-roles=exact legacy-abi=exact failures=0\n";return 0;}catch(const std::exception& e){std::cerr<<"private base plan: "<<e.what()<<'\n';return 1;}}
}
