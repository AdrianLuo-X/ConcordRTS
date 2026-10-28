
// Preserve the already composited foreground on GPU; CPU keeps its original snapshot.
class RuntimeGpuFactoryForegroundScope {
    RuntimeGpuWorldNative* owner_=nullptr;concord::render::MapDrawCommand restore_;bool active_=false;
public:
    RuntimeGpuFactoryForegroundScope(const RECT& clip,const WorldObjectInstance& host,const POINT& center,
        const RuntimeMapRenderContext& context,const RuntimeMapCamera& camera,RuntimeMapRenderCache& cache) {
        if(!runtimeGpuWorldTarget(context))return;owner_=runtimeGpuWorldOwner;
        try {const RECT bounds{std::max<LONG>(0,clip.left),std::max<LONG>(0,clip.top),std::min<LONG>(context.surfaceWidth,clip.right),std::min<LONG>(context.surfaceHeight,clip.bottom)};
            if(bounds.left>=bounds.right||bounds.top>=bounds.bottom)return;
            std::vector<RuntimeSpriteHitSpan> spans;RuntimeSpriteGeometryCapture capture;capture.hitSpans=&spans;
            {RuntimeSpriteGeometryObservationScope observe(nullptr);RuntimeSpriteGeometryScope geometry(&capture);
                (void)drawRuntimeFactoryAirExitForeground(nullptr,bounds,host,center,context,camera,cache);}
            const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;std::vector<unsigned char> mask(static_cast<std::size_t>(width)*height);
            for(const auto& span:spans){if(span.y<bounds.top||span.y>=bounds.bottom)continue;
                for(int x=std::max(span.left,bounds.left);x<std::min(span.right,bounds.right);++x)mask[static_cast<std::size_t>(span.y-bounds.top)*width+x-bounds.left]=1;}
            if(std::none_of(mask.begin(),mask.end(),[](unsigned char value){return value!=0;}))return;
            auto* list=owner_->drawList();if(!list)throw std::runtime_error("factory foreground packet");
            restore_=runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::RestoreMask);restore_.width=width;restore_.height=height;restore_.stride=width;restore_.offset=list->intern(mask);
            active_=runtimeGpuWorldEmit(runtimeGpuWorldRegion(bounds,concord::render::MapDrawKind::Save));
        }catch(...){owner_->reject("world-factory-foreground-budget");}
    }
    void finish() noexcept{if(active_){active_=false;(void)owner_->emit(restore_);}}
    ~RuntimeGpuFactoryForegroundScope(){finish();}
    RuntimeGpuFactoryForegroundScope(const RuntimeGpuFactoryForegroundScope&)=delete;
    RuntimeGpuFactoryForegroundScope& operator=(const RuntimeGpuFactoryForegroundScope&)=delete;
};
