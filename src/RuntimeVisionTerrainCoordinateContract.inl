// Public RuntimeVision API oracle, shared by the existing vision smoke and the
// actual runtime command contract. No private grid/layout API is exported.
#pragma once
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace RuntimeVisionTerrainCoordinateContractDetail {
inline bool equalVision(const RuntimeVisionSystem& actual,const RuntimeVisionSystem& expected,
    int tick,std::string& error) {
    const auto owners=actual.observerOwners();
    if(owners!=expected.observerOwners()) { error="observer owners/order"; return false; }
    for(const auto& owner:owners) {
        RuntimeVisionGridView left,right;
        if(!actual.gridViewFor(owner,left) || !expected.gridViewFor(owner,right)
            || left.cellCount!=right.cellCount || left.revision!=right.revision
            || left.layoutEpoch!=right.layoutEpoch || left.available()!=right.available()) {
            error="grid shape/revision/layout epoch owner="+owner; return false;
        }
        const auto equalBytes=[&](const std::uint8_t* a,const std::uint8_t* b) {
            return (a==nullptr)==(b==nullptr) && (!a || std::memcmp(a,b,left.cellCount)==0);
        };
        if(!equalBytes(left.validCells,right.validCells)
            || !equalBytes(left.visibleCells,right.visibleCells)
            || !equalBytes(left.exploredCells,right.exploredCells)
            || !equalBytes(left.gapShroudedCells,right.gapShroudedCells)) {
            error="complete packed grid owner="+owner; return false;
        }
        const auto* a=actual.statsFor(owner); const auto* b=expected.statsFor(owner);
        if(!a || !b || a->validCells!=b->validCells || a->visibleCells!=b->visibleCells
            || a->exploredCells!=b->exploredCells || a->visionSources!=b->visionSources
            || a->spySatSources!=b->spySatSources || a->gapGeneratorSources!=b->gapGeneratorSources
            || a->gapShroudedCells!=b->gapShroudedCells || a->lastUpdateTick!=b->lastUpdateTick
            || a->revision!=b->revision || actual.exploredCellIndices(owner)!=expected.exploredCellIndices(owner)) {
            error="complete stats/exploration owner="+owner; return false;
        }
    }
    if(actual.serializeFireReveals(tick)!=expected.serializeFireReveals(tick)) {
        error="fire reveal serialization"; return false;
    }
    return true;
}

// Test-only spelling of the pre-lease scalar. Production still calls the exact
// original loop in refreshTerrainLayout on every cold/fallback path.
inline std::uint64_t originalHash(const GameWorld& world) {
    std::uint64_t hash=1469598103934665603ULL;
    const auto mix=[&](std::uint64_t value) { hash^=value; hash*=1099511628211ULL; };
    mix(world.getTerrainCells().size());
    for(const auto& cell:world.getTerrainCells()) {
        const int index=cell.x<0 || cell.y<0 || cell.x>=512 || cell.y>=512 ? -1 : cell.y*512+cell.x;
        mix(static_cast<std::uint64_t>(index+1));
    }
    return hash;
}

inline bool primitive(std::string& error) {
    RuntimeVisionTerrainCoordinateReferenceScope optimized(false);
    const auto fail=[&](const std::string& why) { error="vision terrain-coordinate lease: "+why; return false; };
    GameWorld world;
    for(int y=0;y<24;++y) for(int x=0;x<24;++x) {
        TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=y*512+x;
        world.accessTerrainCells().push_back(cell);
    }
    IniFile ini;
    RulesDatabase rules;
    std::string rulesText="[InfantryTypes]\n0=SCOUT\n[BuildingTypes]\n0=SPY\n1=GAP\n"
        "[SCOUT]\nSight=3\nStrength=100\n[SPY]\nSight=1\nSpySat=yes\nStrength=100\n"
        "[GAP]\nSight=1\nGapGenerator=yes\nGapRadiusInCells=3\nPowered=yes\nStrength=100\n";
    if(!ini.loadFromString(rulesText) || !rules.loadFromIni(ini)) return fail("synthetic rules setup");
    WorldObjectInstance scout;
    scout.instanceId=1; scout.kind=WorldObjectKind::Infantry; scout.type="SCOUT";
    scout.owner="Alpha"; scout.x=5; scout.y=5; scout.health=100; scout.alive=true;
    world.accessObjects().push_back(scout);
    auto gap=scout; gap.instanceId=2; gap.kind=WorldObjectKind::Structure; gap.type="GAP";
    gap.owner="Beta"; gap.x=15; gap.y=15; gap.poweredOperational=true;
    world.accessObjects().push_back(gap);
    auto spy=gap; spy.instanceId=3; spy.type="SPY"; spy.owner="Alpha"; spy.alive=false;
    world.accessObjects().push_back(spy);
    RuntimeVisionSystem actual,expected;
    const std::vector<RuntimeVisionObserverDefinition> observers{{"Alpha",{"Alpha"}},{"Beta",{"Beta"}}};
    const int tick=12;
    const auto equal=[&](const std::string& stage) {
        const auto before=world.deterministicSimulationStateHash();
        const bool a=actual.update(world,&rules,observers,tick);
        bool b=false;
        { RuntimeVisionTerrainCoordinateReferenceScope reference(true); b=expected.update(world,&rules,observers,tick); }
        std::string detail;
        if(a!=b || !equalVision(actual,expected,tick,detail)
            || before!=world.deterministicSimulationStateHash()) return fail(stage+": "+detail);
        return true;
    };
    {
        RuntimeVisionTerrainCoordinateLeaseScope lease(world);
        if(!equal("cold") || !equal("repeat")) return false;
        world.accessObjects()[0].x=9;
        world.accessObjects()[0].hasRenderPosition=true;
        world.accessObjects()[0].renderX=9.5; world.accessObjects()[0].renderY=6.5;
        actual.revealArea("Alpha",22,22,1); expected.revealArea("Alpha",22,22,1);
        actual.resetShroudArea("Beta",15,15,2); expected.resetShroudArea("Beta",15,15,2);
        if(!equal("same-tick object/script")) return false;
        auto& cell=world.accessTerrainCells()[0];
        cell.level=4; cell.iceGrowth=7; cell.hasOverlay=true; cell.overlayType="ORE"; cell.overlayFrame=2;
        world.accessObjects()[1].poweredOperational=false;
        if(!equal("content-only overlay/level/ice and Gap power")) return false;
        rulesText.replace(rulesText.find("Sight=3"),std::strlen("Sight=3"),"Sight=5");
        if(!ini.loadFromString(rulesText) || !rules.loadFromIni(ini) || !equal("same-tick rules reload")) return false;
        WeaponType weapon; weapon.revealOnFire=true;
        if(actual.notifyWeaponFired(world.getObjects()[0],world.getObjects()[1],weapon,tick,15)
            !=expected.notifyWeaponFired(world.getObjects()[0],world.getObjects()[1],weapon,tick,15)
            || !equal("fire reveal")) return false;
        actual.reset(); expected.reset();
        if(!equal("vision reset must rebuild despite cached scalar")) return false;
        RuntimeVisionSystem blankActual,blankExpected;
        actual.swap(blankActual); expected.swap(blankExpected);
        if(!equal("vision swap must rebuild despite cached scalar")) return false;
        if(lease.originalHashes()!=1 || lease.reuses()<7) return fail("cold hash/reuse witness absent");
    }
    // Disabled/reference/foreign nesting masks parents instead of borrowing.
    {
        RuntimeVisionTerrainCoordinateLeaseScope outer(world);
        if(!equal("outer cold")) return false;
        const auto prior=outer.reuses();
        {
            RuntimeVisionTerrainCoordinateLeaseScope disabled(world,false);
            if(!equal("disabled masks outer") || disabled.originalHashes()!=0 || disabled.reuses()!=0)
                return fail("disabled nested scope used a lease");
        }
        {
            RuntimeVisionTerrainCoordinateReferenceScope reference(true);
            RuntimeVisionTerrainCoordinateLeaseScope disabled(world);
            if(!equal("reference-created masks outer") || disabled.originalHashes()!=0 || disabled.reuses()!=0)
                return fail("reference-created nested scope used a lease");
        }
        GameWorld foreign=world;
        {
            RuntimeVisionTerrainCoordinateLeaseScope other(foreign);
            if(!equal("foreign active scope falls back")) return false;
            if(other.originalHashes()!=0 || other.reuses()!=0) return fail("foreign scope borrowed world");
            invalidateRuntimeVisionTerrainCoordinateLeases(foreign);
            if(!outer.enabled()) return fail("foreign invalidation disabled unrelated outer");
        }
        if(outer.reuses()!=prior || !equal("outer restored after foreign") || outer.reuses()!=prior+1)
            return fail("nested masking/restoration counter mismatch");
    }
    // Explicit same-address edits invalidate every matching ancestor even when
    // the current TLS head belongs to a different world or is disabled.
    {
        RuntimeVisionTerrainCoordinateLeaseScope outer(world);
        if(!equal("mutation outer warm")) return false;
        {
            RuntimeVisionTerrainCoordinateLeaseScope inner(world);
            if(!equal("mutation inner warm")) return false;
            GameWorld foreign=world;
            RuntimeVisionTerrainCoordinateLeaseScope disabledForeign(foreign,false);
            inner.invalidate();
            if(outer.enabled() || inner.enabled()) return fail("same-owner ancestor invalidation incomplete");
            world.accessTerrainCells()[0].x=-1;
            if(!equal("negative coordinate after invalidate")) return false;
            world.accessTerrainCells()[0].x=std::numeric_limits<int>::max();
            if(!equal("original out-of-range signature collision")) return false;
            std::reverse(world.accessTerrainCells().begin(),world.accessTerrainCells().end());
            if(!equal("same-size reorder after invalidate")) return false;
        }
        const auto oldReuses=outer.reuses();
        if(!equal("outer cannot resurrect") || outer.reuses()!=oldReuses)
            return fail("invalidated outer reused stale scalar");
    }
    {
        RuntimeVisionTerrainCoordinateLeaseScope lease(world);
        if(!equal("shape warm")) return false;
        auto duplicate=world.getTerrainCells()[1];
        world.accessTerrainCells().push_back(duplicate); // Missing-hook defensive shape guard.
        if(!equal("count/storage guard") || lease.enabled()) return fail("shape mismatch failed to disable lease");
        world.accessTerrainCells().pop_back();
        if(!equal("old count restored must stay invalid")) return false;
    }
    // Both sides of a commit/rollback are invalidated before storage exchange.
    {
        GameWorld foreign=world;
        foreign.accessTerrainCells()[1].y=511;
        RuntimeVisionTerrainCoordinateLeaseScope live(world);
        if(!equal("swap live warm")) return false;
        {
            RuntimeVisionTerrainCoordinateLeaseScope scratch(foreign);
            std::size_t calls=0;
            auto read=[&](const GameWorld& value) {
                return runtimeVisionTerrainCoordinateSignature(value,[&]{++calls;return originalHash(value);});
            };
            if(read(foreign)!=originalHash(foreign) || read(foreign)!=originalHash(foreign) || calls!=1)
                return fail("scratch cold/hot exact scalar");
            invalidateRuntimeVisionTerrainCoordinateLeases(world);
            invalidateRuntimeVisionTerrainCoordinateLeases(foreign);
            std::swap(world,foreign);
            if(live.enabled() || scratch.enabled() || !equal("world commit replacement")) return false;
            invalidateRuntimeVisionTerrainCoordinateLeases(world);
            invalidateRuntimeVisionTerrainCoordinateLeases(foreign);
            std::swap(world,foreign);
            if(!equal("world rollback replacement")) return false;
        }
        if(!equal("post-rollback outer disabled")) return false;
    }
    // Empty/duplicate/negative/extreme coordinate inputs retain exact old FNV.
    for(int kind=0;kind<4;++kind) {
        GameWorld edge;
        if(kind) for(int n=0;n<5;++n) {
            TerrainCell cell; cell.x=kind==1?0:kind==2?-1:std::numeric_limits<int>::max();
            cell.y=kind==1?0:kind==2?std::numeric_limits<int>::min():512;
            edge.accessTerrainCells().push_back(cell);
        }
        std::size_t calls=0;
        RuntimeVisionTerrainCoordinateLeaseScope lease(edge);
        for(int n=0;n<3;++n) {
            const auto value=runtimeVisionTerrainCoordinateSignature(edge,[&]{++calls;return originalHash(edge);});
            if(value!=originalHash(edge)) return fail("raw legacy hash mismatch kind="+std::to_string(kind));
        }
        if(calls!=1 || lease.reuses()!=2) return fail("edge scalar cache witness");
    }
    error.clear(); return true;
}
} // namespace RuntimeVisionTerrainCoordinateContractDetail
