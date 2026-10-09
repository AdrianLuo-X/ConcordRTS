namespace {
std::vector<unsigned char> runtimeVoxelCornerContractWideSparseSource() {
    // A valid native 128x1x128 section requires >1MiB for the two vertex
    // planes, but its authored line has only128 occupied voxels. This exercises
    // the real production admission fallback without a large decoder workload.
    constexpr std::size_t bodyStart = 830, columns = 128, spanSize = 8;
    constexpr std::size_t bodySize = columns * 8 + columns * spanSize;
    constexpr std::size_t footer = bodyStart + bodySize;
    std::vector<unsigned char> bytes(footer + 92, 0);
    const auto put32 = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned shift=0; shift<32; shift+=8)
            bytes[offset+shift/8] = static_cast<unsigned char>(value>>shift);
    };
    const auto putFloat = [&](std::size_t offset, float value) {
        std::memcpy(bytes.data()+offset, &value, sizeof(value));
    };
    std::memcpy(bytes.data(), "Voxel Animation", 15);
    put32(20,1); put32(24,1); put32(28,static_cast<std::uint32_t>(bodySize));
    bytes[32]=16; bytes[33]=31; std::memcpy(bytes.data()+802,"WIDE",4);
    for (std::size_t column=0; column<columns; ++column) {
        const auto offset=static_cast<std::uint32_t>(column*spanSize);
        put32(bodyStart+column*4,offset);
        put32(bodyStart+columns*4+column*4,offset+spanSize-1);
        const std::size_t data=bodyStart+columns*8+column*spanSize;
        bytes[data]=0; bytes[data+1]=1;
        bytes[data+2]=static_cast<unsigned char>(16+column%16);
        bytes[data+3]=static_cast<unsigned char>(column%36);
        bytes[data+4]=1;
        // Skip the remaining127 z entries and terminate at the section edge.
        bytes[data+5]=127; bytes[data+6]=0; bytes[data+7]=0;
    }
    putFloat(footer+12,1.0F);
    for (int diagonal:{0,5,10}) putFloat(footer+16+diagonal*4,1.0F);
    putFloat(footer+76,128.0F); putFloat(footer+80,1.0F); putFloat(footer+84,128.0F);
    bytes[footer+88]=128; bytes[footer+89]=1; bytes[footer+90]=128; bytes[footer+91]=2;
    return bytes;
}

bool runRuntimeVoxelCornerProjectionDecoderContractCheck(std::string& error) {
    const auto fail=[&](const char* detail) {
        error=std::string("voxel corner projection complete decoder: ")+detail; return false;
    };
    RuntimeVoxelFaceContractEnvironmentScope settings("reference","1");
    std::vector<RuntimeHvaTransform> transforms(1); transforms[0].valid=true;
    transforms[0].m[1]=0.125; transforms[0].m[6]=-0.25; transforms[0].m[3]=0.375;
    RuntimeVxlRasterSizing sizing; sizing.forcedFitScale=0.25;
    std::size_t cases=0;
    for (bool wide:{false,true}) {
        const auto bytes=wide?runtimeVoxelCornerContractWideSparseSource():runtimeVoxelFaceContractCube(false);
        const auto original=runtimeVoxelFaceContractDecode(true,bytes,transforms,"corner-contract",7,
            sizing,"",nullptr,0,nullptr);
        if (!original.decoded) return fail("valid native source did not decode");
        for (int mode=0; mode<3; ++mode) {
            RuntimeVoxelCornerProjectionReferenceScope cornerReference(mode==1);
            RuntimeVoxelCornerProjectionObservation observation;
            RuntimeVoxelFaceContractResult actual;
            {
                RuntimeVoxelCornerProjectionObservationScope observed(observation,
                    mode==2?0:RuntimeVoxelCornerProjectionMemo::maximumScratchBytes);
                actual=runtimeVoxelFaceContractDecode(false,bytes,transforms,"corner-contract",7,
                    sizing,"",nullptr,0,nullptr);
            }
            if (!runtimeVoxelFaceContractResultEqual(original,actual))
                return fail("full pixels/masks/material/Z/shadow/fit/bounds/error differ");
            if (observation.requests==0 || observation.evaluations==0
                || observation.maximumScratchBytes>RuntimeVoxelCornerProjectionMemo::maximumScratchBytes)
                return fail("real production path or strict scratch cap was not exercised");
            if (!wide && mode==0) {
                if (observation.admittedSections!=1 || observation.fallbackSections!=0
                    || observation.evaluations>=observation.requests || observation.maximumScratchBytes==0)
                    return fail("actual small decoder did not reuse a projected corner");
            } else if (observation.admittedSections!=0 || observation.fallbackSections!=1
                || observation.evaluations!=observation.requests || observation.maximumScratchBytes!=0)
                return fail("actual capacity/reference fallback skipped original projection");
            ++cases;
        }
    }
    const auto previousObservation=runtimeVoxelCornerProjectionObservation;
    const auto previousLimit=runtimeVoxelCornerProjectionByteLimit;
    const auto previousReference=runtimeVoxelCornerProjectionReferenceOverride;
    {
        RuntimeVoxelCornerProjectionReferenceScope outer(true);
        if (!runtimeVoxelCornerProjectionReferenceForDecode()) return fail("outer reference override");
        {
            RuntimeVoxelCornerProjectionReferenceScope inner(false);
            if (runtimeVoxelCornerProjectionReferenceForDecode()) return fail("inner optimized override");
        }
        if (!runtimeVoxelCornerProjectionReferenceForDecode()) return fail("nested override restore");
    }
    if (runtimeVoxelCornerProjectionReferenceOverride!=previousReference
        || runtimeVoxelCornerProjectionObservation!=previousObservation
        || runtimeVoxelCornerProjectionByteLimit!=previousLimit)
        return fail("caller scope state not restored");
    std::cout<<"Voxel corner projection: complete decoder cases="<<cases
        <<" actualMemoHits=yes actualCapFallback=yes exactOutputs=pass\n";
    return true;
}
} // namespace
