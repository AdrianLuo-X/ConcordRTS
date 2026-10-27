#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
#include "assets/menu/MenuImage.h"
#include <iostream>
#include <thread>
using namespace concord::ui;
using namespace concord::presentation;
using namespace concord::presentation::win32;
int main() {
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"S40 synthetic "<<message<<'\n';}};
    HWND window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Concord S40 integer raster",
        WS_POPUP,20,20,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(window!=nullptr,"owned real HWND");if(!window)return 1;
    SetWindowPos(window,HWND_TOPMOST,0,0,800,600,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    for(auto driver:{FrameDevice::Hardware,FrameDevice::Warp}) {
        FramePresentStatistics stats;D3D11FrameProbeWin32 probe;probe.captureUpload=true;
        auto gpu=D3D11FramePresenterWin32::create(window,{FrameBackend::D3D11,driver},stats,&probe);
        check(gpu&&gpu->active(),"actual requested device");if(!gpu||!gpu->active())continue;
        check(gpu->report().softwareDevice==(driver==FrameDevice::Warp),"hardware and WARP classified separately");
        MenuPixelBuffer cpu;check(cpu.reset(800,600),"reference pixels");
        MenuDrawList list;
        std::vector<unsigned char> bytes(55*(73*4+13),0xAD);
        for(int y=0;y<55;++y)for(int x=0;x<73;++x)for(int c=0;c<4;++c)
            bytes[static_cast<std::size_t>(y)*(73*4+13)+x*4+c]=static_cast<unsigned char>(x*17+y*7+c*13);
        const ConstMenuPixelSurfaceView source{bytes,73,55,73*4+13};
        std::vector<unsigned char> indices(256*5);
        for(std::size_t i=0;i<indices.size();++i)indices[i]=static_cast<unsigned char>(i);
        std::vector<RuntimePaletteColor> palette(256);
        for(std::size_t i=0;i<palette.size();++i)palette[i]={static_cast<unsigned char>((i&63)*4),
            static_cast<unsigned char>(255-i),static_cast<unsigned char>(i)};
        std::vector<unsigned char> remap(256);for(std::size_t i=0;i<256;++i)remap[i]=static_cast<unsigned char>(255-i);
        const auto prepare=[&] {
            list.begin();clearMenuPixels(cpu.view());list.fill({0,0,800,600},RuntimeRgbColorValue::fromChannels(0,0,0));
            const RuntimeInputPixelRect full{-5,-7,810,610};const auto color=RuntimeRgbColorValue::fromChannels(113,59,201);
            fillMenuPixels(cpu.view(),full,color);list.fill(full,color);
            for(int i=0;i<256;++i) {
                const RuntimeInputPixelRect r{(i%32)*25,(i/32)*32,(i%32)*25+25,(i/32)*32+32};
                darkenMenuPixels(cpu.view(),r,static_cast<unsigned int>(i));list.darken(r,static_cast<unsigned int>(i));
            }
            blitMenuPixels(cpu.view(),source,-9,290,43);list.bgra(source,{-9,290,64,333},false);
            blitMenuPixels(cpu.view(),source,15,305);list.bgra(source,{15,305,88,360},false);
            const RuntimeInputPixelRect scale{-11,358,173,405};scaleMenuPixels(cpu.view(),source,scale);list.bgra(source,scale,true);
            for(bool transparent:{false,true}) {
                concord::assets::MenuImage image;
                check(concord::assets::makeMenuImage({5,1,270,8,256,5,indices},palette,"synthetic",transparent,image),"same pure indexed resource producer");
                const int top=transparent?445:420;
                blitMenuPixels(cpu.view(),{image.bgra,image.width,image.height,static_cast<std::size_t>(image.width)*4},-3,top,6);
                list.image(image,-3,top,6);
                if(transparent) {
                    auto mapped=palette;for(std::size_t i=0;i<256;++i)mapped[i]=palette[remap[i]];
                    concord::assets::MenuImage reference;
                    check(concord::assets::makeMenuImage({5,1,270,8,256,5,indices},mapped,"remapped",true,reference),"remap oracle");
                    blitMenuPixels(cpu.view(),{reference.bgra,reference.width,reference.height,static_cast<std::size_t>(reference.width)*4},3,475);
                    list.image(image,3,475,8);list.commands.back().flags|=2;
                    list.commands.back().reserved0=list.intern(remap);
                }
            }
        };
        prepare();const auto sourceBefore=bytes;const auto resourcesBefore=list.resources;
        MenuDrawFrame frame{list,1,800,600,{0,0,800,600}};
        check(gpu->presentMenu(frame),"full integer raster, all alpha/index/255/remap/clip/order cases");
        check(probe.uploadedPixels==cpu.bytes(),"every BGRA byte equals original software painters");
        check(bytes==sourceBefore&&list.resources==resourcesBefore,"source/padding/owned resources immutable");
        check(!gpu->presentMenu(frame),"duplicate sequence rejected");
        const auto draws=gpu->report().menuDraws;
        bool foreign=true;std::thread other([&]{foreign=gpu->presentMenu({list,100,800,600,{0,0,800,600}});});other.join();
        check(!foreign&&gpu->report().menuDraws==draws,"foreign owner rejected without GPU access");
        MenuDrawFrame staleCommands{list,200,800,600,{0,0,800,600}};
        bytes[100]^=17;prepare();
        check(!gpu->presentMenu(staleCommands),"mutated command/resource borrow rejected before sequence publication");
        check(!gpu->presentMenu({list,2,800,600,{0,0,0,600}}),"invalid viewport rejected");
        check(gpu->presentMenu({list,3,800,600,{0,0,800,600}}),"equal-size in-place resource change published");
        check(probe.uploadedPixels==cpu.bytes(),"reloaded resource bytes exact");
        MenuDrawFrame staleResources{list,201,800,600,{0,0,800,600}};
        const auto oldGeneration=list.generation;list.reset();check(list.generation!=oldGeneration,"explicit resource generation invalidation");
        prepare();check(gpu->presentMenu({list,4,800,600,{0,0,800,600}}),"new generation installs owned resources");
        check(!gpu->presentMenu(staleResources),"old resource generation rejected");
        check(probe.uploadedPixels==cpu.bytes(),"generation replacement full pixels exact");
        check(gpu->timingCount()==0,"diagnostic readback not formal timing");
        const auto readbacks=probe.readbacks;probe.captureUpload=false;
        for(std::uint64_t i=5;i<45;++i)check(gpu->presentMenu({list,i,800,600,{0,0,800,600}}),"normal no-readback GPU raster");
        check(probe.readbacks==readbacks,"normal raster never maps GPU output to CPU");
        check(gpu->report().gpuValid>0,"real asynchronous GPU timestamp samples");
        gpu.reset();
    }
    DestroyWindow(window);
    std::cout<<"S40 synthetic raster: checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
