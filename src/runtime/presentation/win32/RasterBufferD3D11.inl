// Private shared upload utility for the single D3D11 presentation owner.
HRESULT uploadRasterBufferD3D11(ID3D11Device* d,ID3D11DeviceContext* c,const void* bytes,std::size_t count,
    std::size_t stride,ComPtr<ID3D11Buffer>& buffer,ComPtr<ID3D11ShaderResourceView>& view,
    std::size_t& capacity,std::uint64_t& uploaded,std::size_t limit) {
    if(count==0||count>limit||count>std::numeric_limits<UINT>::max()||count%4||!bytes)return E_INVALIDARG;
    if(capacity!=count) {
        buffer.Reset();view.Reset();capacity=0;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(count);desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags=stride?D3D11_RESOURCE_MISC_BUFFER_STRUCTURED:D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        desc.StructureByteStride=static_cast<UINT>(stride);
        HRESULT hr=d->CreateBuffer(&desc,nullptr,&buffer);if(FAILED(hr))return hr;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format=stride?DXGI_FORMAT_UNKNOWN:DXGI_FORMAT_R32_TYPELESS;
        srv.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
        srv.BufferEx.NumElements=static_cast<UINT>(count/(stride?stride:4));
        srv.BufferEx.Flags=stride?0:D3D11_BUFFEREX_SRV_FLAG_RAW;
        hr=d->CreateShaderResourceView(buffer.Get(),&srv,&view);if(FAILED(hr))return hr;capacity=count;
    }
    c->UpdateSubresource(buffer.Get(),0,nullptr,bytes,0,0);uploaded+=count;return S_OK;
}