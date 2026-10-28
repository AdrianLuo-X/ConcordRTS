// Private diagnostic consumer: same owned packets/device/shader as production.
#include "runtime/presentation/win32/D3D11FramePresenterWin32.h"
namespace concord::ui::win32::diagnostics {
class MapVisionGpuFixture {
    HWND window_=nullptr;
    SkirmishBackBufferWin32 source_;
    MapNativeOverlayWin32 native_;
    concord::render::MapDrawList list_;
    concord::presentation::FramePresentStatistics counters_;
    concord::presentation::win32::D3D11FrameProbeWin32 probe_;
    std::unique_ptr<concord::presentation::win32::D3D11FramePresenterWin32> gpu_;
    std::uint64_t sequence_=0;
    int width_=0,height_=0;
    static void require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
public:
    explicit MapVisionGpuFixture(bool warp) {
        using namespace concord::presentation;
        window_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"Concord vision GPU contract",WS_POPUP,20,20,320,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(window_!=nullptr,"vision diagnostic own window");probe_.captureUpload=true;
        gpu_=concord::presentation::win32::D3D11FramePresenterWin32::create(window_,{FrameBackend::D3D11,warp?FrameDevice::Warp:FrameDevice::Hardware},counters_,&probe_);
        if(!gpu_||!gpu_->active()){DestroyWindow(window_);window_=nullptr;throw std::runtime_error("vision diagnostic actual requested GPU");}
    }
    ~MapVisionGpuFixture(){native_.release();gpu_.reset();(void)source_.release();if(window_)DestroyWindow(window_);}
    void begin(int width,int height,const std::vector<std::uint32_t>& pixels) {
        require(pixels.size()==static_cast<std::size_t>(width)*height,"vision source dimensions");
        require(SetWindowPos(window_,HWND_TOPMOST,0,0,width,height,SWP_NOMOVE|SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"vision real client resize");
        HDC reference=GetDC(window_);const bool ready=source_.ensure(reference,width,height);ReleaseDC(window_,reference);require(ready,"vision original DIB owner");
        require(source_.finishGdiWrites(),"vision source flush");std::memcpy(source_.writablePixels(),pixels.data(),pixels.size()*4);
        list_.reset();width_=width;height_=height;
        concord::render::MapDrawCommand base;base.right=width;base.bottom=height;base.kind=concord::render::MapDrawKind::Bgra;base.width=width;base.height=height;base.stride=width*4;
        base.offset=list_.internValues<std::uint32_t>(pixels);list_.push(base);
        require(native_.begin(source_.dc(),width,height,list_,reinterpret_cast<const std::uint32_t*>(source_.pixels()))!=nullptr,"vision original native bridge");
    }
    auto* pixels() noexcept{return reinterpret_cast<std::uint32_t*>(source_.writablePixels());}
    auto drawDc() const noexcept{return native_.dc();}
    void finish(const std::vector<std::uint32_t>& source) {
        require(native_.finish(),native_.report().failure.data());require(list_.valid(),"vision complete owned packet");
        require(std::memcmp(source_.pixels(),source.data(),source.size()*4)==0,"GPU vision producer must not perform CPU RGB composition");
        require(native_.report().visionEffects>0,"actual nonempty GPU vision display operations");
        require(gpu_->presentMap({list_,++sequence_,width_,height_}),"vision actual GPU submit");
        require(probe_.readbacks==sequence_,"one explicit diagnostic readback per vision frame");
    }
    bool equals(const std::vector<std::uint32_t>& expected,const std::string& label) const {
        if(probe_.uploadedPixels.size()!=expected.size()*4)return false;
        if(std::memcmp(probe_.uploadedPixels.data(),expected.data(),expected.size()*4)==0)return true;
        const auto* bytes=reinterpret_cast<const unsigned char*>(expected.data());
        for(std::size_t i=0;i<probe_.uploadedPixels.size();++i)if(probe_.uploadedPixels[i]!=bytes[i]){std::cerr<<"GPU vision first difference case="<<label<<" x="<<(i/4)%width_<<" y="<<(i/4)/width_<<" channel="<<i%4<<" actual="<<unsigned(probe_.uploadedPixels[i])<<" expected="<<unsigned(bytes[i])<<'\n';break;}return false;
    }
    void save(const std::string& label,const std::vector<std::uint32_t>& input,const std::vector<std::uint32_t>& expected,bool exact) const {
        if(const char* directory=std::getenv("CONCORD_S41_MAP_VISION_OUTPUT")){
            const std::filesystem::path target(directory);std::filesystem::create_directories(target);
            const auto write=[&](const char* suffix,const void* data,std::size_t bytes){std::ofstream file(target/(label+suffix),std::ios::binary);file.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes));require(bool(file),"vision diagnostic whole-frame output");};
            write(".source.bgra",input.data(),input.size()*4);write(".expected.bgra",expected.data(),expected.size()*4);write(".gpu.bgra",probe_.uploadedPixels.data(),probe_.uploadedPixels.size());
        }
        std::cout<<"S41 GPU vision case="<<label<<" width="<<width_<<" height="<<height_<<" resources="<<list_.resources().size()*4<<" commands="<<list_.commands().size()<<" effects="<<native_.report().visionEffects<<" nativeScratch="<<native_.report().scratchBytes<<" cpuRgbWrites=0 fullBgraExact="<<exact<<'\n';
    }
    using WorldPaint=std::function<void(SkirmishBackBufferWin32&)>;
    std::vector<std::uint32_t> softwareWorld(int,int,const WorldPaint&);
    std::array<std::uint64_t,3> worldFrame(int,int,const std::vector<std::uint32_t>&,const std::string&,const WorldPaint&);
    void finishSprites(const std::vector<std::uint32_t>&,const std::vector<std::uint32_t>&,const std::string&);
    void finishOperations(const std::vector<std::uint32_t>&,const std::vector<std::uint32_t>&,const std::string&,concord::render::MapDrawKind,std::uint64_t);
    void beginTerrain(int,int,const std::vector<std::uint32_t>&);
    void finishTerrain(const std::vector<std::uint32_t>&,const std::vector<std::uint32_t>&,const std::string&);
    void close(){native_.release();gpu_.reset();require(source_.release()&&!source_.ready(),"vision source release");require(DestroyWindow(window_)!=FALSE,"vision actual window close");window_=nullptr;}
};
} // namespace concord::ui::win32::diagnostics
using MapVisionGpuFixture=concord::ui::win32::diagnostics::MapVisionGpuFixture;
