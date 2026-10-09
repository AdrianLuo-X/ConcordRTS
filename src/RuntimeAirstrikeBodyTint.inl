std::uint16_t runtimeAirstrikeColorAdd(const RuntimeMapRenderContext& context) {
    int red=24, green=0, blue=0;
    if (context.rules) {
        const auto value=context.rules->getAudioVisualValue("LaserTargetColorAdd");
        if (!value.empty()) {
            std::istringstream input(value); char first=0,second=0;
            if (!(input>>red>>first>>green>>second>>blue)||first!=','||second!=',') return 0;
        }
    }
    return static_cast<std::uint16_t>((std::clamp(red,0,31)<<11)
        |(std::clamp(green,0,63)<<5)|std::clamp(blue,0,31));
}

bool runtimeObjectHasAirstrikeTint(const WorldObjectInstance& object,
    const RuntimeMapRenderContext& context) {
    if (!object.alive||object.kind!=WorldObjectKind::Structure
        ||!context.specialMechanismSnapshots) return false;
    for (const auto& mark:*context.specialMechanismSnapshots)
        if (mark.kind=="airstrikeMarker"&&mark.targetObjectId==object.instanceId
            &&mark.phase!="Cancelled"&&mark.intensity>0
            &&(mark.expireTick<=0||context.animationTick<mark.expireTick)) return true;
    return false;
}

std::uint32_t runtimeAirstrikeTintPackedColor(std::uint32_t pixel, std::uint16_t add) {
    if (add==0) return pixel;
    // Retail combines the five/six/five color bits using OR. Preserve our
    // extra channel precision in bits where the original has no color add.
    const auto expand5=[](unsigned value) { return (value<<3)|(value>>2); };
    const auto expand6=[](unsigned value) { return (value<<2)|(value>>4); };
    return pixel | (expand5((add>>11)&31U)<<16U)
        |(expand6((add>>5)&63U)<<8U)|expand5(add&31U);
}
