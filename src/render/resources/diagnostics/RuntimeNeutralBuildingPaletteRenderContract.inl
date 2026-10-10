// Test-only original SHP/PAL oracle. Actual colors pass through the normal
// building body renderer; expected colors never use its palette selectors.
#include "../../../../tests/rendering/RuntimeBuildingDepthOracle.h"
namespace RuntimeNeutralBuildingPaletteOracle {
using Bytes = std::vector<std::uint8_t>;
struct Frame {
    int canvasWidth=0,canvasHeight=0,x=0,y=0,width=0,height=0;
    Bytes indices;
    std::vector<std::size_t> sourceOffsets;
};

std::uint32_t little(const Bytes& bytes,std::size_t at,int count) {
    if(at>bytes.size()||static_cast<std::size_t>(count)>bytes.size()-at)
        throw std::runtime_error("truncated original SHP header/row");
    std::uint32_t result=0;
    for(int i=count-1;i>=0;--i)result=(result<<8)|bytes[at+i];
    return result;
}

Bytes archiveBytes(MixArchive& archive,const std::string& name,std::ofstream& evidence) {
    const auto* entry=archive.findEntryByName(name);
    Bytes bytes;std::string error;
    if(!entry||!archive.readEntryData(*entry,bytes,error))
        throw std::runtime_error("original archive entry "+archive.getPath()+"!"+name+": "+error);
    evidence<<archive.getPath()<<','<<name<<','<<entry->id<<','<<entry->absoluteOffset<<','<<bytes.size()<<'\n';
    return bytes;
}

Frame decode(const Bytes& bytes,int number) {
    if(number<0||static_cast<unsigned>(number)>=little(bytes,6,2))
        throw std::runtime_error("original SHP frame absent");
    const std::size_t header=8+static_cast<std::size_t>(number)*24;
    if(header+24>bytes.size())throw std::runtime_error("original SHP frame table truncated");
    Frame frame;frame.canvasWidth=little(bytes,2,2);frame.canvasHeight=little(bytes,4,2);
    frame.x=static_cast<std::int16_t>(little(bytes,header,2));
    frame.y=static_cast<std::int16_t>(little(bytes,header+2,2));
    frame.width=little(bytes,header+4,2);frame.height=little(bytes,header+6,2);
    if(frame.canvasWidth<=0||frame.canvasHeight<=0||frame.width<=0||frame.height<=0
        ||frame.width>4096||frame.height>4096)throw std::runtime_error("original SHP dimensions");
    frame.indices.assign(static_cast<std::size_t>(frame.width)*frame.height,0);
    frame.sourceOffsets.assign(frame.indices.size(),0);
    std::size_t cursor=little(bytes,header+20,4);
    // The baseline bodies use original Format 3. Reject another encoding
    // rather than silently consulting the production decoder for the oracle.
    if(little(bytes,header+8,1)!=3)throw std::runtime_error("original baseline frame is not Format 3");
    for(int y=0;y<frame.height;++y){
        const auto length=little(bytes,cursor,2);
        if(length<2||cursor>bytes.size()||length>bytes.size()-cursor)
            throw std::runtime_error("original SHP row extent");
        const std::size_t end=cursor+length;cursor+=2;int x=0;
        while(cursor<end){
            const auto color=bytes[cursor++];
            if(!color){
                if(cursor==end)throw std::runtime_error("original SHP transparent run truncated");
                const int run=bytes[cursor++];if(!run)throw std::runtime_error("zero-length original SHP run");
                x+=run;
            }else{
                if(x>=frame.width)throw std::runtime_error("original SHP opaque row overflow");
                const auto pixel=static_cast<std::size_t>(y)*frame.width+x++;
                frame.indices[pixel]=color;frame.sourceOffsets[pixel]=cursor-1;
            }
        }
        // Retail may include one trailing transparent padding pixel.
        if(x<frame.width||x>frame.width+1)throw std::runtime_error("original SHP decoded row width");
    }
    return frame;
}

std::array<std::uint32_t,256> palette(const Bytes& bytes) {
    if(bytes.size()!=768||*std::max_element(bytes.begin(),bytes.end())>63)
        throw std::runtime_error("original PAL must contain 256 six-bit RGB entries");
    std::array<std::uint32_t,256> colors{};
    for(std::size_t i=0;i<colors.size();++i)
        colors[i]=(std::uint32_t(bytes[i*3])<<18)|(std::uint32_t(bytes[i*3+1])<<10)
            |(std::uint32_t(bytes[i*3+2])<<2);
    return colors;
}
}

