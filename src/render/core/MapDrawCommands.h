#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "render/core/MapSpriteMaterial.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
namespace concord::render {
// Immutable presentation values. No World, decoder, resource locator or device.
enum class MapDrawKind : std::uint32_t { Fill, Bgra, Indexed, Blend, Shade, Lookup, Vision, NativeLookup, NoOp, Shift, Pending, Terrain, ConstantBlend, Shadow, ReceiverLookup, LocalLight, Save, RestoreBlend, Chain, Sprite, RestoreMask };
struct MapDrawCommand {
    std::int32_t left=0,top=0,right=0,bottom=0;
    MapDrawKind kind=MapDrawKind::Fill;
    std::uint32_t offset=0,stride=0,width=0,height=0,palette=0,parameter=0,flags=0;
    std::int32_t originX=0,originY=0;
    std::uint32_t auxiliary=0,reserved=0;
};
static_assert(sizeof(MapDrawCommand)==64);
class MapDrawList {
public:
    static constexpr std::size_t maxBytes=64U*1024U*1024U,maxCommands=32768;
    static constexpr std::int32_t maxExtent=8192;
    const std::vector<MapDrawCommand>& commands() const noexcept {return commands_;}
    const std::vector<std::uint32_t>& resources() const noexcept {return resources_;}
    std::uint64_t generation() const noexcept {return generation_;}
    std::uint64_t resourceRevision() const noexcept {return resourceRevision_;}
    std::uint64_t commandRevision() const noexcept {return commandRevision_;}
    void begin() {advance(commandRevision_);commands_.clear();}
    void reset() {
        if(generation_==UINT64_MAX||resourceRevision_==UINT64_MAX||commandRevision_==UINT64_MAX)
            throw std::overflow_error("map resource generation_ exhausted");
        ++generation_;++resourceRevision_;++commandRevision_;
        commands_.clear();resources_.clear();entries_.clear();
    }
    std::uint32_t intern(std::span<const unsigned char> bytes) {
        if(bytes.empty())throw std::invalid_argument("empty map resource");
        if(bytes.size()>maxBytes||resources_.size()>maxBytes/4)throw std::length_error("map resource budget");
        for(const auto& e:entries_)if(e.pointer==bytes.data()&&e.size==bytes.size()
            &&std::memcmp(reinterpret_cast<const unsigned char*>(resources_.data())+e.offset,bytes.data(),bytes.size())==0)return e.offset;
        const auto words=(bytes.size()+3)/4;
        if(words>maxBytes/4-resources_.size())throw std::length_error("map resource budget");
        if(resourceRevision_==UINT64_MAX)throw std::overflow_error("map resource revision exhausted");
        const auto offset=static_cast<std::uint32_t>(resources_.size()*4);
        const auto old=resources_.size();resources_.resize(old+words,0);
        std::memcpy(reinterpret_cast<unsigned char*>(resources_.data())+offset,bytes.data(),bytes.size());
        try {entries_.push_back({bytes.data(),bytes.size(),offset});}
        catch(...) {resources_.resize(old);throw;}
        ++resourceRevision_;return offset;
    }
    template<class T> std::uint32_t internValues(std::span<const T> values) {
        return intern({reinterpret_cast<const unsigned char*>(values.data()),values.size_bytes()});
    }
    void push(MapDrawCommand c) {
        if(c.right<=c.left||c.bottom<=c.top)return;
        if(commands_.size()>=maxCommands)throw std::length_error("map command budget");
        if(commandRevision_==UINT64_MAX)throw std::overflow_error("map command revision exhausted");
        commands_.push_back(c);++commandRevision_;
    }
    void replace(std::size_t index,MapDrawCommand c) {
        if(index>=commands_.size())throw std::out_of_range("map command slot");
        advance(commandRevision_);commands_[index]=c;
    }
    void fill(RuntimeInputPixelRect r,std::uint32_t bgra) {
        MapDrawCommand c;c.left=r.left;c.top=r.top;c.right=r.right;c.bottom=r.bottom;c.parameter=bgra;push(c);
    }
    bool valid() const noexcept {
        if(!generation_||!resourceRevision_||!commandRevision_||commands_.empty()
            ||commands_.size()>maxCommands||resources_.size()>maxBytes/4)return false;
        const auto bytes=static_cast<std::uint64_t>(resources_.size())*4;
        const auto region=[bytes](std::uint32_t offset,std::uint64_t size){return offset<=bytes&&size<=bytes-offset;};
        const auto materialValid=[&](std::uint32_t offset,std::uint32_t w=0,std::uint32_t h=0) {
            if(offset%4||!region(offset,sizeof(MapSpriteMaterial)))return false;MapSpriteMaterial m;
            std::memcpy(&m,reinterpret_cast<const unsigned char*>(resources_.data())+offset,sizeof(m));
            if(!m.width||!m.height||m.width>maxExtent||m.height>maxExtent||m.flags>31||m.airstrike>65535||m.reserved
                ||(w&&(m.width!=w||m.height!=h))||((m.flags&MapSpriteMaterial::Mask)&&!(m.flags&MapSpriteMaterial::Owner)))return false;
            const auto area=static_cast<std::uint64_t>(m.width)*m.height;
            if(!region(m.indices,area)||m.baseColors%4||!region(m.baseColors,1024))return false;
            if((m.flags&MapSpriteMaterial::Owner)&&(m.remapColors%4||!region(m.remapColors,1024)))return false;
            if((m.flags&MapSpriteMaterial::Mask)&&!region(m.remapMask,area))return false;
            if(m.flags&MapSpriteMaterial::Light){if(!region(m.lightLevels,area)||m.litColors%4||!region(m.litColors,16384))return false;
                if((m.flags&MapSpriteMaterial::Owner)&&(m.litRemapColors%4||!region(m.litRemapColors,16384)))return false;}
            return true;
        };
        bool shifted=false;
        std::array<RuntimeInputPixelRect,8> saved{};std::size_t savedCount=0;
        for(const auto& c:commands_) {
            const auto kind=static_cast<std::uint32_t>(c.kind);
            if(kind==10||kind>20||c.right<=c.left||c.bottom<=c.top||c.reserved)return false;
            if(kind==16||kind==17||kind==20) {
                if(c.flags)return false;
                const RuntimeInputPixelRect r{c.left,c.top,c.right,c.bottom};
                if(kind==16) {if(savedCount==saved.size())return false;saved[savedCount++]=r;}
                else {if(!savedCount||(kind==17&&c.parameter!=128))return false;
                    if(kind==20&&(c.parameter||c.palette||c.auxiliary||c.width!=static_cast<std::uint64_t>(static_cast<std::int64_t>(c.right)-c.left)
                        ||c.height!=static_cast<std::uint64_t>(static_cast<std::int64_t>(c.bottom)-c.top)||c.stride!=c.width
                        ||!region(c.offset,static_cast<std::uint64_t>(c.width)*c.height)))return false;
                    const auto p=saved[--savedCount];
                    if(p.left!=r.left||p.top!=r.top||p.right!=r.right||p.bottom!=r.bottom)return false;}
                continue;
            }
            if(kind==12) {if(c.flags||c.auxiliary>255)return false;continue;}
            if(kind==15) {
                const auto area=static_cast<std::uint64_t>(static_cast<std::int64_t>(c.right)-c.left)*(static_cast<std::int64_t>(c.bottom)-c.top);
                if(c.flags||!c.width||c.width>192||c.auxiliary>area
                    ||static_cast<std::int64_t>(c.left)-c.originX < -8192||static_cast<std::int64_t>(c.right)-c.originX>8192
                    ||static_cast<std::int64_t>(c.top)-c.originY < -8192||static_cast<std::int64_t>(c.bottom)-c.originY>8192)return false;
                continue;
            }
            if(kind==9) {
                if(shifted||c.flags||c.originX < -maxExtent||c.originX>maxExtent
                    ||c.originY < -maxExtent||c.originY>maxExtent)return false;
                shifted=true;continue;
            }
            if(kind==8) {if(c.flags)return false;continue;}
            if(kind==0) {if(c.flags)return false;continue;}
            if(kind==4) {
                if(c.flags||static_cast<std::int32_t>(c.parameter)<-255
                    ||static_cast<std::int32_t>(c.parameter)>255)return false;
                continue;
            }
            if(kind==5) {if(c.flags||c.palette%4||!region(c.palette,1024))return false;continue;}
            if(!c.width||!c.height||c.left<c.originX||c.top<c.originY
                ||static_cast<std::int64_t>(c.right)-c.originX>c.width
                ||static_cast<std::int64_t>(c.bottom)-c.originY>c.height)return false;
            if(kind==19){if(c.flags||c.palette||c.parameter>255||c.stride!=c.width||!materialValid(c.offset,c.width,c.height)
                    ||!region(c.auxiliary,static_cast<std::uint64_t>(c.width)*c.height))return false;
                continue;}
            const std::uint64_t row=static_cast<std::uint64_t>(c.width)*(kind==2||kind==13||(kind==14&&c.flags==1)?1:kind==7||kind==18?8:4);
            if(row>c.stride||!region(c.offset,static_cast<std::uint64_t>(c.height-1)*c.stride+row))return false;
            if(kind==11) {
                if(c.flags||c.palette%4||!region(c.palette,1024))return false;
                const auto* data=reinterpret_cast<const unsigned char*>(resources_.data());
                for(std::uint32_t y=0;y<c.height;++y)for(std::uint32_t x=0;x<c.width;++x) {std::uint32_t value=0;
                    std::memcpy(&value,data+c.offset+static_cast<std::size_t>(y)*c.stride+x*4U,4);
                    if((value&0x80000000U)?(value&0x7fffff00U)!=0:value>0xffffffU)return false;}
            } else if(kind==13) {if(c.flags>7)return false;}
            else if(kind==14) {
                if(c.flags>1||!c.parameter||(c.flags==1&&c.parameter>256)||c.palette%4||!region(c.palette,static_cast<std::uint64_t>(c.parameter)*1024))return false;
                const auto* data=reinterpret_cast<const unsigned char*>(resources_.data());
                for(std::uint32_t y=0;y<c.height;++y)for(std::uint32_t x=0;x<c.width;++x){std::uint32_t value=0;
                    if(c.flags==1)value=data[c.offset+static_cast<std::size_t>(y)*c.stride+x];
                    else std::memcpy(&value,data+c.offset+static_cast<std::size_t>(y)*c.stride+x*4U,4);if(value>=c.parameter)return false;}
            } else if(kind==18) {
                if(c.flags>1||c.palette%4||!region(c.palette,static_cast<std::uint64_t>(c.parameter)*(c.flags?16:8)))return false;
                const auto* data=reinterpret_cast<const unsigned char*>(resources_.data());
                for(std::uint32_t y=0;y<c.height;++y)for(std::uint32_t x=0;x<c.width;++x){std::uint32_t pair[2];
                    std::memcpy(pair,data+c.offset+static_cast<std::size_t>(y)*c.stride+x*8U,8);
                    if(pair[0]>c.parameter||pair[1]>c.parameter-pair[0])return false;unsigned depth=0;
                    for(std::uint32_t n=0;n<pair[1];++n){std::uint32_t values[4]{};std::memcpy(values,data+c.palette+static_cast<std::size_t>(pair[0]+n)*(c.flags?16U:8U),c.flags?16U:8U);const auto op=values[1];
                        if(op==256){if(++depth>8)return false;}else if(op==257){if(!depth)return false;--depth;}else if(op>255)return false;
                        if(c.flags){if(values[3]>1||(op>255&&(values[0]||values[2]||values[3]))||(!values[3]&&values[2]))return false;
                            if(values[3]){if(!materialValid(values[0]))return false;MapSpriteMaterial m;std::memcpy(&m,data+values[0],sizeof(m));
                                if(values[2]>=static_cast<std::uint64_t>(m.width)*m.height)return false;}}
                    }
                    if(depth)return false;}
            } else if(kind==7) {
                if(c.flags||c.offset%4||c.stride%4||c.palette%4||c.parameter>maxBytes/1024
                    ||!region(c.palette,static_cast<std::uint64_t>(c.parameter)*1024))return false;
                const auto* data=reinterpret_cast<const unsigned char*>(resources_.data());
                for(std::uint32_t y=0;y<c.height;++y)for(std::uint32_t x=0;x<c.width;++x) {
                    std::uint32_t pair[2];std::memcpy(pair,data+c.offset+static_cast<std::size_t>(y)*c.stride+static_cast<std::size_t>(x)*8,8);
                    if(pair[0]>2||(pair[0]==2&&pair[1]>=c.parameter))return false;
                }
            } else if(kind==2) {
                if(c.flags>15||c.palette%4||!region(c.palette,1024)||c.parameter>255
                    ||((c.flags&2U)&&!region(c.auxiliary,256)))return false;
            } else if(kind==3) {if(c.flags>1||c.parameter>255||(c.flags&&!region(c.auxiliary,static_cast<std::uint64_t>(c.width)*c.height)))return false;}
            else if(kind==6) {if(c.flags>1||(c.flags&&!region(c.auxiliary,static_cast<std::uint64_t>(c.width)*c.height)))return false;}
            else if(kind==1) {if(c.flags>1||(c.flags&&!region(c.auxiliary,static_cast<std::uint64_t>(c.width)*c.height)))return false;}
            else if(c.flags)return false;
        }
        return savedCount==0;
    }
private:
    std::vector<MapDrawCommand> commands_;
    std::vector<std::uint32_t> resources_;
    std::uint64_t generation_=1,resourceRevision_=1,commandRevision_=1;
    struct Entry {const unsigned char* pointer;std::size_t size;std::uint32_t offset;};
    std::vector<Entry> entries_;
    static void advance(std::uint64_t& value) {
        if(value==UINT64_MAX)throw std::overflow_error("map command revision exhausted");++value;
    }
};
struct MapDrawFrame {
    const MapDrawList& list;
    std::uint64_t sequence=0,tick=0;
    std::int32_t width=0,height=0;
    std::uint64_t generation=0,resourceRevision=0,commandRevision=0;
    MapDrawFrame(const MapDrawList& owner,std::uint64_t seq,std::int32_t w,std::int32_t h,std::uint64_t logicalTick=0)
        :list(owner),sequence(seq),tick(logicalTick),width(w),height(h),generation(owner.generation()),
        resourceRevision(owner.resourceRevision()),commandRevision(owner.commandRevision()){}
    bool valid() const noexcept {
        return sequence&&width>0&&height>0&&width<=MapDrawList::maxExtent&&height<=MapDrawList::maxExtent
            &&generation==list.generation()&&resourceRevision==list.resourceRevision()
            &&commandRevision==list.commandRevision()&&list.valid();
    }
};
} // namespace concord::render