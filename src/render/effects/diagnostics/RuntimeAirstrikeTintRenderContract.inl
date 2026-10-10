bool runRuntimeAirstrikeTintRenderContract(const RulesDatabase& rules, std::string& error) {
    const auto fail=[&](const char* message) { error=std::string("Airstrike tint: ")+message; return false; };
    RuntimeMapRenderContext context; context.rules=&rules;
    if (runtimeAirstrikeColorAdd(context)!=0xC000U) return fail("native LaserTargetColor/HighRed resolution");
    WorldObjectInstance building; building.instanceId=3; building.alive=true;
    building.kind=WorldObjectKind::Structure;
    SpecialMechanismRenderSnapshot mark; mark.kind="airstrikeMarker"; mark.targetObjectId=3;
    mark.intensity=208; mark.phase="Inbound";
    std::vector<SpecialMechanismRenderSnapshot> marks{mark}; context.specialMechanismSnapshots=&marks;
    if (!runtimeObjectHasAirstrikeTint(building,context)) return fail("live target not tinted");
    building.instanceId=4;
    if (runtimeObjectHasAirstrikeTint(building,context)) return fail("unrelated building tinted");
    building.instanceId=3; building.kind=WorldObjectKind::Infantry;
    if (runtimeObjectHasAirstrikeTint(building,context)) return fail("infantry tinted");
    building.kind=WorldObjectKind::Structure; marks[0].phase="Cancelled";
    if (runtimeObjectHasAirstrikeTint(building,context)) return fail("cancelled target tinted");
    marks[0].phase="Inbound"; marks[0].expireTick=10; context.animationTick=10;
    if (runtimeObjectHasAirstrikeTint(building,context)) return fail("expired target tinted");
    constexpr int width=32,height=32;
    constexpr RECT viewport{0,0,width,height}; const POINT center{16,16};
    RuntimeMapCamera camera;
    RuntimeShpFrameImage sprite; sprite.width=2; sprite.height=2;
    sprite.pixels={0,1,2,3};
    std::vector<RuntimePaletteColor> palette(256);
    palette[1]={32,90,150}; palette[2]={241,121,63}; palette[3]={0,0,0};
    const auto background=packRuntimeDibColor(RGB(33,44,55));
    std::vector<std::uint32_t> normal(width*height,background),tinted(normal),restored(normal);
    context.surfaceWidth=width; context.surfaceHeight=height; context.surfacePixels=normal.data();
    if (!drawRuntimeShpFrame(nullptr,viewport,center,context,camera,sprite,palette)) return fail("normal blit");
    context.airstrikeBodyColorAdd=0xC000U; context.surfacePixels=tinted.data();
    if (!drawRuntimeShpFrame(nullptr,viewport,center,context,camera,sprite,palette)) return fail("tinted blit");
    int changes=0;
    for (std::size_t i=0;i<normal.size();++i) {
        const auto expected=normal[i]==background?background:normal[i]|0xC60000U;
        if (tinted[i]!=expected) return fail("opaque/transparent pixel color mismatch");
        if (normal[i]!=tinted[i]) ++changes;
    }
    context.airstrikeBodyColorAdd=0; context.surfacePixels=restored.data();
    if (!drawRuntimeShpFrame(nullptr,viewport,center,context,camera,sprite,palette)||restored!=normal||changes==0)
        return fail("palette cache did not return to original colors");
    IniFile overrideIni; RulesDatabase overrideRules;
    if (!overrideIni.loadFromString("[General]\nLaserTargetColor=1\n[ColorAdd]\nNone=0,0,0\nGreen=0,56,0\n")
        ||!overrideRules.loadFromIni(overrideIni)) return fail("override rules parse");
    context.rules=&overrideRules;
    if (runtimeAirstrikeColorAdd(context)!=0x0700U) return fail("ordered ColorAdd override ignored");
    std::cout<<"Airstrike tint: passed native RGB565 OR, target/cancel/expiry, transparent pixels, palette restoration, custom table\n";
    return true;
}