#include "render/resources/diagnostics/RuntimeNeutralBuildingPaletteAudit.inl"

bool runRuntimeNeutralBuildingPaletteRenderContractCheck(
    const RuntimeMapRenderContext& input,const std::string& output,std::string& error) {
    if (std::getenv("CONCORD_NEUTRAL_BUILDING_PALETTE_AUDIT_ALL"))
        return runRuntimeNeutralBuildingPaletteAudit(input, output, error);
    using namespace RuntimeNeutralBuildingPaletteOracle;
    const auto fail=[&](const std::string& reason){error="Neutral building palette: "+reason;return false;};
    try {
        if(!input.rules||!input.art||input.assetRootPath.empty())return fail("original context missing");
        const std::filesystem::path root=output.empty()
            ?"build/smoke/feature-set-19-complete/07-neutral-colors/checks":output;
        std::filesystem::create_directories(root);
        std::ofstream sources(root/"original-resources.csv"),records(root/"neutral-colors.csv");
        if(!sources||!records)return fail("evidence files unavailable");
        sources<<"archive,entry,id,absolute_offset,bytes\n";
        records<<"case,type,theater,frame,owner,palette,resolved_image,source,path,visible_pixels,pixel_mismatch,mask_mismatch,wrong_unit_witness,first_x,first_y,first_index,first_actual,first_expected,result\n";
        const auto assets=std::filesystem::path(input.assetRootPath);
        MixArchive shapes,palettes,generic,isogen,snowShapes;
        if(!shapes.loadFromPath(assets/"ra2/isotemp.mix")
            ||!palettes.loadFromPath(assets/"ra2/cache.mix")
            ||!generic.loadFromPath(assets/"ra2/generic.mix")
            ||!isogen.loadFromPath(assets/"ra2/isogen.mix")
            ||!snowShapes.loadFromPath(assets/"ra2/snow.mix"))return fail("original MIX archives unavailable");
        const auto cityTem=palette(archiveBytes(palettes,"citytem.pal",sources));
        const auto citySnow=palette(archiveBytes(palettes,"citysno.pal",sources));
        const auto isoTem=palette(archiveBytes(palettes,"isotem.pal",sources));
        const auto isoSnow=palette(archiveBytes(palettes,"isosno.pal",sources));
        const auto unitTem=palette(archiveBytes(palettes,"unittem.pal",sources));
        const auto unitSnow=palette(archiveBytes(palettes,"unitsno.pal",sources));
        const auto liberty=palette(archiveBytes(palettes,"libtem.pal",sources));
        const auto animation=palette(archiveBytes(palettes,"anim.pal",sources));
        std::array<TheaterDatabase,2> theaters;
        for(int i=0;i<2;++i){
            const auto path=assets/"ra2md/localmd"/(i?"snowmd.ini":"temperatmd.ini");IniFile ini;
            if(!ini.loadFromFile(path.string())||!theaters[i].loadFromIni(ini,path.string()))
                return fail("original theater metadata unavailable");
        }
        IniFile stockArt;
        if(!stockArt.loadFromFile((assets/"ra2md/localmd/artmd.ini").string()))
            return fail("original ART unavailable for explicit configuration controls");
        const auto overlayArt=[&](const std::string& text){
            IniFile ini=stockArt,overlay;auto result=std::make_unique<ArtDatabase>();
            if(!overlay.loadFromString(text))throw std::runtime_error("test ART overlay parse");
            ini.appendFrom(overlay);if(!result->loadFromIni(ini))throw std::runtime_error("test ART database parse");
            return result;
        };
        constexpr int width=512,height=512;
        constexpr std::uint32_t background=0x0019232dU;
        const RECT viewport{0,0,width,height};const POINT center{width/2,height/2};
        struct Surface {
            HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ old=nullptr;std::uint32_t* pixels=nullptr;
            Surface(){BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
                info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
                dc=CreateCompatibleDC(nullptr);void* data=nullptr;
                if(dc)bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
                pixels=static_cast<std::uint32_t*>(data);if(bitmap&&pixels)old=SelectObject(dc,bitmap);}
            ~Surface(){if(old&&dc)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);}
        } surface;
        if(!surface.dc||!surface.pixels)return fail("DIB surface unavailable");
        auto cache=std::make_unique<RuntimeMapRenderCache>();auto context=input;
        context.vision=nullptr;context.lightingTint=nullptr;context.specialMechanismSnapshots=nullptr;
        context.mapFacts=nullptr;context.shadowsEnabled=false;context.animationTick=0;
        std::unordered_map<std::string,COLORREF> ownerColors{{"colorgold",RGB(235,210,35)},{"colorblue",RGB(30,80,235)}};
        context.ownerRemapColors=&ownerColors;
        RuntimeMapCamera camera;camera.initialized=true;camera.tileWidth=60;camera.tileHeight=30;
        const auto theaterContext=[&](int snow){auto result=context;result.theater=&theaters[snow];
            result.palettePath=(assets/"extracted_palettes"/(snow?"cache_isosno.pal":"cache_isotem.pal")).string();
            result.structurePalettePath=(assets/"extracted_palettes"/(snow?"cache_unitsno.pal":"cache_unittem.pal")).string();
            return result;};
        std::uint64_t totalVisible=0,totalWrong=0,totalMismatch=0;int checks=0,failures=0,ownerChecks=0,hdcChecks=0;
        const auto check=[&](const std::string& label,const std::string& type,const Frame& body,int frame,
            const std::array<std::uint32_t,256>& colors,const char* paletteName,RuntimeMapRenderContext view,
            int snow,const std::string& owner,bool save,bool hdc,std::vector<std::uint32_t>* captured=nullptr){
            std::vector<std::uint32_t> expected(width*height,background),wrong(expected);
            std::vector<int> sourceIndex(width*height,-1);
            const int left=center.x-body.canvasWidth/2+body.x,top=center.y-body.canvasHeight/2+body.y;
            const auto& unit=snow?unitSnow:unitTem;int visible=0,wrongWitness=0;
            for(int y=0;y<body.height;++y)for(int x=0;x<body.width;++x){
                const int index=body.indices[static_cast<std::size_t>(y)*body.width+x];if(!index)continue;
                const int px=left+x,py=top+y;
                if(px<0||py<0||px>=width||py>=height)throw std::runtime_error(label+" original frame clipped");
                const auto p=static_cast<std::size_t>(py)*width+px;
                expected[p]=colors[index];wrong[p]=unit[index];sourceIndex[p]=index;
                ++visible;wrongWitness+=colors[index]!=unit[index];
            }
            if(!visible)throw std::runtime_error(label+" empty original frame");
            const auto* rules=view.rules->findBuildingType(type);
            WorldObjectInstance object;object.instanceId=1;object.kind=WorldObjectKind::Structure;
            object.type=type;object.owner=owner;object.health=frame?1:(rules?rules->strength:1000);
            object.alive=object.poweredOperational=true;object.mission="Guard";
            const auto* actualFrame=loadRuntimeObjectShpSpriteFromCache(object,view,*cache);
            if(!actualFrame||actualFrame->frameIndex!=frame||actualFrame->image.pixels!=body.indices
                ||actualFrame->image.globalWidth!=body.canvasWidth||actualFrame->image.globalHeight!=body.canvasHeight
                ||actualFrame->image.offsetX!=body.x||actualFrame->image.offsetY!=body.y)
                throw std::runtime_error(label+" actual loader differs from independent original frame: "
                    +(actualFrame?"source="+actualFrame->sourcePath+" image="+actualFrame->resolvedImageId
                        +" frame="+std::to_string(actualFrame->frameIndex)
                        +" canvas="+std::to_string(actualFrame->image.globalWidth)+"x"+std::to_string(actualFrame->image.globalHeight)
                        +" crop="+std::to_string(actualFrame->image.width)+"x"+std::to_string(actualFrame->image.height)
                        +" pixels="+std::to_string(actualFrame->image.pixels.size()):"missing")
                    +" expectedFrame="+std::to_string(frame)+" expectedCanvas="+std::to_string(body.canvasWidth)+"x"+std::to_string(body.canvasHeight)
                    +" expectedCrop="+std::to_string(body.width)+"x"+std::to_string(body.height));
            GdiFlush();std::fill_n(surface.pixels,width*height,background);
            view.surfacePixels=hdc?nullptr:surface.pixels;view.surfaceWidth=hdc?0:width;view.surfaceHeight=hdc?0:height;
            if(!tryDrawObjectShpSprite(surface.dc,viewport,object,center,view,camera,*cache))
                throw std::runtime_error(label+" real building body draw failed");
            GdiFlush();std::vector<std::uint32_t> actual(surface.pixels,surface.pixels+width*height);
            for(auto& color:actual)color&=0xffffffU;
            int mismatch=0,maskMismatch=0;std::size_t firstBad=actual.size();
            for(std::size_t p=0;p<actual.size();++p){
                if(actual[p]!=expected[p]){++mismatch;if(firstBad==actual.size())firstBad=p;}
                maskMismatch+=(actual[p]!=background)!=(sourceIndex[p]>=0);
            }
            if(captured)*captured=actual;
            const bool okay=mismatch==0&&maskMismatch==0;
            records<<label<<','<<type<<','<<(snow?"SNOW":"TEMPERATE")<<','<<frame<<','<<owner<<','<<paletteName
                <<','<<actualFrame->resolvedImageId<<",\""<<actualFrame->sourcePath<<'"'
                <<','<<(hdc?"hdc":"pixel")<<','<<visible<<','<<mismatch<<','<<maskMismatch<<','<<wrongWitness
                <<','<<(firstBad==actual.size()?-1:static_cast<int>(firstBad%width))
                <<','<<(firstBad==actual.size()?-1:static_cast<int>(firstBad/width))
                <<','<<(firstBad==actual.size()?-1:sourceIndex[firstBad])
                <<','<<(firstBad==actual.size()?0:actual[firstBad])<<','<<(firstBad==actual.size()?0:expected[firstBad])
                <<','<<(okay?"pass":"fail")<<'\n';
            ++checks;hdcChecks+=hdc;failures+=!okay;totalMismatch+=mismatch;totalVisible+=visible;totalWrong+=wrongWitness;
            if(!okay)std::cout<<"Neutral color "<<label<<" mismatch="<<mismatch<<" mask="<<maskMismatch<<'\n';
            if(save){
                std::vector<std::uint32_t> sheet(width*3*height,background);
                const std::vector<std::uint32_t>* columns[]{&actual,&expected,&wrong};
                for(int column=0;column<3;++column)for(int y=0;y<height;++y)
                    std::copy_n(columns[column]->begin()+y*width,width,sheet.begin()+y*width*3+column*width);
                if(!writeRuntimeBgraBmp((root/(label+"-actual-original-wrong-unit.bmp")).string(),width*3,height,sheet))
                    throw std::runtime_error("palette evidence BMP write");
            }
            return okay;
        };
        struct Original {std::string type;Bytes bytes;};std::vector<Original> originals;
        for(const char* type:{"CITY01","CITY02","CITY03","CITY04","CITY05","CITY06"})
            originals.push_back({type,archiveBytes(shapes,std::string(type)+".shp",sources)});
        originals.push_back({"GALITE",archiveBytes(generic,"gglite.shp",sources)});
        const auto snowLampBytes=archiveBytes(snowShapes,"galite.shp",sources);
        if(snowLampBytes==originals.back().bytes)return fail("original snow lamp variant unexpectedly identical to generic lamp");
        std::vector<std::vector<std::uint32_t>> firstTheater(14);
        for(int phase=0;phase<3;++phase){const int snow=phase==1;auto view=theaterContext(snow);
            for(std::size_t n=0;n<originals.size();++n){const auto& original=originals[n];const bool terrain=n>=5;
                const bool snowLamp=snow&&n==6;
                for(int frame=0;frame<2;++frame){const auto body=decode(snowLamp?snowLampBytes:original.bytes,frame);
                    // TerrainPalette=yes reads the current theater's retail
                    // ISO palette. Expected colors come from MIX bytes above.
                    const auto& colors=terrain?(snow?isoSnow:isoTem):cityTem;
                    const char* paletteName=terrain?(snow?"isosno.pal":"isotem.pal"):"legacy-citytem-fixed";
                    const std::string label=original.type+"-phase"+std::to_string(phase)+"-frame"+std::to_string(frame);
                    std::vector<std::uint32_t> first;
                    for(int owner=0;owner<4;++owner){
                        const std::string name=owner==1?"ColorGold":owner==2?"ColorBlue":"Neutral";
                        std::vector<std::uint32_t> actual;
                        check(label+"-owner"+std::to_string(owner),original.type,body,frame,colors,paletteName,view,snow,name,
                            (phase==0||snowLamp)&&owner==0,false,&actual);
                        if(owner==0){first=actual;if(phase==0)firstTheater[n*2+frame]=actual;
                            // The snow phase independently checks its own SHP
                            // and PAL. Returning to temperate must restore the
                            // original pixels in the same render cache.
                            else if(phase==2&&actual!=firstTheater[n*2+frame])throw std::runtime_error(label+" original temperate colors did not return across theater/cache");}
                        else{++ownerChecks;if(actual!=first)throw std::runtime_error(label+" original palette changed with owner/cache");}
                    }
                    std::vector<std::uint32_t> hdc;
                    check(label+"-hdc",original.type,body,frame,colors,paletteName,view,snow,"Neutral",false,true,&hdc);
                    if(hdc!=first)throw std::runtime_error(label+" pixel/HDC colors differ");
                }
            }
        }
        const auto cityBody=decode(originals[3].bytes,0);
        // Explicit ART is parsed from an actual INI overlay, including the
        // new Specified fields; direct struct edits cannot stand in for parsing.
        struct Override {const char* name;const char* ini;const std::array<std::uint32_t,256>* colors;const char* palette;};
        const Override overrides[]{
            {"explicit-lib","Palette=lib\n",&liberty,"libtem.pal"},
            {"explicit-custom","CustomPalette=anim.pal\n",&animation,"anim.pal"},
            {"explicit-anim","AnimPalette=yes\n",&animation,"anim.pal"},
            {"explicit-alt","AltPalette=yes\n",&unitTem,"unittem.pal"},
            {"explicit-terrain","TerrainPalette=yes\n",&isoTem,"isotem.pal"},
            {"explicit-terrain-no","TerrainPalette=no\n",&unitTem,"unittem.pal"},
            {"explicit-anim-no","AnimPalette=no\n",&unitTem,"unittem.pal"}
        };
        for(const auto& control:overrides){auto art=overlayArt(std::string("[CITY04]\n")+control.ini);
            auto view=theaterContext(0);view.art=art.get();const auto* parsed=art->findArtType("CITY04");
            if(std::string(control.name)=="explicit-terrain-no"&&(!parsed->terrainPaletteSpecified||parsed->terrainPalette))
                return fail("explicit TerrainPalette=no parser contract");
            if(std::string(control.name)=="explicit-anim-no"&&(!parsed->animPaletteSpecified||parsed->animPalette))
                return fail("explicit AnimPalette=no parser contract");
            check(control.name,"CITY04",cityBody,0,*control.colors,control.palette,view,0,"Neutral",true,false);
        }
        auto explicitCity=overlayArt("[CITY04]\nPalette=city\n");
        for(int phase=0;phase<3;++phase){const int snow=phase==1;auto view=theaterContext(snow);view.art=explicitCity.get();
            check("explicit-prefix-phase"+std::to_string(phase),"CITY04",cityBody,0,snow?citySnow:cityTem,
                snow?"citysno.pal":"citytem.pal",view,snow,"Neutral",phase<2,false);}
        // Same original lamp SHP, independent nonlegacy ART identity. This
        // verifies the same retail TerrainPalette meaning as the stock lamp,
        // without selecting expected colors from the production renderer.
        auto terrainAlias=overlayArt("[PALETTE_TERRAIN_CONTROL]\nImage=gglite\nNewTheater=no\nFoundation=1x1\nTerrainPalette=yes\nRemapable=no\nExtraDamageStage=false\n");
        const auto lamp=decode(originals[6].bytes,0);
        for(int phase=0;phase<3;++phase){const int snow=phase==1;auto view=theaterContext(snow);view.art=terrainAlias.get();
            check("terrain-alias-phase"+std::to_string(phase),"PALETTE_TERRAIN_CONTROL",lamp,0,snow?isoSnow:isoTem,
                snow?"isosno.pal":"isotem.pal",view,snow,"Neutral",true,false);}
        // A legal one-index replacement keeps the authored canvas and mask.
        // Its complete-file identity differs and must not inherit the stock
        // legacy palette merely because the file/type name is CITY04.
        auto replacement=originals[3].bytes;std::size_t changed=cityBody.indices.size();
        for(std::size_t p=0;p<cityBody.indices.size();++p)if(cityBody.indices[p]>31){
            const auto old=cityBody.indices[p];const std::uint8_t other=old==100?101:100;
            if(unitTem[old]!=unitTem[other]){replacement[cityBody.sourceOffsets[p]]=other;changed=p;break;}}
        if(changed==cityBody.indices.size())return fail("replacement lacks a real nontransparent changed index");
        const auto replacementDir=root/"replacement";std::filesystem::create_directories(replacementDir);
        {std::ofstream file(replacementDir/"CITY04.shp",std::ios::binary);
            file.write(reinterpret_cast<const char*>(replacement.data()),static_cast<std::streamsize>(replacement.size()));
            if(!file)return fail("replacement fixture write");}
        const auto replacementFrame=decode(replacement,0);auto replaced=theaterContext(0);replaced.structureRootPath=replacementDir.string();
        check("replacement-fingerprint","CITY04",replacementFrame,0,unitTem,"unittem.pal",replaced,0,"Neutral",true,false);
        // Unrelated ordinary and explicitly custom-paletted stock buildings
        // retain their own palette. An unconfigured house disables owner tint
        // in these palette-only controls; legacy owner switching is above.
        const auto power=decode(archiveBytes(generic,"ggpowr.shp",sources),0);
        check("ordinary-unit-palette","GAPOWR",power,0,unitTem,"unittem.pal",theaterContext(0),0,"PaletteUnconfiguredHouse",true,false);
        const auto statue=decode(archiveBytes(isogen,"cgnewy04.shp",sources),0);
        check("stock-custom-lib","CANEWY04",statue,0,liberty,"libtem.pal",theaterContext(0),0,"PaletteUnconfiguredHouse",true,false);
        // Restore default metadata and source after explicit overrides and a
        // same-name replacement in the same cache; stale policy cannot leak.
        check("default-after-overrides","CITY04",cityBody,0,cityTem,"legacy-citytem-fixed",theaterContext(0),0,"Neutral",false,false);
        std::cout<<"Neutral building palette: checks="<<checks<<" ownerChecks="<<ownerChecks<<" hdc="<<hdcChecks
            <<" visible="<<totalVisible<<" wrongUnitWitness="<<totalWrong<<" mismatch="<<totalMismatch<<" failures="<<failures<<'\n';
        return failures?fail(std::to_string(failures)+" actual body palette checks failed; see neutral-colors.csv"):true;
    }catch(const std::exception& exception){return fail(exception.what());}
}
