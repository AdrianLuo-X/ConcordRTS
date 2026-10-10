// The oracle executes the complete original decoder through its independent
// reference scope. It does not compare copies of the six-normal table builder.
namespace {
thread_local const char* runtimeVoxelFaceContractProfile = nullptr;
thread_local const char* runtimeVoxelFaceContractAllFaces = nullptr;

const char* runtimeVoxelFaceContractEnvironment(const char* name) {
    // Atlas switches normally disable environment memo scopes. These direct
    // decoder checks must still select their own explicit test profiles.
    for(const char* diagnostic:{"CONCORD_VXL_DUMP_ATLAS","CONCORD_VXL_DUMP_SWEEPS",
        "CONCORD_VXL_DUMP_SWEEP_GROUP","CONCORD_VXL_DUMP_ATLAS_OBJECTS",
        "CONCORD_VXL_DUMP_OWNER_COLORS"})
        if(std::strcmp(name,diagnostic)==0) return nullptr;
    if (std::strcmp(name, "CONCORD_VXL_RENDER_PROFILE") == 0)
        return runtimeVoxelFaceContractProfile;
    if (std::strcmp(name, "CONCORD_VXL_DRAW_ALL_EXPOSED_FACES") == 0)
        return runtimeVoxelFaceContractAllFaces;
    if (std::strcmp(name, "CONCORD_VXL_SIDE_FACE_CULL") == 0) return "0";
    return runtimeRendererRawEnvironmentValue(name);
}

class RuntimeVoxelFaceContractEnvironmentScope {
public:
    RuntimeVoxelFaceContractEnvironmentScope(const char* profile, const char* allFaces)
        : previousProfile_(runtimeVoxelFaceContractProfile),
          previousFaces_(runtimeVoxelFaceContractAllFaces) {
        runtimeVoxelFaceContractProfile = profile;
        runtimeVoxelFaceContractAllFaces = allFaces;
        environment_.emplace(runtimeVoxelFaceContractEnvironment);
    }
    ~RuntimeVoxelFaceContractEnvironmentScope() {
        environment_.reset();
        runtimeVoxelFaceContractProfile = previousProfile_;
        runtimeVoxelFaceContractAllFaces = previousFaces_;
    }
private:
    const char* previousProfile_;
    const char* previousFaces_;
    std::optional<RuntimeRendererEnvironmentScope> environment_;
};

bool runtimeVoxelFaceContractDoubleEqual(double a, double b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool runtimeVoxelFaceContractImageEqual(
    const RuntimeShpFrameImage& a, const RuntimeShpFrameImage& b
) {
    // Every decoder output, including auxiliary coverage, scene-Z, materials,
    // diagnostics and still-empty derived image caches, is compared explicitly.
    if (a.offsetX != b.offsetX || a.offsetY != b.offsetY
        || a.globalWidth != b.globalWidth || a.globalHeight != b.globalHeight
        || a.width != b.width || a.height != b.height
        || a.pixels != b.pixels || a.rawColorIndices != b.rawColorIndices
        || a.ownerRemapMask != b.ownerRemapMask || a.lightLevels != b.lightLevels
        || a.normalIndices != b.normalIndices
        || a.normalBrightnessLevels != b.normalBrightnessLevels
        || a.vplSections != b.vplSections || a.depthLevels != b.depthLevels
        || a.pixelMaterials != b.pixelMaterials
        || a.sceneDepthBoundsValid != b.sceneDepthBoundsValid
        || a.sceneDepthBoundsTop != b.sceneDepthBoundsTop
        || a.sceneDepthBoundsBottom != b.sceneDepthBoundsBottom
        || a.paletteRemapStart != b.paletteRemapStart
        || a.paletteRemapEnd != b.paletteRemapEnd
        || a.paletteRemapRangeFromHeader != b.paletteRemapRangeFromHeader
        || a.fallbackOwnerRemapCount != b.fallbackOwnerRemapCount
        || a.zeroColorVoxelCount != b.zeroColorVoxelCount
        || a.rasterFunctionActuallyCalled != b.rasterFunctionActuallyCalled
        || a.rasterCacheKey != b.rasterCacheKey || a.rasterCacheHit != b.rasterCacheHit
        || a.vxlAlreadyVplResolved != b.vxlAlreadyVplResolved
        || a.lightLevelsApplied != b.lightLevelsApplied
        || a.remapLightApplied != b.remapLightApplied
        || a.opaqueBoundsWidth != b.opaqueBoundsWidth
        || a.opaqueBoundsHeight != b.opaqueBoundsHeight
        || a.topFacePixelCount != b.topFacePixelCount
        || a.sideFacePixelCount != b.sideFacePixelCount
        || a.lowerFacePixelCount != b.lowerFacePixelCount
        || a.pointSampleCount != b.pointSampleCount
        || a.faceSampleCount != b.faceSampleCount
        || a.exposedFaceCount != b.exposedFaceCount || a.sourcePath != b.sourcePath
        || a.rasterPathStats.pointSamplesWritten != b.rasterPathStats.pointSamplesWritten
        || a.rasterPathStats.cuboidFacesRasterized != b.rasterPathStats.cuboidFacesRasterized
        || a.rasterPathStats.indexedCoveragePixelsResolved != b.rasterPathStats.indexedCoveragePixelsResolved
        || a.rasterPathStats.supersampledPixelsResolved != b.rasterPathStats.supersampledPixelsResolved
        || a.rasterPathStats.triangleFacesRasterized != b.rasterPathStats.triangleFacesRasterized
        || a.rasterPathStats.coverageZOverwrites != b.rasterPathStats.coverageZOverwrites
        || a.packedColorTablesValid != b.packedColorTablesValid
        || a.packedColorPaletteSource != b.packedColorPaletteSource
        || a.packedColorRemap != b.packedColorRemap
        || a.packedColorBrightnessLift != b.packedColorBrightnessLift
        || !runtimeVoxelFaceContractDoubleEqual(a.packedColorGradeContrast,b.packedColorGradeContrast)
        || !runtimeVoxelFaceContractDoubleEqual(a.packedColorGradeSaturation,b.packedColorGradeSaturation)
        || !runtimeVoxelFaceContractDoubleEqual(a.packedColorGradeValueLift,b.packedColorGradeValueLift)
        || !runtimeVoxelFaceContractDoubleEqual(a.packedColorExtraUnitLight,b.packedColorExtraUnitLight)
        || a.packedColorRemapStart != b.packedColorRemapStart
        || a.packedColorRemapEnd != b.packedColorRemapEnd
        || a.packedColorOwnerRemap != b.packedColorOwnerRemap
        || a.packedColorGradeEnabled != b.packedColorGradeEnabled
        || a.packedColorVxlPaletteLookup != b.packedColorVxlPaletteLookup
        || a.packedBaseColors != b.packedBaseColors || a.packedRemapColors != b.packedRemapColors
        || a.spriteOpaqueBoundsValid != b.spriteOpaqueBoundsValid
        || a.spriteOpaqueBounds.left != b.spriteOpaqueBounds.left
        || a.spriteOpaqueBounds.top != b.spriteOpaqueBounds.top
        || a.spriteOpaqueBounds.right != b.spriteOpaqueBounds.right
        || a.spriteOpaqueBounds.bottom != b.spriteOpaqueBounds.bottom
        || a.spriteOpaqueSpansValid != b.spriteOpaqueSpansValid
        || a.spriteOpaqueSpans.size() != b.spriteOpaqueSpans.size()
        || a.shadowMaskSpanCache.hasNonzero() != b.shadowMaskSpanCache.hasNonzero()
        || a.shadowMaskSpanCache.usable() != b.shadowMaskSpanCache.usable()
        || a.shadowMaskSpanCache.rebuilds() != b.shadowMaskSpanCache.rebuilds()
        || a.shadowMaskSpanCache.spans().size() != b.shadowMaskSpanCache.spans().size()
        || a.infantryContactScanned != b.infantryContactScanned
        || a.infantryContactValid != b.infantryContactValid
        || a.infantryContact.x != b.infantryContact.x || a.infantryContact.y != b.infantryContact.y)
        return false;
    for (std::size_t i=0;i<a.spriteOpaqueSpans.size();++i) {
        const auto& x=a.spriteOpaqueSpans[i]; const auto& y=b.spriteOpaqueSpans[i];
        if (x.y!=y.y || x.left!=y.left || x.right!=y.right) return false;
    }
    for (std::size_t i=0;i<a.shadowMaskSpanCache.spans().size();++i) {
        const auto& x=a.shadowMaskSpanCache.spans()[i]; const auto& y=b.shadowMaskSpanCache.spans()[i];
        if (x.y!=y.y || x.left!=y.left || x.right!=y.right) return false;
    }
    return true;
}

struct RuntimeVoxelFaceContractResult {
    bool decoded=false;
    RuntimeShpFrameImage image;
    std::vector<unsigned char> shadow;
    double scale=-123.0, rangeX=-456.0, rangeY=-789.0;
    RuntimeVoxelModelBounds bounds;
    std::string error;
};

RuntimeVoxelFaceContractResult runtimeVoxelFaceContractDecode(
    bool reference, const std::vector<unsigned char>& bytes,
    const std::vector<RuntimeHvaTransform>& transforms, const std::string& source,
    int facing, const RuntimeVxlRasterSizing& sizing, const std::string& debug,
    const RuntimeMapRenderCache* cache, int shadowSection,
    const RuntimeVoxelBodyRotation* rotation
) {
    RuntimeVoxelFacePreparationReferenceScope scope(reference);
    RuntimeVoxelFaceContractResult result;
    // Failed decodes must retain exactly the same previous output as well.
    result.image.width=1; result.image.height=1; result.image.pixels={77};
    result.shadow={99};
    result.decoded=decodeRuntimeVxlSpriteFromBytes(bytes,transforms,source,facing,sizing,
        debug,cache,shadowSection,result.image,result.shadow,&result.scale,&result.rangeX,
        &result.rangeY,result.error,rotation,&result.bounds);
    return result;
}

bool runtimeVoxelFaceContractResultEqual(const RuntimeVoxelFaceContractResult& a,
    const RuntimeVoxelFaceContractResult& b) {
    if (a.decoded!=b.decoded || a.error!=b.error || a.shadow!=b.shadow
        || !runtimeVoxelFaceContractImageEqual(a.image,b.image)
        || !runtimeVoxelFaceContractDoubleEqual(a.scale,b.scale)
        || !runtimeVoxelFaceContractDoubleEqual(a.rangeX,b.rangeX)
        || !runtimeVoxelFaceContractDoubleEqual(a.rangeY,b.rangeY)
        || a.bounds.valid!=b.bounds.valid) return false;
    for (std::size_t i=0;i<3;++i)
        if (!runtimeVoxelFaceContractDoubleEqual(a.bounds.minimum[i],b.bounds.minimum[i])
            || !runtimeVoxelFaceContractDoubleEqual(a.bounds.maximum[i],b.bounds.maximum[i])) return false;
    return true;
}

// A genuine 3x3x3 VXL limb has one fully interior voxel. The hollow variant
// exposes six additional faces; index-zero interior and exterior voxels keep
// occupancy intact. Column spans are encoded independently of the renderer.
std::vector<unsigned char> runtimeVoxelFaceContractCube(bool hollow, bool empty=false) {
    constexpr std::size_t bodyStart=802+28, columns=9;
    std::vector<unsigned char> spans;
    std::array<std::uint32_t,columns> offsets{};
    const auto appendSpan=[&](int skip,int count,int x,int y,int firstZ) {
        spans.push_back(static_cast<unsigned char>(skip));
        spans.push_back(static_cast<unsigned char>(count));
        for(int i=0;i<count;++i) {
            const int z=firstZ+i;
            spans.push_back((x==1&&y==1&&z==1)||(x==0&&y==0&&z==0)
                ? 0 : static_cast<unsigned char>(16+(x+y+z)%16));
            spans.push_back(static_cast<unsigned char>((x*13+y*7+z*3)%36));
        }
        spans.push_back(static_cast<unsigned char>(count));
    };
    for(std::size_t column=0;column<columns;++column) {
        offsets[column]=static_cast<std::uint32_t>(spans.size());
        const int x=static_cast<int>(column%3),y=static_cast<int>(column/3);
        if(empty) appendSpan(0,0,x,y,0);
        else if(hollow&&x==1&&y==1) { appendSpan(0,1,x,y,0); appendSpan(1,1,x,y,2); }
        else appendSpan(0,3,x,y,0);
    }
    const std::size_t bodySize=columns*8+spans.size(),footer=bodyStart+bodySize;
    std::vector<unsigned char> bytes(footer+92,0);
    const auto put32=[&](std::size_t offset,std::uint32_t value) {
        for(unsigned int shift=0;shift<32;shift+=8) bytes[offset+shift/8]=static_cast<unsigned char>(value>>shift);
    };
    const auto putFloat=[&](std::size_t offset,float value) { std::memcpy(bytes.data()+offset,&value,4); };
    std::memcpy(bytes.data(),"Voxel Animation",15);
    put32(20,1); put32(24,1); put32(28,static_cast<std::uint32_t>(bodySize));
    bytes[32]=16;bytes[33]=31;std::memcpy(bytes.data()+802,"CUBE",4);
    for(std::size_t column=0;column<columns;++column) {
        put32(bodyStart+column*4,offsets[column]);
        const auto next=column+1==columns ? spans.size() : offsets[column+1];
        put32(bodyStart+columns*4+column*4,static_cast<std::uint32_t>(next-1));
    }
    std::copy(spans.begin(),spans.end(),bytes.begin()+bodyStart+columns*8);
    putFloat(footer+12,1.0F);
    for(int diagonal:{0,5,10}) putFloat(footer+16+diagonal*4,1.0F);
    for(int offset:{76,80,84}) putFloat(footer+offset,3.0F);
    bytes[footer+88]=3;bytes[footer+89]=3;bytes[footer+90]=3;bytes[footer+91]=2;
    return bytes;
}

std::array<RuntimeVoxelBodyRotation,5> runtimeVoxelFaceContractRotations() {
    std::array<RuntimeVoxelBodyRotation,5> rotations{};
    rotations[1].pitch=7;rotations[1].roll=-11;rotations[1].floorZ=-2.5;
    rotations[2].useQuaternion=true;
    const double angle=0.23;
    rotations[2].quaternion={std::sin(angle),0.0,0.0,std::cos(angle)};
    rotations[2].floorZ=1.25;
    rotations[3].waterClipZ=1.5;
    rotations[4].waterClipZ=1.0e6; // Complete clipping has its existing 1x1-success path.
    return rotations;
}
} // namespace

#include "render/voxel/diagnostics/RuntimeVoxelCornerProjectionDecoderContract.inl"

bool runRuntimeVoxelFacePreparationPrimitiveContractCheck(std::string& error) {
    if (!runRuntimeVoxelCornerProjectionDecoderContractCheck(error)) return false;
    const auto fail=[&](const std::string& detail) { error="voxel face preparation primitive: "+detail;return false; };
    const auto rotations=runtimeVoxelFaceContractRotations();
    RuntimeVxlRasterSizing sizing;sizing.forcedFitScale=0.75;
    std::uint64_t cases=0;
    for(const char* profile:{"reference","current"}) for(const char* allFaces:{"0","1"}) {
        RuntimeVoxelFaceContractEnvironmentScope settings(profile,allFaces);
        for(bool hollow:{false,true}) {
            const auto bytes=runtimeVoxelFaceContractCube(hollow);
            std::vector<RuntimeHvaTransform> transforms(1);transforms[0].valid=true;
            for(int facing=0;facing<32;++facing) {
                const auto original=runtimeVoxelFaceContractDecode(true,bytes,transforms,"cube",facing,sizing,"",nullptr,0,nullptr);
                const auto optimized=runtimeVoxelFaceContractDecode(false,bytes,transforms,"cube",facing,sizing,"",nullptr,0,nullptr);
                if(!original.decoded||!runtimeVoxelFaceContractResultEqual(original,optimized))
                    return fail("complete cube output facing="+std::to_string(facing)+" profile="+profile);
                if(std::strcmp(profile,"reference")==0&&std::strcmp(allFaces,"1")==0
                    && (original.image.faceSampleCount!=(hollow?60:54)
                        ||original.image.zeroColorVoxelCount!=(hollow?1:2)))
                    return fail("independent solid/hollow/zero-color occupancy fixture");
                ++cases;
            }
            for(int facing:{0,7,16,31}) for(std::size_t pose=1;pose<rotations.size();++pose) {
                transforms[0].m[1]=0.125;transforms[0].m[6]=-0.25;transforms[0].m[3]=0.375;
                const auto original=runtimeVoxelFaceContractDecode(true,bytes,transforms,"cube-pose",facing,sizing,"",nullptr,0,&rotations[pose]);
                const auto optimized=runtimeVoxelFaceContractDecode(false,bytes,transforms,"cube-pose",facing,sizing,"",nullptr,0,&rotations[pose]);
                if(!original.decoded||!runtimeVoxelFaceContractResultEqual(original,optimized)) return fail("tilt/quaternion/water complete output");
                if(pose==4&&(original.image.width!=1||original.image.height!=1||original.image.pixels!=std::vector<unsigned char>{0}))
                    return fail("complete water clipping branch not exercised");
                ++cases;
            }
        }
    }
    std::vector<RuntimeHvaTransform> transforms(1);transforms[0].valid=true;
    {
        RuntimeVoxelFaceContractEnvironmentScope settings("current","0");
        for(int matrixCase=0;matrixCase<3;++matrixCase) {
            auto bytes=runtimeVoxelFaceContractCube(false);
            std::vector<RuntimeHvaTransform> matrices;
            if(matrixCase==1) {
                matrices.resize(1); // Invalid HVA falls back to the authored footer matrix.
            } else if(matrixCase==2) {
                const float invalid=std::numeric_limits<float>::quiet_NaN();
                std::memcpy(bytes.data()+bytes.size()-92+16,&invalid,sizeof(invalid));
                // Invalid footer plus absent HVA exercises the null-matrix path.
            }
            for(int facing:{0,7,16,31}) {
                const auto original=runtimeVoxelFaceContractDecode(true,bytes,matrices,"cube-matrix",facing,sizing,"",nullptr,0,nullptr);
                const auto optimized=runtimeVoxelFaceContractDecode(false,bytes,matrices,"cube-matrix",facing,sizing,"",nullptr,0,nullptr);
                if(!original.decoded||!runtimeVoxelFaceContractResultEqual(original,optimized)) return fail("footer/invalid-HVA/null matrix fallback");
                ++cases;
            }
        }
        const auto bytes=runtimeVoxelFaceContractCube(false);
        for(const char* debug:{"flat","depth","normal","remap","raw"}) {
            const auto original=runtimeVoxelFaceContractDecode(true,bytes,transforms,"cube-debug",7,sizing,debug,nullptr,0,nullptr);
            const auto optimized=runtimeVoxelFaceContractDecode(false,bytes,transforms,"cube-debug",7,sizing,debug,nullptr,0,nullptr);
            if(!original.decoded||!runtimeVoxelFaceContractResultEqual(original,optimized)) return fail(std::string("debug complete output ")+debug);
            ++cases;
        }
    }
    for(int invalid=0;invalid<4;++invalid) {
        auto bytes=runtimeVoxelFaceContractCube(false,invalid==0);
        if(invalid==1) bytes.resize(20);
        if(invalid==2) bytes[0]='X';
        if(invalid==3) for(std::size_t i=0;i<4;++i) bytes[830+i]=static_cast<unsigned char>(0xfffffffeU>>(i*8));
        const auto original=runtimeVoxelFaceContractDecode(true,bytes,transforms,"invalid-cube",8,sizing,"",nullptr,0,nullptr);
        const auto optimized=runtimeVoxelFaceContractDecode(false,bytes,transforms,"invalid-cube",8,sizing,"",nullptr,0,nullptr);
        if(original.decoded||original.error.empty()||!runtimeVoxelFaceContractResultEqual(original,optimized)) return fail("invalid file error/previous output");
        ++cases;
    }
    const int previous=runtimeVoxelFacePreparationReferenceOverride;
    {
        RuntimeVoxelFacePreparationReferenceScope outer(true);
        if(!runtimeVoxelFacePreparationReferenceForDecode()) return fail("reference override not active");
        {
            RuntimeVoxelFacePreparationReferenceScope inner(false);
            if(runtimeVoxelFacePreparationReferenceForDecode()) return fail("nested optimized override not active");
        }
        if(!runtimeVoxelFacePreparationReferenceForDecode()) return fail("nested reference override not restored");
    }
    if(runtimeVoxelFacePreparationReferenceOverride!=previous) return fail("caller reference override not restored");
    std::cout<<"Voxel face preparation primitive: exact complete decoder cases="<<cases<<" passed\n";
    return true;
}

bool runRuntimeVoxelFacePreparationAssetContractCheck(const RuntimeMapRenderContext& context,
    std::string& error) {
    error.clear();
    if(!runRuntimeVoxelFacePreparationPrimitiveContractCheck(error)) return false;
    if(!runRuntimeGroundShadowEligibilityAssetContractCheck(context, error)) return false;
    if(!runRuntimeTerrainViewportFillContractCheck(context, error)) return false;
    if(!runRuntimeGroundVehicleRepairRecordsAssetContractCheck(context, error)) return false;
    if(!runRuntimePaintSpecialObjectIdMemoContractCheck(context, error)) return false;
    const auto fail=[&](const std::string& detail) {error="voxel face preparation assets: "+detail;return false;};
    RuntimeMapRenderCache cache;
    const auto rotations=runtimeVoxelFaceContractRotations();
    std::uint64_t cases=0,shadowCases=0,animatedCases=0;
    for(const char* imageId:{"GTNK","GTNKTUR","GTNKBARL","SCHP","YAGGUN"}) {
        RuntimeVoxelSourceCacheEntry& source=loadRuntimeVoxelSourceFromCache(imageId,context,cache,false);
        if(!source.loaded||source.vxlBytes.empty()||source.hvaBytes.empty()) return fail(std::string("real VXL/HVA load ")+imageId+": "+source.errorMessage);
        RuntimeVxlRasterSizing sizing=runtimeVxlRasterSizingForOwner(imageId);
        {
            RuntimeVoxelFacePreparationReferenceScope original(true);
            auto* first=loadRuntimeVoxelImageFromCache(imageId,imageId,0,context,cache);
            if(!first||!first->hasSprite||first->rasterFitScale<=0) return fail(std::string("real stock sizing ")+imageId);
            sizing.forcedFitScale=first->rasterFitScale;
        }
        std::vector<int> frames{0};
        if(source.metadata.hvaFrameCount>1) {
            frames.push_back(1);
            if(source.metadata.hvaFrameCount>2) frames.push_back(source.metadata.hvaFrameCount-1);
        }
        for(int frame:frames) {
            std::string hvaError;
            const auto* transforms=runtimeVoxelFrameTransforms(source,frame,hvaError);
            if(!transforms) return fail(std::string("real HVA frame ")+imageId+": "+hvaError);
            for(int facing=0;facing<32;++facing) {
                const auto original=runtimeVoxelFaceContractDecode(true,source.vxlBytes,*transforms,source.metadata.vxlPath,facing,sizing,runtimeVxlDebugMode(),&cache,0,nullptr);
                const auto optimized=runtimeVoxelFaceContractDecode(false,source.vxlBytes,*transforms,source.metadata.vxlPath,facing,sizing,runtimeVxlDebugMode(),&cache,0,nullptr);
                if(!original.decoded||original.image.pixels.empty()||!original.bounds.valid
                    ||!runtimeVoxelFaceContractResultEqual(original,optimized))
                    return fail(std::string(imageId)+" frame="+std::to_string(frame)+" facing="+std::to_string(facing)+" error="+original.error);
                ++cases;if(frame>0) ++animatedCases;
                if(std::any_of(original.shadow.begin(),original.shadow.end(),[](unsigned char p){return p!=0;})) ++shadowCases;
            }
        }
        for(int facing:{0,7,16,31}) for(std::size_t pose=1;pose<rotations.size();++pose) {
            const int shadowSection=source.metadata.vxlSectionCount>2?2:0;
            const auto original=runtimeVoxelFaceContractDecode(true,source.vxlBytes,source.firstFrameTransforms,source.metadata.vxlPath,facing,sizing,runtimeVxlDebugMode(),&cache,shadowSection,&rotations[pose]);
            const auto optimized=runtimeVoxelFaceContractDecode(false,source.vxlBytes,source.firstFrameTransforms,source.metadata.vxlPath,facing,sizing,runtimeVxlDebugMode(),&cache,shadowSection,&rotations[pose]);
            if(!original.decoded||!runtimeVoxelFaceContractResultEqual(original,optimized))
                return fail(std::string(imageId)+" pose="+std::to_string(pose)+" facing="+std::to_string(facing)+" error="+original.error);
            if(pose==4&&(original.image.width!=1||original.image.height!=1||original.image.pixels!=std::vector<unsigned char>{0}))
                return fail(std::string(imageId)+" stock complete water clip not exercised");
            ++cases;
        }
    }
    if(shadowCases==0||animatedCases==0) return fail("real shadow/animated HVA coverage absent");
    std::cout<<"Voxel face preparation assets: exact VXL/HVA/facing/tilt/water complete decoder cases="
        <<cases<<" shadows="<<shadowCases<<" animated="<<animatedCases<<" passed\n";
    return true;
}
