// G005: borrow original palette-table preparation, return before every source RGB loop.
using RuntimeGpuSpriteLightTables=std::array<std::array<std::uint32_t,256>,RuntimeVxlLightBucketCount>;
static thread_local std::optional<std::uint32_t>* runtimeGpuSpriteMaterialCapture=nullptr;
static thread_local std::uint64_t runtimeGpuSpriteMaterialCaptures=0;
struct RuntimeGpuSpriteMaterialCaptureScope {
    decltype(runtimeGpuSpriteMaterialCapture) previous=runtimeGpuSpriteMaterialCapture;
    explicit RuntimeGpuSpriteMaterialCaptureScope(std::optional<std::uint32_t>& target){runtimeGpuSpriteMaterialCapture=&target;}
    ~RuntimeGpuSpriteMaterialCaptureScope(){runtimeGpuSpriteMaterialCapture=previous;}
};
static bool runtimeGpuCaptureSpriteMaterial(const RuntimeShpFrameImage& image,const RuntimeMapRenderContext& context,
    bool ownerRemap,bool remapMaskAvailable,bool lightLevelsAvailable,bool soften,
    const std::vector<std::uint32_t>& base,const std::vector<std::uint32_t>& remap,
    const RuntimeGpuSpriteLightTables& lit,const RuntimeGpuSpriteLightTables& litRemap) {
    if(!runtimeGpuSpriteMaterialCapture)return false;
    auto* list=runtimeGpuWorldOwner?runtimeGpuWorldOwner->drawList():nullptr;if(!list)throw std::runtime_error("sprite material owner");
    using Material=concord::render::MapSpriteMaterial;Material m;m.width=image.width;m.height=image.height;
    m.indices=list->intern(image.pixels);m.baseColors=list->internValues<std::uint32_t>(base);
    if(ownerRemap){m.flags|=Material::Owner;m.remapColors=list->internValues<std::uint32_t>(remap);}
    if(remapMaskAvailable){m.flags|=Material::Mask;m.remapMask=list->intern(image.ownerRemapMask);}
    if(lightLevelsAvailable){m.flags|=Material::Light;m.lightLevels=list->intern(image.lightLevels);
        m.litColors=list->internValues<std::array<std::uint32_t,256>>(lit);if(ownerRemap)m.litRemapColors=list->internValues<std::array<std::uint32_t,256>>(litRemap);}
    if(soften)m.flags|=Material::Soften;if(context.chaosBodyTint)m.flags|=Material::Chaos;m.airstrike=context.airstrikeBodyColorAdd;
    *runtimeGpuSpriteMaterialCapture=list->internValues<Material>({&m,1});++runtimeGpuSpriteMaterialCaptures;return true;
}
