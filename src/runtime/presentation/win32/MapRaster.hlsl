// Exact 32x32 multiplication from four16-bit limbs; all intermediate sums fit uint.
void mulWide(uint a,uint b,out uint low,out uint high){uint a0=a&65535U,a1=a>>16U,b0=b&65535U,b1=b>>16U;
    uint w0=a0*b0,t=a1*b0+(w0>>16U),w1=(t&65535U)+a0*b1;high=a1*b1+(t>>16U)+(w1>>16U);low=(w1<<16U)|(w0&65535U);}
// S41 ordered integer map operations. Device Z/blending/filtering is disabled.
struct Command {int4 rect;uint kind,offset,stride,width;uint height,palette,parameter,flags;int2 origin;uint auxiliary,reserved;};
StructuredBuffer<Command> commands : register(t0);
ByteAddressBuffer resources : register(t1);
StructuredBuffer<uint> tiles : register(t2);
cbuffer Frame : register(b0) {uint width,height,columns,shiftIndex;int4 shiftRect;int2 shiftOffset;uint commandCount,padding;};
uint byteAt(uint p) {return (resources.Load(p&~3U)>>((p&3U)*8U))&255U;}
uint wordAt(uint p) {return byteAt(p)|(byteAt(p+1U)<<8U)|(byteAt(p+2U)<<16U)|(byteAt(p+3U)<<24U);}
uint4 unpack(uint p) {return uint4(p&255U,(p>>8U)&255U,(p>>16U)&255U,p>>24U);}
float4 vs(uint id:SV_VertexID):SV_Position {return float4(id==2U?3.0:-1.0,id==1U?3.0:-1.0,0,1);}
uint divideLight(uint channel,uint light,uint intensity,uint falloff,uint denominator) {
    uint low,high;mulWide((255U-channel)*light*intensity,falloff,low,high);
    uint add=denominator/2U,old=low;low+=add;high+=low<old?1U:0U;
    uint q=0U;[unroll]for(int bit=7;bit>=0;--bit){uint candidate=q|(1U<<uint(bit)),lo,hi;mulWide(candidate,denominator,lo,hi);
        if(hi<high||(hi==high&&lo<=low))q=candidate;}return min(255U,channel+q);
}
// G005 original prepared palettes, raw indices/masks/light; all per-pixel source RGB is GPU.
uint4 spriteBase(uint material,uint sourceIndex) {
    uint flags=resources.Load(material+36U),index=byteAt(resources.Load(material+8U)+sourceIndex);
    bool remap=(flags&1U)!=0U&&((flags&2U)!=0U?byteAt(resources.Load(material+12U)+sourceIndex)!=0U:(index>=16U&&index<=31U));
    uint table=resources.Load(material+(remap?24U:20U));
    if((flags&4U)!=0U){uint level=clamp(byteAt(resources.Load(material+16U)+sourceIndex),128U,240U),bucket=((level-128U)*15U+56U)/112U;
        table=resources.Load(material+(remap?32U:28U))+bucket*1024U;}
    return unpack(resources.Load(table+index*4U));
}
uint4 spriteColor(uint material,uint sourceIndex) {
    uint flags=resources.Load(material+36U),w=resources.Load(material),h=resources.Load(material+4U);uint4 source=spriteBase(material,sourceIndex);
    if((flags&8U)!=0U){uint3 sum=source.rgb*4U;uint weight=4U;int2 at=int2(sourceIndex%w,sourceIndex/w);
        [unroll]for(uint i=0U;i<4U;++i){int2 d=i==0U?int2(-1,0):i==1U?int2(1,0):i==2U?int2(0,-1):int2(0,1),p=at+d;
            if(any(p<0)||p.x>=int(w)||p.y>=int(h))continue;uint n=uint(p.y)*w+uint(p.x);if(byteAt(resources.Load(material+8U)+n)==0U)continue;
            sum+=spriteBase(material,n).rgb;++weight;}source=uint4(sum/weight,0U);}
    if((flags&16U)!=0U){uint light=(source.z*3U+source.y*6U+source.x)/10U;source=uint4(source.x/4U,source.y/4U,min(255U,72U+light*3U/4U),0U);}
    uint add=resources.Load(material+40U),b=add&31U,g=(add>>5U)&63U,r=(add>>11U)&31U;
    source.rgb|=uint3((b<<3U)|(b>>2U),(g<<2U)|(g>>4U),(r<<3U)|(r>>2U));return source;
}
uint4 compose(int2 p,uint first,uint last,uint4 color) {
    uint4 saved[8];uint savedCount=0U;
    uint tile=(uint(p.y)>>5U)*columns+(uint(p.x)>>5U);
    [loop]for(uint n=tiles[tile*2U];n<tiles[tile*2U+1U];++n) {
        uint index=tiles[n];if(index<first||index>=last)continue;
        Command c=commands[index];
        if(any(p<c.rect.xy)||any(p>=c.rect.zw))continue;
        if(c.kind==8U||c.kind==9U)continue;
        if(c.kind==16U){saved[savedCount++]=color;continue;}
        if(c.kind==20U){uint4 old=saved[--savedCount];uint mask=byteAt(c.offset+uint(p.y-c.rect.y)*c.width+uint(p.x-c.rect.x));if(mask!=0U)color=old;continue;}
        if(c.kind==17U){uint4 old=saved[--savedCount];if(any(color!=old))color=uint4((color.rgb*128U+old.rgb*127U+127U)/255U,0U);continue;}
        if(c.kind==12U){color=uint4((unpack(c.parameter).rgb*c.auxiliary+color.rgb*(255U-c.auxiliary)+127U)/255U,0U);continue;}
        if(c.kind==15U){
            uint pixelOrdinal=uint(p.y-c.rect.y)*uint(c.rect.z-c.rect.x)+uint(p.x-c.rect.x);if(pixelOrdinal>=c.auxiliary)continue;
            int2 delta=p-c.origin;uint distance=uint(delta.x*delta.x+delta.y*delta.y),square=c.width*c.width;if(distance>square)continue;
            uint4 light=unpack(c.parameter);uint falloff=square-distance,denominator=65025U*square;
            color=uint4(divideLight(color.r,light.r,light.a,falloff,denominator),divideLight(color.g,light.g,light.a,falloff,denominator),divideLight(color.b,light.b,light.a,falloff,denominator),0U);continue;
        }
        if(c.kind==0U) {color=unpack(c.parameter);continue;}
        if(c.kind==4U) {color=uint4(uint3(clamp(int3(color.rgb)+int(c.parameter),0,255)),0U);continue;}
        if(c.kind==5U) {
            color.rgb=uint3(resources.Load(c.palette+color.x*4U)&255U,
                (resources.Load(c.palette+color.g*4U)>>8U)&255U,
                (resources.Load(c.palette+color.z*4U)>>16U)&255U);continue;
        }
        uint2 local=uint2(p-c.origin);uint4 source;
        if((c.kind==6U||c.kind==1U||c.kind==3U)&&c.flags==1U&&byteAt(c.auxiliary+local.y*c.width+local.x)==0U)continue;
        if(c.kind==11U){uint value=wordAt(c.offset+local.y*c.stride+local.x*4U);color=unpack((value&0x80000000U)!=0U?resources.Load(c.palette+(value&255U)*4U):value);continue;}
        if(c.kind==13U){uint mask=byteAt(c.offset+local.y*c.stride+local.x);if(mask==0U)continue;uint alpha=(c.flags&4U)!=0U?mask:(c.flags&1U)!=0U?128U:max(1U,(mask*128U+127U)/255U);
            if((c.flags&2U)!=0U&&alpha==128U)color=uint4(color.rgb>>1U,0U);else color=uint4((color.rgb*(255U-alpha)+127U)/255U,0U);continue;}
        if(c.kind==14U){uint receiver=c.flags==1U?byteAt(c.offset+local.y*c.stride+local.x):wordAt(c.offset+local.y*c.stride+local.x*4U),table=c.palette+receiver*1024U;
            color.rgb=uint3(resources.Load(table+color.x*4U)&255U,(resources.Load(table+color.y*4U)>>8U)&255U,(resources.Load(table+color.z*4U)>>16U)&255U);continue;}
        if(c.kind==19U){uint sourceIndex=local.y*c.width+local.x;if(byteAt(c.auxiliary+sourceIndex)==0U||byteAt(resources.Load(c.offset+8U)+sourceIndex)==0U)continue;
            source=spriteColor(c.offset,sourceIndex);if(c.parameter>=255U)color=source;else color=uint4((source.rgb*c.parameter+color.rgb*(255U-c.parameter)+127U)/255U,0U);continue;}
        if(c.kind==18U){uint2 chain=resources.Load2(c.offset+local.y*c.stride+local.x*8U);uint4 groups[8];uint depth=0U;
            [loop]for(uint i=0;i<chain.y;++i){uint4 operation=c.flags==1U?resources.Load4(c.palette+(chain.x+i)*16U):uint4(resources.Load2(c.palette+(chain.x+i)*8U),0U,0U);
                if(operation.y==256U){groups[depth++]=color;continue;}if(operation.y==257U){color=uint4((color.rgb*128U+groups[--depth].rgb*127U+127U)/255U,0U);continue;}
                uint4 layer=operation.w==1U?spriteColor(operation.x,operation.z):unpack(operation.x);color=uint4((layer.rgb*operation.y+color.rgb*(255U-operation.y)+127U)/255U,0U);}continue;} 
        if(c.kind==7U) {
            uint2 descriptor=resources.Load2(c.offset+local.y*c.stride+local.x*8U);
            if(descriptor.x==0U)continue;
            if(descriptor.x==1U) {color=unpack(descriptor.y);continue;}
            uint table=c.palette+descriptor.y*1024U;
            color=uint4(resources.Load(table+color.x*4U)&255U,
                (resources.Load(table+color.y*4U)>>8U)&255U,
                (resources.Load(table+color.z*4U)>>16U)&255U,
                resources.Load(table+color.w*4U)>>24U);continue;
        }
        if(c.kind==2U) {
            uint index=byteAt(c.offset+local.y*c.stride+local.x);
            if((c.flags&1U)!=0U&&index==0U)continue;
            if((c.flags&2U)!=0U)index=byteAt(c.auxiliary+index);
            source=unpack(resources.Load(c.palette+index*4U));
        }else source=unpack(wordAt(c.offset+local.y*c.stride+local.x*4U));
        if(c.kind==1U||(c.kind==2U&&(c.flags&4U)==0U)) {color=source;continue;}
        uint alpha=c.kind==6U?source.a:c.parameter;
        if(alpha==0U) {if(c.kind!=2U||(c.flags&8U)!=0U)color.a=0U;continue;}
        color=uint4((source.rgb*alpha+color.rgb*(255U-alpha)+127U)/255U,0U);
    }
    return color;
}
float4 ps(float4 position:SV_Position):SV_Target {
    int2 p=int2(position.xy),source=p;
    if(shiftIndex<commandCount&&all(p>=shiftRect.xy)&&all(p<shiftRect.zw))
        source=clamp(p-shiftOffset,shiftRect.xy,shiftRect.zw-1);
    uint4 color=compose(source,0U,shiftIndex,uint4(0,0,0,0));
    if(shiftIndex<commandCount)color=compose(p,shiftIndex+1U,commandCount,color);
    return float4(color.b,color.g,color.r,color.a)/255.0;
}