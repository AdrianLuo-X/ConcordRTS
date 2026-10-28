const ArtType* runtimeProjectilePresentationArt(const RuntimeProjectileRenderActor& actor,
    const RuntimeMapRenderContext& context) {
    if (!context.art) return nullptr;
    std::string image = actor.projectileImageId;
    if (image.empty() && context.rules) {
        if (const auto* projectile = context.rules->findProjectileType(actor.projectileId)) image = projectile->image;
    }
    return context.art->findArtType(runtimeProjectilePresentationImage(actor,
        image.empty() ? actor.projectileId : image, context));
}

bool runtimeImpactReplacedByLineTrail(const RuntimeImpactRenderActor& actor,
    const RuntimeMapRenderContext& context) {
    if (actor.renderIntent != "missileExhaust" || !context.art || !context.rules) return false;
    const auto* type = context.rules->findProjectileType(actor.projectileImageId);
    const auto* art = context.art->findArtType(actor.projectileImageId);
    if (!art && type) art = context.art->findArtType(type->image);
    // Older exhaust actors carry the projectile's type as their image id.
    return art && art->useLineTrail;
}

std::array<int,3> runtimeReadLineTrailColor(const std::string& text) {
    std::istringstream input(text);
    std::array<int,3> color{}; char first=0, second=0;
    if (!(input >> color[0] >> first >> color[1] >> second >> color[2]) || first!=',' || second!=',') return {};
    for (auto& component : color) component = std::clamp(component,0,255);
    return color;
}

POINT runtimeLineTrailScreenPoint(const RuntimeLineTrailPoint& point, const RECT& viewport,
    const RuntimeMapCamera& camera) {
    POINT screen = screenPoint(projectCellXY(point.x,point.y,point.z,camera),viewport,camera);
    screen.x += runtimeMuzzlePixelX(point.offsetX, camera, point.fixedX);
    screen.y += runtimeMuzzlePixelY(point.offsetY, camera, point.fixedY);
    return screen;
}

int drawRuntimeLineTrailSegment(HDC dc, const RECT& viewport, const RuntimeMapRenderContext& context,
    const RuntimeMapCamera& camera, const RuntimeProjectileLineTrail& trail,
    const RuntimeLineTrailPoint& a, const RuntimeLineTrailPoint& b, double now) {
    if (!runtimeVisionActorIsVisible(context,trail.owner,a.x,a.y)
        || !runtimeVisionActorIsVisible(context,trail.owner,b.x,b.y)) return 0;
    const POINT pa = runtimeLineTrailScreenPoint(a,viewport,camera);
    const POINT pb = runtimeLineTrailScreenPoint(b,viewport,camera);
    const double dx=static_cast<double>(pb.x)-pa.x, dy=static_cast<double>(pb.y)-pa.y;
    if (std::abs(dx)+std::abs(dy) < 0.001) return 0;
    double lo=0, hi=1;
    const auto clip=[&](double p,double q) {
        if (std::abs(p)<1e-12) return q>=0;
        const double t=q/p;
        if (p<0) lo=std::max(lo,t); else hi=std::min(hi,t);
        return lo<=hi;
    };
    if (!clip(-dx,pa.x-viewport.left) || !clip(dx,viewport.right-1.0-pa.x)
        || !clip(-dy,pa.y-viewport.top) || !clip(dy,viewport.bottom-1.0-pa.y)) return 0;
    const int steps=std::max(1,static_cast<int>(std::ceil(std::max(std::abs(dx),std::abs(dy))*(hi-lo))));
    int drawn=0;
    for(int step=0;step<steps;++step) {
        const double t=lo+(hi-lo)*step/steps;
        const auto color=runtimeLineTrailFadedColor(trail.color,trail.decrement,
            now-(a.frame+(b.frame-a.frame)*t));
        if (color[0]+color[1]+color[2]==0) continue;
        const int x=static_cast<int>(std::lround(pa.x+dx*t)), y=static_cast<int>(std::lround(pa.y+dy*t));
        if (x<viewport.left || y<viewport.top || x>=viewport.right || y>=viewport.bottom) continue;
        std::uint32_t* pixel = hasPixelSurface(context) && x>=0 && y>=0
            && x<context.surfaceWidth && y<context.surfaceHeight
            ? &context.surfacePixels[static_cast<std::size_t>(y)*context.surfaceWidth+x] : nullptr;
        const COLORREF old = pixel ? colorRefFromRuntimeDibColor(*pixel) : dc ? concord::ui::win32::MapNativeOverlayWin32::readPixel(dc,x,y) : CLR_INVALID;
        if (old==CLR_INVALID) continue;
        const COLORREF next=RGB(std::min(255,GetRValue(old)+color[0]),
            std::min(255,GetGValue(old)+color[1]),std::min(255,GetBValue(old)+color[2]));
        if (pixel) *pixel=packRuntimeDibColor(next); else SetPixelV(dc,x,y,next);
        ++drawn;
    }
    return drawn;
}

