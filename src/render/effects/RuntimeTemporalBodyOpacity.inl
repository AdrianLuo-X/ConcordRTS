// Blend the completed body once. This includes the hull, turret, barrel and
// building animation layers, while shadows and later UI keep their own opacity.
class RuntimeTemporalBodyOpacityScope {
public:
    RuntimeTemporalBodyOpacityScope(HDC dc,const RECT& viewport,
        const RuntimeMapRenderContext& context,RuntimeMapRenderStats& stats,
        POINT center,int margin,bool enabled)
        : dc_(dc),context_(context),stats_(stats) {
        if(!enabled)return;
        direct_=hasPixelSurface(context);
        if(!direct_&&dc==nullptr)return;
        bounds_={std::max(viewport.left,center.x-margin),std::max(viewport.top,center.y-margin),
            std::min(viewport.right,center.x+margin+1),std::min(viewport.bottom,center.y+margin+1)};
        if(direct_) {
            bounds_.left=std::max<LONG>(0,bounds_.left);bounds_.top=std::max<LONG>(0,bounds_.top);
            bounds_.right=std::min<LONG>(context.surfaceWidth,bounds_.right);
            bounds_.bottom=std::min<LONG>(context.surfaceHeight,bounds_.bottom);
        }
        if(bounds_.left>=bounds_.right||bounds_.top>=bounds_.bottom)return;
        GdiFlush();before_.reserve(static_cast<std::size_t>(bounds_.right-bounds_.left)*(bounds_.bottom-bounds_.top));
        for(int y=bounds_.top;y<bounds_.bottom;++y)for(int x=bounds_.left;x<bounds_.right;++x)before_.push_back(read(x,y));
    }
    ~RuntimeTemporalBodyOpacityScope() {
        if(before_.empty())return;
        GdiFlush();std::size_t i=0;bool changed=false;
        for(int y=bounds_.top;y<bounds_.bottom;++y)for(int x=bounds_.left;x<bounds_.right;++x,++i) {
            const auto pixel=read(x,y);
            if(pixel==before_[i]||(!direct_&&(pixel==CLR_INVALID||before_[i]==CLR_INVALID)))continue;
            const auto background=direct_?colorRefFromRuntimeDibColor(before_[i]):before_[i];
            const auto body=direct_?colorRefFromRuntimeDibColor(pixel):pixel;
            const auto blended=runtimeBlendTint(background,body,128);
            if(direct_)context_.surfacePixels[y*context_.surfaceWidth+x]=packRuntimeDibColor(blended);
            else SetPixelV(dc_,x,y,blended);
            changed=true;
        }
        if(changed){++stats_.specialMechanismRenderActors;runtimeMixRenderIntentHash(stats_.renderIntentHash,"temporalFrozenBody");}
    }
    RuntimeTemporalBodyOpacityScope(const RuntimeTemporalBodyOpacityScope&)=delete;
    RuntimeTemporalBodyOpacityScope& operator=(const RuntimeTemporalBodyOpacityScope&)=delete;
private:
    std::uint32_t read(int x,int y)const{return direct_?context_.surfacePixels[y*context_.surfaceWidth+x]:GetPixel(dc_,x,y);}
    HDC dc_;const RuntimeMapRenderContext& context_;RuntimeMapRenderStats& stats_;
    RECT bounds_{};bool direct_=false;std::vector<std::uint32_t> before_;
};
