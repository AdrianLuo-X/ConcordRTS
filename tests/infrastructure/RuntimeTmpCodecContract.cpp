#include "assets/codecs/RuntimeTmpCodec.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error Pure TMP codec headers must not import Windows headers
#endif
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "fixtures/RuntimeTmpCodecOld.inl"

namespace TmpContract {
using Bytes=std::vector<unsigned char>;
using Image=RuntimeTmpSubTileImage;
using Tile=RuntimeTmpTileFile;
struct Counts {std::size_t checks=0,cases=0,accepted=0,rejected=0,partial=0,partialFailures=0,partialSuccesses=0,fieldComparisons=0,files=0;} count;
std::string currentCase;
void need(bool value,const std::string& why){++count.checks;if(!value)throw std::runtime_error(currentCase+": "+why);}
bool sameImage(const Image& a,const Image& b,bool assertions){bool result=true;
 const auto field=[&](bool equal,const char* name){result=result&&equal;if(assertions){++count.fieldComparisons;need(equal,std::string("image field ")+name);}};
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
 const auto field=[&](bool equal,const char* name){result=result&&equal;if(assertions){++count.fieldComparisons;need(equal,std::string("tile field ")+name);}};
#define TILE_FIELD(name) field(a.name==b.name,#name)
 TILE_FIELD(loadAttempted);TILE_FIELD(loaded);TILE_FIELD(blockWidth);TILE_FIELD(blockHeight);TILE_FIELD(errorMessage);TILE_FIELD(gridWidth);TILE_FIELD(gridHeight);
 field(a.images.size()==b.images.size(),"images.size");
 for(std::size_t i=0;i<std::min(a.images.size(),b.images.size());++i){const bool equal=sameImage(a.images[i],b.images[i],assertions);result=result&&equal;}
#undef TILE_FIELD
 return result;
}
Tile seeded(){Tile t;t.loadAttempted=t.loaded=true;t.blockWidth=31;t.blockHeight=29;t.gridWidth=3;t.gridHeight=5;t.errorMessage="retained tile error";Image i;
 i.present=i.hasTerrainType=i.hasRadarColors=true;i.terrainType=251;i.rampType=249;i.radarLeft={7,8,9};i.radarRight={10,11,12};
 i.offsetX=-7;i.offsetY=8;i.width=2;i.height=3;i.globalWidth=41;i.globalHeight=43;i.pixels={1,0,3,0,5,6};
 i.packedPixelPaletteHash=0x123456789abcdef0ULL;i.packedPixels={0x10203040,0xffffffff};i.resourcePixelNonZeroCount=71;
 i.baseOffsetX=13;i.baseOffsetY=-14;i.baseWidth=2;i.baseHeight=2;i.basePixels={9,8,7,6};i.resourceBaseNonZeroCount=73;i.baseZData={5,4,3,2};i.hasBaseZData=true;
 i.hasExtra=true;i.extraOffsetX=-3;i.extraOffsetY=4;i.extraWidth=1;i.extraHeight=2;i.extraPixels={11,12};i.resourceExtraNonZeroCount=79;i.extraZData={13,14};i.hasExtraZData=true;t.images={i};return t;
}
struct Result {bool accepted=false;Tile tile;std::string error;};
Result compare(const Bytes& bytes,const std::string& name,const std::string& source="literal fixture.tmp",std::optional<bool> expected={}){
 Result result;
 for(int initial=0;initial!=2;++initial){currentCase=name+"/initial="+std::to_string(initial);const Tile seed=initial?seeded():Tile{};
  Tile old=seed,actual=seed;const std::string initialError=initial?"retained caller error":"";std::string oldError=initialError,actualError=initialError;
  const Bytes input=bytes;
  const bool oldAccepted=frozenTmpCodec::loadRuntimeTmpTileBytes(bytes,source,old,oldError);
  const bool actualAccepted=::loadRuntimeTmpTileBytes(bytes,source,actual,actualError);
  need(oldAccepted==actualAccepted,"return status differs");need(oldError==actualError,"exact error/warning differs");static_cast<void>(sameTile(old,actual,true));need(bytes==input,"input byte vector changed");
  need(actual.loadAttempted==seed.loadAttempted&&actual.loaded==seed.loaded&&actual.errorMessage==seed.errorMessage,"byte decoder may not silently rewrite cache-owner fields");
  if(expected)need(actualAccepted==*expected,"independent fixture acceptance expectation");
  ++count.cases;if(actualAccepted)++count.accepted;else ++count.rejected;
  const bool partialFailure=!actualAccepted&&!sameTile(actual,seed,false);const bool partialSuccess=actualAccepted&&!actualError.empty()&&actualError!=initialError;
  if(partialFailure){++count.partialFailures;++count.partial;}if(partialSuccess){++count.partialSuccesses;++count.partial;}
  result={actualAccepted,std::move(actual),std::move(actualError)};
 }
 return result;
}
void put16(Bytes& bytes,std::size_t at,std::uint16_t value){if(bytes.size()<at+2)bytes.resize(at+2);bytes[at]=static_cast<unsigned char>(value);bytes[at+1]=static_cast<unsigned char>(value>>8);}
void put32(Bytes& bytes,std::size_t at,std::uint32_t value){if(bytes.size()<at+4)bytes.resize(at+4);for(unsigned shift=0;shift!=4;++shift)bytes[at+shift]=static_cast<unsigned char>(value>>(shift*8));}
std::uint32_t get32(const Bytes& bytes,std::size_t at){return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|(std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);}
Bytes diamond(unsigned char terrain=0,unsigned char ramp=0,bool z=false,bool extra=false,bool extraZ=false){
 Bytes data(52,0);put32(data,0,3);put32(data,4,5);put32(data,16,extraZ?1U:0U);put32(data,20,1);put32(data,24,4);
 put32(data,28,extra?3U:0U);put32(data,32,extra?2U:0U);put32(data,36,(z?2U:0U)|(extra?1U:0U));
 data[41]=terrain;data[42]=ramp;data[43]=1;data[44]=2;data[45]=3;data[46]=4;data[47]=5;data[48]=6;
 for(unsigned i=0;i!=16;++i)data.push_back(static_cast<unsigned char>(i%5==0?0:i+1));
 if(z)for(unsigned i=0;i!=16;++i)data.push_back(static_cast<unsigned char>(20+i));
 if(extra)for(unsigned i=0;i!=6;++i)data.push_back(static_cast<unsigned char>(i%2?40+i:0));
 if(extra&&z&&extraZ)for(unsigned i=0;i!=6;++i)data.push_back(static_cast<unsigned char>(50+i));return data;
}
Bytes normal(const std::vector<Bytes>& records){Bytes bytes(16+records.size()*4,0);put32(bytes,0,static_cast<std::uint32_t>(records.size()));put32(bytes,4,1);put32(bytes,8,8);put32(bytes,12,4);
 for(std::size_t i=0;i<records.size();++i){if(records[i].empty())continue;put32(bytes,16+i*4,static_cast<std::uint32_t>(bytes.size()));bytes.insert(bytes.end(),records[i].begin(),records[i].end());}return bytes;
}
Bytes row(const Bytes& operations){Bytes bytes(2,0);put16(bytes,0,static_cast<std::uint16_t>(operations.size()+2));bytes.insert(bytes.end(),operations.begin(),operations.end());return bytes;}
Bytes raw(const std::vector<Bytes>& records,unsigned width=4,unsigned height=1){Bytes bytes(8+24*records.size(),0);put16(bytes,2,static_cast<std::uint16_t>(width));put16(bytes,4,static_cast<std::uint16_t>(height));put16(bytes,6,static_cast<std::uint16_t>(records.size()));
 for(std::size_t i=0;i<records.size();++i){const std::size_t at=8+24*i;put32(bytes,at,0x8000ffffU);put32(bytes,at+4,static_cast<std::uint32_t>(width|(height<<16)));if(records[i].empty())continue;put32(bytes,at+20,static_cast<std::uint32_t>(bytes.size()));bytes.insert(bytes.end(),records[i].begin(),records[i].end());}return bytes;
}
void ordinaryCases(){
 const auto plain=normal({diamond()});const auto basic=compare(plain,"ordinary 8x4 diamond","ordinary.tmp",true);
 need(basic.tile.blockWidth==8&&basic.tile.blockHeight==4&&basic.tile.gridWidth==1&&basic.tile.gridHeight==1,"ordinary independent grid");
 const auto& image=basic.tile.images.at(0);need(image.present&&image.hasTerrainType&&image.hasRadarColors&&image.width==8&&image.height==4&&image.pixels.size()==32&&image.basePixels.size()==16,"complete diamond image dimensions");
 need(image.radarLeft.r==1&&image.radarLeft.g==2&&image.radarLeft.b==3&&image.radarRight.r==4&&image.radarRight.g==5&&image.radarRight.b==6,"radar RGB bytes decoded independently");
 Bytes expectedBase;for(unsigned i=0;i!=16;++i)expectedBase.push_back(static_cast<unsigned char>(i%5==0?0:i+1));
 need(image.basePixels==expectedBase&&image.pixels.at(2)==expectedBase.at(0)&&image.pixels.at(3)==expectedBase.at(1)&&image.pixels.at(8)==expectedBase.at(4),"packed diamond placement and transparency");
 need(image.packedPixelPaletteHash==0&&image.packedPixels.empty()&&image.resourcePixelNonZeroCount==-1&&image.resourceBaseNonZeroCount==-1&&image.resourceExtraNonZeroCount==-1,"new image derived caches retain original defaults");
 const auto empty=compare(normal({Bytes{},Bytes{}}),"ordinary authored empty slots","empty.tmp",true);
 need(empty.tile.images.size()==2&&!empty.tile.images[0].present&&!empty.tile.images[1].present,"authored empty slots remain explicit");
 for(unsigned flags=0;flags!=8;++flags){const bool z=(flags&1)!=0,extra=(flags&2)!=0,extraZ=(flags&4)!=0;const auto value=compare(normal({diamond(6,255,z,extra,extraZ)}),"ordinary optional data "+std::to_string(flags),"optional.tmp",true);const auto& i=value.tile.images.at(0);
  need(i.hasBaseZData==z&&i.baseZData.size()==(z?16U:0U)&&i.hasExtra==extra&&i.extraPixels.size()==(extra?6U:0U),"optional base Z and extra color records");
  need(i.hasExtraZData==(z&&extra&&extraZ)&&i.extraZData.size()==(z&&extra&&extraZ?6U:0U),"optional extra Z availability and dataBlock flag");
  if(extra)need(i.extraOffsetX==-2&&i.extraOffsetY==-1&&i.offsetX==-2&&i.offsetY==-1&&i.width==10&&i.height==5,"bounded negative extra origin");
 }
 const std::array<unsigned char,18> terrains{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,255};
 for(const auto terrain:terrains)for(unsigned ramp=0;ramp!=256;++ramp){const auto result=compare(normal({diamond(terrain,static_cast<unsigned char>(ramp))}),"authored terrain/ramp","authored.tmp",true);need(result.tile.images[0].terrainType==terrain&&static_cast<unsigned>(result.tile.images[0].rampType)==ramp,"all authored bytes preserved, including out-of-range terrain values");}
 for(std::size_t size=0;size<plain.size();++size){const Bytes prefix(plain.begin(),plain.begin()+static_cast<std::ptrdiff_t>(size));static_cast<void>(compare(prefix,"ordinary truncation "+std::to_string(size)));}
 const auto fullyDecorated=normal({diamond(7,128,true,true,true)});
 for(std::size_t size=20;size<fullyDecorated.size();++size){const Bytes prefix(fullyDecorated.begin(),fullyDecorated.begin()+static_cast<std::ptrdiff_t>(size));static_cast<void>(compare(prefix,"optional-data truncation "+std::to_string(size)));}
 {
  auto bytes=normal({diamond(),diamond()});const auto offset=get32(bytes,16);put32(bytes,20,offset);
  const auto result=compare(bytes,"duplicate image offsets","duplicate.tmp",true);need(result.tile.images.size()==2&&sameImage(result.tile.images[0],result.tile.images[1],false),"duplicate offset semantics remain accepted");
 }
 for(const auto offset:std::array<std::uint32_t,4>{0,16,23,0xffffffffU}){auto bytes=normal({diamond(),diamond()});put32(bytes,20,offset);static_cast<void>(compare(bytes,"offset-table boundary "+std::to_string(offset)));}
 {auto bytes=normal({diamond(),diamond()});put32(bytes,20,get32(bytes,16)+1);static_cast<void>(compare(bytes,"overlapping short image record","overlap.tmp",false));}
 for(const auto width:std::array<std::uint32_t,6>{0,1,8,64,512,513}){auto bytes=plain;put32(bytes,8,width);static_cast<void>(compare(bytes,"bounded width header "+std::to_string(width)));}
 for(const auto height:std::array<std::uint32_t,6>{0,1,4,8,512,513}){auto bytes=plain;put32(bytes,12,height);static_cast<void>(compare(bytes,"bounded height header "+std::to_string(height)));}
 for(const std::size_t field:std::array<std::size_t,2>{0,4})for(const auto blocks:std::array<std::uint32_t,5>{0,1,2,64,65}){auto bytes=plain;put32(bytes,field,blocks);static_cast<void>(compare(bytes,"bounded grid header "+std::to_string(field)+'/'+std::to_string(blocks)));}
 // Mutations are restricted to already valid image payload/color/flag fields.
 // Signed origins and table dimensions are not arbitrary fuzzed allocation sizes.
 for(const auto extraWidth:std::array<std::uint32_t,5>{0,1,3,512,513})for(const auto extraHeight:std::array<std::uint32_t,4>{0,1,2,513}){auto bytes=fullyDecorated;put32(bytes,20+28,extraWidth);put32(bytes,20+32,extraHeight);static_cast<void>(compare(bytes,"bounded extra shape"));}
 for(const int x:std::array<int,4>{-4,-1,0,8})for(const int y:std::array<int,4>{-4,-1,0,8}){auto bytes=fullyDecorated;put32(bytes,20+20,static_cast<std::uint32_t>(x));put32(bytes,20+24,static_cast<std::uint32_t>(y));static_cast<void>(compare(bytes,"bounded extra origin"));}
 for(const auto at:std::array<std::size_t,13>{16,36,41,42,43,44,45,46,47,48,52,60,67})for(const auto value:std::array<unsigned char,4>{0,1,16,255}){auto bytes=plain;bytes[20+at]=value;static_cast<void>(compare(bytes,"safe payload mutation "+std::to_string(at)));}
 // A successful first image followed by a malformed second image must retain
 // exactly the old partially decoded first-image state when returning false.
 {auto first=diamond(1,2);auto second=diamond(3,4);second.resize(52);const auto result=compare(normal({first,second}),"ordinary partial failure","partial-normal.tmp",false);need(result.tile.images.size()==2&&result.tile.images[0].present&&!result.tile.images[1].present,"ordinary partial decode remains observable");}
}
void rawCases(){
 const auto opaque=row({1,2,3,4});const auto transparent=row({0,4});const auto clamped=row({0,255});
 const auto basic=compare(raw({opaque}),"raw opaque","raw-opaque.tmp",true);const auto& i=basic.tile.images[0];
 need(basic.tile.gridWidth==0&&basic.tile.gridHeight==0&&!i.hasTerrainType&&!i.hasRadarColors&&i.terrainType==0&&i.rampType==0,"raw images have no authored terrain metadata/grid");
 need(i.offsetX==-1&&i.offsetY==-32768&&i.globalWidth==4&&i.globalHeight==1&&i.pixels==Bytes({1,2,3,4}),"raw packed signed anchors and opaque pixels");
 const auto clear=compare(raw({transparent}),"raw transparent","raw-transparent.tmp",true);need(clear.tile.images[0].pixels==Bytes(4,0),"raw transparent run");
 const auto clamp=compare(raw({clamped}),"raw overrun clamps","raw-clamp.tmp",true);need(clamp.tile.images[0].pixels==Bytes(4,0),"oversize transparent run is clamped, not rejected");
 static_cast<void>(compare(raw({row({})}),"raw empty row body","raw-empty-row.tmp",true));
 static_cast<void>(compare(raw({row({1,0,2,4})}),"raw mixed runs","raw-mixed.tmp",true));
 static_cast<void>(compare(raw({row({1,2,3,4,5})}),"raw too many opaque pixels","raw-opaque-overflow.tmp",false));
 static_cast<void>(compare(raw({row({0})}),"raw truncated transparent opcode","raw-run-truncated.tmp",false));
 for(const std::uint16_t length:std::array<std::uint16_t,4>{0,1,7,65535}){auto bad=opaque;put16(bad,0,length);static_cast<void>(compare(raw({bad}),"raw row length "+std::to_string(length),"raw-row.tmp",false));}
 {
  const auto result=compare(raw({Bytes{1,0},opaque}),"raw partial success warning","raw-partial.tmp",true);
  need(result.tile.images.size()==2&&!result.tile.images[0].present&&result.tile.images[1].present,"one invalid raw slot does not erase successful slots");
  need(result.error=="TMP raw format 3 row has an invalid length. slot=0 path=raw-partial.tmp","partial raw success retains first exact warning");
 }
 static_cast<void>(compare(raw({Bytes{1,0},Bytes{1,0}}),"raw all invalid slots","raw-all-invalid.tmp",false));
 static_cast<void>(compare(raw({Bytes{},opaque}),"raw explicit empty and valid slot","raw-empty-slot.tmp",true));
 const auto valid=raw({opaque});for(std::size_t size=0;size<valid.size();++size){const Bytes prefix(valid.begin(),valid.begin()+static_cast<std::ptrdiff_t>(size));static_cast<void>(compare(prefix,"raw truncation "+std::to_string(size)));}
 {
  auto bytes=raw({opaque,opaque});put16(bytes,6,0);put16(bytes,2,0);put16(bytes,4,0);const auto result=compare(bytes,"raw inferred count and dimensions","raw-inferred.tmp",true);
  need(result.tile.blockWidth==4&&result.tile.blockHeight==1&&result.tile.images.size()==2,"raw zero header dimensions fall back to actual slot maxima");
 }
 {auto bytes=raw({opaque,opaque});put16(bytes,6,3);static_cast<void>(compare(bytes,"raw slot-count mismatch","raw-count.tmp",false));}
 for(const unsigned dimension:std::array<unsigned,5>{0,1,4,512,513}){auto bytes=raw({opaque});put32(bytes,8+4,dimension|(1U<<16));static_cast<void>(compare(bytes,"raw bounded width "+std::to_string(dimension)));}
 for(const unsigned dimension:std::array<unsigned,5>{0,1,2,512,513}){auto bytes=raw({opaque});put32(bytes,8+4,4U|(dimension<<16));static_cast<void>(compare(bytes,"raw bounded height "+std::to_string(dimension)));}
 for(const auto offset:std::array<std::uint32_t,4>{0,8,28,0xffffffffU}){auto bytes=raw({opaque});put32(bytes,8+20,offset);static_cast<void>(compare(bytes,"raw data/table overlap "+std::to_string(offset),"raw-overlap.tmp",false));}
 {auto bytes=raw({opaque,opaque});put32(bytes,8+24+20,get32(bytes,8+20));const auto result=compare(bytes,"raw duplicate offsets","raw-duplicate.tmp",true);need(sameImage(result.tile.images[0],result.tile.images[1],false),"raw duplicate offsets remain accepted");}
 {auto bytes=raw({opaque,opaque});put32(bytes,8+24+20,get32(bytes,8+20)+1);static_cast<void>(compare(bytes,"raw overlapping record ranges"));}
}
void realFile(const std::string& path){std::ifstream file(path,std::ios::binary);need(static_cast<bool>(file),"cannot open requested real TMP fixture: "+path);const Bytes bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());need(!file.bad(),"real TMP fixture read failed: "+path);
 const auto result=compare(bytes,"real resource "+path,path);++count.files;std::cout<<"TMP fixture: source="<<path<<"; bytes="<<bytes.size()<<"; accepted="<<result.accepted<<"; images="<<result.tile.images.size()<<"; grid="<<result.tile.gridWidth<<'x'<<result.tile.gridHeight<<"; warning="<<result.error<<'\n';}
}
int main(int argc,char** argv){try{TmpContract::ordinaryCases();TmpContract::rawCases();for(int index=1;index<argc;++index)TmpContract::realFile(argv[index]);const auto& c=TmpContract::count;
 std::cout<<"TMP codec contract passed: checks="<<c.checks<<"; cases="<<c.cases<<"; accepted="<<c.accepted<<"; rejected="<<c.rejected<<"; partial="<<c.partial<<"; partialFailures="<<c.partialFailures<<"; partialSuccesses="<<c.partialSuccesses<<"; fields="<<c.fieldComparisons<<"; tileMembers=8; imageMembers=34; files="<<c.files<<"; windowsHeader=absent; oracleSource=RuntimeMapRenderer.cpp@805BE537672389106017BEC1301BD9CBE1ABE8B12074B78CE90E568A2A605687\n";return 0;
 }catch(const std::exception& e){std::cerr<<"TMP codec contract failed: "<<e.what()<<'\n';return 1;}}