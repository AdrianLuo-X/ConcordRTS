#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
namespace concord::presentation::win32 {
using Microsoft::WRL::ComPtr;
using Clock=std::chrono::steady_clock;
namespace {
double elapsed(Clock::time_point start) noexcept {
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();
}
const char* backendName(FrameBackend b) noexcept {return b==FrameBackend::D3D11?"d3d11":"software";}
}
struct D3D11FramePresenterWin32::Impl {
    HWND window=nullptr,fontWindow=nullptr;
    DWORD thread=GetCurrentThreadId();
    D3D11FrameReport report;
    D3D11FrameProbeWin32* probe=nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11Texture2D> back;
    int width=0,height=0,fontWidth=0,fontHeight=0;
    struct Query {
        ComPtr<ID3D11Query> disjoint,start,end;
        bool pending=false;
        std::size_t sample=0;
    };
    std::array<Query,8> queries;
    std::array<D3D11FrameTiming,4096> samples{};
    std::size_t sampleCount=0;
    bool printed=false,printTimings=false;
    void collect() noexcept {
        if(!context)return;
        for(auto& q:queries)if(q.pending){
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};UINT64 start=0,end=0;
            const HRESULT a=context->GetData(q.disjoint.Get(),&d,sizeof(d),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const HRESULT b=context->GetData(q.start.Get(),&start,sizeof(start),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            const HRESULT c=context->GetData(q.end.Get(),&end,sizeof(end),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(a==S_FALSE||b==S_FALSE||c==S_FALSE)continue;
            q.pending=false;
            if(SUCCEEDED(a)&&SUCCEEDED(b)&&SUCCEEDED(c)&&!d.Disjoint&&d.Frequency&&end>=start){
                ++report.gpuValid;
                if(q.sample<sampleCount){auto& s=samples[q.sample];s.gpuValid=true;s.gpuFrequency=d.Frequency;s.gpuStart=start;s.gpuEnd=end;s.gpuUploadMs=1000.0*static_cast<double>(end-start)/static_cast<double>(d.Frequency);}
            }else ++report.gpuInvalid;
        }
    }
    void release() noexcept {
        collect();
        for(auto& q:queries){if(q.pending){++report.gpuPending;q.pending=false;}q.disjoint.Reset();q.start.Reset();q.end.Reset();}
        // One-way transition: release ALL GPU references before any GDI fallback.
        back.Reset();
        if(context){context->ClearState();context->Flush();}
        chain.Reset();context.Reset();device.Reset();width=height=0;
        if(fontWindow&&IsWindow(fontWindow))DestroyWindow(fontWindow);
        fontWindow=nullptr;fontWidth=fontHeight=0;
    }
    void fail(const char* reason,HRESULT hr) noexcept {
        const bool transition=report.selected==FrameBackend::D3D11;
        std::snprintf(report.fallbackReason.data(),report.fallbackReason.size(),"%s",reason);
        report.failureCode=hr;
        if(device)report.removedReason=device->GetDeviceRemovedReason();
        release();report.selected=FrameBackend::Software;
        if(transition)std::printf("Frame backend transition requested=d3d11 selected=software fallback=%s failureCode=%ld removedReason=%ld\n",report.fallbackReason.data(),report.failureCode,report.removedReason);
    }
    bool initialize() noexcept {
        if(report.requested.backend==FrameBackend::Software)return true;
        if(!window||GetWindowThreadProcessId(window,nullptr)!=thread){fail("window-owner",E_INVALIDARG);return false;}
        constexpr D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0,D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        D3D_FEATURE_LEVEL chosen{};
        const auto driver=report.requested.device==FrameDevice::Warp?D3D_DRIVER_TYPE_WARP:D3D_DRIVER_TYPE_HARDWARE;
        HRESULT hr=D3D11CreateDevice(nullptr,driver,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_SINGLETHREADED,
            levels,static_cast<UINT>(std::size(levels)),probe&&probe->failDevice?D3D11_SDK_VERSION+1:D3D11_SDK_VERSION,&device,&chosen,&context);
        // Older runtimes may reject the newer feature-level enumeration.
        if(hr==E_INVALIDARG&&!(probe&&probe->failDevice))hr=D3D11CreateDevice(nullptr,driver,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_SINGLETHREADED,
            levels+3,1,D3D11_SDK_VERSION,&device,&chosen,&context);
        if(FAILED(hr)){fail("device-create",hr);return false;}
        report.featureLevel=chosen;
        hr=device->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM,&report.formatSupport);
        if(FAILED(hr)||(report.formatSupport&(D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_RENDER_TARGET))
            !=(D3D11_FORMAT_SUPPORT_TEXTURE2D|D3D11_FORMAT_SUPPORT_RENDER_TARGET)){fail("BGRA8-format",FAILED(hr)?hr:E_FAIL);return false;}
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter1> adapter;ComPtr<IDXGIAdapter> baseAdapter;ComPtr<IDXGIFactory> factory;
        hr=device.As(&dxgi);if(SUCCEEDED(hr))hr=dxgi->GetAdapter(&baseAdapter);
        if(SUCCEEDED(hr))hr=baseAdapter.As(&adapter);
        DXGI_ADAPTER_DESC1 desc{};
        if(SUCCEEDED(hr))hr=adapter->GetDesc1(&desc);
        if(SUCCEEDED(hr))hr=adapter->GetParent(IID_PPV_ARGS(&factory));
        if(FAILED(hr)){fail("adapter-query",hr);return false;}
        report.adapterFlags=desc.Flags;report.softwareDevice=(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE)!=0;
        if(report.requested.device==FrameDevice::Hardware&&report.softwareDevice){fail("hardware-request-selected-software",E_FAIL);return false;}
        LARGE_INTEGER version{};if(SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version)))report.driverVersion=static_cast<std::uint64_t>(version.QuadPart);
        WideCharToMultiByte(CP_UTF8,0,desc.Description,-1,report.adapter.data(),static_cast<int>(report.adapter.size()),nullptr,nullptr);
        std::printf("D3D11 adapter selection=default actualVendorId=%u actualDeviceId=%u luidHigh=%ld luidLow=%lu minimumFeatureLevel=45056\n",desc.VendorId,desc.DeviceId,desc.AdapterLuid.HighPart,desc.AdapterLuid.LowPart);
        // Deliberate DXGI bitblt model: this stage must support a permanent,
        // coordinated transition back to the original GDI HWND after failure.
        // No GDI writes to that HWND are allowed while this chain is alive.
        // Exact client-size copy needs no shader/sampler/sRGB transform.
        DXGI_SWAP_CHAIN_DESC swap{};
        swap.BufferDesc.Width=1;swap.BufferDesc.Height=1;
        swap.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        swap.SampleDesc.Count=1;swap.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.BufferCount=1;swap.OutputWindow=window;swap.Windowed=TRUE;
        swap.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        if(probe&&probe->failSwapChain)swap.BufferCount=0; // Real API error, no allocation stress.
        hr=factory->CreateSwapChain(device.Get(),&swap,&chain);
        if(SUCCEEDED(hr))hr=factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER|DXGI_MWA_NO_WINDOW_CHANGES);
        if(FAILED(hr)){fail("swap-chain-create",hr);return false;}
        for(auto& q:queries){
            D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
            if(probe&&probe->failResource)d.Query=static_cast<D3D11_QUERY>(0x7fffffff);
            hr=device->CreateQuery(&d,&q.disjoint);
            d.Query=D3D11_QUERY_TIMESTAMP;
            if(SUCCEEDED(hr))hr=device->CreateQuery(&d,&q.start);
            if(SUCCEEDED(hr))hr=device->CreateQuery(&d,&q.end);
            if(FAILED(hr)){fail("timing-resource-create",hr);return false;}
        }
        report.selected=FrameBackend::D3D11;return true;
    }
    bool ensureSize(int w,int h) noexcept {
        if(w==width&&h==height&&back)return true;
        back.Reset();context->ClearState();
        const HRESULT hr=chain->ResizeBuffers(probe&&probe->failResize?17u:1u,static_cast<UINT>(w),static_cast<UINT>(h),DXGI_FORMAT_B8G8R8A8_UNORM,0);
        if(FAILED(hr)){fail("resize-buffers",hr);return false;}
        const HRESULT got=chain->GetBuffer(0,IID_PPV_ARGS(&back));
        if(FAILED(got)){fail("back-buffer-create",got);return false;}
        width=w;height=h;return true;
    }
    bool capture() noexcept {
        if(!probe||!probe->captureUpload)return true;
        D3D11_TEXTURE2D_DESC d{};back->GetDesc(&d);
        d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
        ComPtr<ID3D11Texture2D> staging;HRESULT hr=device->CreateTexture2D(&d,nullptr,&staging);
        if(FAILED(hr)){fail("diagnostic-readback-create",hr);return false;}
        context->CopyResource(staging.Get(),back.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
        if(FAILED(hr)){fail("diagnostic-readback-map",hr);return false;}
        bool ok=true;
        try{
            probe->uploadedPixels.resize(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4);
            for(int y=0;y<height;++y)std::memcpy(probe->uploadedPixels.data()+static_cast<std::size_t>(y)*static_cast<std::size_t>(width)*4,
                static_cast<const unsigned char*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,static_cast<std::size_t>(width)*4);
            ++probe->readbacks;
        }catch(...){ok=false;}
        context->Unmap(staging.Get(),0);
        if(!ok)fail("diagnostic-readback-allocation",E_OUTOFMEMORY);
        return ok;
    }
};
D3D11FramePresenterWin32::D3D11FramePresenterWin32(FramePresentStatistics& s,std::unique_ptr<Impl> impl) noexcept
    :FramePresenter(s),impl_(std::move(impl)){}
