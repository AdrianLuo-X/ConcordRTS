#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
#include "render/core/RuntimeMapPixelRules.h"
#include "render/lighting/RuntimeLightingLookupRaster.h"
#include "render/shadows/RuntimeShadowDibHalf.h"
#include <array>
#include <iostream>
#include <thread>
using namespace concord::render;
using namespace concord::presentation;
using namespace concord::presentation::win32;
namespace {
struct Fixture {
    int width=384,height=224;
    MapDrawList list;
    std::vector<std::uint32_t> expected;
    std::vector<unsigned char> image,indices,remap;
    std::array<std::uint32_t,256> palette{},lookup{};
    std::array<std::uint8_t,256> red{},green{},blue{};
    Fixture() : image(55*(73*4+13)),indices(256*4),remap(256) {
        for(std::size_t i=0;i<image.size();++i)image[i]=static_cast<unsigned char>(i*17+31);
        for(std::size_t i=0;i<indices.size();++i)indices[i]=static_cast<unsigned char>(i);
        for(unsigned i=0;i<256;++i) {
            remap[i]=static_cast<unsigned char>(255-i);
            palette[i]=(i*19U&255U)|((i*23U&255U)<<8U)|((i*3U&255U)<<16U);
            red[i]=static_cast<std::uint8_t>(std::min(255U,i*2U));
            green[i]=static_cast<std::uint8_t>(255U-i);blue[i]=static_cast<std::uint8_t>(i/3U);
            lookup[i]=static_cast<unsigned>(blue[i])|(static_cast<unsigned>(green[i])<<8U)|(static_cast<unsigned>(red[i])<<16U);
        }
    }
    void prepare(int w=384,int h=224) {
        width=w;height=h;list.begin();expected.assign(static_cast<std::size_t>(w)*h,0x57332f12U);
        list.fill({0,0,w,h},0x57332f12U);
        MapDrawCommand c;c.left=0;c.top=7;c.right=253;c.bottom=11;c.kind=MapDrawKind::Indexed;
        c.width=256;c.height=4;c.stride=256;c.originX=-3;c.originY=7;c.flags=3;c.parameter=255;
        c.offset=list.intern(indices);c.palette=list.internValues<std::uint32_t>(palette);c.auxiliary=list.intern(remap);list.push(c);
        for(int y=7;y<11;++y)for(int x=0;x<253;++x) {
            const unsigned i=indices[static_cast<std::size_t>(y-7)*256+x+3];if(i)at(x,y)=palette[remap[i]];
        }
        // Index zero is visible in this source rectangle; its remap is 255.
        c.left=300;c.right=556;c.originX=300;list.push(c);
        for(int y=7;y<11;++y)for(int x=300;x<std::min(w,556);++x) {
            const unsigned i=indices[static_cast<std::size_t>(y-7)*256+x-300];if(i)at(x,y)=palette[remap[i]];
        }
        c={};c.left=51;c.top=25;c.right=124;c.bottom=80;c.kind=MapDrawKind::Bgra;
        c.originX=51;c.originY=25;c.width=73;c.height=55;c.stride=73*4+13;c.offset=list.intern(image);list.push(c);
        for(int y=25;y<80;++y)for(int x=51;x<124;++x)std::memcpy(&at(x,y),image.data()+static_cast<std::size_t>(y-25)*(73*4+13)+(x-51)*4,4);
        c={};c.left=10;c.top=30;c.right=105;c.bottom=90;c.kind=MapDrawKind::Shade;c.parameter=static_cast<std::uint32_t>(-47);list.push(c);
        for(int y=30;y<90;++y)for(int x=10;x<105;++x) {
            auto& p=at(x,y);unsigned next=0;for(unsigned shift:{0U,8U,16U})next|=static_cast<unsigned>(std::max(0,static_cast<int>((p>>shift)&255U)-47))<<shift;p=next;
        }
        const std::array<std::uint32_t,1> tint{0x001fe3a7U};
        const auto tintOffset=list.internValues<std::uint32_t>(tint);
        for(unsigned alpha=0;alpha<256;++alpha) {
            c={};c.left=static_cast<int>(alpha);c.top=100;c.right=c.left+1;c.bottom=101;c.kind=MapDrawKind::Blend;
            c.originX=c.left;c.originY=100;c.width=c.height=1;c.stride=4;c.offset=tintOffset;c.parameter=alpha;list.push(c);
            at(c.left,100)=blendMapVisionRgb(at(c.left,100),tint[0],static_cast<int>(alpha));
        }
        c={};c.left=0;c.top=140;c.right=w;c.bottom=160;c.kind=MapDrawKind::Lookup;c.palette=list.internValues<std::uint32_t>(lookup);list.push(c);
        for(int y=140;y<160;++y)runtimeApplyLightingLookupScalarLeaf(&at(0,y),static_cast<std::size_t>(w),red,green,blue);
        // BEHIND is above lighting and below shroud. Order is observable here.
        list.fill({4,150,19,169},0x00d6d7dfU);for(int y=150;y<169;++y)for(int x=4;x<19;++x)at(x,y)=0x00d6d7dfU;
        std::vector<std::uint32_t> fog(256*48);
        for(int y=0;y<48;++y)for(unsigned x=0;x<256;++x)fog[static_cast<std::size_t>(y)*256+x]=0x0006090cU|(x<<24U);
        c={};c.left=0;c.top=150;c.right=256;c.bottom=198;c.kind=MapDrawKind::Vision;c.originY=150;
        c.width=256;c.height=48;c.stride=256*4;c.offset=list.internValues<std::uint32_t>(fog);list.push(c);
        for(int y=150;y<198;++y)for(int x=0;x<256;++x)at(x,y)=blendMapVisionRgb(at(x,y),0x0006090cU,x);
        world();
        list.fill({-20,h-3,w+20,h+12},0x004b2d1fU);
        for(int y=h-3;y<h;++y)for(int x=0;x<w;++x)at(x,y)=0x004b2d1fU;
    }
    void world() {
        MapDrawCommand c;std::vector<std::uint32_t> materials(256);for(unsigned i=0;i<256;++i)materials[i]=0x80000000U|i;materials[127]=0x006a93efU;
        c.left=0;c.top=12;c.right=256;c.bottom=13;c.originY=12;c.width=256;c.height=1;c.stride=1024;c.kind=MapDrawKind::Terrain;c.offset=list.internValues<std::uint32_t>(materials);c.palette=list.internValues<std::uint32_t>(palette);list.push(c);
        for(int x=0;x<256;++x)at(x,12)=x==127?materials[x]:palette[x];
        std::array<unsigned char,9> mask{1,0,1,0,1,0,1,0,1};std::array<std::uint32_t,9> pixels{0x91775533U,0x12345678U,0x31796553U,0x7199abcdU,0x00abcdefU,0U,0xff123456U,0U,0x21fedcbaU};
        c={};c.left=10;c.top=90;c.right=13;c.bottom=93;c.originX=10;c.originY=90;c.width=c.height=3;c.stride=12;c.kind=MapDrawKind::Bgra;c.offset=list.internValues<std::uint32_t>(pixels);c.flags=1;c.auxiliary=list.intern(mask);list.push(c);
        for(int y=0;y<3;++y)for(int x=0;x<3;++x)if(mask[y*3+x])at(x+10,y+90)=pixels[y*3+x];
        c.kind=MapDrawKind::Blend;c.parameter=128;list.push(c);for(int y=0;y<3;++y)for(int x=0;x<3;++x)if(mask[y*3+x])at(x+10,y+90)=blendMapVisionRgb(at(x+10,y+90),pixels[y*3+x],128);
        std::vector<unsigned char> shadow(256);for(unsigned i=0;i<256;++i)shadow[i]=static_cast<unsigned char>(i);
        for(unsigned mode=0;mode<8;++mode){c={};c.left=0;c.top=104+mode;c.right=256;c.bottom=c.top+1;c.originY=c.top;c.width=256;c.height=1;c.stride=256;c.kind=MapDrawKind::Shadow;c.flags=mode;c.offset=list.intern(shadow);list.push(c);
            for(int x=1;x<256;++x){const int alpha=(mode&4)?x:(mode&1)?128:std::max(1,(x*128+127)/255);auto& p=at(x,c.top);p=(mode&2)&&alpha==128?runtimeHalfBlackShadowDib(p):blendMapVisionRgb(p,0,alpha);}}
        std::vector<std::uint32_t> receivers(static_cast<std::size_t>(width)*2);std::array<std::array<std::uint32_t,256>,3> tables{};for(unsigned t=0;t<3;++t)for(unsigned i=0;i<256;++i)tables[t][i]=((std::min(255U,i*(t+1)))<<16U)|(((i+t*33U)&255U)<<8U)|((255U-i)/(t+1));
        for(std::size_t i=0;i<receivers.size();++i)receivers[i]=static_cast<unsigned>(i%3);c={};c.left=0;c.top=112;c.right=width;c.bottom=114;c.originY=112;c.width=width;c.height=2;c.stride=width*4;c.kind=MapDrawKind::ReceiverLookup;c.offset=list.internValues<std::uint32_t>(receivers);c.palette=list.internValues<std::array<std::uint32_t,256>>(tables);c.parameter=3;list.push(c);
        for(int y=112;y<114;++y)for(int x=0;x<width;++x){auto& p=at(x,y);const auto& t=tables[receivers[static_cast<std::size_t>(y-112)*width+x]];p=(p&0xff000000U)|(t[(p>>16)&255]&0xff0000U)|(t[(p>>8)&255]&0xff00U)|(t[p&255]&255U);}
        c={};c.left=0;c.top=200;c.right=width;c.bottom=height-4;c.originX=150;c.originY=105;c.width=192;c.kind=MapDrawKind::LocalLight;c.parameter=0xf79bfb55U;c.auxiliary=static_cast<unsigned>(width*(height-204)-7);list.push(c);
        const std::int64_t square=192LL*192,denominator=255LL*255*square;for(int y=c.top;y<c.bottom;++y)for(int x=c.left;x<c.right;++x){if(static_cast<unsigned>((y-c.top)*width+x)>=c.auxiliary)continue;const auto distance=static_cast<std::int64_t>(x-150)*(x-150)+static_cast<std::int64_t>(y-105)*(y-105);if(distance>square)continue;const auto p=at(x,y);std::uint32_t next=0;for(unsigned shift:{0U,8U,16U}){const auto channel=(p>>shift)&255U,light=(c.parameter>>shift)&255U;const auto numerator=static_cast<std::int64_t>(255U-channel)*light*247*(square-distance);next|=static_cast<std::uint32_t>(std::clamp(static_cast<int>(channel+(numerator+denominator/2)/denominator),0,255))<<shift;}at(x,y)=next;}
        c={};c.left=260;c.top=24;c.right=300;c.bottom=91;c.kind=MapDrawKind::Save;list.push(c);const auto before=expected;list.fill({270,30,295,70},0x00effd9bU);for(int y=30;y<70;++y)for(int x=270;x<295;++x)at(x,y)=0x00effd9bU;c.kind=MapDrawKind::RestoreBlend;c.parameter=128;list.push(c);for(int y=24;y<91;++y)for(int x=260;x<300;++x){const auto b=before[static_cast<std::size_t>(y)*width+x];if(at(x,y)!=b)at(x,y)=blendMapVisionRgb(b,at(x,y),128);}
        std::vector<std::array<std::uint32_t,2>> descriptors(70*60);const std::array<std::array<std::uint32_t,2>,6> ops{{{0,256},{0x00ff0102U,128},{0x0013dc45U,191},{0,257},{0x00d49219U,0},{0x000103ffU,53}}};for(std::size_t i=0;i<descriptors.size();++i)descriptors[i]={0,static_cast<unsigned>(i%2?6:4)};
        c={};c.left=130;c.top=33;c.right=200;c.bottom=93;c.originX=130;c.originY=33;c.width=70;c.height=60;c.stride=70*8;c.kind=MapDrawKind::Chain;c.offset=list.internValues<std::array<std::uint32_t,2>>(descriptors);c.palette=list.internValues<std::array<std::uint32_t,2>>(ops);c.parameter=6;list.push(c);
        for(int y=33;y<93;++y)for(int x=130;x<200;++x){auto& p=at(x,y);const auto b=p;p=blendMapVisionRgb(p,ops[1][0],128);p=blendMapVisionRgb(p,ops[2][0],191);p=blendMapVisionRgb(b,p,128);if(((y-33)*70+x-130)%2){p=blendMapVisionRgb(p,ops[4][0],0);p=blendMapVisionRgb(p,ops[5][0],53);}}
    }
    std::uint32_t& at(int x,int y){return expected[static_cast<std::size_t>(y)*width+x];}
    bool same(const std::vector<unsigned char>& bytes) const {
        return bytes.size()==expected.size()*4&&std::memcmp(bytes.data(),expected.data(),bytes.size())==0;
    }
};
#include "MapReceiverPackedD3D11Checks.inl"
}
int main() {
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"S41 kernel "<<message<<'\n';}};
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Concord S41 integer map kernel",
        WS_POPUP,20,20,384,224,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"owned real window");if(!window)return 1;
    for(auto driver:{FrameDevice::Hardware,FrameDevice::Warp}) {
        SetWindowPos(window,HWND_TOPMOST,0,0,384,224,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        runPackedReceiverGpuContract(window,driver,check);
        Fixture f;f.prepare();check(f.list.valid(),"bounded pure command values");
        FramePresentStatistics counters;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
        auto gpu=D3D11FramePresenterWin32::create(window,{FrameBackend::D3D11,driver},counters,&probe);
        check(gpu&&gpu->active(),"requested real device exists");if(!gpu||!gpu->active())continue;
        check(gpu->report().softwareDevice==(driver==FrameDevice::Warp),"hardware and WARP separate");
        const auto before=f.expected;MapDrawFrame frame{f.list,1,f.width,f.height,19};
        check(gpu->presentMap(frame),"actual ordered GPU map commands");
        check(f.same(probe.uploadedPixels),"entire BGRA exact against existing CPU lookup and shroud primitives");
        check(f.expected==before,"reference storage immutable");
        check(!gpu->presentMap(frame),"duplicate submission rejected");
        const auto count=gpu->report().mapDraws;
        bool foreign=true;std::thread other([&]{foreign=gpu->presentMap({f.list,2,f.width,f.height});});other.join();
        check(!foreign&&gpu->report().mapDraws==count,"wrong owner cannot access GPU");
        MapDrawFrame stale{f.list,90,f.width,f.height};f.image[50]^=19;f.prepare();
        check(!gpu->presentMap(stale),"stale resource/command revision rejected");
        check(gpu->presentMap({f.list,2,f.width,f.height}),"equal-size in-place bytes upload");check(f.same(probe.uploadedPixels),"equal-size reload pixels exact");
        MapDrawFrame old{f.list,91,f.width,f.height};f.list.reset();f.prepare();check(!gpu->presentMap(old),"old resource generation rejected");
        check(gpu->presentMap({f.list,3,f.width,f.height}),"new generation");check(f.same(probe.uploadedPixels),"generation full pixels exact");
        MapDrawList invalid;MapDrawCommand invalidCommand;invalidCommand.right=invalidCommand.bottom=1;
        invalidCommand.kind=MapDrawKind::Bgra;invalidCommand.width=invalidCommand.height=1;invalidCommand.stride=4;
        invalidCommand.offset=UINT32_MAX;invalid.push(invalidCommand);
        check(!gpu->presentMap({invalid,4,f.width,f.height}),"unbounded payload rejected before GPU access");
        check(!gpu->presentMap({f.list,4,8193,224}),"extent budget rejected");
        f.prepare(401,231);SetWindowPos(window,nullptr,0,0,401,231,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        check(gpu->presentMap({f.list,4,f.width,f.height}),"odd client resize");check(f.same(probe.uploadedPixels),"all resize pixels exact");
        check(gpu->timingCount()==0,"diagnostic readback excluded from formal timestamps");
        const auto reads=probe.readbacks;probe.captureUpload=false;
        for(std::uint64_t i=5;i<25;++i)check(gpu->presentMap({f.list,i,f.width,f.height,i}),"normal raster without readback");
        check(probe.readbacks==reads,"normal path maps no GPU output");check(gpu->report().gpuValid>0,"real asynchronous GPU pass query");
        probe.failUpload=true;check(!gpu->presentMap({f.list,25,f.width,f.height}),"upload failure reported");check(!gpu->active(),"failure releases GPU before software fallback");
        check(!gpu->presentMap({f.list,26,f.width,f.height}),"permanent software state");gpu.reset();
    }
    DestroyWindow(window);std::cout<<"S41 map kernel checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}