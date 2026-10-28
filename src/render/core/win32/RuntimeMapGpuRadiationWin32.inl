// G007: original CPU geometry/alpha, exact original channel LUTs, GPU destination RGB.
class RuntimeGpuRadiationScope;
static thread_local RuntimeGpuRadiationScope* runtimeGpuRadiationScope=nullptr;
class RuntimeGpuRadiationScope {
    RuntimeGpuRadiationScope* previous_=runtimeGpuRadiationScope;
    RuntimeGpuWorldNative* owner_=nullptr;RECT bounds_{};int width_=0,height_=0;COLORREF tint_=0;
    double tintFactor_=1.0,lightFactor_=0.1;std::vector<unsigned char> alphas_;bool touched_=false;int minX_=0,minY_=0,maxX_=-1,maxY_=-1;
public:
    RuntimeGpuRadiationScope(const RECT& viewport,const RuntimeMapRenderContext& context,COLORREF tint):tint_(tint) {
        if(!runtimeGpuWorldTarget(context))return;owner_=runtimeGpuWorldOwner;runtimeGpuRadiationScope=this;
        try {bounds_={std::max<LONG>(0,viewport.left),std::max<LONG>(0,viewport.top),std::min<LONG>(context.surfaceWidth,viewport.right),std::min<LONG>(context.surfaceHeight,viewport.bottom)};
            width_=bounds_.right-bounds_.left;height_=bounds_.bottom-bounds_.top;if(width_<=0||height_<=0)return;
            const auto area=static_cast<std::uint64_t>(width_)*height_;
            if(area>concord::render::MapDrawList::maxBytes)throw std::length_error("radiation CPU alpha budget");
            alphas_.assign(static_cast<std::size_t>(area),0);minX_=bounds_.right;minY_=bounds_.bottom;maxX_=bounds_.left-1;maxY_=bounds_.top-1;if(context.rules){const auto& rules=context.rules->getSpecialMechanismRules();tintFactor_=rules.radiationTintFactor;lightFactor_=rules.radiationLightFactor;}
        }catch(...){owner_->reject("world-radiation-budget");alphas_.clear();}
    }
    bool pixel(int x,int y,COLORREF tint,int alpha) {
        if(!owner_||alphas_.empty())return false;if(owner_!=runtimeGpuWorldOwner){owner_->reject("world-radiation-owner");return false;}if(x<bounds_.left||x>=bounds_.right||y<bounds_.top||y>=bounds_.bottom)return false;
        if(tint!=tint_||alpha<0||alpha>255){owner_->reject("world-radiation-input");return false;}
        if(!alpha)return true;auto& receiver=alphas_[static_cast<std::size_t>(y-bounds_.top)*width_+x-bounds_.left];
        if(receiver){owner_->reject("world-radiation-duplicate");return false;}receiver=static_cast<unsigned char>(alpha);touched_=true;minX_=std::min(minX_,x);minY_=std::min(minY_,y);maxX_=std::max(maxX_,x);maxY_=std::max(maxY_,y);return true;
    }
    ~RuntimeGpuRadiationScope() {
        runtimeGpuRadiationScope=previous_;if(!owner_||!touched_)return;
        try {auto* list=owner_->drawList();if(!list)return;const RECT changed{minX_,minY_,maxX_+1,maxY_+1};const int width=changed.right-changed.left,height=changed.bottom-changed.top;
            std::array<bool,256> used{};used[0]=true;for(int y=changed.top;y<changed.bottom;++y)for(int x=changed.left;x<changed.right;++x)used[alphas_[static_cast<std::size_t>(y-bounds_.top)*width_+x-bounds_.left]]=true;
            const auto tableCount=static_cast<std::size_t>(std::count(used.begin(),used.end(),true)),occupied=list->resources().size()*4;const auto planeBytes=static_cast<std::uint64_t>(width)*height*4;
            if(occupied>concord::render::MapDrawList::maxBytes||planeBytes+tableCount*1024>concord::render::MapDrawList::maxBytes-occupied)throw std::length_error("radiation owned packet budget");
            std::array<std::uint32_t,256> ids{};std::vector<std::array<std::uint32_t,256>> tables;
            for(unsigned alpha=0;alpha<256;++alpha)if(used[alpha]){ids[alpha]=static_cast<std::uint32_t>(tables.size());auto& table=tables.emplace_back();
                for(unsigned value=0;value<256;++value)table[value]=packDibColor(runtimeRadiationTerrainLight(RGB(value,value,value),tint_,static_cast<int>(alpha),tintFactor_,lightFactor_));}
            std::vector<std::uint32_t> receivers(static_cast<std::size_t>(width)*height);
            for(int y=changed.top;y<changed.bottom;++y)for(int x=changed.left;x<changed.right;++x)receivers[static_cast<std::size_t>(y-changed.top)*width+x-changed.left]=ids[alphas_[static_cast<std::size_t>(y-bounds_.top)*width_+x-bounds_.left]];
            auto c=runtimeGpuWorldRegion(changed,concord::render::MapDrawKind::ReceiverLookup);c.originX=changed.left;c.originY=changed.top;c.width=width;c.height=height;c.stride=width*4;c.parameter=static_cast<std::uint32_t>(tables.size());
            c.offset=list->internValues<std::uint32_t>(receivers);c.palette=list->internValues<std::array<std::uint32_t,256>>(tables);(void)owner_->emit(c);
        }catch(...){owner_->reject("world-radiation-packet-budget");}
    }
    RuntimeGpuRadiationScope(const RuntimeGpuRadiationScope&)=delete;
    RuntimeGpuRadiationScope& operator=(const RuntimeGpuRadiationScope&)=delete;
};
static bool runtimeGpuCollectRadiationPixel(const RuntimeMapRenderContext& context,int x,int y,COLORREF tint,int alpha) {
    if(x<0||y<0||x>=context.surfaceWidth||y>=context.surfaceHeight)return false;
    if(!runtimeGpuRadiationScope){runtimeGpuWorldOwner->reject("world-radiation-unscoped");return false;}
    return runtimeGpuRadiationScope->pixel(x,y,tint,alpha);
}
