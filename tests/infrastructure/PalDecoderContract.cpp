#include "assets/codecs/PalDecoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace concord::assets;
constexpr std::array<std::uint8_t,64> rounded{
    0,4,8,12,16,20,24,28,32,36,40,45,49,53,57,61,
    65,69,73,77,81,85,89,93,97,101,105,109,113,117,121,125,
    130,134,138,142,146,150,154,158,162,166,170,174,178,182,186,190,
    194,198,202,206,210,215,219,223,227,231,235,239,243,247,251,255};
constexpr std::array<std::uint8_t,64> shifted{
    0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
    64,68,72,76,80,84,88,92,96,100,104,108,112,116,120,124,
    128,132,136,140,144,148,152,156,160,164,168,172,176,180,184,188,
    192,196,200,204,208,212,216,220,224,228,232,236,240,244,248,252};
constexpr std::array<std::uint8_t,64> replicated{
    0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,
    65,69,73,77,81,85,89,93,97,101,105,109,113,117,121,125,
    130,134,138,142,146,150,154,158,162,166,170,174,178,182,186,190,
    195,199,203,207,211,215,219,223,227,231,235,239,243,247,251,255};
constexpr std::array<PalColorPolicy,3> policies{
    PalColorPolicy::RoundedSixBit,PalColorPolicy::DacShiftTwo,PalColorPolicy::ReplicatedSixBit};
std::size_t checks=0;
void require(bool condition,const std::string& message){
    ++checks;
    if(!condition) throw std::runtime_error(message);
}
const std::array<std::uint8_t,64>& golden(PalColorPolicy policy){
    if(policy==PalColorPolicy::DacShiftTwo) return shifted;
    if(policy==PalColorPolicy::ReplicatedSixBit) return replicated;
    return rounded;
}
std::uint64_t digest(const std::array<PalRgbChannels,256>& colors){
    std::uint64_t hash=1469598103934665603ull;
    for(const auto& c:colors) for(const auto value:{c.r,c.g,c.b}){
        hash^=value;hash*=1099511628211ull;
    }
    return hash;
}
void compareBytes(const std::vector<std::uint8_t>& bytes,const std::string& label){
    PalRawPalette raw;
    require(decodePalBytes(bytes,raw,PalLengthPolicy::Exact768),label+": decode");
    bool expectedSixBit=true;
    for(const auto value:bytes) if(value>63U) expectedSixBit=false;
    require(raw.sixBit==expectedSixBit,label+": global six-bit detection");
    for(std::size_t index=0;index<256;++index){
        require(raw.colors[index]==PalRgbChannels{bytes[index*3U],bytes[index*3U+1U],bytes[index*3U+2U]},label+": stored RGB order/index");
    }
    for(const auto policy:policies){
        const auto mapped=mapPalColors(raw,policy);
        const auto& table=golden(policy);
        const auto channel=[&](std::uint8_t value){return expectedSixBit?table[value]:value;};
        for(std::size_t index=0;index<256;++index){
            const PalRgbChannels expected{channel(bytes[index*3U]),channel(bytes[index*3U+1U]),channel(bytes[index*3U+2U])};
            require(mapped[index]==expected,label+": fixed original policy table");
        }
        // Freeze a complete indexed image as well as a permuted pixel stream.
        for(std::size_t pixel=0;pixel<4096;++pixel){
            const auto index=(pixel*73U+pixel/256U)%256U;
            const PalRgbChannels expected{channel(bytes[index*3U]),channel(bytes[index*3U+1U]),channel(bytes[index*3U+2U])};
            require(mapped[index]==expected,label+": indexed pixel output");
        }
        std::cout<<"PAL sample "<<label<<" policy="<<static_cast<int>(policy)<<" colors=256 pixelDigest="<<digest(mapped)<<'\n';
    }
}
void synthetic(){
    std::vector<std::uint8_t> bytes(768U,0);
    compareBytes(bytes,"all-black");
    bytes.assign(768U,63U);compareBytes(bytes,"six-bit-white");
    for(std::size_t index=0;index<256;++index){
        bytes[index*3U]=static_cast<std::uint8_t>(index%64U);
        bytes[index*3U+1U]=static_cast<std::uint8_t>((index*3U)%64U);
        bytes[index*3U+2U]=static_cast<std::uint8_t>((index*5U)%64U);
    }
    compareBytes(bytes,"all-six-bit-channels-and-256-indexes");
    PalRawPalette sixBit;require(decodePalBytes(bytes,sixBit,PalLengthPolicy::Exact768),"base decode");
    const auto old=sixBit;
    for(const std::size_t size:{0U,1U,2U,3U,63U,255U,256U,511U,765U,766U,767U,769U,770U,1024U}){
        std::vector<std::uint8_t> malformed(size,1U);
        PalRawPalette retained=old;
        require(!decodePalBytes(malformed,retained,PalLengthPolicy::Exact768),"exact length reject");
        require(retained==old,"exact failure retains output");
        retained=old;
        const bool accepted=decodePalBytes(malformed,retained,PalLengthPolicy::Prefix768);
        require(accepted==(size>=768U),"prefix length policy");
        if(!accepted) require(retained==old,"prefix failure retains output");
    }
    auto tailed=bytes;tailed.push_back(255U);tailed.push_back(64U);
    PalRawPalette prefix;
    require(decodePalBytes(tailed,prefix,PalLengthPolicy::Prefix768)&&prefix==sixBit,"campaign ignores all tail bytes including high channels");
    for(std::size_t index=0;index<768U;++index){
        auto eightBit=bytes;eightBit[index]=64U;
        PalRawPalette raw;require(decodePalBytes(eightBit,raw,PalLengthPolicy::Exact768)&&!raw.sixBit,"any one high channel changes entire palette");
        for(const auto policy:policies){
            const auto mapped=mapPalColors(raw,policy);
            require(mapped==raw.colors,"all entries use eight-bit identity after one high channel");
        }
    }
    for(std::size_t index=0;index<768U;++index) bytes[index]=static_cast<std::uint8_t>(index%256U);
    compareBytes(bytes,"full-eight-bit-range");
    PalRawPalette retained=old;
    require(!decodePalBytes(bytes,retained,static_cast<PalLengthPolicy>(99))&&retained==old,"unknown length policy rejects atomically");
    require(rounded[11]==45U&&shifted[11]==44U&&replicated[11]==44U,"rounding distinction frozen");
    require(rounded[48]==194U&&shifted[48]==192U&&replicated[48]==195U,"replication distinction frozen");
    require(rounded[63]==255U&&shifted[63]==252U&&replicated[63]==255U,"white distinction frozen");
}
void material(const std::string& path){
    std::ifstream input(path,std::ios::binary);
    require(input.is_open(),"required PAL sample missing: "+path);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
    require(bytes.size()==768U,"required PAL sample length: "+path);
    compareBytes(bytes,path);
}
} // namespace
int main(int argc,char** argv){
    try{
        synthetic();
        for(int index=1;index<argc;++index) material(argv[index]);
        std::cout<<"PAL decoder contract passed: checks="<<checks<<" samples="<<(argc-1)
            <<" allChannels=64 colors=256 indexedPixels=4096 policies=3 malformed=atomic\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"PAL decoder contract failed after "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}