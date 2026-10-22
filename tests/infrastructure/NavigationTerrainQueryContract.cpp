#include "simulation/navigation/terrain/RuntimeNavigationTerrainQuery.h"
#include "world/model/GameWorld.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error Navigation terrain public/transitive headers must not import Windows headers
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TerrainContract {
using Info=RuntimeNavigationTerrainInfo;
using Land=RuntimeTerrainLandType;
using Kind=RuntimeTerrainKind;
std::string infoValue(const Info& v){std::ostringstream s;s<<int(v.movementLandType)<<','<<int(v.movementRampType)<<','<<int(v.kind)<<','<<int(v.authoredLandType)<<','<<v.passable<<','<<v.water<<','<<v.shore<<','<<v.cliff<<','<<v.ramp<<','<<v.bridge<<','<<v.cost;return s.str();}
std::string cellValue(const TerrainCell& c){std::ostringstream s;s<<c.x<<','<<c.y<<','<<c.cellIndex512<<','<<c.tileIndex<<','<<c.subTileIndex<<','<<c.level<<','<<c.iceGrowth<<','<<c.hasOverlay<<','<<c.overlayIndex<<','<<c.overlayFrame<<','<<c.overlayType<<','<<c.overlayImage<<','<<c.overlayIsOre<<','<<c.overlayIsGem<<','<<c.overlayWall.originalIndex<<','<<c.overlayWall.originalFrame<<','<<c.overlayWall.originalType<<','<<c.overlayWall.originalImage<<','<<c.overlayWall.health;return s.str();}
struct Spec {
 bool theater=true,wallRule=false,wallCurrent=false,deck=false,broken=false,placeholder=false,disconnected=false;
 Info base{};std::vector<Land> lands{Land::Clear};unsigned char ramp=0;
};
struct Model final:RuntimeNavigationTerrainQueryPort {
 Spec cfg;bool old=false;mutable std::size_t calls=0,landCalls=0;int depth=0;std::size_t throwAt=0;
 mutable std::vector<std::string> trace;std::function<void(Model&,const std::string&)> callback;
 explicit Model(const Spec& value,bool reference):cfg(value),old(reference){}
 void begin(const char* query,const TerrainCell* cell) const {
  ++calls;trace.push_back(std::to_string(depth)+":"+query+":"+(cell?cellValue(*cell):"none"));
  if(callback)callback(const_cast<Model&>(*this),query);
  if(throwAt==calls)throw std::runtime_error("injected query failure "+std::to_string(calls));
 }
 bool answer(const char* query,const TerrainCell& cell,bool Spec::*member) const {begin(query,&cell);const bool value=cfg.*member;trace.push_back(std::string("return:")+(value?"1":"0"));return value;}
 bool theaterLoaded() const override {begin("theater",nullptr);trace.push_back(std::string("return:")+(cfg.theater?"1":"0"));return cfg.theater;}
 bool overlayWallRuleForCell(const TerrainCell& c) const override{return answer("wall-rule",c,&Spec::wallRule);}
 bool overlayWallUsesCurrentOverlay(const TerrainCell& c) const override{return answer("wall-current",c,&Spec::wallCurrent);}
 bool overlayProvidesBridgeDeck(const TerrainCell& c) const override{return answer("deck",c,&Spec::deck);}
 bool highBridgeFrameIsDisconnected(const TerrainCell& c) const override{return answer("broken",c,&Spec::broken);}
 bool overlayIsBrokenLowBridgePlaceholder(const TerrainCell& c) const override{return answer("placeholder",c,&Spec::placeholder);}
 Info baseTerrainInfoForCell(const TerrainCell& c) const override {begin("base",&c);const Info result=cfg.base;trace.push_back("return:"+infoValue(result));return result;}
 Land subTileLandType(const TerrainCell& c) override {begin("land",&c);const auto index=std::min(landCalls,cfg.lands.size()-1);++landCalls;const Land value=cfg.lands[index];trace.push_back("return:"+std::to_string(int(value)));return value;}
 unsigned char subTileRampType(const TerrainCell& c) override {begin("ramp",&c);const auto value=cfg.ramp;trace.push_back("return:"+std::to_string(int(value)));return value;}
 bool cellHasDisconnectedHighBridgeDeck(const TerrainCell& c) const override{return answer("disconnected",c,&Spec::disconnected);}
};
}
namespace FrozenNavigation {
using RuntimeTerrainInfo=RuntimeNavigationTerrainInfo;
using Port=RuntimeNavigationTerrainQueryPort;
struct TheaterFlag {Port* port;operator bool()const{return port->theaterLoaded();}};
struct RuntimeWindowState {Port* port;TheaterFlag theaterLoaded;explicit RuntimeWindowState(Port& p):port(&p),theaterLoaded{&p}{};struct Cache{Port* port;} renderCache{port};};
struct RuntimeMapRenderContext{};
inline thread_local Port* currentPort=nullptr;
struct Binding {Port* previous=currentPort;explicit Binding(Port& p){currentPort=&p;}~Binding(){currentPort=previous;}};
bool runtimeOverlayWallRuleForCell(RuntimeWindowState& s,const TerrainCell& c){return s.port->overlayWallRuleForCell(c);}
bool runtimeOverlayWallUsesCurrentOverlay(const TerrainCell& c){return currentPort->overlayWallUsesCurrentOverlay(c);}
bool runtimeOverlayProvidesBridgeDeck(const TerrainCell& c){return currentPort->overlayProvidesBridgeDeck(c);}
bool runtimeHighBridgeFrameIsDisconnected(const TerrainCell& c){return currentPort->highBridgeFrameIsDisconnected(c);}
bool runtimeOverlayIsBrokenLowBridgePlaceholder(const TerrainCell& c){return currentPort->overlayIsBrokenLowBridgePlaceholder(c);}
RuntimeTerrainInfo runtimeTerrainInfoForCell(RuntimeWindowState& s,const TerrainCell& c){return s.port->baseTerrainInfoForCell(c);}
RuntimeTerrainLandType resolveRuntimeTerrainSubTileLandType(const TerrainCell& c,const RuntimeMapRenderContext&,RuntimeWindowState::Cache& cache){return cache.port->subTileLandType(c);}
unsigned char resolveRuntimeTerrainSubTileRampType(const TerrainCell& c,const RuntimeMapRenderContext&,RuntimeWindowState::Cache& cache){return cache.port->subTileRampType(c);}
bool runtimeCellHasDisconnectedHighBridgeDeck(RuntimeWindowState& s,const TerrainCell& c){return s.port->cellHasDisconnectedHighBridgeDeck(c);}
struct Algorithm {
static 
RuntimeTerrainInfo runtimeNavigationTerrainInfoForCell(
    RuntimeWindowState& state,
    const TerrainCell& terrain,
    const RuntimeMapRenderContext& metadataContext
) {
    if (terrain.overlayWall.health > 0 && runtimeOverlayWallRuleForCell(state, terrain)
        && runtimeOverlayWallUsesCurrentOverlay(terrain)) {
        RuntimeTerrainInfo blocked;
        blocked.kind = RuntimeTerrainKind::Blocked;
        blocked.passable = false;
        blocked.cost = std::numeric_limits<int>::max() / 4;
        blocked.movementLandType = blocked.authoredLandType = RuntimeTerrainLandType::Wall;
        return blocked;
    }
    // Resolve only ordinary sentinel ground through canonical metadata. Intact
    // bridge overlays retain their previous underlying movement-surface policy.
    if (terrain.tileIndex == 65535 && state.theaterLoaded
        && !runtimeOverlayProvidesBridgeDeck(terrain)
        && !runtimeHighBridgeFrameIsDisconnected(terrain)
        && !runtimeOverlayIsBrokenLowBridgePlaceholder(terrain)) {
        TerrainCell clear = terrain;
        clear.tileIndex = 0;
        clear.subTileIndex = 0;
        return runtimeNavigationTerrainInfoForCell(state, clear, metadataContext);
    }
    RuntimeTerrainInfo info = runtimeTerrainInfoForCell(state, terrain);
    info.movementLandType = resolveRuntimeTerrainSubTileLandType(terrain, metadataContext, state.renderCache);
    info.movementRampType = resolveRuntimeTerrainSubTileRampType(terrain, metadataContext, state.renderCache);
    if (info.bridge && !runtimeOverlayProvidesBridgeDeck(terrain)
        && runtimeCellHasDisconnectedHighBridgeDeck(state, terrain)) {
        info.bridge = false;
        info.authoredLandType = info.movementLandType;
        info.water = info.movementLandType == RuntimeTerrainLandType::Water;
        info.kind = info.water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
        info.cost = info.water ? 18
            : (info.movementLandType == RuntimeTerrainLandType::Road ? 8 : 10);
        if (info.movementLandType == RuntimeTerrainLandType::Rock
            || info.movementLandType == RuntimeTerrainLandType::Wall) {
            info.kind = RuntimeTerrainKind::Blocked;
            info.passable = false;
            info.cost = std::numeric_limits<int>::max() / 4;
        }
    }
    if (info.kind != RuntimeTerrainKind::Shore) {
        return info;
    }

    info.authoredLandType = resolveRuntimeTerrainSubTileLandType(
        terrain,
        metadataContext,
        state.renderCache
    );
    if (info.authoredLandType == RuntimeTerrainLandType::Water) {
        info.kind = RuntimeTerrainKind::Water;
        info.water = true;
        info.shore = false;
        info.cost = 18;
    } else if (info.authoredLandType == RuntimeTerrainLandType::Rough
               || info.authoredLandType == RuntimeTerrainLandType::Clear) {
        // Landward shoreline cells remain ordinary traversable land.
        info.kind = RuntimeTerrainKind::Land;
        info.water = false;
        info.shore = false;
        info.cost = 10;
    }
    return info;
}


};
}
namespace TerrainContract {
Info invoke(Model& model,const TerrainCell& cell){if(!model.old)return queryRuntimeNavigationTerrainInfo(model,cell);FrozenNavigation::Binding binding(model);FrozenNavigation::RuntimeWindowState state(model);return FrozenNavigation::Algorithm::runtimeNavigationTerrainInfoForCell(state,cell,{});}
struct Result {std::string value,error;};
Result run(Model& model,const TerrainCell& cell){try{return {infoValue(invoke(model,cell)),{}};}catch(const std::exception& e){return {{},e.what()};}}
struct Counters {std::size_t checks=0,cases=0,rows=0,dynamic=0,nested=0,exceptions=0,owned=0;} totals;
void need(bool condition,const std::string& reason){++totals.checks;if(!condition)throw std::runtime_error(reason);}
void compare(const Spec& spec,const TerrainCell& cell,std::size_t throwAt=0,const std::function<void(Model&,const std::string&)>& callback={}){
 Model old(spec,true),current(spec,false);old.throwAt=current.throwAt=throwAt;old.callback=current.callback=callback;
 const auto before=cellValue(cell);const auto a=run(old,cell),b=run(current,cell);
 need(a.value==b.value&&a.error==b.error,"all eleven owned fields/exception differ");need(old.trace==current.trace,"complete ordered query/input/return trace differs");need(old.calls==current.calls&&old.landCalls==current.landCalls,"query counts differ");need(cellValue(cell)==before,"input terrain cell changed");need(infoValue(old.cfg.base)==infoValue(current.cfg.base)&&old.cfg.lands==current.cfg.lands&&old.cfg.ramp==current.cfg.ramp,"dynamic final source differs");totals.rows+=old.trace.size();++totals.cases;
}
TerrainCell sample(){TerrainCell c;c.x=17;c.y=23;c.cellIndex512=11793;c.tileIndex=43;c.subTileIndex=5;c.level=4;c.iceGrowth=3;c.hasOverlay=true;c.overlayIndex=98;c.overlayFrame=7;c.overlayType="BRIDGE1";c.overlayImage="BRIDGE";c.overlayIsOre=true;c.overlayWall.originalIndex=4;c.overlayWall.originalFrame=2;c.overlayWall.originalType="GAWALL";c.overlayWall.originalImage="GAWALL";return c;}
Spec sampleSpec(){Spec s;s.base.movementLandType=Land::Ice;s.base.movementRampType=19;s.base.authoredLandType=Land::Beach;s.base.passable=false;s.base.water=true;s.base.shore=true;s.base.cliff=true;s.base.ramp=true;s.base.cost=-73;s.ramp=4;return s;}
}namespace TerrainContract {
void exercise() {
 static_assert(static_cast<int>(Land::Unknown)==-1&&static_cast<int>(Land::Tunnel)==10);
 static_assert(static_cast<int>(Kind::Land)==0&&static_cast<int>(Kind::Blocked)==6);
 static_assert(std::is_same_v<decltype(Info{}.authoredLandType),Land>);
 const std::array<Land,12> lands{Land::Unknown,Land::Clear,Land::Road,Land::Water,Land::Rock,Land::Wall,Land::Tiberium,Land::Beach,Land::Rough,Land::Ice,Land::Railroad,Land::Tunnel};
 const std::array<int,6> tiles{std::numeric_limits<int>::min(),-1,0,65534,65535,std::numeric_limits<int>::max()};
 for(int kind=0;kind!=7;++kind)for(const auto first:lands)for(const auto second:lands){
  auto s=sampleSpec();s.base.kind=static_cast<Kind>(kind);s.lands={first,second};compare(s,sample());
 }
 for(unsigned mask=0;mask!=128;++mask)for(const int health:std::array<int,3>{-1,0,1})for(const int tile:std::array<int,2>{43,65535}){
  auto s=sampleSpec();s.theater=(mask&1)!=0;s.wallRule=(mask&2)!=0;s.wallCurrent=(mask&4)!=0;s.deck=(mask&8)!=0;
  s.broken=(mask&16)!=0;s.placeholder=(mask&32)!=0;s.disconnected=(mask&64)!=0;s.base.bridge=true;s.base.kind=Kind::Shore;s.lands={Land::Road,Land::Water};
  auto cell=sample();cell.overlayWall.health=health;cell.tileIndex=tile;compare(s,cell);
 }
 for(const int tile:tiles)for(const int sub:std::array<int,4>{-1,0,1,std::numeric_limits<int>::max()})for(const auto ramp:std::array<unsigned char,4>{0,1,20,255}){
  auto cell=sample();cell.tileIndex=tile;cell.subTileIndex=sub;auto s=sampleSpec();s.ramp=ramp;compare(s,cell);
 }
 for(unsigned mask=0;mask!=32;++mask)for(const int cost:std::array<int,3>{std::numeric_limits<int>::min(),0,std::numeric_limits<int>::max()}){
  auto s=sampleSpec();s.base.passable=(mask&1)!=0;s.base.water=(mask&2)!=0;s.base.shore=(mask&4)!=0;s.base.cliff=(mask&8)!=0;s.base.ramp=(mask&16)!=0;s.base.cost=cost;
  compare(s,sample());
 }
 // Explicit boundary checks supplement the independent full-body differential.
 {
  auto s=sampleSpec();s.wallRule=s.wallCurrent=true;auto cell=sample();cell.overlayWall.health=1;
  Model m(s,false);const auto value=invoke(m,cell);
  need(m.calls==2&&m.landCalls==0,"wall short circuit may not query metadata");
  need(value.kind==Kind::Blocked&&!value.passable&&value.cost==std::numeric_limits<int>::max()/4&&value.movementLandType==Land::Wall&&value.authoredLandType==Land::Wall,"wall domain result");
 }
 {
  auto cell=sample();cell.tileIndex=65535;auto normalized=cell;normalized.tileIndex=normalized.subTileIndex=0;
  auto s=sampleSpec();Model m(s,false);static_cast<void>(invoke(m,cell));
  const auto marker=std::string("0:base:")+cellValue(normalized);
  need(std::find(m.trace.begin(),m.trace.end(),marker)!=m.trace.end(),"sentinel must copy every field except the original two canonical indices");
  need(m.calls==7,"sentinel must retain four lazy guards then original base/land/ramp calls");
 }
 {
  auto s=sampleSpec();s.base.kind=Kind::Shore;s.lands={Land::Road,Land::Water};Model m(s,false);const auto value=invoke(m,sample());
  need(m.landCalls==2&&value.movementLandType==Land::Road&&value.authoredLandType==Land::Water&&value.kind==Kind::Water,"shore uses second fresh land read without replacing movement land");
 }
 for(const std::string& query:std::array<std::string,5>{"theater","base","land","ramp","deck"}){
  auto s=sampleSpec();s.base.kind=Kind::Shore;s.base.bridge=true;s.disconnected=true;auto cell=sample();cell.tileIndex=65535;
  compare(s,cell,0,[query](Model& m,const std::string& at){if(at==query){m.cfg.base.cost=91;m.cfg.ramp=255;m.cfg.lands={Land::Road,Land::Water};m.cfg.deck=!m.cfg.deck;}});++totals.dynamic;
 }
 {
  auto s=sampleSpec();s.base.kind=Kind::Shore;s.lands={Land::Road,Land::Beach,Land::Water};
  compare(s,sample(),0,[](Model& m,const std::string& at){if(at=="land"&&m.depth==0){++m.depth;auto inner=sample();inner.x=-2048;inner.tileIndex=0;const auto result=invoke(m,inner);m.trace.push_back("nested-result:"+infoValue(result));--m.depth;}});++totals.nested;
 }
 {
  auto s=sampleSpec();s.base.kind=Kind::Shore;
  compare(s,sample(),0,[](Model& m,const std::string& at){if(at=="base"){const auto old=m.cfg;Spec replacement;replacement.base=old.base;replacement.base.authoredLandType=Land::Tunnel;replacement.base.cost=-917;replacement.lands={Land::Water,Land::Clear};replacement.ramp=17;m.cfg=std::move(replacement);}});++totals.dynamic;
 }
 // Every reachable primitive boundary: the thrown prefix and all later non-calls agree.
 for(unsigned shape=0;shape!=4;++shape){auto s=sampleSpec();auto cell=sample();
  if(shape==0){cell.tileIndex=65535;s.base.kind=Kind::Shore;}
  if(shape==1){s.base.bridge=true;s.disconnected=true;s.lands={Land::Rock};}
  if(shape==2){cell.overlayWall.health=1;s.wallRule=s.wallCurrent=true;}
  if(shape==3){s.base.kind=Kind::Shore;s.lands={Land::Clear,Land::Water};}
  Model count(s,false);static_cast<void>(invoke(count,cell));
  for(std::size_t stop=1;stop<=count.calls;++stop){compare(s,cell,stop);++totals.exceptions;}
 }
 // Returned values own all eleven fields; no borrow survives model/cell changes.
 for(bool old:std::array<bool,2>{true,false}){auto cell=sample();Model m(sampleSpec(),old);const auto owned=invoke(m,cell);const auto snapshot=infoValue(owned);
  m.cfg.base={};m.cfg.lands={Land::Wall};m.cfg.ramp=0;cell=TerrainCell{};
  need(infoValue(owned)==snapshot,"owned result detached from source/root/input mutation");++totals.owned;
 }
}
}
int main(){try{TerrainContract::exercise();const auto& c=TerrainContract::totals;
 std::cout<<"Navigation terrain query contract passed: checks="<<c.checks<<"; differentialCases="<<c.cases<<"; orderedTraceRows="<<c.rows<<"; dynamic="<<c.dynamic<<"; nested="<<c.nested<<"; exceptions="<<c.exceptions<<"; owned="<<c.owned<<"; fields=11; windowsHeader=absent\n";return 0;
 }catch(const std::exception& e){std::cerr<<"Navigation terrain query contract failed: "<<e.what()<<'\n';return 1;}}