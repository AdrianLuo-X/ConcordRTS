// S40 integer reference composition. One raster pass, no CPU framebuffer.
// Byte resources retain original index/palette/glyph data; no filtered sample,
// sRGB conversion, fixed-function blend rounding or GPU -> CPU composition.
struct Command {
    int4 rect;
    uint kind, offset, stride, width;
    uint height, palette, color, flags;
    int2 origin;
    uint2 reserved;
};
StructuredBuffer<Command> commands : register(t0);
ByteAddressBuffer resources : register(t1);
// Stable tile lists preserve original order while skipping unrelated commands.
StructuredBuffer<uint> tileIndices : register(t2);
cbuffer Frame : register(b0) {int4 viewport;uint4 extent;};
uint byteAt(uint p) {return (resources.Load(p&~3U)>>((p&3U)*8U))&255U;}
uint4 unpack(uint p) {return uint4(p&255U,(p>>8)&255U,(p>>16)&255U,p>>24);}
float4 vs(uint id:SV_VertexID):SV_Position {
    return float4(id==2?3.0:-1.0,id==1?3.0:-1.0,0,1);
}
float4 ps(float4 position:SV_Position):SV_Target {
    int2 client=int2(position.xy);
    if(any(client<viewport.xy)||any(client>=viewport.xy+viewport.zw))return float4(0,0,0,1);
    int2 p;
    // GDI COLORONCOLOR uses center-selected nearest pixel for minification.
    // Identity/expansion policy is supplied via precomputed integer maps.
    p.x=int(tileIndices[extent.z+uint(client.x-viewport.x)]);
    p.y=int(tileIndices[extent.w+uint(client.y-viewport.y)]);
    uint tile=(uint(p.y)>>5U)*25U+(uint(p.x)>>5U);
    uint begin=tileIndices[tile*2],end=tileIndices[tile*2+1];
    uint4 pixel=uint4(0,0,0,255);
    [loop]for(uint n=begin;n<end;++n) {
        Command c=commands[tileIndices[n]];
        if(any(p<c.rect.xy)||any(p>=c.rect.zw))continue;
        if(c.kind==0) {pixel=unpack(c.color);continue;}
        if(c.kind==1) {pixel.rgb=pixel.rgb*c.color/255U;pixel.a=255;continue;}
        int2 local=p-c.origin;uint4 source;
        if(c.kind==5) {
            if((byteAt(c.offset+uint(local.y)*c.stride+(uint(local.x)>>3U))&(128U>>(uint(local.x)&7U)))!=0)pixel=unpack(c.color);
            continue;
        }
        if(c.kind==3) local=int2(uint2(p-c.rect.xy)*uint2(c.width,c.height)/uint2(c.rect.zw-c.rect.xy));
        if(c.kind==4) {
            uint index=byteAt(c.offset+uint(local.y)*c.stride+uint(local.x));
            if((c.flags&1U)!=0&&index==0)continue;
            if((c.flags&2U)!=0)index=byteAt(c.reserved.x+index);
            source=unpack(resources.Load(c.palette+index*4U));
        } else {
            uint offset=c.offset+uint(local.y)*c.stride+uint(local.x)*4U;
            source=uint4(byteAt(offset),byteAt(offset+1),byteAt(offset+2),byteAt(offset+3));
        }
        if(c.kind==3) {pixel=uint4(source.rgb,255);continue;}
        if(source.a==255)pixel=uint4(source.rgb,255);
        else if(source.a!=0)pixel.rgb=(source.rgb*source.a+pixel.rgb*(255U-source.a))/255U;
    }
    return float4(pixel.b,pixel.g,pixel.r,pixel.a)/255.0;
}
