#pragma once
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include "assets/menu/MenuImage.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace concord::ui {
// Stable ordered value commands, with owned packed resource bytes. All byte
// offsets are bounded by the owner, never native handles. Serial UI owner only.
enum class MenuDrawKind : std::uint32_t { Fill, Darken, Bgra, Scale, Indexed, Glyph };
struct MenuDrawCommand {
    std::int32_t left=0,top=0,right=0,bottom=0;
    MenuDrawKind kind=MenuDrawKind::Fill;
    std::uint32_t offset=0,stride=0,width=0,height=0,palette=0,color=0,flags=0;
    std::int32_t originX=0,originY=0;
    std::uint32_t reserved0=0,reserved1=0;
};
static_assert(sizeof(MenuDrawCommand)==64);
class MenuDrawList {
public:
    static constexpr std::size_t maxBytes=32U*1024U*1024U,maxCommands=16384;
    std::vector<MenuDrawCommand> commands;
    std::vector<std::uint32_t> resources;
    std::uint64_t generation=1,revision=1;
    std::uint64_t frameRevision=1;
    void begin() {commands.clear();++frameRevision;}
    void reset() {commands.clear();resources.clear();entries_.clear();++generation;++revision;++frameRevision;}
    std::uint32_t intern(std::span<const unsigned char> bytes) {
        if(bytes.empty())throw std::invalid_argument("empty menu resource");
        // Compare owned bytes as well as identity: equal-size in-place reloads
        // cannot reuse an old GPU lease. Dynamic previews/flags append safely.
        for(const auto& e:entries_)if(e.pointer==bytes.data()&&e.size==bytes.size()
            &&std::memcmp(reinterpret_cast<const unsigned char*>(resources.data())+e.offset,bytes.data(),bytes.size())==0)return e.offset;
        const auto words=(bytes.size()+3)/4;
        if(words>maxBytes/4-resources.size())throw std::length_error("menu resource budget");
        const auto offset=static_cast<std::uint32_t>(resources.size()*4);
        resources.resize(resources.size()+words,0);
        std::memcpy(reinterpret_cast<unsigned char*>(resources.data())+offset,bytes.data(),bytes.size());
        entries_.push_back({bytes.data(),bytes.size(),offset});++revision;return offset;
    }
    void push(MenuDrawCommand c) {
        if(commands.size()==maxCommands)throw std::length_error("menu command budget");
        if(c.right>c.left&&c.bottom>c.top){commands.push_back(c);++frameRevision;}
    }
    void fill(RuntimeInputPixelRect r,RuntimeRgbColorValue color) {
        MenuDrawCommand c;c.left=r.left;c.top=r.top;c.right=r.right;c.bottom=r.bottom;
        c.color=color.blue()|(static_cast<std::uint32_t>(color.green())<<8)|(static_cast<std::uint32_t>(color.red())<<16)|0xff000000U;push(c);
    }
    void darken(RuntimeInputPixelRect r,unsigned opacity) {
        MenuDrawCommand c;c.left=r.left;c.top=r.top;c.right=r.right;c.bottom=r.bottom;
        c.kind=MenuDrawKind::Darken;c.color=255U-std::min(255U,opacity);push(c);
    }
    void bgra(ConstMenuPixelSurfaceView s,RuntimeInputPixelRect r,bool scale) {
        if(!s.valid())return;
        MenuDrawCommand c;c.left=r.left;c.top=r.top;c.right=r.right;c.bottom=r.bottom;
        c.kind=scale?MenuDrawKind::Scale:MenuDrawKind::Bgra;c.offset=intern(s.bytes);
        c.stride=static_cast<std::uint32_t>(s.rowStride);c.width=s.width;c.height=s.height;
        c.originX=r.left;c.originY=r.top;push(c);
    }
    void image(const assets::MenuImage& image,int left,int top,int clipHeight) {
        if(!image.valid())return;
        if(image.indices.empty()) {bgra({image.bgra,image.width,image.height,static_cast<std::size_t>(image.width)*4},
            {left,top,left+image.width,top+std::min(image.height,clipHeight)},false);return;}
        MenuDrawCommand c;c.left=left+std::max(0,image.indexLeft);c.top=top+std::max(0,image.indexTop);
        c.right=left+std::min(image.width,image.indexLeft+image.indexWidth);
        c.bottom=top+std::min({image.height,clipHeight,image.indexTop+image.indexHeight});
        c.kind=MenuDrawKind::Indexed;c.offset=intern(image.indices);c.stride=image.indexWidth;
        c.width=image.indexWidth;c.height=image.indexHeight;c.originX=left+image.indexLeft;c.originY=top+image.indexTop;
        c.palette=intern({reinterpret_cast<const unsigned char*>(image.paletteBgra.data()),sizeof(image.paletteBgra)});
        c.flags=image.transparentZero?1U:0U;push(c);
    }
    bool valid() const noexcept {
        if(!generation||commands.empty()||commands.size()>maxCommands||resources.empty()||resources.size()>maxBytes/4)return false;
        const auto bytes=static_cast<std::uint64_t>(resources.size())*4;
        for(const auto& c:commands) {
            const auto kind=static_cast<std::uint32_t>(c.kind);
            if(kind>5||c.right<=c.left||c.bottom<=c.top)return false;
            if(kind<2)continue;
            if(kind==4&&(static_cast<std::uint64_t>(c.palette)+1024>bytes||c.palette%4
                ||((c.flags&2U)&&static_cast<std::uint64_t>(c.reserved0)+256>bytes)))return false;
            if(kind==5) {
                const auto rows=static_cast<std::int64_t>(c.bottom)-c.originY;
                const auto cols=static_cast<std::int64_t>(c.right)-c.originX;
                if(c.left<c.originX||c.top<c.originY||rows<=0||cols<=0||c.stride==0
                    ||static_cast<std::uint64_t>((cols+7)/8)>c.stride
                    ||static_cast<std::uint64_t>(c.offset)+(rows-1)*c.stride+(cols+7)/8>bytes)return false;
            } else {
                const auto channels=kind==4?1U:4U;
                if(!c.width||!c.height||static_cast<std::uint64_t>(c.width)*channels>c.stride
                    ||static_cast<std::uint64_t>(c.offset)+static_cast<std::uint64_t>(c.height-1)*c.stride
                        +static_cast<std::uint64_t>(c.width)*channels>bytes)return false;
                if(kind!=3&&(c.left<c.originX||c.top<c.originY
                    ||static_cast<std::int64_t>(c.right)-c.originX>c.width
                    ||static_cast<std::int64_t>(c.bottom)-c.originY>c.height))return false;
            }
        }
        return true;
    }
private:
    struct Entry {const unsigned char* pointer;std::size_t size;std::uint32_t offset;};
    std::vector<Entry> entries_;
};
// A synchronous borrow. Owner must outlive return; mutation invalidates it.
struct MenuDrawFrame {
    const MenuDrawList& list;
    std::uint64_t sequence=0;
    std::int32_t width=0,height=0;
    RuntimeInputPixelRect viewport;
    std::uint64_t generation,resourceRevision,commandRevision;
    MenuDrawFrame(const MenuDrawList& owner,std::uint64_t seq,std::int32_t w,std::int32_t h,RuntimeInputPixelRect view)
        :list(owner),sequence(seq),width(w),height(h),viewport(view),generation(owner.generation),
        resourceRevision(owner.revision),commandRevision(owner.frameRevision){}
};
} // namespace concord::ui