void drawRuntimeProjectileLineTrails(HDC dc, const RECT& viewport,
    const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache, RuntimeMapRenderStats& stats) {
    auto& trails=cache.projectileLineTrails;
    if (!context.projectileActors || context.animationTick<cache.projectileLineTrailTick) trails.clear();
    cache.projectileLineTrailTick=context.animationTick;
    if (!context.projectileActors) return;
    const int hz=std::max(1,context.fixedTickHz);
    const double now=static_cast<double>(context.animationTick)*15.0/hz;
    for(const auto& actor:*context.projectileActors) {
        const auto* art=runtimeProjectilePresentationArt(actor,context);
        if (!art || !art->useLineTrail || actor.actorId<=0 || actor.remainingTicks<=0
            || runtimeActorLifecycleIsExpired(actor.lifecycleState)) continue;
        const int spawnTick=context.animationTick-std::max(0,actor.frameIndex);
        if (!trails.count(actor.actorId) && trails.size()>=8192U) continue;
        auto& trail=trails[actor.actorId];
        const double progress=std::clamp(actor.progress,0.0,1.0);
        RuntimeLineTrailPoint next{actor.currentX,actor.currentY,actor.currentLevel,now,
            actor.sourceScreenOffsetX*(1-progress),actor.sourceScreenOffsetY*(1-progress),
            actor.sourceNativeFixedX*(1-progress),actor.sourceNativeFixedY*(1-progress)};
        if (!actor.hasCurrentPosition) {
            next.x=actor.fromX+(actor.toX-actor.fromX)*progress;
            next.y=actor.fromY+(actor.toY-actor.fromY)*progress;
            next.z=actor.fromLevel+(actor.toLevel-actor.fromLevel)*progress;
        }
        if (trail.owner!=actor.owner || trail.image!=art->id || trail.spawnTick!=spawnTick) {
            trail={}; trail.owner=actor.owner; trail.image=art->id; trail.spawnTick=spawnTick;
            if (actor.frameIndex>=0 && actor.frameIndex*15.0/hz<=4) {
                runtimeLineTrailSample(trail,{actor.fromX,actor.fromY,actor.fromLevel,
                    static_cast<double>(spawnTick)*15.0/hz,
                    static_cast<double>(actor.sourceScreenOffsetX),static_cast<double>(actor.sourceScreenOffsetY),
                    static_cast<double>(actor.sourceNativeFixedX),static_cast<double>(actor.sourceNativeFixedY)});
            }
        }
        trail.lastSeenTick=context.animationTick;
        trail.color=runtimeReadLineTrailColor(art->lineTrailColor);
        trail.decrement=art->lineTrailColorDecrement;
        runtimeLineTrailSample(trail,next);
    }
    if (dc && hasPixelSurface(context)) GdiFlush();
    for(auto it=trails.begin();it!=trails.end();) {
        auto& trail=it->second;
        const double sinceLast=static_cast<double>(context.animationTick-trail.lastSeenTick)*15.0/hz;
        if (sinceLast>32 || (trail.decrement>0 && sinceLast*trail.decrement>=255)) {
            it=trails.erase(it); continue;
        }
        const auto draw=[&](const RuntimeLineTrailPoint& a,const RuntimeLineTrailPoint& b) {
            const int pixels=drawRuntimeLineTrailSegment(dc,viewport,context,camera,trail,a,b,now);
            if(pixels>0){++stats.projectileLineTrailSegments;stats.projectileLineTrailPixels+=pixels;}
        };
        for(std::size_t n=1;n<trail.points.size();++n) draw(trail.points[n-1],trail.points[n]);
        if(!trail.points.empty() && trail.head.frame>trail.points.back().frame) draw(trail.points.back(),trail.head);
        ++it;
    }
}

int runtimeProjectileBodyFrame(const RuntimeProjectileRenderActor& actor, const ArtType* art,
    const std::string& image, const RuntimeMapRenderContext& context, const RuntimeMapCamera& camera,
    RuntimeMapRenderCache& cache) {
    if (!art || !art->rotates) return runtimeActorFrameIndex(actor.frameIndex,actor.totalTicks,actor.remainingTicks);
    const int frames=runtimeAuxiliaryShpFrameCount(context,cache,image);
    const int directions=std::max(1,std::min(frames,art->facings>0 ? art->facings : 32));
    // Retail projectile SHP directions run counterclockwise from north.
    // The screen-facing index used by the rest of the renderer is clockwise.
    const auto spriteFrame=[&](double dx,double dy){
        return (directions-runtimeDirectionalProjectileFrame(dx,dy,directions))%directions;
    };
    const auto from=projectCellXY(actor.hasCurrentPosition?actor.currentX:actor.fromX,
        actor.hasCurrentPosition?actor.currentY:actor.fromY,
        actor.hasCurrentPosition?actor.currentLevel:actor.fromLevel,camera);
    if (actor.hasFlightDirection) {
        const auto next=projectCellXY(actor.currentX+actor.flightDirectionX,
            actor.currentY+actor.flightDirectionY,actor.currentLevel+actor.flightDirectionZ,camera);
        return spriteFrame(next.x-from.x,next.y-from.y);
    }
    const auto target=projectCellXY(actor.toX,actor.toY,actor.toLevel,camera);
    if(from.x==target.x && from.y==target.y) {
        const auto direction=runtimeFacingScreenVector(std::max(0,actor.facing));
        return spriteFrame(direction.x,direction.y);
    }
    return spriteFrame(target.x-from.x,target.y-from.y);
}
