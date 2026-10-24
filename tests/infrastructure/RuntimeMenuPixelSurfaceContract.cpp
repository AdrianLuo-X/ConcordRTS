#include "ui/surface/RuntimeMenuPixelSurface.h"
#if defined(_WINDOWS_)
#error A CPU surface must not import Windows headers.
#endif
#include <algorithm>
#include <array>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>
namespace {
int checks=0,failures=0;
void expect(bool value,const char* name){++checks;if(!value){++failures;std::cerr<<name<<'\n';}}
std::vector<unsigned char> fail(std::size_t){throw std::bad_alloc();}
std::vector<unsigned char> shortAllocation(std::size_t n){return std::vector<unsigned char>(n-1);}
std::vector<unsigned char> tooLong(std::size_t){throw std::length_error("test allocation limit");}
std::vector<unsigned char> once(std::size_t n){static int calls=0;if(calls++==0)return std::vector<unsigned char>(n,0);throw std::bad_alloc();}
}
int main(){
    using namespace concord::ui;
    static_assert(!std::is_copy_constructible_v<MenuPixelBuffer> && !std::is_move_constructible_v<MenuPixelBuffer>);
    MenuPixelBuffer b;expect(!b.view().valid()&&b.bytes().empty(),"initial released storage");
    expect(b.reset(3,2,16),"padded allocation");auto first=b.view();expect(first.valid()&&b.owns(first)&&b.bytes().size()==32,"owned generation and stride");
    std::fill(first.bytes.begin(),first.bytes.end(),0x6d);clearMenuPixels(first);
    for(std::size_t i=0;i<32;++i){const bool pad=i%16>=12;expect(first.bytes[i]==(pad?0x6d:(i%4==3?255:0)),"topdown BGRA clear preserves padding");}
    const auto retained=b.bytes();expect(!b.reset(-1,2)&&!b.reset(0,2)&&!b.reset(3,0)&&!b.reset(3,2,11),"invalid dimensions and short stride");
    expect(!b.reset(3,2,std::numeric_limits<std::size_t>::max()),"stride multiplication overflow");
    expect(b.bytes()==retained&&b.owns(first),"failed reset preserves old buffer and borrow");
    expect(b.reset(2,3),"resize");expect(!b.owns(first)&&b.view().rowStride==8,"successful resize revokes previous generation");auto second=b.view();
    b.release();expect(!b.owns(second)&&!b.view().valid()&&b.bytes().empty(),"release invalidates borrow and frees pixels");
    b.release();expect(!b.view().valid(),"release idempotent");expect(b.reset(1,1),"recreate after release");expect(!b.owns(first)&&!b.owns(second),"address reuse cannot revive old generations");
    MenuPixelBuffer failed(fail),shorted(shortAllocation),limited(tooLong),preserved(once);
    expect(!failed.reset(4,4)&&!failed.view().valid(),"bad_alloc reported without fake surface");
    expect(!shorted.reset(4,4)&&!shorted.view().valid(),"short allocator result rejected");
    expect(!limited.reset(4,4)&&!limited.view().valid(),"length_error reported");
    expect(preserved.reset(2,2),"fault owner initial allocation");auto borrowed=preserved.view();borrowed.bytes[0]=83;
    expect(!preserved.reset(3,3)&&preserved.owns(borrowed)&&preserved.bytes()[0]==83,"allocation failure preserves current pixels, dimensions and generation");
    std::array<unsigned char,20> memory{};std::fill(memory.begin(),memory.end(),0x6d);
    MenuPixelSurfaceView v{std::span<unsigned char>(memory).first(12),2,1,16};expect(v.valid(),"last row only requires active bytes");
    fillMenuPixels(v,{-4,-4,1,1},RuntimeRgbColorValue::fromChannels(10,20,30));
    expect(memory[0]==30&&memory[1]==20&&memory[2]==10&&memory[3]==255&&memory[4]==0x6d,"half-open clipped fill and RGB packing");
    const auto saved=memory;MenuPixelSurfaceView invalid{std::span<unsigned char>(memory).first(7),2,1,8};
    expect(!invalid.valid(),"short span invalid");clearMenuPixels(invalid);fillMenuPixels(invalid,{0,0,2,1},{});darkenMenuPixels(invalid,{0,0,2,1},200);frameMenuPixels(invalid,{0,0,2,1},{});blitMenuPixels(invalid,{saved,2,1,8},0,0);scaleMenuPixels(invalid,{saved,2,1,8},{0,0,2,1});
    expect(memory==saved,"all raster operations reject short destination without writes");
    std::array<unsigned char,4> src{},dst{};MenuPixelSurfaceView d{dst,1,1,4};ConstMenuPixelSurfaceView s{src,1,1,4};
    for(unsigned alpha=0;alpha<256;++alpha)for(unsigned value=0;value<256;++value){
        src={static_cast<unsigned char>(value),37,211,static_cast<unsigned char>(alpha)};dst={123,111,99,71};const auto old=dst;
        blitMenuPixels(d,s,0,0);
        for(std::size_t ch=0;ch<3;++ch){const auto wanted=(static_cast<unsigned>(src[ch])*alpha+static_cast<unsigned>(old[ch])*(255-alpha))/255;expect(dst[ch]==wanted,"all alpha/channel integer rounding exact");}
        expect(dst[3]==(alpha==255?255:71),"intermediate/zero alpha preserves destination alpha");
    }
    for(unsigned opacity=0;opacity<301;++opacity)for(unsigned value=0;value<256;++value){
        dst={static_cast<unsigned char>(value),31,219,71};darkenMenuPixels(d,{0,0,1,1},opacity);
        expect(dst[0]==value*(255-std::min(255U,opacity))/255&&dst[3]==255,"opacity clamp, full channel rounding and opaque darken");
    }
    std::array<unsigned char,16> tiles{1,2,3,9,4,5,6,10,7,8,9,11,10,11,12,12};
    std::array<unsigned char,36> scaled{};scaleMenuPixels({scaled,3,3,12},{tiles,2,2,8},{0,0,3,3});
    constexpr int indices[9]{0,0,1,0,0,1,2,2,3};
    for(std::size_t i=0;i<9;++i){expect(scaled[i*4]==tiles[static_cast<std::size_t>(indices[i])*4]&&scaled[i*4+3]==255,"nearest integer sample golden");}
    const auto before=scaled;scaleMenuPixels({scaled,3,3,12},{tiles,2,2,8},{2,2,1,1});blitMenuPixels({scaled,3,3,12},{tiles,2,2,8},0,0,-1);expect(scaled==before,"reversed scale and negative clip height no-op");
    std::cout<<"Menu pixel surface: checks="<<checks<<" failures="<<failures<<" allocationFailure=preservesOld borrow=revoked stridePadding=untouched alpha=exact\n";
    return failures?1:0;
}
