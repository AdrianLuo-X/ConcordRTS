#include "render/units/diagnostics/RuntimePlayerCheerSequenceContract.h"

bool runRuntimePlayerCheerSequenceCheck(std::string& error) {
    ArtType art; art.id="HotkeyCheer";
    art.sequenceFrames={{"Ready",true,0,1,0},{"Walk",true,8,6,0},{"Fire",true,20,6,0},{"Cheer",true,30,8,0}};
    WorldObjectInstance object; object.instanceId=42; object.kind=WorldObjectKind::Infantry;
    object.alive=true; object.health=100; object.mission="Guard";
    RuntimeMapRenderContext context; context.animationTick=1;
    std::unordered_map<int,int> cheering{{42,60}}; context.playerCheerUntilTick=&cheering;
    RuntimeMapRenderCache cache;
    const auto frame=selectRuntimeObjectSequenceFrame(object,context,&art,100,&cache);
    if (!frame.matched || frame.sequenceName!="Cheer" || frame.frameIndex<30 || frame.frameIndex>=38
        || object.mission!="Guard") { error="idle Cheer sequence failed"; return false; }
    context.animationTick=61;
    const auto expired=selectRuntimeObjectSequenceFrame(object,context,&art,100,&cache);
    if (expired.sequenceName=="Cheer") { error="Cheer did not expire"; return false; }
    context.animationTick=2; object.mission="Attack";
    const auto attack=selectRuntimeObjectSequenceFrame(object,context,&art,100,&cache);
    if (attack.sequenceName=="Cheer") { error="Cheer interrupted firing"; return false; }
    object.mission="Guard"; object.hasRenderPosition=true; object.renderMoveActive=true;
    const auto move=selectRuntimeObjectSequenceFrame(object,context,&art,100,&cache);
    if (move.sequenceName=="Cheer") { error="Cheer interrupted movement"; return false; }
    error.clear(); return true;
}
