#include "assets/text/WestwoodFontCodec.h"
#include <cstddef>
#include <array>
#include <cstdint>
#include <iostream>
#include <tuple>
#include <utility>
#include <vector>
using namespace concord::assets;
namespace {
int checks=0,failures=0;
constexpr std::size_t mapAt=0x1c,dataAt=0x1c+65536U*2U;
void check(bool value,const char* message){++checks;if(!value){++failures;if(failures<20)std::cerr<<message<<'\n';}}
void put32(std::vector<unsigned char>& b,std::size_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)b[at+i]=static_cast<unsigned char>(n>>(i*8U));}
void map(std::vector<unsigned char>& b,std::uint16_t code,std::uint16_t index){b[mapAt+static_cast<std::size_t>(code)*2U]=static_cast<unsigned char>(index);b[mapAt+static_cast<std::size_t>(code)*2U+1U]=static_cast<unsigned char>(index>>8U);}
auto fields(const WestwoodFontMetrics& m){return std::tuple(m.stride,m.bitmapRows,m.lineHeight,m.glyphCount,m.recordSize);}
std::vector<unsigned char> font(){std::vector<unsigned char> b(dataAt+10U);b[0]='f';b[1]='o';b[2]='n';b[3]='T';put32(b,8,2);put32(b,12,2);put32(b,16,4);put32(b,20,2);put32(b,24,5);
 for(auto code:{std::uint16_t{'A'},std::uint16_t{0x4e2d},std::uint16_t{0xd800},std::uint16_t{0xdc00}})map(b,code,1);map(b,'?',2);
 b[dataAt]=3;b[dataAt+1]=0xe0;b[dataAt+5]=4;b[dataAt+6]=0xf0;return b;}
}
int main(){auto bytes=font();const auto original=bytes;WestwoodFontMetrics metrics;
 check(decodeWestwoodFontHeader(bytes,metrics),"synthetic font accepted");
 check(fields(metrics)==std::tuple{2U,2U,4U,2U,5U},"exact raw header fields");
 for(std::uint32_t code=0;code<65536;++code){const bool mapped=code=='A'||code==0x4e2d||code==0xd800||code==0xdc00;const auto* expected=bytes.data()+dataAt+(mapped?0U:5U);
  for(auto policy:{WestwoodGlyphFallback::AnyUnavailableRecord,WestwoodGlyphFallback::MissingReferenceOnly})check(westwoodFontGlyph(bytes,metrics,static_cast<std::uint16_t>(code),policy)==expected,"full BMP map/fallback or surrogate code-unit changed");}
 check(bytes==original,"lookup mutates no owner bytes");
 auto bad=bytes;bad[dataAt]=255;
 check(westwoodFontGlyph(bad,metrics,'A',WestwoodGlyphFallback::AnyUnavailableRecord)==bad.data()+dataAt+5,"main invalid width fallback");
 check(westwoodFontGlyph(bad,metrics,'A',WestwoodGlyphFallback::MissingReferenceOnly)==nullptr,"skirmish/result invalid width stays unavailable");
 bad=bytes;map(bad,'A',65535);
 for(auto policy:{WestwoodGlyphFallback::AnyUnavailableRecord,WestwoodGlyphFallback::MissingReferenceOnly})check(westwoodFontGlyph(bad,metrics,'A',policy)==bad.data()+dataAt+5,"out of range reference fallback");
 bad=bytes;map(bad,'A',0);check(westwoodFontGlyph(bad,metrics,'A',WestwoodGlyphFallback::MissingReferenceOnly)==bad.data()+dataAt+5,"zero mapping fallback");
 bad[dataAt+5]=255;
 for(auto policy:{WestwoodGlyphFallback::AnyUnavailableRecord,WestwoodGlyphFallback::MissingReferenceOnly})check(westwoodFontGlyph(bad,metrics,'A',policy)==nullptr,"bad question record does not recurse");
 bad=bytes;bad[dataAt]=0;check(westwoodFontGlyph(bad,metrics,'A',WestwoodGlyphFallback::MissingReferenceOnly)==bad.data()+dataAt,"zero glyph width remains valid");
 WestwoodFontMetrics sentinel{9,8,7,6,5};const auto preserved=fields(sentinel);
 for(std::size_t size:std::array<std::size_t,4>{0U,3U,28U,dataAt-1U}){check(!decodeWestwoodFontHeader(std::span<const unsigned char>(bytes.data(),size),sentinel),"short font rejected");check(fields(sentinel)==preserved,"short input preserves metadata");}
 bad=bytes;bad[0]='x';check(!decodeWestwoodFontHeader(bad,sentinel)&&fields(sentinel)==preserved,"magic failure preserves reload metadata");
 for(auto value:{std::pair<std::size_t,std::uint32_t>{8,0},{8,17},{12,0},{12,65},{16,1},{16,65},{20,0},{20,3},{24,0},{24,4},{24,0xffffffffU}}){bad=bytes;put32(bad,value.first,value.second);WestwoodFontMetrics m;
  check(!decodeWestwoodFontHeader(bad,m),"semantic bad header rejected");check(fields(m)!=std::tuple{0U,0U,0U,0U,0U},"recognizable header assigns raw fields for result reload");}
 bad=bytes;bad.pop_back();WestwoodFontMetrics m;check(!decodeWestwoodFontHeader(bad,m),"truncated record rejected");
 bad=bytes;bad.insert(bad.end(),9,0xa5);check(decodeWestwoodFontHeader(bad,m)&&fields(m)==fields(metrics),"extra tail accepted");
 bad=bytes;put32(bad,4,0xffffffffU);check(decodeWestwoodFontHeader(bad,m),"legacy unused header word remains unrestricted");
 std::vector<unsigned char> many(dataAt+2U*65536U);many[0]='f';many[1]='o';many[2]='n';many[3]='T';put32(many,8,1);put32(many,12,1);put32(many,16,1);put32(many,20,65536);put32(many,24,2);map(many,'A',65535);
 check(decodeWestwoodFontHeader(many,m)&&m.glyphCount==65536,"glyph count not arbitrarily capped to mapping width");
 check(westwoodFontGlyph(many,m,'A',WestwoodGlyphFallback::MissingReferenceOnly)==many.data()+dataAt+65534U*2U,"highest map reference exact record");
 std::vector<unsigned char> boundary(dataAt+1025U);boundary[0]='f';boundary[1]='o';boundary[2]='n';boundary[3]='T';put32(boundary,8,16);put32(boundary,12,64);put32(boundary,16,64);put32(boundary,20,1);put32(boundary,24,1025);map(boundary,'A',1);boundary[dataAt]=128;
 check(decodeWestwoodFontHeader(boundary,m),"max stride rows line-height accepted");check(westwoodFontGlyph(boundary,m,'A',WestwoodGlyphFallback::AnyUnavailableRecord)==boundary.data()+dataAt,"max glyph width accepted");
 std::cout<<"Westwood byte codec: checks="<<checks<<" failures="<<failures<<" fullBmpCodes=65536 policies=2\n";return failures?1:0;}
