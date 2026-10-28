// Explicit process-only diagnostic display input; normal rendering remains unchanged.
class RuntimeGpuLightFixtureContext {
    RuntimeLightingTint tint_{0.73,0.91,0.68};
    std::vector<RuntimeLocalLightSnapshot> lights_;
    std::vector<SpecialMechanismRenderSnapshot> sites_;
public:
    explicit RuntimeGpuLightFixtureContext(RuntimeMapRenderContext& context) {
        if(!runtimeRendererEnvironmentValue("CONCORD_S41_LIGHT_SCENE"))return;
        if(context.localLightSnapshots)lights_=*context.localLightSnapshots;
        if(context.specialMechanismSnapshots)sites_=*context.specialMechanismSnapshots;
        RuntimeLocalLightSnapshot light;light.stableId=61002;light.owner="Americans";light.worldX=80;light.worldY=80;light.radiusPixels=137;light.red=255;light.green=93;light.blue=31;light.intensity=173;light.revision=61002;lights_.push_back(light);
        light.stableId=61001;light.worldX=78.5;light.worldY=80;light.radiusPixels=192;light.red=17;light.green=53;light.blue=255;light.intensity=117;light.revision=61001;lights_.push_back(light);
        SpecialMechanismRenderSnapshot site;site.kind="radiation";site.cell={76,80};site.radius=6;site.intensity=site.maximumIntensity=500;site.expireTick=500;site.color="0,255,0";sites_.push_back(site);
        site.cell={82,78};site.radius=4;site.intensity=333;site.color="160,31,211";sites_.push_back(site);
        context.lightingTint=&tint_;context.lightingProfile=nullptr;context.localLightSnapshots=&lights_;context.specialMechanismSnapshots=&sites_;
    }
};
