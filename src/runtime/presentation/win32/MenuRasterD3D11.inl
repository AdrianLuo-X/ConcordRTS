// Included only by the existing private D3D11 owner. No device/context/Present
// duplication. Resource buffers and shader objects die before the swap chain.
#include "MenuRaster-vs.h"
#include "MenuRaster-ps.h"
struct MenuRasterD3D11 {
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Buffer> commands,resources,tiles,constants;
    ComPtr<ID3D11ShaderResourceView> commandView,resourceView,tileView;
    std::size_t commandBytes=0,resourceBytes=0,tileBytes=0;
    const concord::ui::MenuDrawList* owner=nullptr;
    std::vector<std::uint32_t> resourceSnapshot;
    std::uint64_t generation=0,revision=0;
    HRESULT buffer(ID3D11Device* d,ID3D11DeviceContext* c,const void* bytes,std::size_t count,
        std::size_t stride,ComPtr<ID3D11Buffer>& b,ComPtr<ID3D11ShaderResourceView>& v,
        std::size_t& capacity,std::uint64_t& uploaded) {
        if(count==0||count>concord::ui::MenuDrawList::maxBytes)return E_INVALIDARG;
        if(capacity!=count) {
            b.Reset();v.Reset();capacity=0;
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(count);desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags=stride?D3D11_RESOURCE_MISC_BUFFER_STRUCTURED:D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            desc.StructureByteStride=static_cast<UINT>(stride);
            HRESULT hr=d->CreateBuffer(&desc,nullptr,&b);if(FAILED(hr))return hr;
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format=stride?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;
            srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
            srv.BufferEx.NumElements=static_cast<UINT>(count/(stride?stride:4));
            srv.BufferEx.Flags=stride?0:D3D11_BUFFEREX_SRV_FLAG_RAW;
            hr=d->CreateShaderResourceView(b.Get(),&srv,&v);if(FAILED(hr))return hr;capacity=count;
        }
        c->UpdateSubresource(b.Get(),0,nullptr,bytes,0,0);uploaded+=count;return S_OK;
    }
    HRESULT draw(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Texture2D* back,
        const concord::ui::MenuDrawFrame& f,std::uint64_t& uploaded,ID3D11Query* start) {
        if(!vertex) {
            HRESULT hr=d->CreateVertexShader(s40_vs,sizeof(s40_vs),nullptr,&vertex);
            if(SUCCEEDED(hr))hr=d->CreatePixelShader(s40_ps,sizeof(s40_ps),nullptr,&pixel);
            D3D11_RASTERIZER_DESC r{};r.FillMode=D3D11_FILL_SOLID;r.CullMode=D3D11_CULL_NONE;r.DepthClipEnable=TRUE;
            if(SUCCEEDED(hr))hr=d->CreateRasterizerState(&r,&raster);
            D3D11_BUFFER_DESC b{};b.ByteWidth=32;b.Usage=D3D11_USAGE_DEFAULT;b.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
            if(SUCCEEDED(hr))hr=d->CreateBuffer(&b,nullptr,&constants);
            if(FAILED(hr))return hr;
        }
        const auto& list=f.list;
        HRESULT hr=buffer(d,c,list.commands.data(),list.commands.size()*sizeof(concord::ui::MenuDrawCommand),
            sizeof(concord::ui::MenuDrawCommand),commands,commandView,commandBytes,uploaded);
        if(FAILED(hr))return hr;
        if(owner!=&list||generation!=list.generation||revision!=list.revision||resourceSnapshot!=list.resources) {
            hr=buffer(d,c,list.resources.data(),list.resources.size()*4,0,resources,resourceView,resourceBytes,uploaded);
            if(FAILED(hr))return hr;
            resourceSnapshot=list.resources;
            owner=&list;generation=list.generation;revision=list.revision;
        }
        constexpr int columns=25,rows=19,total=columns*rows;
        std::array<std::vector<std::uint32_t>,total> bins;
        for(std::size_t i=0;i<list.commands.size();++i) {
            const auto& op=list.commands[i];
            const int left=std::clamp(op.left,0,800),right=std::clamp(op.right,0,800);
            const int top=std::clamp(op.top,0,600),bottom=std::clamp(op.bottom,0,600);
            if(left>=right||top>=bottom)continue;
            for(int y=top/32;y<=(bottom-1)/32;++y)for(int x=left/32;x<=(right-1)/32;++x)
                bins[static_cast<std::size_t>(y*columns+x)].push_back(static_cast<std::uint32_t>(i));
        }
        std::vector<std::uint32_t> indices(total*2);
        for(int i=0;i<total;++i) {
            indices[static_cast<std::size_t>(i)*2]=static_cast<std::uint32_t>(indices.size());
            indices.insert(indices.end(),bins[i].begin(),bins[i].end());
            indices[static_cast<std::size_t>(i)*2+1]=static_cast<std::uint32_t>(indices.size());
        }
        const int w=f.viewport.right-f.viewport.left,h=f.viewport.bottom-f.viewport.top;
        const bool nearIdentity=w==799&&h==599;
        const auto mapX=static_cast<std::uint32_t>(indices.size());
        for(int x=0;x<w;++x)indices.push_back(nearIdentity?static_cast<std::uint32_t>(x):static_cast<std::uint32_t>((2LL*x+1)*800/(2LL*w)));
        const auto mapY=static_cast<std::uint32_t>(indices.size());
        for(int y=0;y<h;++y)indices.push_back(nearIdentity?static_cast<std::uint32_t>(y):static_cast<std::uint32_t>((2LL*y+1)*600/(2LL*h)));
        hr=buffer(d,c,indices.data(),indices.size()*4,4,tiles,tileView,tileBytes,uploaded);if(FAILED(hr))return hr;
        const std::array<std::uint32_t,8> frame={static_cast<std::uint32_t>(f.viewport.left),static_cast<std::uint32_t>(f.viewport.top),
            static_cast<std::uint32_t>(w),static_cast<std::uint32_t>(h),static_cast<std::uint32_t>(f.width),static_cast<std::uint32_t>(f.height),mapX,mapY};
        c->UpdateSubresource(constants.Get(),0,nullptr,frame.data(),0,0);uploaded+=sizeof(frame);
        ComPtr<ID3D11RenderTargetView> target;hr=d->CreateRenderTargetView(back,nullptr,&target);if(FAILED(hr))return hr;
        c->OMSetRenderTargets(1,target.GetAddressOf(),nullptr);
        c->OMSetBlendState(nullptr,nullptr,0xffffffffU);c->OMSetDepthStencilState(nullptr,0);
        c->RSSetState(raster.Get());
        const D3D11_VIEWPORT view{0,0,static_cast<float>(f.width),static_cast<float>(f.height),0,1};c->RSSetViewports(1,&view);
        c->IASetInputLayout(nullptr);c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vertex.Get(),nullptr,0);c->PSSetShader(pixel.Get(),nullptr,0);
        ID3D11ShaderResourceView* views[]={commandView.Get(),resourceView.Get(),tileView.Get()};
        c->PSSetShaderResources(0,3,views);c->PSSetConstantBuffers(0,1,constants.GetAddressOf());
        if(start)c->End(start);
        c->Draw(3,0);
        ID3D11ShaderResourceView* empty[3]={};c->PSSetShaderResources(0,3,empty);c->OMSetRenderTargets(0,nullptr,nullptr);
        return d->GetDeviceRemovedReason();
    }
};
