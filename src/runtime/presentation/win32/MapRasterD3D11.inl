// Uses the S39 device, immediate context, swap chain and diagnostic capture.
#include "MapRaster-vs.h"
#include "MapRaster-ps.h"
struct MapRasterD3D11 {
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Buffer> commands,resources,tiles,constants;
    ComPtr<ID3D11ShaderResourceView> commandView,resourceView,tileView;
    std::size_t commandBytes=0,resourceBytes=0,tileBytes=0;
    const concord::render::MapDrawList* owner=nullptr;
    std::vector<std::uint32_t> resourceSnapshot;
    std::uint64_t generation=0,revision=0;
    concord::render::MapTilePreparer tilePreparer;
    // The fixed-frame fixture constructs its own backend options. Select its
    // explicit worker matrix only when BOTH private diagnostic outputs exist.
    // Ordinary startup and the live CLI always use FrameBackendOptions.
    static unsigned diagnosticWorkers(unsigned requested) {
        if(!GetEnvironmentVariableW(L"CONCORD_MAIN_FRAME_CONTRACT_OUTPUT",nullptr,0)
            ||!GetEnvironmentVariableW(L"CONCORD_S43_TILE_TRACE",nullptr,0))return requested;
        std::array<wchar_t,8> value{};
        const auto length=GetEnvironmentVariableW(L"CONCORD_S43_FRAME_WORKERS",value.data(),static_cast<DWORD>(value.size()));
        if(!length)return requested;
        if(length!=1||(value[0]!=L'0'&&value[0]!=L'1'&&value[0]!=L'2'&&value[0]!=L'4'))throw std::invalid_argument("diagnostic frame workers");
        return static_cast<unsigned>(value[0]-L'0');
    }
    explicit MapRasterD3D11(unsigned workers):tilePreparer(diagnosticWorkers(workers)){}
    HRESULT draw(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* back,
        const concord::render::MapDrawFrame& f,std::uint64_t& uploaded,ID3D11Query* start,std::uint64_t deviceGeneration) {
        using concord::render::MapDrawList;
        if(!vertex) {
            HRESULT hr=d->CreateVertexShader(s41_vs,sizeof(s41_vs),nullptr,&vertex);
            if(SUCCEEDED(hr))hr=d->CreatePixelShader(s41_ps,sizeof(s41_ps),nullptr,&pixel);
            D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
            if(SUCCEEDED(hr))hr=d->CreateRasterizerState(&r,&raster);
            D3D11_BUFFER_DESC b{};b.ByteWidth=48;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(SUCCEEDED(hr))hr=d->CreateBuffer(&b,nullptr,&constants);
            if(FAILED(hr))return hr;
        }
        const auto& list=f.list;
        HRESULT hr=uploadRasterBufferD3D11(d,c,list.commands().data(),list.commands().size()*sizeof(concord::render::MapDrawCommand),
            sizeof(concord::render::MapDrawCommand),commands,commandView,commandBytes,uploaded,MapDrawList::maxBytes);
        if(FAILED(hr))return hr;
        if(owner!=&list||generation!=list.generation()||revision!=list.resourceRevision()||resourceSnapshot!=list.resources()) {
            const std::uint32_t empty=0;
            hr=uploadRasterBufferD3D11(d,c,list.resources().empty()?&empty:list.resources().data(),std::max<std::size_t>(4,list.resources().size()*4),
                0,resources,resourceView,resourceBytes,uploaded,MapDrawList::maxBytes);
            if(FAILED(hr))return hr;
            resourceSnapshot=list.resources();owner=&list;generation=list.generation();revision=list.resourceRevision();
        }
        const int columns=(f.width+31)/32;
        const auto snapshotStart=Clock::now();
        auto snapshot=concord::render::snapshotMapTiles(f,deviceGeneration);
        const auto stamp=snapshot.stamp;const double snapshotMs=elapsed(snapshotStart);
        tilePreparer.start(std::move(snapshot));
        auto prepared=tilePreparer.finish(stamp);
        if(!prepared.accepted)return E_ABORT;
        prepared.metrics.snapshotMs=snapshotMs;prepared.metrics.totalMs+=snapshotMs;
        const auto& indices=prepared.indices;
        #include "runtime/presentation/win32/MapTileDiagnosticWin32.inl"
        hr=uploadRasterBufferD3D11(d,c,indices.data(),indices.size()*4,4,tiles,tileView,tileBytes,uploaded,MapDrawList::maxBytes);
        if(FAILED(hr))return hr;
        const auto count=static_cast<std::uint32_t>(list.commands().size());
        std::array<std::int32_t,12> frame={f.width,f.height,columns,static_cast<std::int32_t>(count),0,0,0,0,0,0,static_cast<std::int32_t>(count),0};
        for(std::uint32_t i=0;i<count;++i)if(list.commands()[i].kind==concord::render::MapDrawKind::Shift) {
            const auto& shift=list.commands()[i];frame[3]=static_cast<std::int32_t>(i);
            frame[4]=std::clamp(shift.left,0,f.width);frame[5]=std::clamp(shift.top,0,f.height);
            frame[6]=std::clamp(shift.right,frame[4],f.width);frame[7]=std::clamp(shift.bottom,frame[5],f.height);
            frame[8]=std::clamp(shift.originX,frame[4]-frame[6],frame[6]-frame[4]);
            frame[9]=std::clamp(shift.originY,frame[5]-frame[7],frame[7]-frame[5]);break;
        }
        c->UpdateSubresource(constants.Get(),0,nullptr,frame.data(),0,0);uploaded+=sizeof(frame);
        ComPtr<ID3D11RenderTargetView> target;hr=d->CreateRenderTargetView(back,nullptr,&target);if(FAILED(hr))return hr;
        c->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        c->OMSetBlendState(nullptr,nullptr,0xffffffffU);c->OMSetDepthStencilState(nullptr,0);c->RSSetState(raster.Get());
        const D3D11_VIEWPORT view{0,0,static_cast<float>(f.width),static_cast<float>(f.height),0,1};c->RSSetViewports(1,&view);
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(pixel.Get(),nullptr,0);
        ID3D11ShaderResourceView* views[]={commandView.Get(),resourceView.Get(),tileView.Get()};
        c->PSSetShaderResources(0,3,views);c->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        if(start)c->End(start);c->Draw(3,0);
        ID3D11ShaderResourceView* empty[3]={};c->PSSetShaderResources(0,3,empty);c->OMSetRenderTargets(0,nullptr,nullptr);
        return d->GetDeviceRemovedReason();
    }
};