std::unique_ptr<D3D11FramePresenterWin32> D3D11FramePresenterWin32::create(HWND window,
    FrameBackendOptions request,FramePresentStatistics& statistics,D3D11FrameProbeWin32* probe) noexcept {
    try{
        auto p=std::make_unique<Impl>();p->window=window;p->report.requested=request;p->probe=probe;
        p->initialize();
        auto result=std::unique_ptr<D3D11FramePresenterWin32>(new D3D11FramePresenterWin32(statistics,std::move(p)));
        const auto& r=result->report();
        std::cout<<"Frame backend requested="<<backendName(request.backend)<<" selected="<<backendName(r.selected)
            <<" driver="<<(request.device==FrameDevice::Warp?"warp":"hardware")<<" adapter=\""<<r.adapter.data()
            <<"\" softwareDevice="<<r.softwareDevice<<" featureLevel="<<r.featureLevel
            <<" format=BGRA8_UNORM formatSupport="<<r.formatSupport<<" vsync=1 fallback=\""<<r.fallbackReason.data()<<"\"\n";
        return result;
    }catch(...){return {};}
}
D3D11FramePresenterWin32::~D3D11FramePresenterWin32(){impl_->release();printReport();}
bool D3D11FramePresenterWin32::onOwningThread() const noexcept {return GetCurrentThreadId()==impl_->thread;}
bool D3D11FramePresenterWin32::active() const noexcept {return impl_->report.selected==FrameBackend::D3D11;}
void D3D11FramePresenterWin32::fallbackToSoftware(const char* reason,long code) noexcept {
    if(onOwningThread())impl_->fail(reason,code);
}
const D3D11FrameReport& D3D11FramePresenterWin32::report() const noexcept{return impl_->report;}
const D3D11FrameTiming* D3D11FramePresenterWin32::timings() const noexcept{return impl_->samples.data();}
std::size_t D3D11FramePresenterWin32::timingCount() const noexcept{return impl_->sampleCount;}
void D3D11FramePresenterWin32::enableTimingOutput() noexcept{if(onOwningThread())impl_->printTimings=true;}
void D3D11FramePresenterWin32::recordCpuWork(double drawMs,double compositionMs) noexcept {
    if(onOwningThread()&&impl_->sampleCount){auto& s=impl_->samples[impl_->sampleCount-1];s.cpuDrawMs=drawMs;s.compositionMs=compositionMs;}
}
bool D3D11FramePresenterWin32::submitCompleteFrame(const SoftwareFrameView& f) noexcept {
    auto& p=*impl_;
    if(!active())return false;
    if(!IsWindow(p.window)){p.fail("window-closed",E_HANDLE);return false;}
    if(IsIconic(p.window)||!IsWindowVisible(p.window))return false;
    RECT client{};if(!GetClientRect(p.window,&client)||client.right!=f.pixels.width||client.bottom!=f.pixels.height
        ||f.pixels.rowStride>std::numeric_limits<UINT>::max())return false;
    if(!p.ensureSize(f.pixels.width,f.pixels.height))return false;
    p.collect();
    if(p.probe&&p.probe->deviceRemoved){p.report.removedReason=DXGI_ERROR_DEVICE_REMOVED;p.fail("device-removed-injected",DXGI_ERROR_DEVICE_REMOVED);p.report.removedReason=DXGI_ERROR_DEVICE_REMOVED;return false;}
    if(p.probe&&p.probe->failUpload){p.fail("upload-injected",E_FAIL);return false;}
    const auto index=p.sampleCount;
    D3D11FrameTiming* sample=nullptr;
    // Diagnostic readbacks and normal performance samples are disjoint.
    if(!(p.probe&&p.probe->captureUpload)){
        if(index<p.samples.size()){sample=&p.samples[p.sampleCount++];sample->sequence=f.sequence;sample->tick=f.simulationTick;
            sample->uploadBytes=static_cast<std::uint64_t>(f.pixels.width)*static_cast<std::uint64_t>(f.pixels.height)*4;}
        else ++p.report.timingDropped;
    }
    Impl::Query* query=nullptr;
    if(sample){for(auto& q:p.queries)if(!q.pending){query=&q;break;}if(!query)++p.report.gpuDropped;}
    if(query){query->sample=index;p.context->Begin(query->disjoint.Get());p.context->End(query->start.Get());}
    const auto uploadStart=Clock::now();
    p.context->UpdateSubresource(p.back.Get(),0,nullptr,f.pixels.bytes.data(),static_cast<UINT>(f.pixels.rowStride),0);
    if(sample)sample->uploadCpuMs=elapsed(uploadStart);
    if(query){p.context->End(query->end.Get());p.context->End(query->disjoint.Get());query->pending=true;}
    p.report.uploadedBytes+=static_cast<std::uint64_t>(f.pixels.width)*static_cast<std::uint64_t>(f.pixels.height)*4;
    if(!p.capture())return false;
    const auto presentStart=Clock::now();
    const HRESULT hr=p.chain->Present(p.probe&&p.probe->failPresent?5u:1u,0);
    if(sample)sample->presentCpuMs=elapsed(presentStart);
    if(FAILED(hr)){p.fail("present-failed",hr);return false;}
    if(hr==DXGI_STATUS_OCCLUDED)return false; // No claimed successful visible submission.
    if(p.probe&&p.probe->captureUpload){const HRESULT sync=DwmFlush();if(FAILED(sync)){p.fail("diagnostic-visible-sync",sync);return false;}}
    const HRESULT removed=p.device->GetDeviceRemovedReason();
    if(FAILED(removed)){p.fail("device-removed",removed);return false;}
    ++p.report.presented;return true;
}
bool D3D11FramePresenterWin32::composeDeviceOverlay(const concord::ui::win32::MainBackBufferWin32& clean,
    concord::ui::win32::MainBackBufferWin32& completed,RuntimeInputPixelRect r,DrawWindowOverlay draw,void* cookie) noexcept {
    auto& p=*impl_;
    if(!onOwningThread()||!active()||!draw||!clean.pixelView().valid()||!completed.pixelView().valid()
        ||clean.width!=completed.width||clean.height!=completed.height||r.left<0||r.top<0
        ||GetCurrentObject(clean.memoryDc,OBJ_BITMAP)!=clean.bitmap||GetCurrentObject(completed.memoryDc,OBJ_BITMAP)!=completed.bitmap
        ||r.right<=r.left||r.bottom<=r.top||r.right>clean.width||r.bottom>clean.height)return false;
    // A transparent GDI-only device target preserves the original window-device
    // font raster. It is NOT the DXGI HWND, never receives input/focus, and no
    // GPU resource is read back. Its tiny ROI is copied into the final CPU DIB.
    POINT origin{r.left,r.top};if(!ClientToScreen(p.window,&origin))return false;
    const int w=r.right-r.left,h=r.bottom-r.top;
    if(!p.fontWindow){
        p.fontWindow=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
            L"STATIC",L"Concord private CPU font target",WS_POPUP,origin.x,origin.y,w,h,
            p.window,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!p.fontWindow||!SetLayeredWindowAttributes(p.fontWindow,0,0,LWA_ALPHA))return false;
    }
    if(!SetWindowPos(p.fontWindow,nullptr,origin.x,origin.y,w,h,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW))return false;
    p.fontWidth=w;p.fontHeight=h;
    HDC dc=GetDC(p.fontWindow);if(!dc)return false;
    const int saved=SaveDC(dc);bool ok=false;
    try{
        if(saved&&BitBlt(dc,0,0,w,h,clean.memoryDc,r.left,r.top,SRCCOPY)
            &&SetViewportOrgEx(dc,-r.left,-r.top,nullptr)){
            draw(dc,cookie);
            ok=GdiFlush()!=FALSE;
            if(!RestoreDC(dc,saved))ok=false;
            if(ok)ok=BitBlt(completed.memoryDc,r.left,r.top,w,h,dc,0,0,SRCCOPY)!=FALSE&&GdiFlush()!=FALSE;
        }else if(saved)(void)RestoreDC(dc,saved);
    }catch(...){if(saved)(void)RestoreDC(dc,saved);ok=false;}
    return ReleaseDC(p.fontWindow,dc)!=0&&ok;
}
void D3D11FramePresenterWin32::printReport() noexcept {
    auto& p=*impl_;if(p.printed)return;p.printed=true;p.collect();
    try{
        const auto& r=p.report;
        std::cout<<"D3D11 presentation summary requested="<<backendName(r.requested.backend)<<" selected="<<backendName(r.selected)
            <<" hardware="<<(r.featureLevel>=0xB000&&!r.softwareDevice)<<" featureLevel="<<r.featureLevel<<" driverVersion="<<r.driverVersion
            <<" frames="<<r.presented<<" uploadBytes="<<r.uploadedBytes<<" gpuValid="<<r.gpuValid<<" gpuInvalid="<<r.gpuInvalid
            <<" gpuPending="<<r.gpuPending<<" gpuDropped="<<r.gpuDropped<<" timingDropped="<<r.timingDropped
            <<" failureCode="<<r.failureCode<<" removedReason="<<r.removedReason<<" fallback=\""<<r.fallbackReason.data()
            <<"\" worldDrawing=CPU shaderCompiler=none sampling=exact-client-copy color=BGRA8-UNORM-no-conversion vsync=1 displayFpsMeasured=false\n";
        for(std::size_t i=0;p.printTimings&&i<p.sampleCount;++i){const auto& s=p.samples[i];
            std::cout<<"D3D11 timing sequence="<<s.sequence<<" tick="<<s.tick<<" cpuDrawMs="<<s.cpuDrawMs<<" compositionMs="<<s.compositionMs
                <<" uploadBytes="<<s.uploadBytes<<" uploadCpuMs="<<s.uploadCpuMs<<" presentCpuMs="<<s.presentCpuMs
                <<" gpuValid="<<s.gpuValid<<" gpuUploadMs="<<s.gpuUploadMs<<" gpuFrequency="<<s.gpuFrequency<<" gpuStart="<<s.gpuStart<<" gpuEnd="<<s.gpuEnd<<'\n';}
    }catch(...){}
}
} // namespace concord::presentation::win32
