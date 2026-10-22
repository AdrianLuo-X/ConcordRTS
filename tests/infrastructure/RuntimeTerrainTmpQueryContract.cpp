#include "assets/terrain/RuntimeTerrainTmpQuery.h"
#include "data/art/TheaterDatabase.h"
#include "world/model/GameWorld.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error Pure TMP query public headers must not import Windows headers
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fixtures/RuntimeTmpCodecOld.inl"
#include "fixtures/RuntimeTerrainTmpQueryOld-r2.inl"

namespace QueryContract {
using Bytes=std::vector<unsigned char>;
using Tile=RuntimeTmpTileFile;
using Image=RuntimeTmpSubTileImage;
struct Counts{std::size_t checks=0,cases=0,fields=0,traceRows=0,exceptions=0;} count;
std::string currentCase;
void need(bool value,const std::string& why){++count.checks;if(!value)throw std::runtime_error(currentCase+": "+why);}
bool sameImage(const Image& a,const Image& b,bool assertions){bool result=true;
 const auto field=[&](bool equal,const char* name){result=result&&equal;if(assertions){++count.fields;need(equal,std::string("image field ")+name);}};
#define IMAGE_FIELD(name) field(a.name==b.name,#name)
 IMAGE_FIELD(present);IMAGE_FIELD(hasTerrainType);IMAGE_FIELD(terrainType);IMAGE_FIELD(rampType);IMAGE_FIELD(hasRadarColors);
 field(a.radarLeft.r==b.radarLeft.r,"radarLeft.r");field(a.radarLeft.g==b.radarLeft.g,"radarLeft.g");field(a.radarLeft.b==b.radarLeft.b,"radarLeft.b");
 field(a.radarRight.r==b.radarRight.r,"radarRight.r");field(a.radarRight.g==b.radarRight.g,"radarRight.g");field(a.radarRight.b==b.radarRight.b,"radarRight.b");
 IMAGE_FIELD(offsetX);IMAGE_FIELD(offsetY);IMAGE_FIELD(width);IMAGE_FIELD(height);IMAGE_FIELD(globalWidth);IMAGE_FIELD(globalHeight);IMAGE_FIELD(pixels);
 IMAGE_FIELD(packedPixelPaletteHash);IMAGE_FIELD(packedPixels);IMAGE_FIELD(resourcePixelNonZeroCount);
 IMAGE_FIELD(baseOffsetX);IMAGE_FIELD(baseOffsetY);IMAGE_FIELD(baseWidth);IMAGE_FIELD(baseHeight);IMAGE_FIELD(basePixels);IMAGE_FIELD(resourceBaseNonZeroCount);IMAGE_FIELD(baseZData);IMAGE_FIELD(hasBaseZData);
 IMAGE_FIELD(hasExtra);IMAGE_FIELD(extraOffsetX);IMAGE_FIELD(extraOffsetY);IMAGE_FIELD(extraWidth);IMAGE_FIELD(extraHeight);IMAGE_FIELD(extraPixels);IMAGE_FIELD(resourceExtraNonZeroCount);IMAGE_FIELD(extraZData);IMAGE_FIELD(hasExtraZData);
#undef IMAGE_FIELD
 return result;
}
bool sameTile(const Tile& a,const Tile& b,bool assertions){bool result=true;
 const auto field=[&](bool equal,const char* name){result=result&&equal;if(assertions){++count.fields;need(equal,std::string("tile field ")+name);}};
#define TILE_FIELD(name) field(a.name==b.name,#name)
 TILE_FIELD(loadAttempted);TILE_FIELD(loaded);TILE_FIELD(blockWidth);TILE_FIELD(blockHeight);TILE_FIELD(errorMessage);TILE_FIELD(gridWidth);TILE_FIELD(gridHeight);
 field(a.images.size()==b.images.size(),"images.size");
 for(std::size_t i=0;i<std::min(a.images.size(),b.images.size());++i){const bool equal=sameImage(a.images[i],b.images[i],assertions);result=result&&equal;}
#undef TILE_FIELD
 return result;
}
void sameCache(const RuntimeTerrainTmpCache& a,const RuntimeTerrainTmpCache& b){
 need(a.lastTmpTileLoadError==b.lastTmpTileLoadError,"last error");
 need(a.terrainTilePathCacheRoot==b.terrainTilePathCacheRoot,"locator root");
 need(a.terrainTilePathByIndex==b.terrainTilePathByIndex,"positive locators");
 need(a.missingTerrainTilePathByIndex==b.missingTerrainTilePathByIndex,"negative locators");
 need(a.tmpTilesByPath.size()==b.tmpTilesByPath.size(),"complete TMP cache key count");
 for(const auto& [key,tile]:a.tmpTilesByPath){const auto it=b.tmpTilesByPath.find(key);need(it!=b.tmpTilesByPath.end(),"complete TMP cache key");if(it!=b.tmpTilesByPath.end())need(sameTile(tile,it->second,true),"complete cached TMP value");}
}
void put16(Bytes& b,std::size_t p,unsigned int v){b.at(p)=static_cast<unsigned char>(v);b.at(p+1)=static_cast<unsigned char>(v>>8);}
void put32(Bytes& b,std::size_t p,unsigned int v){for(unsigned int i=0;i<4;++i)b.at(p+i)=static_cast<unsigned char>(v>>(i*8));}
Bytes ordinary(unsigned char terrain=9,unsigned char ramp=247,bool empty=false){
 Bytes b(empty?20U:76U,0);put32(b,0,1);put32(b,4,1);put32(b,8,4);put32(b,12,2);
 if(!empty){put32(b,16,20);put32(b,36,56);b[61]=terrain;b[62]=ramp;for(std::size_t i=0;i<4;++i)b[72+i]=static_cast<unsigned char>(i+1);}
 return b;
}
Bytes rawFormat(){Bytes b(36,0);put16(b,2,2);put16(b,4,1);put16(b,6,1);put32(b,8,0xffff8001U);put32(b,12,0x00010002U);put32(b,28,32);put16(b,32,4);b[34]=1;b[35]=2;return b;}
Tile seed(unsigned char terrain,unsigned char ramp){Tile t;t.loadAttempted=t.loaded=true;t.gridWidth=t.gridHeight=1;t.blockWidth=4;t.blockHeight=2;t.errorMessage="seed-warning";Image i;
 i.present=i.hasTerrainType=i.hasRadarColors=true;i.terrainType=terrain;i.rampType=ramp;i.radarLeft={7,8,9};i.radarRight={10,11,12};
 i.offsetX=-7;i.offsetY=8;i.width=2;i.height=3;i.globalWidth=41;i.globalHeight=43;i.pixels={1,0,3,0,5,6};
 i.packedPixelPaletteHash=0x123456789abcdef0ULL;i.packedPixels={0x10203040,0xffffffff};i.resourcePixelNonZeroCount=71;
 i.baseOffsetX=13;i.baseOffsetY=-14;i.baseWidth=2;i.baseHeight=2;i.basePixels={9,8,7,6};i.resourceBaseNonZeroCount=73;i.baseZData={5,4,3,2};i.hasBaseZData=true;
 i.hasExtra=true;i.extraOffsetX=-3;i.extraOffsetY=4;i.extraWidth=1;i.extraHeight=2;i.extraPixels={11,12};i.resourceExtraNonZeroCount=79;i.extraZData={13,14};i.hasExtraZData=true;t.images={i};return t;
}
struct Response{bool ok=true;Bytes bytes=ordinary();std::string error="resource-warning";std::string origin="fake.mix:test";bool throws=false;};
struct Source final:RuntimeTerrainTmpByteSource{
 std::vector<std::string>* trace=nullptr;std::map<std::string,Response> loose,named;Response missing{false,{},"missing resource","",false};
 std::string cachedRoot="fallback-assets";bool mixes=false;std::function<void()> callback;std::vector<std::string> lastRoots;std::string lastAsset;
 void notify(){auto call=std::move(callback);callback={};if(call)call();}
 Response response(const std::map<std::string,Response>& values,const std::string& key){const auto it=values.find(key);return it==values.end()?missing:it->second;}
 bool readLooseBytes(const std::string& path,Bytes& bytes,std::string& error)override{
  trace->push_back("loose:"+path);const auto r=response(loose,path);notify();if(r.throws)throw std::runtime_error("source-loose-exception");bytes=r.bytes;error=r.error;return r.ok;
 }
 bool readResourceBytesByName(const std::vector<std::string>& roots,const std::string& asset,const std::string& name,Bytes& bytes,std::string& origin,std::string& error)override{
  std::string row="named:"+name+"|asset="+asset;for(const auto& root:roots)row+="|root="+root;trace->push_back(row);lastRoots=roots;lastAsset=asset;const auto r=response(named,name);notify();if(r.throws)throw std::runtime_error("source-named-exception");bytes=r.bytes;origin=r.origin;error=r.error;return r.ok;
 }
 std::string cachedMixExportsRoot()override{trace->push_back("cached-root:"+cachedRoot);return cachedRoot;}
 bool assetRootHasMixArchives(const std::string& asset)override{trace->push_back("has-mix:"+asset+":"+(mixes?"1":"0"));return mixes;}
};
struct Memo final:RuntimeTerrainTmpMemoPort{
 struct Entry{Tile* file=nullptr;bool updates=false;};std::map<int,Entry> entries;std::vector<std::string>* trace=nullptr;bool enabled=false,throwFind=false,throwRemember=false;
 bool find(int tile,Tile*& file,bool& updates)override{trace->push_back("memo-find:"+std::to_string(tile));if(throwFind)throw std::runtime_error("memo-find-exception");if(!enabled)return false;const auto it=entries.find(tile);if(it==entries.end())return false;file=it->second.file;updates=it->second.updates;return true;}
 void remember(int tile,Tile* file,bool updates)override{trace->push_back("memo-remember:"+std::to_string(tile)+":"+(file?"file":"null")+":"+(updates?"1":"0"));if(throwRemember)throw std::runtime_error("memo-remember-exception");if(enabled)entries[tile]={file,updates};}
};
enum class Op{Metadata,Land,Ramp,Grid,Tile};
struct Outcome{bool present=false;std::optional<Image> image;std::optional<Tile> tile;int value=0,width=0,height=0;std::string error;};
struct Pair {
 RuntimeTerrainTmpCache oldCache,newCache;std::vector<std::string> oldTrace,newTrace;Source oldSource,newSource;Memo oldMemo,newMemo;
 std::string tileRoot,assetRoot,structureRoot;TheaterDatabase* theater=nullptr;
 Pair(){oldSource.trace=&oldTrace;newSource.trace=&newTrace;oldMemo.trace=&oldTrace;newMemo.trace=&newTrace;}
 void sources(const Source& s){oldSource=s;newSource=s;oldSource.trace=&oldTrace;newSource.trace=&newTrace;}
 RuntimeTerrainTmpContext context()const{return{theater,tileRoot,assetRoot,structureRoot};}
 Outcome invoke(bool old,Op op,int tile,int sub){
  Outcome result;auto c=context();auto& cache=old?oldCache:newCache;auto& source=old?oldSource:newSource;auto& memo=old?oldMemo:newMemo;
  frozenTmpQuery::PortBorrow borrow(source,memo);TerrainCell cell;cell.tileIndex=tile;cell.subTileIndex=sub;
  try{switch(op){
   case Op::Metadata:{const auto* p=old?frozenTmpQuery::resolveRuntimeTerrainMetadata(tile,sub,c,cache):resolveRuntimeTerrainMetadata(tile,sub,c,cache,source,memo);result.present=p!=nullptr;if(p)result.image=*p;break;}
   case Op::Land:result.value=static_cast<int>(old?frozenTmpQuery::resolveRuntimeTerrainSubTileLandType(cell,c,cache):resolveRuntimeTerrainSubTileLandType(tile,sub,c,cache,source,memo));break;
   case Op::Ramp:result.value=old?frozenTmpQuery::resolveRuntimeTerrainSubTileRampType(cell,c,cache):resolveRuntimeTerrainSubTileRampType(tile,sub,c,cache,source,memo);break;
   case Op::Grid:result.present=old?frozenTmpQuery::resolveRuntimeTerrainTileGridShape(cell,c,cache,result.width,result.height):resolveRuntimeTerrainTmpTileGridShape(tile,sub,c,cache,source,memo,result.width,result.height);break;
   case Op::Tile:{auto* p=old?frozenTmpQuery::loadRuntimeTerrainTileForIndex(tile,c,cache):loadRuntimeTerrainTileForIndex(tile,c,cache,source,memo);result.present=p!=nullptr;if(p)result.tile=*p;break;}
  }}catch(const std::exception& e){result.error=e.what();}
  return result;
 }
 Outcome step(const std::string& name,Op op=Op::Metadata,int tile=0,int sub=0){
  currentCase=name;++count.cases;const auto a=invoke(true,op,tile,sub);const auto b=invoke(false,op,tile,sub);
  need(a.present==b.present&&a.value==b.value&&a.width==b.width&&a.height==b.height&&a.error==b.error,"complete query result");
  need(a.image.has_value()==b.image.has_value(),"image optional");if(a.image&&b.image)need(sameImage(*a.image,*b.image,true),"returned full image");
  need(a.tile.has_value()==b.tile.has_value(),"tile optional");if(a.tile&&b.tile)need(sameTile(*a.tile,*b.tile,true),"returned full tile");
  sameCache(oldCache,newCache);need(oldTrace==newTrace,"complete ordered source/memo trace");count.traceRows+=newTrace.size();if(!b.error.empty())++count.exceptions;
  return b;
 }
 void plant(unsigned char terrain=9,unsigned char ramp=247){
  const auto key=runtimeTerrainTileLookupCacheRoot(context());oldCache.terrainTilePathCacheRoot=newCache.terrainTilePathCacheRoot=key;
  oldCache.terrainTilePathByIndex[0]=newCache.terrainTilePathByIndex[0]="seed";
  oldCache.tmpTilesByPath["seed"]=newCache.tmpTilesByPath["seed"]=seed(terrain,ramp);
 }
};
void freshFile(const std::filesystem::path& path,const Bytes& bytes){
 need(!std::filesystem::exists(path),"refuse fixture overwrite");std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);need(static_cast<bool>(out),"fixture open");out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));need(static_cast<bool>(out),"fixture write");
}
void run(const std::filesystem::path& temp){
 need(!std::filesystem::exists(temp),"temp-root must be new");std::filesystem::create_directories(temp);
 IniFile ini;need(ini.loadFromString("[TileSet0000]\nFileName=test\nTilesInSet=2\n"),"real INI");TheaterDatabase temperate,snow;
 need(temperate.loadFromIni(ini,"temperat.ini"),"real temperate database");need(snow.loadFromIni(ini,"snow.ini"),"real snow database");
 const std::string loose=(temp/"outside").string(),assets=(temp/"assets").string(),inside=(temp/"assets"/"tiles").string(),structure=(temp/"structures").string();
 freshFile(std::filesystem::path(loose)/"test01.tem",ordinary());freshFile(std::filesystem::path(inside)/"test01.tem",ordinary());
 const auto loosePath=temperate.resolveTileIndex(0,loose).resolvedPath;const auto insidePath=temperate.resolveTileIndex(0,inside).resolvedPath;
 need(!loosePath.empty()&&!insidePath.empty(),"real locator sees private fixture");
 const std::string name=temperate.resolveTileIndex(0).candidateFileNames.at(0);const std::string name2=temperate.resolveTileIndex(1).candidateFileNames.at(0);
 const auto make=[&](Pair& p){p.theater=&temperate;p.assetRoot=assets;p.structureRoot=structure;Source s;s.named[name]=Response{};s.named[name2]=Response{};p.sources(s);};
 {Pair p;make(p);p.theater=nullptr;p.step("null theater");need(p.newTrace.empty(),"null theater zero ports");p.theater=&temperate;p.assetRoot.clear();p.step("both roots empty");need(p.newTrace.empty(),"empty roots zero ports");p.assetRoot=assets;for(const auto& [tile,sub]:std::array<std::pair<int,int>,3>{{{-1,0},{65535,0},{0,-1}}})p.step("invalid metadata gate",Op::Metadata,tile,sub);need(p.newTrace.empty(),"invalid metadata zero ports");}
 constexpr std::array<int,16> lands{{0,8,8,8,8,10,9,3,3,2,6,1,1,0,7,3}};
 for(unsigned int i=0;i<256;++i){Pair p;make(p);p.plant(static_cast<unsigned char>(i),static_cast<unsigned char>(255-i));p.step("all terrain bytes metadata");auto land=p.step("all terrain bytes land",Op::Land);need(land.value==(i<lands.size()?lands[i]:-1),"independent 16-land golden");auto ramp=p.step("all ramp bytes",Op::Ramp);need(ramp.value==static_cast<int>(255-i),"all 256 raw ramp golden");p.step("valid authored grid",Op::Grid);}
 {Pair p;make(p);p.plant();p.step("subtile out of range",Op::Metadata,0,1);p.oldCache.tmpTilesByPath["seed"].images[0].present=p.newCache.tmpTilesByPath["seed"].images[0].present=false;need(!p.step("authored empty").present,"empty unavailable");p.oldCache.tmpTilesByPath["seed"].images[0].present=p.newCache.tmpTilesByPath["seed"].images[0].present=true;p.oldCache.tmpTilesByPath["seed"].images[0].hasTerrainType=p.newCache.tmpTilesByPath["seed"].images[0].hasTerrainType=false;need(!p.step("raw pixel no metadata").present,"raw lacks metadata");}
 {Pair p;make(p);p.oldMemo.enabled=p.newMemo.enabled=true;p.step("cold MIX");need(p.newSource.lastRoots==std::vector<std::string>({"",assets,structure}),"all three ordered roots");p.oldCache.lastTmpTileLoadError=p.newCache.lastTmpTileLoadError="changed";p.step("memo named hit error replay");need(p.newCache.lastTmpTileLoadError=="resource-warning","MIX warning restored");}
 {Pair p;make(p);p.tileRoot=loose;Source s;s.loose[loosePath]=Response{};s.loose[loosePath].error="loose-warning";p.sources(s);p.oldMemo.enabled=p.newMemo.enabled=true;p.oldCache.lastTmpTileLoadError=p.newCache.lastTmpTileLoadError="retained";p.step("explicit nondefault loose wins");need(p.newCache.lastTmpTileLoadError=="retained","loose preserves last diagnostic");need(p.newTrace.at(1)=="loose:"+loosePath,"loose before MIX");p.step("memo loose preserves error");}
 {Pair p;make(p);p.tileRoot=loose;Source s;s.loose[loosePath]={false,{},"loose-failed","",false};s.named[name]=Response{};p.sources(s);p.step("failed explicit loose then MIX");need(p.newTrace.at(1)=="loose:"+loosePath,"explicit first");need(p.newSource.lastRoots==std::vector<std::string>({loose,assets,structure}),"three roots exact order");}
 for(const bool mix:std::array<bool,2>{{false,true}}){Pair p;make(p);p.tileRoot=inside;Source s;s.mixes=mix;s.loose[insidePath]=Response{};p.sources(s);const auto result=p.step(mix?"MIX present forbids loose fallback":"no MIX permits loose fallback");need(result.present==!mix,"MIX fallback golden");need(p.newTrace.at(1).rfind("named:",0)==0,"default root searches MIX first");}
 {Pair p;make(p);Source s;s.named[name]={true,Bytes{1,2,3},"bad-bytes","broken.tmp",false};p.sources(s);need(!p.step("failed decoded resource").present,"bad decode fails");const auto prior=p.newTrace.size();p.oldSource.named[name]=p.newSource.named[name]=Response{};p.step("failed no automatic retry");need(p.newTrace.size()==prior+2,"negative hit only memo find/remember");Pair fresh;make(fresh);need(fresh.step("fresh cache retries good source").present,"new cache retry");}
 {Pair p;make(p);Source s;s.named[name].bytes=rawFormat();p.sources(s);need(!p.step("real raw format visible but no terrain").present,"raw metadata absent");need(!p.step("real raw has no authored grid",Op::Grid).present,"raw grid unknown");p.step("raw decoded tile retained",Op::Tile);}
 {Pair p;make(p);Source s;s.named[name].bytes=ordinary(9,0,true);p.sources(s);need(!p.step("ordinary empty authored slot").present,"empty slot absent");need(p.step("empty authored grid retained",Op::Grid).present,"authored grid known despite empty image");}
 {Pair p;make(p);p.tileRoot=loose;p.assetRoot.clear();Source s;s.cachedRoot=assets;s.loose[loosePath]={false,{},"bad loose","",false};s.named[name]=Response{};p.sources(s);p.step("lazy export root");need(p.newTrace.at(1)=="cached-root:"+assets,"lazy root before resource");need(p.newSource.lastRoots==std::vector<std::string>({loose,"",structure}),"original roots not substituted");}
 for(int which=0;which<3;++which){Pair p;make(p);p.step("root before changes");p.oldCache.tmpTilesByPath["unrelated"]=p.newCache.tmpTilesByPath["unrelated"]=seed(1,2);p.oldCache.terrainTilePathByIndex[7]=p.newCache.terrainTilePathByIndex[7]="other";p.oldCache.missingTerrainTilePathByIndex.insert(9);p.newCache.missingTerrainTilePathByIndex.insert(9);
  const auto beforeKey=p.newCache.terrainTilePathCacheRoot;if(which==0)p.tileRoot=(temp/"new-loose").string();else if(which==1)p.assetRoot=(temp/"new-assets").string();else p.theater=&snow;
  p.step("each individual tile/asset/extension key invalidation");need(p.newCache.terrainTilePathCacheRoot!=beforeKey,"each key input independently changes root");need(p.newCache.tmpTilesByPath.find("unrelated")!=p.newCache.tmpTilesByPath.end(),"decoded nodes retained");
  need(p.newCache.terrainTilePathByIndex.find(7)==p.newCache.terrainTilePathByIndex.end()&&p.newCache.missingTerrainTilePathByIndex.find(9)==p.newCache.missingTerrainTilePathByIndex.end(),"both locator layers cleared");
 }
 {Pair p;make(p);p.step("before structure root change");const auto key=p.newCache.terrainTilePathCacheRoot;const auto positive=p.newCache.terrainTilePathByIndex;const auto negative=p.newCache.missingTerrainTilePathByIndex;p.structureRoot+="changed";p.step("structure root excluded from lookup key");need(key==p.newCache.terrainTilePathCacheRoot&&positive==p.newCache.terrainTilePathByIndex&&negative==p.newCache.missingTerrainTilePathByIndex,"structure alone keeps locators");}
 {Pair p;make(p);p.oldMemo.enabled=p.newMemo.enabled=true;p.oldMemo.entries[0]={nullptr,false};p.newMemo.entries[0]={nullptr,false};p.oldCache.lastTmpTileLoadError=p.newCache.lastTmpTileLoadError="retained-null";need(!p.step("resolved null memo").present,"memo null meaningful");need(p.newTrace.size()==1&&p.newCache.lastTmpTileLoadError=="retained-null","null hit no extra calls");}
 {Pair p;make(p);p.plant();p.oldMemo.enabled=p.newMemo.enabled=true;p.oldMemo.entries[0]={&p.oldCache.tmpTilesByPath.at("seed"),true};p.newMemo.entries[0]={&p.newCache.tmpTilesByPath.at("seed"),true};p.step("explicit memo named error flag");need(p.newCache.lastTmpTileLoadError=="seed-warning","flag replays exact cached warning");}
 for(int which=0;which<4;++which){Pair p;make(p);if(which==0){p.tileRoot=loose;Response r;r.throws=true;p.oldSource.loose[loosePath]=p.newSource.loose[loosePath]=r;}else if(which==1){Response r;r.throws=true;p.oldSource.named[name]=p.newSource.named[name]=r;}else if(which==2){p.oldMemo.throwFind=p.newMemo.throwFind=true;}else{p.oldMemo.throwRemember=p.newMemo.throwRemember=true;}need(!p.step("exception partial cache and ordered trace").error.empty(),"exception propagated equally");}
 {Pair p;make(p);p.oldSource.callback=[&](){p.oldTrace.push_back("nested-source");const auto c=p.context();frozenTmpQuery::PortBorrow borrow(p.oldSource,p.oldMemo);need(frozenTmpQuery::resolveRuntimeTerrainMetadata(1,0,c,p.oldCache)!=nullptr,"old nested insertion");};
  p.newSource.callback=[&](){p.newTrace.push_back("nested-source");const auto c=p.context();need(resolveRuntimeTerrainMetadata(1,0,c,p.newCache,p.newSource,p.newMemo)!=nullptr,"new nested insertion");};p.step("nested legal distinct node insertion");const auto* oldNode=&p.oldCache.tmpTilesByPath.at("resource:"+frozenTmpQuery::normalizedLowerPath(assets)+"|"+name2);const auto* newNode=&p.newCache.tmpTilesByPath.at("resource:"+frozenTmpQuery::normalizedLowerPath(assets)+"|"+name2);
  for(int i=0;i<128;++i){p.oldCache.tmpTilesByPath.emplace("extra"+std::to_string(i),Tile{});p.newCache.tmpTilesByPath.emplace("extra"+std::to_string(i),Tile{});}need(oldNode==&p.oldCache.tmpTilesByPath.at("resource:"+frozenTmpQuery::normalizedLowerPath(assets)+"|"+name2)&&newNode==&p.newCache.tmpTilesByPath.at("resource:"+frozenTmpQuery::normalizedLowerPath(assets)+"|"+name2),"unordered node borrow survives insertion");sameCache(p.oldCache,p.newCache);}
}
}
int main(int argc,char** argv){try{if(argc!=3||std::string(argv[1])!="--temp-root")throw std::runtime_error("usage: contract --temp-root NEW_PRIVATE_DIRECTORY");QueryContract::run(std::filesystem::path(argv[2]));const auto& c=QueryContract::count;
 std::cout<<"TMP query contract checks="<<c.checks<<" cases="<<c.cases<<" fullFieldComparisons="<<c.fields<<" orderedTraceRows="<<c.traceRows<<" exceptions="<<c.exceptions<<" failures=0 sameProductionQueryCpp=1 sameProductionCodecCpp=1 completeFrozenOldQuery=1 independentFrozenOldCodec=1 completeFiveCacheFields=1 landBytes=256 rampBytes=256 nativeMemoFrameOwner=not-covered nativeResources=not-covered performance=not-run\n";return 0;
 }catch(const std::exception& e){std::cerr<<"TMP query contract failed: "<<e.what()<<'\n';return 1;}}
