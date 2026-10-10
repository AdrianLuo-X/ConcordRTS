// Issue 23/8: untouched original maps, real GDI frames, cache and camera continuity.
// These checks establish render consistency and preserve authored map semantics.
// Naturalness remains a review of the actual before/after BMPs, not a coverage score.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
namespace RuntimeTerrainSurfaceContinuityDetail {
std::uint64_t hashBytes(const void* data,std::size_t size) {
    std::uint64_t value=14695981039346656037ULL;
    const auto* bytes=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<size;++i){value^=bytes[i];value*=1099511628211ULL;}
    return value;
}
std::string csv(const std::string& text) {
    std::string result="\"";
    for(char c:text){if(c=='\"')result+='\"';result+=c;}
    return result+'\"';
}
std::uint64_t terrainSignature(const GameWorld& world) {
    std::ostringstream stream;
    for(const auto& c:world.getTerrainCells())stream<<c.x<<','<<c.y<<','<<c.cellIndex512<<','<<c.tileIndex<<','<<c.subTileIndex
        <<','<<c.level<<','<<c.iceGrowth<<','<<c.hasOverlay<<','<<c.overlayIndex<<','<<c.overlayFrame<<','
        <<c.overlayType.size()<<':'<<c.overlayType<<','<<c.overlayImage.size()<<':'<<c.overlayImage
        <<','<<c.overlayIsOre<<','<<c.overlayIsGem<<'\n';
    const auto text=stream.str();return hashBytes(text.data(),text.size());
}
struct TerrainSurfaceFrame {
    std::vector<std::uint32_t> pixels;
    std::vector<std::int32_t> terrainOwners,surfaceOwners;
};
bool captureTerrainSurface(const GameWorld& world,const RuntimeMapRenderCache& cache,const RuntimeMapCamera& camera,
    const RECT& viewport,int width,int height,TerrainSurfaceFrame& result,std::string& error) {
    const auto& layer=cache.terrainLayer;
    if(!layer.built || layer.width<=0 || layer.height<=0){error="actual terrain layer was not built";return false;}
    const auto layerSize=static_cast<std::size_t>(layer.width)*static_cast<std::size_t>(layer.height);
    if(layer.pixels.size()!=layerSize || layer.terrainCellIndices.size()!=layerSize
        || layer.surfaceCellIndices.size()!=layerSize){error="actual terrain layer pixel/ownership dimensions disagree";return false;}
    const auto frameSize=static_cast<std::size_t>(width)*height;
    result.pixels.resize(frameSize);result.terrainOwners.resize(frameSize);result.surfaceOwners.resize(frameSize);
    std::unordered_set<int> originalOwners;
    for(const auto& cell:world.getTerrainCells()){
        const int index=cell.cellIndex512>=0?cell.cellIndex512:(cell.y*512)+cell.x;
        if(index<0 || index>=512*512 || !originalOwners.insert(index).second){error="invalid or duplicate original terrain owner";return false;}
    }
    const auto validOwner=[&](std::int32_t owner){return owner==-1 || originalOwners.contains(owner);};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){
        // This diagnostic maps each actual screen pixel independently. Retained
        // layer origin and dimensions may change on the pan or cache rebuild.
        const int sx=x-viewport.left+camera.worldOffsetX-layer.originX;
        const int sy=y-viewport.top+camera.worldOffsetY-layer.originY;
        if(sx<0 || sy<0 || sx>=layer.width || sy>=layer.height){
            error="actual terrain layer does not cover screen "+std::to_string(x)+","+std::to_string(y)
                +" source="+std::to_string(sx)+","+std::to_string(sy)+" layer="+std::to_string(layer.width)+","+std::to_string(layer.height);
            return false;
        }
        const auto source=static_cast<std::size_t>(sy)*layer.width+sx,screen=static_cast<std::size_t>(y)*width+x;
        const auto terrainOwner=layer.terrainCellIndices[source],surfaceOwner=layer.surfaceCellIndices[source];
        if(!validOwner(terrainOwner) || !validOwner(surfaceOwner)){
            error="actual terrain ownership has no original map cell at screen "+std::to_string(x)+","+std::to_string(y)
                +" terrain="+std::to_string(terrainOwner)+" surface="+std::to_string(surfaceOwner);return false;
        }
        result.pixels[screen]=layer.pixels[source];result.terrainOwners[screen]=terrainOwner;result.surfaceOwners[screen]=surfaceOwner;
    }
    return true;
}
RuntimeMapRenderContext visualEvidenceContext(const RuntimeWindowState& state) {
    RuntimeMapRenderContext context;context.theater=&state.theater;context.art=&state.art;context.rules=&state.rules;
    context.mapFacts=state.mapFactsLoaded?&state.mapFacts:nullptr;context.animationTick=state.tick;context.fixedTickHz=state.config.fixedTickHz;
    context.tileRootPath=runtimeTileRootPath(state.config,state.report.map);context.assetRootPath=runtimeAssetRootPath(state.config);
    context.structureRootPath=runtimeStructureRootPath(state.config,state.report.map);context.palettePath=runtimePalettePath(state.config,state.report.map);
    return context;
}
std::uint32_t evidenceColor(const RuntimePaletteColor& color) {
    return (static_cast<std::uint32_t>(color.r)<<16)|(static_cast<std::uint32_t>(color.g)<<8)|color.b;
}
std::string canonicalEvidence(const TerrainCell& cell,const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache) {
    int gridWidth=0,gridHeight=0;RuntimePaletteColor left{},right{};
    const auto land=resolveRuntimeTerrainSubTileLandType(cell,context,cache);
    const auto ramp=resolveRuntimeTerrainSubTileRampType(cell,context,cache);
    const bool grid=resolveRuntimeTerrainTileGridShape(cell,context,cache,gridWidth,gridHeight);
    const bool radar=resolveRuntimeTerrainRadarColors(cell,context,cache,left,right);
    std::ostringstream value;value<<static_cast<int>(land)<<':'<<static_cast<int>(ramp)<<':'<<grid<<':'<<gridWidth<<':'<<gridHeight
        <<':'<<radar<<':'<<evidenceColor(left)<<':'<<evidenceColor(right);return value.str();
}
struct VisualFamilyEvidence {
    std::set<std::string> resources;std::set<std::uint64_t> decodedHashes;
    std::size_t drawnCells=0,noncanonicalCells=0;std::uint32_t eligible=0,rejected=0;
};
bool recordColdVisualEvidence(RuntimeWindowState& state,const TerrainCell& center,const RuntimeMapCamera& camera,
    int width,int height,const RuntimeMapRenderContext& context,const std::unordered_map<int,std::string>& canonicalBefore,
    const std::string& theater,std::size_t roi,std::ofstream& output,std::map<std::string,VisualFamilyEvidence>& families,std::string& error) {
    const auto& layer=state.renderCache.terrainLayer;
    if(state.renderCache.palette.size()<256){error="actual cold palette unavailable";return false;}
    RuntimeMapCamera worldCamera=camera;worldCamera.worldOffsetX=0;worldCamera.worldOffsetY=0;
    for(const auto& cell:state.world.getTerrainCells())if(std::abs(cell.x-center.x)<=8 && std::abs(cell.y-center.y)<=8){
        const auto before=canonicalBefore.find(cell.cellIndex512);
        if(before==canonicalBefore.end()){error="missing isolated canonical metadata";return false;}
        RuntimeTerrainVisualCellInfo info;const auto* image=resolveRuntimeTerrainVisualCellImage(cell,context,state.renderCache,&info);
        const std::string after=canonicalEvidence(cell,context,state.renderCache);
        if(before->second!=after){error="visual query changed canonical land/ramp/grid/radar at "+std::to_string(cell.x)+","+std::to_string(cell.y);return false;}
        if(info.tileIndex!=(cell.tileIndex==65535?0:cell.tileIndex) || info.subTileIndex!=(cell.tileIndex==65535?0:cell.subTileIndex)
            || info.mappedClear!=(cell.tileIndex==65535)){error="visual descriptor changed original cell identity";return false;}
        if(!image && !info.authoredEmptySubTile){error="original ROI TMP failed to decode";return false;}
        bool indexed=false;
        if(image){
            const auto family=state.renderCache.terrainVisualFamiliesByTileIndex.find(info.tileIndex);
            if(family!=state.renderCache.terrainVisualFamiliesByTileIndex.end()){
                const auto& entry=family->second;
                indexed=entry.canonicalLocator==info.canonicalLocator && entry.canonicalSourceName==info.canonicalSourceName
                    && entry.variants.size()==info.eligibleVariantCount && entry.rejectedVariantCount==info.rejectedVariantCount
                    && entry.siblingOnly==info.siblingOnly;
                bool selected=info.selectedResourceName==info.canonicalResourceName && info.selectedLocator==info.canonicalLocator
                    && info.selectedSourceName==info.canonicalSourceName;
                for(const auto& variant:entry.variants)selected=selected || (variant.resourceName==info.selectedResourceName
                    && variant.locator==info.selectedLocator && variant.sourceName==info.selectedSourceName);
                indexed=indexed && selected && !info.canonicalSourceName.empty() && !info.selectedSourceName.empty();
            }
            if(!indexed || info.selectedVariantFallback){error="selected decoded image lacks actual family/source provenance";return false;}
        }
        const auto decoded=image?hashBytes(image->pixels.data(),image->pixels.size()):0;
        const auto base=image?hashBytes(image->basePixels.data(),image->basePixels.size()):0;
        const auto baseZ=image?hashBytes(image->baseZData.data(),image->baseZData.size()):0;
        const auto extraZ=image?hashBytes(image->extraZData.data(),image->extraZData.size()):0;
        const std::string stem=toLowerAsciiLocal(std::filesystem::path(info.canonicalResourceName).stem().string());
        const bool groundFamily=stem=="clear01" || stem=="ruff01";
        std::size_t owned=0,matched=0;std::uint64_t mask=14695981039346656037ULL;
        if(image)for(unsigned char pixel:image->pixels){mask^=static_cast<unsigned char>(pixel!=0);mask*=1099511628211ULL;}
        if(image && groundFamily && !cell.hasOverlay && !layer.bridgeOverlayCellIndexes.contains(cell.cellIndex512)){
            if(image->width<=0 || image->height<=0 || image->pixels.size()!=static_cast<std::size_t>(image->width)*image->height){error="decoded ground dimensions disagree";return false;}
            const auto projected=runtimeProjectCellXY(cell.x,cell.y,cell.level,worldCamera);
            const int startX=projected.x-layer.originX-camera.tileWidth/2+image->offsetX;
            const int startY=projected.y-layer.originY-camera.tileHeight/2+image->offsetY;
            for(int y=0;y<image->height;++y)for(int x=0;x<image->width;++x){
                const auto paletteIndex=image->pixels[static_cast<std::size_t>(y)*image->width+x];if(paletteIndex==0)continue;
                const int sx=startX+x,sy=startY+y,screenX=sx+layer.originX-camera.worldOffsetX,screenY=sy+layer.originY-camera.worldOffsetY;
                if(sx<0 || sy<0 || sx>=layer.width || sy>=layer.height || screenX<0 || screenY<0 || screenX>=width || screenY>=height)continue;
                const auto offset=static_cast<std::size_t>(sy)*layer.width+sx;
                if(layer.terrainCellIndices[offset]!=cell.cellIndex512 || layer.surfaceCellIndices[offset]!=cell.cellIndex512)continue;
                ++owned;matched+=layer.pixels[offset]==evidenceColor(state.renderCache.palette[paletteIndex]);
            }
            if(owned!=matched){error="selected decoded ground differs from actual cold terrain layer at "+std::to_string(cell.x)+","+std::to_string(cell.y);return false;}
            if(owned>=16){auto& family=families[theater+"|"+info.canonicalResourceName];++family.drawnCells;
                family.resources.insert(info.selectedResourceName);family.decodedHashes.insert(decoded);
                family.eligible=info.eligibleVariantCount;family.rejected=info.rejectedVariantCount;
                family.noncanonicalCells+=info.selectedResourceName!=info.canonicalResourceName;}
        }
        output<<theater<<','<<csv(state.config.defaultMapPath)<<','<<roi<<",cold,"<<cell.x<<','<<cell.y<<','<<cell.cellIndex512
            <<','<<cell.tileIndex<<','<<cell.subTileIndex<<','<<cell.level<<','<<info.tileIndex<<','<<info.subTileIndex<<','<<info.mappedClear
            <<','<<csv(info.canonicalResourceName)<<','<<csv(info.selectedResourceName)<<','<<csv(info.canonicalSourceName)<<','<<csv(info.selectedSourceName)
            <<','<<csv(info.canonicalLocator)<<','<<csv(info.selectedLocator)<<','<<info.selectionHash<<','<<info.eligibleVariantCount
            <<','<<info.rejectedVariantCount<<','<<info.siblingOnly<<','<<info.authoredEmptySubTile<<','<<info.selectedVariantFallback<<','<<indexed
            <<','<<(image?image->width:0)<<','<<(image?image->height:0)<<','<<decoded<<','<<base<<','<<mask<<','<<baseZ<<','<<extraZ
            <<','<<owned<<','<<matched<<','<<csv(before->second)<<','<<csv(after)<<','<<(owned>=16?"DRAWN":"METADATA_ONLY")<<'\n';
    }
    return true;
}
bool runOriginalTmpPrivateControls(RuntimeWindowState& state,const std::filesystem::path& output,std::string& error) {
    const auto worldBefore=state.world.deterministicSimulationStateHash(),terrainBefore=terrainSignature(state.world);
    const auto layerHashBefore=hashBytes(state.renderCache.terrainLayer.pixels.data(),state.renderCache.terrainLayer.pixels.size()*sizeof(std::uint32_t));
    const std::string layerKeyBefore=state.renderCache.terrainLayer.key;
    const auto stockContext=visualEvidenceContext(state);RuntimeMapRenderCache stockCache;
    std::vector<const TerrainCell*> clearCells;
    for(const auto& cell:state.world.getTerrainCells())if(cell.tileIndex!=65535 && cell.subTileIndex==0){
        const auto reference=state.theater.resolveTileIndex(cell.tileIndex);
        if(toLowerAsciiLocal(reference.expectedFileName)=="clear01.tem")clearCells.push_back(&cell);
    }
    if(clearCells.empty()){error="TEMPERATE has no authored clear01 cell for private original-TMP controls";return false;}
    RuntimeTerrainVisualCellInfo original;const auto* decoded=resolveRuntimeTerrainVisualCellImage(*clearCells.front(),stockContext,stockCache,&original);
    const auto family=stockCache.terrainVisualFamiliesByTileIndex.find(original.tileIndex);
    if(!decoded || family==stockCache.terrainVisualFamiliesByTileIndex.end()){error="original clear family unavailable";return false;}
    std::string letterSource;
    for(const auto& entry:family->second.variants)if(toLowerAsciiLocal(entry.resourceName)=="clear01a.tem")letterSource=entry.sourceName;
    const auto separator=original.canonicalSourceName.find_last_of('!');
    if(separator==std::string::npos || letterSource.empty()){error="actual original MIX clear/a sources unavailable";return false;}
    const std::array<std::string,3> sources{{original.canonicalSourceName,letterSource,original.canonicalSourceName.substr(0,separator)+"!Slope01.tem"}};
    const auto sha=[](const std::vector<std::uint8_t>& bytes){
        static constexpr char digits[]="0123456789abcdef";std::string result;
        for(auto byte:runtimeMultiplayerSnapshotSha256(bytes)){result+=digits[byte>>4];result+=digits[byte&15];}return result;
    };
    std::array<std::vector<std::uint8_t>,3> payloads;std::array<std::string,3> payloadSha;
    for(std::size_t index=0;index<sources.size();++index){
        const auto split=sources[index].find_last_of('!');MixArchive archive;
        if(split==std::string::npos || !archive.loadFromFile(sources[index].substr(0,split))){error="actual original MIX source: "+sources[index]+" "+archive.getErrorMessage();return false;}
        const auto* entry=archive.findEntryByName(sources[index].substr(split+1));
        if(!entry || !archive.readEntryData(*entry,payloads[index],error) || payloads[index].empty()){error="actual original TMP payload: "+sources[index]+" "+error;return false;}
        payloadSha[index]=sha(payloads[index]);
    }
    std::ofstream files(output/"control-files.csv"),cells(output/"controls.csv");
    if(!files || !cells){error="private TMP control CSV unavailable";return false;}
    files<<"control,path,original_source,byte_count,original_sha256,reread_sha256,identical\n";
    cells<<"control,x,y,original_tile,original_subtile,original_level,canonical_source,selected_source,selected_resource,eligible,rejected,sibling_only,fallback,decoded_hash,selected_payload_sha256,selected_ramp,selected_width,selected_height,selected_extra,candidate_ramp,candidate_width,candidate_height,candidate_extra,candidate_base_z_hash,canonical_metadata_before,canonical_metadata_after,world_hash,terrain_hash,result\n";
    std::array<const TerrainCell*,2> chosen{{nullptr,nullptr}};std::uint64_t originalDecoded=0,letterDecoded=0;RuntimeTmpSubTileImage canonicalImage;
    const auto sameFile=[](const std::string& source,const std::filesystem::path& expected){
        std::error_code ec;return source.find('!')==std::string::npos && std::filesystem::equivalent(std::filesystem::path(source),expected,ec) && !ec;
    };
    std::size_t queries=0;
    for(int control=0;control<2;++control){
        const char* name=control==0?"custom-compatible":"custom-rejected";
        const auto directory=output/"controls"/name;std::error_code ec;std::filesystem::create_directories(directory,ec);
        if(ec){error="private TMP control directory: "+ec.message();return false;}
        const std::array<std::filesystem::path,2> paths{{directory/"clear01.tem",directory/"clear01a.tem"}};
        for(std::size_t file=0;file<paths.size();++file){
            const std::size_t sourceIndex=file==0?0:(control==0?1:2);
            std::ofstream written(paths[file],std::ios::binary|std::ios::trunc);
            written.write(reinterpret_cast<const char*>(payloads[sourceIndex].data()),static_cast<std::streamsize>(payloads[sourceIndex].size()));
            written.close();if(!written){error="private TMP write/close: "+paths[file].string();return false;}
            std::ifstream read(paths[file],std::ios::binary);
            if(!read){error="private TMP reread: "+paths[file].string();return false;}
            std::vector<std::uint8_t> recovered((std::istreambuf_iterator<char>(read)),std::istreambuf_iterator<char>());
            if(read.bad() || recovered!=payloads[sourceIndex] || sha(recovered)!=payloadSha[sourceIndex]){error="copied original TMP bytes changed";return false;}
            read.close();files<<name<<','<<csv(paths[file].string())<<','<<csv(sources[sourceIndex])<<','<<recovered.size()
                <<','<<payloadSha[sourceIndex]<<','<<sha(recovered)<<",1\n";
        }
        auto context=stockContext;context.tileRootPath=directory.generic_string();RuntimeMapRenderCache cache;
        bool baseSeen=false,letterSeen=false;const std::size_t count=control==0?clearCells.size():chosen.size();
        for(std::size_t index=0;index<count;++index){
            const auto* cell=control==0?clearCells[index]:chosen[index];if(!cell){error="missing actual original-cell selection witness";return false;}
            const std::string before=canonicalEvidence(*cell,stockContext,stockCache);
            RuntimeTerrainVisualCellInfo info;const auto* image=resolveRuntimeTerrainVisualCellImage(*cell,context,cache,&info);
            const std::string after=canonicalEvidence(*cell,context,cache);
            if(!image || before!=after || !info.siblingOnly || info.selectedVariantFallback
                || info.eligibleVariantCount!=(control==0?1U:0U) || info.rejectedVariantCount!=(control==0?0U:1U)
                || !sameFile(info.canonicalSourceName,paths[0])){error="private original-TMP inventory/canonical authority changed";return false;}
            const bool isLetter=toLowerAsciiLocal(info.selectedResourceName)=="clear01a.tem";
            if((!isLetter && toLowerAsciiLocal(info.selectedResourceName)!="clear01.tem") || (control==1 && isLetter)
                || !sameFile(info.selectedSourceName,paths[isLetter?1:0])){error="private selection escaped explicit same-parent files";return false;}
            const auto decodedHash=hashBytes(image->pixels.data(),image->pixels.size());
            if(control==0){if(isLetter){letterSeen=true;chosen[1]=cell;letterDecoded=decodedHash;}
                else {baseSeen=true;chosen[0]=cell;originalDecoded=decodedHash;canonicalImage=*image;}}
            else if(decodedHash!=originalDecoded){error="rejected original slope replaced canonical clear pixels";return false;}
            const RuntimeTmpTileFile* candidateFile=nullptr;
            for(const auto& [locator,file]:cache.tmpTilesByPath)if(sameFile(locator,paths[1]))candidateFile=&file;
            if(!candidateFile || !candidateFile->loaded || candidateFile->images.empty() || !candidateFile->images.front().present){error="original candidate was not actually decoded";return false;}
            const auto& candidateImage=candidateFile->images.front();
            ++queries;cells<<name<<','<<cell->x<<','<<cell->y<<','<<cell->tileIndex<<','<<cell->subTileIndex<<','<<cell->level
                <<','<<csv(info.canonicalSourceName)<<','<<csv(info.selectedSourceName)<<','<<csv(info.selectedResourceName)
                <<','<<info.eligibleVariantCount<<','<<info.rejectedVariantCount<<','<<info.siblingOnly<<','<<info.selectedVariantFallback
                <<','<<decodedHash<<','<<payloadSha[isLetter?1:0]<<','<<static_cast<int>(image->rampType)<<','<<image->width<<','<<image->height<<','<<image->hasExtra
                <<','<<static_cast<int>(candidateImage.rampType)<<','<<candidateImage.width<<','<<candidateImage.height<<','<<candidateImage.hasExtra
                <<','<<hashBytes(candidateImage.baseZData.data(),candidateImage.baseZData.size())<<','<<csv(before)<<','<<csv(after)<<','<<worldBefore<<','<<terrainBefore<<",PASS\n";
            if(control==0 && baseSeen && letterSeen)break;
        }
        if(control==0 && (!baseSeen || !letterSeen || originalDecoded==letterDecoded)){error="real clear coordinates did not select two distinct original custom textures";return false;}
        if(control==1){
            const RuntimeTmpTileFile* rejected=nullptr;
            for(const auto& [locator,file]:cache.tmpTilesByPath)if(sameFile(locator,paths[1]))rejected=&file;
            if(!rejected || !rejected->loaded || rejected->images.empty() || !rejected->images.front().present){error="original slope rejection was not a decoded compatible-format TMP";return false;}
            const auto& slope=rejected->images.front();
            if(slope.rampType==canonicalImage.rampType && slope.width==canonicalImage.width && slope.height==canonicalImage.height
                && slope.hasExtra==canonicalImage.hasExtra && slope.baseZData==canonicalImage.baseZData){error="original slope lacked recorded geometry incompatibility";return false;}
        }
    }
    if(state.world.deterministicSimulationStateHash()!=worldBefore || terrainSignature(state.world)!=terrainBefore
        || state.renderCache.terrainLayer.key!=layerKeyBefore
        || hashBytes(state.renderCache.terrainLayer.pixels.data(),state.renderCache.terrainLayer.pixels.size()*sizeof(std::uint32_t))!=layerHashBefore){error="private TMP controls altered actual world/render layer";return false;}
    files.flush();cells.flush();if(!files.good() || !cells.good()){error="private TMP evidence write";return false;}
    std::cout<<"Terrain original TMP controls passed: cases=2 actualCopiedFiles=4 originalCellQueries="<<queries
        <<" originalMIXBytesAndSHA256=yes compatibleBaseAndA=yes originalSlopeRejected=yes canonicalMetadata=yes actualWorldAndDrawCacheUnchanged=yes\n";
    return true;
}
struct Roi {const TerrainCell* center=nullptr;std::string category;};
bool selectRois(const RuntimeWindowState& state,const std::string& theater,std::ofstream& distribution,
    std::array<Roi,2>& result,std::string& error) {
    const auto& cells=state.world.getTerrainCells();
    std::unordered_map<int,const TerrainCell*> byCell;
    std::unordered_map<int,TheaterTileReference> references;
    std::map<int,std::size_t> setCounts,levelCounts;
    std::unordered_set<int> objectCells;
    for(const auto& o:state.world.getObjects())if(o.alive)objectCells.insert(runtimeCellKey(o.x,o.y));
    std::size_t overlays=0;
    int minX=std::numeric_limits<int>::max(),minY=minX,maxX=-1,maxY=-1;
    for(const auto& c:cells){
        if(!byCell.emplace(runtimeCellKey(c.x,c.y),&c).second){error="duplicate original terrain cell";return false;}
        const int index=c.tileIndex==65535?0:c.tileIndex;
        if(!references.contains(index))references.emplace(index,state.theater.resolveTileIndex(index));
        ++setCounts[references.at(index).tileSetIndex];++levelCounts[c.level];overlays+=c.hasOverlay;
        minX=std::min(minX,c.x);maxX=std::max(maxX,c.x);minY=std::min(minY,c.y);maxY=std::max(maxY,c.y);
    }
    for(const auto& [set,count]:setCounts){std::string name;
        for(const auto& typed:state.theater.getTileSets())if(typed.index==set){name=typed.setName;break;}
        distribution<<theater<<",tileset,"<<set<<','<<csv(name)<<','<<count<<'\n';}
    for(const auto& [level,count]:levelCounts)distribution<<theater<<",level,"<<level<<",,"<<count<<'\n';
    distribution<<theater<<",overlay,-1,,"<<overlays<<'\n';
    const auto natural=[](const TheaterTileReference& reference){
        if(!reference.validTileSet)return false;
        const auto name=toLowerAsciiLocal(reference.tileSetName+" "+reference.baseFileName);
        for(const char* excluded:{"water","shore","road","cliff","bridge","ramp","slope","track","tunnel"})
            if(name.find(excluded)!=std::string::npos)return false;
        return true;
    };
    const double middleX=(minX+maxX)/2.0,middleY=(minY+maxY)/2.0;
    double bestFlat=-std::numeric_limits<double>::infinity(),bestTransition=bestFlat,bestSmallFlat=bestFlat;
    Roi smallFlat;
    struct Transition {Roi roi;double score;};std::vector<Transition> transitionCandidates;
    std::size_t interior=0,flat=0,smallFlatCount=0,transitions=0;
    for(const auto& c:cells){
        const auto& reference=references.at(c.tileIndex==65535?0:c.tileIndex);
        if(!reference.validTileSet)continue;
        std::unordered_set<int> nearbySets,nearbyLevels;
        int sameMembers=0,overlayMembers=0,nearbyObjects=0;bool full=true;
        for(int dy=-3;dy<=3 && full;++dy)for(int dx=-3;dx<=3;++dx){
            const int key=runtimeCellKey(c.x+dx,c.y+dy);const auto found=byCell.find(key);
            if(found==byCell.end()){full=false;break;}
            const auto& n=*found->second;const auto& r=references.at(n.tileIndex==65535?0:n.tileIndex);
            nearbySets.insert(r.tileSetIndex);nearbyLevels.insert(n.level);
            sameMembers+=r.tileSetIndex==reference.tileSetIndex;overlayMembers+=n.hasOverlay;nearbyObjects+=objectCells.contains(key);
        }
        const double distance=(c.x-middleX)*(c.x-middleX)+(c.y-middleY)*(c.y-middleY);
        const double proximity=-distance-nearbyObjects*100.0;
        const auto name=toLowerAsciiLocal(reference.tileSetName+" "+reference.baseFileName);
        std::string stem=toLowerAsciiLocal(reference.expectedFileName);
        if(const auto dot=stem.find_last_of('.');dot!=std::string::npos)stem.resize(dot);
        // Prefer original clear/rough bases with independently documented
        // letter alternatives. Names are actual theater data, not new terrain.
        const bool sampledFamily=stem=="clear01" || stem=="ruff01";
        const bool namedGround=name.find("grass")!=std::string::npos || name.find("rough")!=std::string::npos
            || name.find("sand")!=std::string::npos || name.find("pav")!=std::string::npos || name.find("lunar")!=std::string::npos;
        const double groundScore=(sampledFamily?10000000.0:0.0)+(namedGround?1000000.0:0.0)+proximity;
        if(natural(reference)){
            if(full && nearbyLevels.size()==1 && sameMembers>=25 && overlayMembers==0){
                ++flat;if(groundScore>bestFlat){bestFlat=groundScore;result[0]={&c,"original-flat-7x7-set"
                    +std::to_string(reference.tileSetIndex)+" "+reference.tileSetName+" same="+std::to_string(sameMembers)+"/49"};}
            }
            int smallMembers=0;bool smallValid=true;
            for(int dy=-1;dy<=1 && smallValid;++dy)for(int dx=-1;dx<=1;++dx){
                const auto found=byCell.find(runtimeCellKey(c.x+dx,c.y+dy));
                if(found==byCell.end()){smallValid=false;break;}
                const auto& n=*found->second;const auto& r=references.at(n.tileIndex==65535?0:n.tileIndex);
                if(n.level!=c.level || n.hasOverlay || !natural(r)){smallValid=false;break;}
                smallMembers+=r.tileSetIndex==reference.tileSetIndex;
            }
            if(smallValid && smallMembers>=5){++smallFlatCount;if(groundScore>bestSmallFlat){bestSmallFlat=groundScore;
                smallFlat={&c,"original-flat-3x3-fallback-set"+std::to_string(reference.tileSetIndex)+" "+reference.tileSetName
                    +" same="+std::to_string(smallMembers)+"/9"};}}
        }
        if(!full)continue;
        ++interior;
        if(nearbySets.size()<2 && nearbyLevels.size()<2 && overlayMembers==0)continue;
        ++transitions;
        int priority=1;std::string category="original-material-transition";
        if(nearbyLevels.size()>1){priority=3;category="original-height-transition";}
        if(name.find("road")!=std::string::npos){priority=4;category="original-road-transition";}
        if(name.find("ramp")!=std::string::npos || name.find("slope")!=std::string::npos || name.find("cliff")!=std::string::npos){
            priority=5;category="original-slope-cliff-transition";}
        if(name.find("shore")!=std::string::npos){priority=6;category="original-shore-transition";}
        if(name.find("bridge")!=std::string::npos){priority=7;category="original-bridge-transition";}
        if(nearbySets.size()==1 && nearbyLevels.size()==1){priority=0;category="original-overlay-neighborhood";}
        const double score=priority*1000000.0+nearbySets.size()*1000.0+nearbyLevels.size()*100.0+proximity;
        transitionCandidates.push_back({Roi{&c,category},score});
    }
    if(!result[0].center)result[0]=smallFlat;
    distribution<<theater<<",flat7x7,-1,\"same-level natural center; same material >=25/49; overlay0\","<<flat<<'\n';
    distribution<<theater<<",flat3x3-fallback,-1,\"all same-level natural ground; same material >=5/9; overlay0\","<<smallFlatCount<<'\n';
    for(const auto& candidate:transitionCandidates)if(candidate.roi.center!=result[0].center && candidate.score>bestTransition){
        bestTransition=candidate.score;result[1]=candidate.roi;}
    if(!result[0].center || !result[1].center || result[0].center==result[1].center){
        std::ostringstream why;why<<"two real ROIs unavailable: cells="<<cells.size()<<" tileSets="<<setCounts.size()
            <<" levels="<<levelCounts.size()<<" overlays="<<overlays<<" full7x7="<<interior<<" flat7x7="<<flat
            <<" flat3x3="<<smallFlatCount<<" transition="<<transitions<<" bounds="<<minX<<','<<minY<<".."<<maxX<<','<<maxY;
        error=why.str();return false;
    }
    return true;
}
}
bool runRuntimeTerrainSurfaceContinuitySmoke(const RuntimeConfig& config,std::string& error) {
    using namespace RuntimeTerrainSurfaceContinuityDetail;
    std::cout<<std::unitbuf;
    std::string scene="startup",stage="output";
    const auto fail=[&](const std::string& why){error="Terrain surface "+scene+" ["+stage+"]: "+why;return false;};
    const char* configured=std::getenv("CONCORD_TERRAIN_SURFACE_CONTINUITY_OUTPUT");
    const std::filesystem::path output=configured && *configured?std::filesystem::path(configured)
        :std::filesystem::path(config.localDataRootPath).parent_path()/"build/smoke/terrain-surface";
    std::error_code directoryError;std::filesystem::create_directories(output,directoryError);
    if(directoryError)return fail("evidence directory: "+directoryError.message());
    std::ofstream frames(output/"frames.csv"),original(output/"original_cells.csv"),distribution(output/"distribution.csv");
    if(!frames || !original || !distribution)return fail("evidence CSV unavailable");
    frames<<"theater,map,roi,category,center_x,center_y,center_level,pass,tick,tile_width,tile_height,offset_x,offset_y,pixel_hash,world_hash,terrain_signature,visible,textured,fallback,authored_empty,bmp,compared_pixels,terrain_surface_hash,layer_origin_x,layer_origin_y,layer_width,layer_height,pan_compare_left,pan_compare_top,pan_compare_right,pan_compare_bottom,pan_surface_compared_pixels,pan_surface_owned_pixels,pan_surface_owner_mismatches,pan_full_frame_compared_pixels,pan_full_frame_mismatches,pan_full_frame_mismatch_left,pan_full_frame_mismatch_top,pan_full_frame_mismatch_right,pan_full_frame_mismatch_bottom,result\n";
    original<<"theater,roi,x,y,cell_index512,tile,subtile,level,ice_growth,has_overlay,overlay_index,overlay_frame,overlay_type,overlay_image,ore,gem,tileset,tileset_name,tmp\n";
    distribution<<"theater,kind,id,name,count\n";
    std::ofstream visualCells(output/"actual_visual_cells.csv"),visualFamilies(output/"actual_visual_families.csv");
    if(!visualCells || !visualFamilies)return fail("actual visual evidence CSV unavailable");
    visualCells<<"theater,map,roi,pass,x,y,cell_index512,original_tile,original_subtile,original_level,normalized_tile,normalized_subtile,mapped_clear,canonical_resource,selected_resource,canonical_source,selected_source,canonical_locator,selected_locator,selection_hash,eligible_variants,rejected_variants,sibling_only,authored_empty,selected_fallback,source_indexed,width,height,decoded_pixels_hash,decoded_base_hash,decoded_mask_hash,decoded_base_z_hash,decoded_extra_z_hash,layer_owned_visible_pixels,layer_matched_pixels,canonical_metadata_before,canonical_metadata_after,evidence_kind\n";
    visualFamilies<<"family,drawn_cells,eligible,rejected,selected_resources,decoded_hashes,verified_noncanonical_cells,multiple_variants\n";
    std::map<std::string,VisualFamilyEvidence> visualFamilyEvidence;
    struct Map {const char* theater;const char* relative;};
    const std::array<Map,6> maps{{{"TEMPERATE","multimd/2peaks.map"},{"SNOW","multimd/xxmas.map"},
        {"URBAN","multimd/hillbtwn.map"},{"NEWURBAN","mapsmd03/all01umd.map"},
        {"DESERT","multimd/dryheat.map"},{"LUNAR","mapsmd03/sov06lmd.map"}}};
    constexpr int width=960,height=640;
    // No sidebar/chat/tooltip/interaction overlay is drawn by these arguments.
    // Compare only the map interior: exclude top HUD band and 32px perimeter.
    const RECT panCompare{32,64,width-32,height-32};
    std::size_t rois=0,draws=0,bitmapCount=0,exactComparisons=0,panComparisons=0;
    for(const auto& map:maps){
        scene=map.theater;stage="original startup";auto storage=std::make_unique<RuntimeWindowState>();auto& state=*storage;
        state.config=config;state.config.defaultMapPath=(std::filesystem::path(config.localDataRootPath)/"resources/mix_exports"/map.relative).string();
        state.runtimePreferencePersistenceEnabled=false;state.runtimeSuppressPlayerAudioFeedback=true;state.runtimeSoundsEnabled=false;
        state.report=buildRuntimeStartupReport(state.config,&state.world,&state.theater,&state.art,&state.rules,&state.mapFacts);
        if(!state.report.success)return fail(state.config.defaultMapPath+": "+state.report.errorMessage);
        state.rulesLoaded=state.report.rulesLoaded;state.artLoaded=state.report.artLoaded;state.theaterLoaded=state.report.theaterLoaded;
        state.mapFactsLoaded=!state.mapFacts.cells.empty();state.visionEnabled=false;
        state.lighting.reset(state.mapFacts.lighting);state.lightingInitialized=true;
        state.ownerRemapColors=loadRuntimeMapOwnerRemapColors(state.config,&state.neutralRemapOwners);
        if(!state.rulesLoaded || !state.artLoaded || !state.theaterLoaded || state.report.map.theater!=map.theater)
            return fail("required original assets/theater mismatch: actual="+state.report.map.theater);
        const auto worldHash=state.world.deterministicSimulationStateHash(),terrainHash=terrainSignature(state.world);
        std::array<Roi,2> selected{};stage="real ROI selection";std::string selectionError;
        if(!selectRois(state,map.theater,distribution,selected,selectionError))return fail(state.config.defaultMapPath+": "+selectionError);
        for(std::size_t roi=0;roi<selected.size();++roi){
            scene=std::string(map.theater)+"/"+std::to_string(roi);const auto& center=*selected[roi].center;
            const RuntimeCellPoint cell{center.x,center.y};
            for(const auto& c:state.world.getTerrainCells())if(std::abs(c.x-center.x)<=8 && std::abs(c.y-center.y)<=8){
                const auto reference=state.theater.resolveTileIndex(c.tileIndex==65535?0:c.tileIndex);
                original<<map.theater<<','<<roi<<','<<c.x<<','<<c.y<<','<<c.cellIndex512<<','<<c.tileIndex<<','<<c.subTileIndex
                    <<','<<c.level<<','<<c.iceGrowth<<','<<c.hasOverlay<<','<<c.overlayIndex<<','<<c.overlayFrame<<','<<csv(c.overlayType)
                    <<','<<csv(c.overlayImage)<<','<<c.overlayIsOre<<','<<c.overlayIsGem<<','<<reference.tileSetIndex
                    <<','<<csv(reference.tileSetName)<<','<<csv(reference.expectedFileName)<<'\n';
            }
            RuntimeMapCamera baseCamera;baseCamera.initialized=true;
            const auto projected=runtimeProjectCellXY(center.x,center.y,center.level,baseCamera);
            baseCamera.worldOffsetX=projected.x-width/2;baseCamera.worldOffsetY=projected.y-height/2;
            state.renderCache=RuntimeMapRenderCache{};
            const auto visualContext=visualEvidenceContext(state);RuntimeMapRenderCache canonicalAuditCache;
            std::unordered_map<int,std::string> canonicalBefore;
            for(const auto& authored:state.world.getTerrainCells())if(std::abs(authored.x-center.x)<=8 && std::abs(authored.y-center.y)<=8)
                canonicalBefore.emplace(authored.cellIndex512,canonicalEvidence(authored,visualContext,canonicalAuditCache));
            std::vector<std::uint32_t> cold;TerrainSurfaceFrame coldSurface;
            for(int pass=0;pass<5;++pass){
                static constexpr const char* names[]{"cold","warm","rebuilt","pan","returned"};stage=names[pass];
                if(pass==2)state.renderCache=RuntimeMapRenderCache{};
                auto camera=baseCamera;if(pass==3){++camera.worldOffsetX;++camera.worldOffsetY;}
                const bool saveBmp=pass==0 || pass==3;
                const std::string bmp=saveBmp?std::string(map.theater)+"-roi"+std::to_string(roi)+"-"+names[pass]+".bmp":"";
                std::vector<std::uint32_t> pixels;std::string drawError;
                const auto stats=drawRuntimeStateOffscreenAtCell(state,cell,width,height,drawError,
                    saveBmp?(output/bmp).string():"",false,false,-1,&pixels,false,&camera,false,false);
                if(!stats || !stats->rendered || pixels.size()!=static_cast<std::size_t>(width)*height)
                    return fail("actual offscreen draw: "+drawError);
                const auto afterWorld=state.world.deterministicSimulationStateHash(),afterTerrain=terrainSignature(state.world);
                if(afterWorld!=worldHash || afterTerrain!=terrainHash)return fail("drawing changed original world/tile/subtile/level/overlay authority");
                TerrainSurfaceFrame surface;std::string surfaceError;
                if(!captureTerrainSurface(state.world,state.renderCache,camera,RECT{0,0,width,height},width,height,surface,surfaceError))return fail(surfaceError);
                const auto& layer=state.renderCache.terrainLayer;
                if(pass==0){std::string evidenceError;
                    if(!recordColdVisualEvidence(state,center,camera,width,height,visualContext,canonicalBefore,map.theater,roi,visualCells,visualFamilyEvidence,evidenceError))return fail(evidenceError);
                    if(state.world.deterministicSimulationStateHash()!=worldHash || terrainSignature(state.world)!=terrainHash)
                        return fail("visual evidence changed original world/terrain authority");
                }
                std::size_t compared=0,mismatch=0,firstMismatch=0,surfaceCompared=0,surfaceOwned=0,ownerMismatch=0,fullPanMismatch=0;
                RECT fullPanBounds{-1,-1,-1,-1};
                if(pass==0){cold=pixels;coldSurface=surface;}
                else if(pass==3){
                    for(int y=panCompare.top;y<panCompare.bottom;++y)for(int x=panCompare.left;x<panCompare.right;++x){
                        const auto index=static_cast<std::size_t>(y)*width+x,previous=static_cast<std::size_t>(y+1)*width+x+1;
                        if(surface.pixels[index]!=coldSurface.pixels[previous]){if(mismatch++==0)firstMismatch=index;}
                        if(surface.terrainOwners[index]!=coldSurface.terrainOwners[previous]
                            || surface.surfaceOwners[index]!=coldSurface.surfaceOwners[previous])++ownerMismatch;
                        surfaceOwned+=surface.terrainOwners[index]>=0;++surfaceCompared;
                    }
                    // Record the whole world-frame overlap, including original
                    // objects and shadows. This diagnostic does not mask differences.
                    for(int y=0;y<height-1;++y)for(int x=0;x<width-1;++x){
                        const auto index=static_cast<std::size_t>(y)*width+x,previous=static_cast<std::size_t>(y+1)*width+x+1;
                        if(pixels[index]!=cold[previous]){
                            if(fullPanMismatch++==0)fullPanBounds=RECT{x,y,x+1,y+1};
                            else {fullPanBounds.left=std::min(fullPanBounds.left,static_cast<LONG>(x));
                                fullPanBounds.top=std::min(fullPanBounds.top,static_cast<LONG>(y));
                                fullPanBounds.right=std::max(fullPanBounds.right,static_cast<LONG>(x+1));
                                fullPanBounds.bottom=std::max(fullPanBounds.bottom,static_cast<LONG>(y+1));}
                        }
                    }compared=surfaceCompared;++panComparisons;
                } else {
                    for(std::size_t n=0;n<pixels.size();++n)if(pixels[n]!=cold[n]){if(mismatch++==0)firstMismatch=n;}
                    compared=pixels.size();++exactComparisons;
                }
                const bool coverage=stats->texturedTerrainCells>0 && stats->fallbackTerrainCells==0;
                const bool panSurfaceValid=pass!=3 || (surfaceCompared==static_cast<std::size_t>(panCompare.right-panCompare.left)
                    *(panCompare.bottom-panCompare.top) && surfaceOwned>0 && ownerMismatch==0);
                frames<<map.theater<<','<<csv(state.config.defaultMapPath)<<','<<roi<<','<<csv(selected[roi].category)
                    <<','<<center.x<<','<<center.y<<','<<center.level<<','<<names[pass]<<','<<state.tick<<','<<camera.tileWidth<<','<<camera.tileHeight
                    <<','<<camera.worldOffsetX<<','<<camera.worldOffsetY<<','<<hashBytes(pixels.data(),pixels.size()*sizeof(std::uint32_t))
                    <<','<<afterWorld<<','<<afterTerrain<<','<<stats->visibleTerrainCells<<','<<stats->texturedTerrainCells<<','<<stats->fallbackTerrainCells
                    <<','<<stats->authoredEmptyTerrainSubTiles<<','<<csv(bmp)<<','<<compared
                    <<','<<hashBytes(surface.pixels.data(),surface.pixels.size()*sizeof(std::uint32_t))
                    <<','<<layer.originX<<','<<layer.originY<<','<<layer.width<<','<<layer.height
                    <<','<<panCompare.left<<','<<panCompare.top<<','<<panCompare.right<<','<<panCompare.bottom
                    <<','<<surfaceCompared<<','<<surfaceOwned<<','<<ownerMismatch<<','<<(pass==3?static_cast<std::size_t>(width-1)*(height-1):0)<<','<<fullPanMismatch
                    <<','<<fullPanBounds.left<<','<<fullPanBounds.top<<','<<fullPanBounds.right<<','<<fullPanBounds.bottom
                    <<','<<((coverage && mismatch==0 && panSurfaceValid)?"PASS":"FAIL")<<'\n';
                ++draws;if(saveBmp)++bitmapCount;
                if(!coverage)return fail("actual TMP coverage has fallback: visible="+std::to_string(stats->visibleTerrainCells)
                    +" textured="+std::to_string(stats->texturedTerrainCells)+" fallback="+std::to_string(stats->fallbackTerrainCells));
                if(!panSurfaceValid)return fail("actual terrain pan ownership/coverage: compared="+std::to_string(surfaceCompared)
                    +" owned="+std::to_string(surfaceOwned)+" ownerMismatch="+std::to_string(ownerMismatch));
                if(mismatch)return fail(std::string(pass==3?"actual terrain surface pan pixels differ":"native world-frame pixels differ")
                    +": mismatch="+std::to_string(mismatch)+" first="+std::to_string(firstMismatch%width)+","+std::to_string(firstMismatch/width)
                    +" compared="+std::to_string(compared)+" recordedWorldPanMismatch="+std::to_string(fullPanMismatch));
            }
            ++rois;std::cout<<"Terrain surface "<<scene<<" PASS center="<<center.x<<','<<center.y<<" category="<<selected[roi].category
                <<" originalWorld="<<worldHash<<" originalTerrain="<<terrainHash<<" cold/warm/rebuilt/returnedWorld=exact panTerrain=exact\n";
        }
        if(std::string_view(map.theater)=="TEMPERATE"){stage="two private original TMP controls";std::string controlError;
            if(!runOriginalTmpPrivateControls(state,output,controlError))return fail(controlError);}
    }
    if(rois!=12 || draws!=60 || bitmapCount!=24 || exactComparisons!=36 || panComparisons!=12)return fail("incomplete six-theater matrix");
    bool clearMultiple=false,roughMultiple=false;
    for(const auto& [name,witness]:visualFamilyEvidence){
        const bool multiple=witness.resources.size()>=2 && witness.decodedHashes.size()>=2 && witness.noncanonicalCells>0;
        visualFamilies<<csv(name)<<','<<witness.drawnCells<<','<<witness.eligible<<','<<witness.rejected<<','<<witness.resources.size()
            <<','<<witness.decodedHashes.size()<<','<<witness.noncanonicalCells<<','<<multiple<<'\n';
        const auto separator=name.find('|');const auto stem=toLowerAsciiLocal(std::filesystem::path(name.substr(separator+1)).stem().string());
        clearMultiple=clearMultiple || (stem=="clear01" && multiple);roughMultiple=roughMultiple || (stem=="ruff01" && multiple);
    }
    visualCells.flush();visualFamilies.flush();
    if(!visualCells.good() || !visualFamilies.good())return fail("actual visual evidence CSV write");
    if(!clearMultiple || !roughMultiple)return fail("actual twelve ROIs lack layer-verified multiple clear/rough variants: clear="+std::to_string(clearMultiple)+" rough="+std::to_string(roughMultiple));
    frames.flush();original.flush();distribution.flush();if(!frames.good() || !original.good() || !distribution.good())return fail("evidence CSV write");
    std::cout<<"Terrain surface continuity passed: theaters=6 rois="<<rois<<" draws="<<draws<<" actualBMP="<<bitmapCount
        <<" exactWorldPixelPairs="<<exactComparisons<<" strictTerrainPanPairs="<<panComparisons<<" originalWorldAndTerrain=yes simulationTicks=0"
        <<" panRect=32,64,928,608 worldPanDifferencesRecorded=yes visualNaturalnessRequiresBMPReview=yes\n";
    error.clear();return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
