#include "RuntimeDetailRulesHash.h"

bool configureRuntimeMultiplayerSnapshotSmokeState(RuntimeWindowState&, bool, bool, bool, double);

bool runRuntimeFireRevealSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "Fire reveal: " + message; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original rules unavailable");
    for (const char* id : {"AWP", "AWPE", "Virusgun", "VirusgunE"}) {
        const auto* weapon = stock->rules.findWeaponType(id);
        if (!weapon || weapon->revealOnFire) return fail(std::string("original quiet weapon: ") + id);
    }
    const auto* tankWeapon = stock->rules.findWeaponType("105mm");
    if (!tankWeapon || !tankWeapon->revealOnFire) return fail("original default reveal flag");
    const auto originalRuleHash = runtimeDetailRulesHash(stock->rules);
    const_cast<WeaponType*>(tankWeapon)->revealOnFire = false;
    if (originalRuleHash == runtimeDetailRulesHash(stock->rules)) return fail("flag absent from rule hash");
    const_cast<WeaponType*>(tankWeapon)->revealOnFire = true;

    IniFile ini;
    if (!ini.loadFromString(R"FIRE(
[VehicleTypes]
0=SHOOTER
1=TARGET
[BuildingTypes]
0=GAP
[SHOOTER]
Strength=1000
Sight=12
Primary=LOUD
Secondary=QUIET
Speed=5
[TARGET]
Strength=1000
Sight=1
[GAP]
Strength=1000
Sight=1
GapGenerator=yes
GapRadiusInCells=6
[LOUD]
Damage=10
Range=12
ROF=30
Burst=3
BurstDelay=3
Projectile=PLAIN
Warhead=WH
Speed=100
OmniFire=yes
[QUIET]
Damage=10
Range=12
ROF=30
Projectile=PLAIN
Warhead=WH
Speed=100
RevealOnFire=no
DecloakToFire=no
[PLAIN]
Inviso=yes
[WH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)FIRE")) return fail("fixture INI");
    const auto makeWorld = [&]() {
        auto s = std::make_unique<RuntimeWindowState>();
        s->config.fixedTickHz=30; s->config.computerAiEnabled=false;
        s->config.playerSlots.clear(); s->economy.owner="Alpha"; s->tick=10;
        s->runtimeSuppressPlayerAudioFeedback=true; s->runtimeSoundsEnabled=false;
        s->runtimePreferencePersistenceEnabled=false; s->visionEnabled=true;
        s->rules.loadFromIni(ini); s->rulesLoaded=true;
        for(int y=0;y<40;++y) for(int x=0;x<40;++x) {
            TerrainCell t; t.x=x;t.y=y;t.cellIndex512=y*512+x;
            s->world.accessTerrainCells().push_back(t);
            s->terrainInfoByCell.emplace(runtimeCellKey(x,y),RuntimeTerrainInfo{});
        }
        WorldObjectInstance a; a.instanceId=1;a.type="SHOOTER";a.owner="Alpha";
        a.kind=WorldObjectKind::Unit;a.x=6;a.y=6;a.cell=runtimeCellKey(6,6);
        a.alive=true;a.health=1000;a.mission="Guard";
        auto b=a;b.instanceId=2;b.type="TARGET";b.owner="Beta";b.x=14;b.cell=runtimeCellKey(14,6);
        s->world.accessObjects()={a,b};
        s->terrainByCell=buildRuntimeTerrainLookup(s->world);
        return s;
    };
    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Alpha",{}},{"Beta",{}},{"Ally",{"Beta"}},{"Other",{}}};
    const auto refresh=[&](RuntimeWindowState& s) { s.vision.update(s.world,&s.rules,observers,s.tick); };
    const auto hidden=[](const RuntimeWindowState& s, const char* owner, int x=6,int y=6) {
        return s.vision.visibilityFor(owner,x,y)==RuntimeCellVisibility::Shrouded;
    };
    for(int hz : {15,30,60}) {
        auto s=makeWorld();s->config.fixedTickHz=hz;refresh(*s);
        const auto& a=s->world.getObjects()[0];const auto& b=s->world.getObjects()[1];
        const auto& loud=*s->rules.findWeaponType("LOUD");const auto& quiet=*s->rules.findWeaponType("QUIET");
        const auto explored=s->vision.exploredCellIndices("Beta");
        if(!hidden(*s,"Beta") || s->vision.notifyWeaponFired(a,b,quiet,s->tick,hz)
            || !hidden(*s,"Beta")) return fail("quiet weapon cleared shroud");
        if(!s->vision.notifyWeaponFired(a,b,loud,s->tick,hz) || hidden(*s,"Beta")
            || hidden(*s,"Beta",9,6) || !hidden(*s,"Beta",9,7)
            || !hidden(*s,"Ally") || !hidden(*s,"Other")
            || s->vision.exploredCellIndices("Beta")!=explored) return fail("radius/recipient/permanent exploration");
        RuntimeVisionGridView view;
        if(!s->vision.gridViewFor("Beta",view)
            || view.visibilityAtIndex(6*512+6)!=RuntimeCellVisibility::Visible
            || view.exploredCells[6*512+6]) return fail("render grid lost temporary visibility");
        auto cloaked=a;cloaked.cloaked=true;
        if(s->vision.isObjectVisibleTo("Beta",cloaked) || !s->vision.isObjectVisibleTo("Beta",a))
            return fail("temporary sight bypassed cloak or hid ordinary shooter");
        const auto active=s->vision.serializeFireReveals(s->tick);
        for(int n=0;n<5;++n) refresh(*s);
        if(s->vision.serializeFireReveals(s->tick)!=active) return fail("same-tick updates consumed time");
        s->tick+=5*hz-1;refresh(*s);
        if(hidden(*s,"Beta")) return fail("reveal ended early");
        ++s->tick;refresh(*s);
        if(!hidden(*s,"Beta") || s->vision.exploredCellIndices("Beta")!=explored)
            return fail("reveal did not expire to original shroud");
    }
    {
        auto s=makeWorld(); auto gap=s->world.getObjects()[0];
        gap.instanceId=3;gap.kind=WorldObjectKind::Structure;gap.type="GAP";gap.poweredOperational=true;
        s->world.accessObjects().push_back(gap);refresh(*s);
        s->vision.exploreArea("Beta",6,6,4);refresh(*s);
        const auto explored=s->vision.exploredCellIndices("Beta");
        const auto a=s->world.getObjects()[0], b=s->world.getObjects()[1];
        const auto& weapon=*s->rules.findWeaponType("LOUD");
        if(!hidden(*s,"Beta") || !s->vision.notifyWeaponFired(a,b,weapon,s->tick,30)
            || hidden(*s,"Beta")) return fail("gap reveal");
        s->tick+=50;s->vision.notifyWeaponFired(a,b,weapon,s->tick,30);refresh(*s);
        s->tick=160;refresh(*s);
        if(hidden(*s,"Beta")) return fail("repeat fire did not refresh expiration");
        s->tick=210;refresh(*s);
        if(!hidden(*s,"Beta") || s->vision.exploredCellIndices("Beta")!=explored)
            return fail("gap not restored at expiration");
        s->world.accessObjects()[2].alive=false;refresh(*s);
        if(hidden(*s,"Beta")) return fail("gap erased preexisting exploration");
        s->vision.resetShroud("Beta");refresh(*s);
        s->vision.notifyWeaponFired(a,b,weapon,s->tick,30);
        s->vision.exploreArea("Beta",6,6,1);
        s->tick+=150;refresh(*s);
        if(hidden(*s,"Beta")) return fail("real exploration during reveal was lost");
        s->vision.resetShroud("Beta");refresh(*s);
        auto ground=b;ground.instanceId=-1;ground.owner="Beta";
        if(s->vision.notifyWeaponFired(a,ground,weapon,s->tick,30)) return fail("ground force fire gained an observer");
    }
    {
        auto s=makeWorld();refresh(*s);
        auto* shooter=findMutableObjectById(*s,1);const auto* victim=findObjectById(*s,2);
        const auto profile=runtimeWeaponProfileForObject(*s,*shooter,victim);
        if(!profile.weapon || !fireRuntimeIndependentCombatWeapon(*s,*shooter,nullptr,*victim,profile)
            || s->projectiles.size()!=1 || hidden(*s,"Beta")
            || !s->combatDetails.hasPendingBurst(1)) return fail("actual first burst round");
        s->vision.resetShroud("Beta");refresh(*s);
        if(!hidden(*s,"Beta")) return fail("explicit shroud reset retained a pulse");
        for(int i=0;i<6;++i) {++s->tick;advanceRuntimeCombatBursts(*s);refresh(*s);}
        if(s->projectiles.size()<2 || hidden(*s,"Beta")) return fail("later burst round failed to reveal");
        s->vision.resetShroud("Beta");refresh(*s);
        const auto* quiet=s->rules.findWeaponType("QUIET");RuntimeWeaponProfile quietProfile;
        finalizeRuntimeWeaponProfile(quietProfile,*s,RuntimeWeaponSlot::Secondary,*quiet,"quiet probe");
        spawnRuntimeProjectile(*s,*shooter,*victim,*quiet,quietProfile,10);
        if(!hidden(*s,"Beta")) return fail("actual quiet projectile revealed shooter");
        spawnRuntimeProjectile(*s,*shooter,*victim,*profile.weapon,profile,10,77,1,true);
        if(!hidden(*s,"Beta")) return fail("secondary projectile revealed remote source");
        auto moved=*shooter;moved.x=25;moved.y=25;moved.cell=runtimeCellKey(25,25);
        const auto pose=runtimeCombatFiringPose(*shooter,&moved);
        spawnRuntimeProjectile(*s,pose,*victim,*profile.weapon,profile,10);
        if(!hidden(*s,"Beta") || hidden(*s,"Beta",25,25)) return fail("passenger firing used hidden passenger location");
    }
    std::cout << "Fire reveal: native flags/rules hash, target-only radius, timer, gap, cloak, burst, passenger pose passed\n";

    auto s=std::make_unique<RuntimeWindowState>();
    if(!configureRuntimeMultiplayerSnapshotSmokeState(*s,false,false,false,0.0)) return fail("snapshot fixture");
    for(int y=0;y<40;++y) for(int x=0;x<40;++x) {
        TerrainCell t;t.x=x;t.y=y;t.level=2;t.cellIndex512=y*512+x;s->world.accessTerrainCells().push_back(t);
    }
    WorldObjectInstance a; a.instanceId=101;a.type="SNAPSHOT_TANK";a.owner="SkirmishPlayer1";
    a.kind=WorldObjectKind::Unit;a.x=6;a.y=6;a.cell=runtimeCellKey(6,6);a.level=2;
    a.alive=true;a.health=200;a.mission="Guard";
    auto b=a;b.instanceId=102;b.owner="SkirmishPlayer2";b.x=25;b.cell=runtimeCellKey(25,6);
    s->world.accessObjects().push_back(a);s->world.accessObjects().push_back(b);
    // The reusable snapshot fixture gives all slots the same team string.
    // This scenario needs explicit opposing houses with no shared unit sight.
    s->playerCommands.alliances[{"skirmishplayer1","skirmishplayer2"}]=false;
    s->playerCommands.alliances[{"skirmishplayer2","skirmishplayer1"}]=false;
    initializeRuntimeNavigation(*s);
    s->visionEnabled=true;completeRuntimeWorldRefresh(*s);updateRuntimeVisionState(*s);
    WeaponType loud;
    if(!s->vision.notifyWeaponFired(a,b,loud,s->tick,30)) return fail("snapshot pulse creation");
    const auto saved=s->vision.serializeFireReveals(s->tick);
    const auto hash=runtimeMultiplayerAuthoritativeStateHash(*s,error);
    if(hash==0) return fail("snapshot source authority: "+error);
    s->vision.notifyWeaponFired(a,b,loud,s->tick+1,30);
    if(hash==runtimeMultiplayerAuthoritativeStateHash(*s,error)) return fail("expiry absent from authority hash");
    if(!s->vision.restoreFireReveals(saved,s->tick,30)) return fail("valid pulse restore");
    if(s->vision.restoreFireReveals(saved+"trailing",s->tick,30)
        || s->vision.restoreFireReveals("1 1\n\"skirmishplayer2\" 6 6 999999\n",s->tick,30)
        || s->vision.serializeFireReveals(s->tick)!=saved) return fail("malformed restore was not atomic");
    RuntimeMultiplayerSnapshotIdentity identity;identity.proposalId=1;
    identity.sourceTimelineEpoch=s->config.multiplayerTimelineEpoch;
    identity.snapshotTick=identity.barrierTick=s->tick;identity.snapshotToken="fire-reveal";
    identity.authoritativeStateHash=hash;identity.gameplaySetupFingerprint=s->config.multiplayerSetupHash;
    RuntimeMultiplayerSnapshotMemoryImage image;RuntimeMultiplayerSnapshotStagedState staged;
    if(!exportRuntimeMultiplayerSnapshotV15(*s,identity,image,error)
        || !stageRuntimeMultiplayerSnapshot(*s,image.bytes,image.descriptor,staged,error))
        return fail("snapshot roundtrip: "+error);
    commitRuntimeMultiplayerSnapshotAuthority(*s,std::move(staged));
    if(s->vision.serializeFireReveals(s->tick)!=saved
        || runtimeMultiplayerAuthoritativeStateHash(*s,error)!=hash) return fail("snapshot lost pulse authority");
    s->tick+=150;updateRuntimeVisionState(*s);
    if(!hidden(*s,"SkirmishPlayer2")) return fail("restored pulse did not expire");
    std::cout << "Fire reveal: passed; full snapshot/save, expiration after restore, malformed rejection, authority verified\n";
    return true;
}
