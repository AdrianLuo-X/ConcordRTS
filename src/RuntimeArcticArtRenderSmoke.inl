bool runRuntimeArcticArtRenderContractCheck(
    const RuntimeMapRenderContext& temperate, const RuntimeMapRenderContext& snow,
    const std::string& output, int& liveFrames, int& deathFrames, std::string& error
) {
    const auto fail=[&](const std::string& why){error="Arctic render: "+why;return false;};
    auto cache=std::make_unique<RuntimeMapRenderCache>();
    std::ofstream records(output+"/render-frames.csv");
    records<<"theater,hz,pose,facing,phase,image,frame,source\n";
    std::ofstream deathRecords(output+"/death-frames.csv");
    deathRecords<<"theater,wet,infDeath,facing,phase,image,frame,sourceVisible,drawn,source\n";
    struct Pose {const char* mission;const char* sequence;bool water,prone,moving;};
    const Pose poses[]={{"Guard","Guard",false,false,false},{"Move","Walk",false,false,true},
        {"Attack","FireUp",false,false,false},{"Guard","Prone",false,true,false},
        {"Move","Crawl",false,true,true},{"Attack","FireProne",false,true,false},
        {"Move","Swim",true,false,true},{"Guard","Tread",true,false,false},
        {"Attack","WetAttack",true,false,false},{"Idle1","WetIdle1",true,false,false},
        {"Idle2","WetIdle2",true,false,false}};
    std::vector<std::uint32_t> firstReady;
    for(int theaterIndex=0;theaterIndex<2;++theaterIndex) {
        RuntimeMapRenderContext context=theaterIndex?snow:temperate;
        const std::string image=theaterIndex?"SEALA":"SEAL";
        if(!context.art||!context.rules||runtimeRenderUsesArcticArt(context)!=(theaterIndex==1))
            return fail("real map theater does not match fixture");
        context.vision=nullptr;context.specialMechanismSnapshots=nullptr;
        MapFacts facts;facts.theater=theaterIndex?"SNOW":"TEMPERATE";
        MapCellFacts cell;cell.x=cell.y=cell.cellIndex=0;
        facts.cellIndexLookup.emplace(0,0);facts.cells.push_back(cell);context.mapFacts=&facts;
        constexpr int size=96,columns=3,rows=11;
        constexpr std::uint32_t background=0x00242c30U;
        std::vector<std::uint32_t> pixels(size*size,background),sheet(size*columns*size*rows,background);
        context.surfacePixels=pixels.data();context.surfaceWidth=context.surfaceHeight=size;
        RuntimeMapCamera camera;camera.initialized=true;
        WorldObjectInstance object;object.instanceId=1;object.kind=WorldObjectKind::Infantry;
        object.type="GHOST";object.owner="ArcticProbe";object.health=125;object.alive=true;
        object.hasRenderPosition=true;object.renderX=object.renderY=0;
        std::unordered_map<std::string,COLORREF> colors{{"arcticprobe",RGB(255,210,25)}};
        context.ownerRemapColors=&colors;
        int row=0;
        for(const auto& pose:poses) {
            facts.cells[0].isWater=pose.water;object.prone=pose.prone;
            object.mission=pose.mission;object.renderMoveActive=pose.moving;
            for(int hz:{30,60})for(int facing=0;facing<8;++facing)for(int phase=0;phase<3;++phase) {
                context.fixedTickHz=hz;context.animationTick=phase*hz/5;
                object.facing=facing*32;object.movementAnimationDistance=(phase+0.1)/3.0;
                const auto* sprite=loadRuntimeObjectShpSpriteFromCache(object,context,*cache);
                if(!sprite||!sprite->usedSequenceFrame||sprite->sequenceName!=pose.sequence
                    ||!equalAsciiNoCase(sprite->resolvedImageId,image))
                    return fail(image+"/"+pose.sequence+" selected "+(sprite?sprite->sequenceName+"/"+sprite->resolvedImageId:"missing"));
                const auto* art=findArtTypeForObject(object,context);
                const auto* sequence=art?findRuntimeSequenceFrame(*art,{pose.sequence}):nullptr;
                if(!sequence||art->id!=image)return fail("alternate ART sequence metadata");
                const int bucket=sequence->facingMultiplier>0?(8-facing)%8:0;
                const int begin=sequence->startFrame+bucket*sequence->facingMultiplier;
                if(sprite->frameIndex<begin||sprite->frameIndex>=begin+sequence->frameCount)
                    return fail("selected frame outside original directional strip");
                records<<(theaterIndex?"SNOW":"TEMPERATE")<<','<<hz<<','<<pose.sequence<<','<<facing<<','
                    <<phase<<','<<sprite->resolvedImageId<<','<<sprite->frameIndex<<','<<sprite->sourcePath<<'\n';
                std::fill(pixels.begin(),pixels.end(),background);
                if(!tryDrawObjectShpSprite(nullptr,RECT{0,0,size,size},object,POINT{size/2,72},context,camera,*cache)
                    ||std::all_of(pixels.begin(),pixels.end(),[&](auto p){return p==background;}))
                    return fail(image+"/"+pose.sequence+" empty actual render");
                if(row==0&&hz==30&&facing==0&&phase==0) {
                    if(theaterIndex==0)firstReady=pixels;
                    else if(pixels==firstReady)return fail("arctic body did not change original pixels");
                    const auto gold=pixels;colors["arcticprobe"]=RGB(40,80,255);
                    std::fill(pixels.begin(),pixels.end(),background);
                    if(!tryDrawObjectShpSprite(nullptr,RECT{0,0,size,size},object,POINT{size/2,72},context,camera,*cache)
                        ||pixels==gold)return fail("alternate art lost owner remap");
                    colors["arcticprobe"]=RGB(255,210,25);pixels=gold;
                }
                if(hz==30&&facing==0)for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size,size,
                    sheet.data()+(row*size+y)*size*columns+phase*size);
                ++liveFrames;
            }
            ++row;
        }
        if(!writeRuntimeBgraBmp(output+"/"+image+"-poses.bmp",size*columns,size*rows,sheet))return fail("pose sheet write");
        const auto* unit=context.rules->findUnitType("InfantryType","GHOST");
        if(!unit)return fail("original SEAL rule");
        std::vector<std::uint32_t> corpses(size*columns*size*4,background);int deathRow=0;
        for(bool wet:{false,true})for(int infDeath:{1,2}) {
            const auto sequence=runtimeInfantryDeathSequence(*context.art,*unit,infDeath,0,wet,theaterIndex==1);
            if(!sequence||sequence->artId!=image)return fail("death switched to normal body");
            for(int facing=0;facing<8;++facing)for(int phase=0;phase<3;++phase) {
                RuntimeImpactRenderActor actor;actor.animId=sequence->artId;actor.renderIntent="infantryDeath";
                actor.owner="ArcticProbe";actor.totalTicks=actor.remainingTicks=60;
                actor.frameIndex=sequence->startFrame+phase*(sequence->frameCount-1)/2;
                actor.facing=facing*32;
                const auto* deathArt=context.art->findArtType(sequence->artId);
                const auto* frame=loadRuntimeAuxiliaryShpSpriteFromCache(sequence->artId,actor.frameIndex,
                    context,*cache,runtimeArtShadowEnabled(deathArt,true));
                if(!frame||frame->frameIndex!=actor.frameIndex||!equalAsciiNoCase(frame->resolvedImageId,image))
                    return fail(image+" corpse frame missing, wrapped or wrong image");
                const bool sourceVisible=std::any_of(frame->image.pixels.begin(),frame->image.pixels.end(),
                    [](unsigned char pixel){return pixel!=0;});
                std::fill(pixels.begin(),pixels.end(),background);
                RuntimeMapRenderStats stats;RuntimeMapCamera corpseCamera=camera;
                corpseCamera.worldOffsetX=-size/2;corpseCamera.worldOffsetY=-72;
                GameWorld empty;drawRuntimeImpactActor(nullptr,RECT{0,0,size,size},empty,context,corpseCamera,*cache,actor,stats);
                const bool outputVisible=std::any_of(pixels.begin(),pixels.end(),[&](auto p){return p!=background;});
                deathRecords<<(theaterIndex?"SNOW":"TEMPERATE")<<','<<wet<<','<<infDeath<<','<<facing<<','
                    <<phase<<','<<frame->resolvedImageId<<','<<actor.frameIndex<<','<<sourceVisible<<','
                    <<stats.impactActorDrawn<<','<<frame->sourcePath<<'\n';
                if(sourceVisible!=outputVisible||(sourceVisible&&stats.impactActorDrawn!=1))
                    return fail(image+" corpse frame "+std::to_string(actor.frameIndex)+" visibility differs from original pixels");
                if(facing==0)for(int y=0;y<size;++y)std::copy_n(pixels.data()+y*size,size,
                    corpses.data()+(deathRow*size+y)*size*columns+phase*size);
                ++deathFrames;
            }
            ++deathRow;
        }
        if(!writeRuntimeBgraBmp(output+"/"+image+"-deaths.bmp",size*columns,size*4,corpses))return fail("corpse sheet write");
        // Existing shoreline, swimming, bridge, frozen and 30/60 Hz checks
        // now operate on the selected body for each real theater as well.
        if(!runRuntimeInfantrySwimRenderContractCheck(context,error))return false;
    }
    WorldObjectInstance object;object.instanceId=7;object.kind=WorldObjectKind::Infantry;
    object.type="GHOST";object.alive=true;object.health=125;object.mission="Guard";
    for(int index:{1,0,1,0}) {
        const auto& context=index?snow:temperate;
        const auto* sprite=loadRuntimeObjectShpSpriteFromCache(object,context,*cache);
        if(!sprite||!equalAsciiNoCase(sprite->resolvedImageId,index?"SEALA":"SEAL"))return fail("cross-map cache/suffix accumulation");
    }
    for(const char* theater:{"TEMPERATE","URBAN","NEWURBAN","DESERT","LUNAR"}) {
        auto context=temperate;MapFacts facts;facts.theater=theater;
        context.theater=nullptr;context.mapFacts=&facts;
        const auto* sprite=loadRuntimeObjectShpSpriteFromCache(object,context,*cache);
        if(!sprite||!equalAsciiNoCase(sprite->resolvedImageId,"SEAL"))return fail("non-snow theater selected arctic art");
    }
    for(const char* type:{"E1","TANY"}) {
        object.type=type;
        const auto* unit=snow.rules->findUnitType("InfantryType",type);
        const auto* sprite=loadRuntimeObjectShpSpriteFromCache(object,snow,*cache);
        if(!unit||unit->alternateArcticArt||!sprite)return fail("unflagged stock infantry changed");
        const std::string base=unit->image.empty()?unit->id:unit->image;
        const auto* art=snow.art->findArtType(base);
        if(!equalAsciiNoCase(sprite->resolvedImageId,art&&!art->image.empty()?art->image:base))
            return fail("unflagged stock infantry gained suffix");
    }
    IniFile ini,extra,disabled;
    RulesDatabase baselineRules,disabledRules;
    if(!ini.loadFromFile("local_data/rulesmd.ini")||!baselineRules.loadFromIni(ini)
        ||!disabled.loadFromString("[GHOST]\nAlternateArcticArt=no\n"))return fail("isolated hash fixture");
    IniFile onlyDisabled=ini;onlyDisabled.appendFrom(disabled);
    if(!disabledRules.loadFromIni(onlyDisabled)
        ||disabledRules.getDetailRulesHash()==baselineRules.getDetailRulesHash())return fail("arctic flag missing from rule hash");
    if(!extra.loadFromString(
        "[GHOST]\nAlternateArcticArt=no\n[InfantryTypes]\nQ13Alias=ARCTIC_ALIAS\nQ13Default=ARCTIC_DEFAULT\n"
        "[ARCTIC_ALIAS]\nImage=SEAL\nAlternateArcticArt=yes\nStrength=125\n"
        "[ARCTIC_DEFAULT]\nImage=SEAL\nStrength=125\n"))return fail("rule fixture");
    ini.appendFrom(extra);RulesDatabase rules;
    if(!rules.loadFromIni(ini)||rules.getDetailRulesHash()==snow.rules->getDetailRulesHash())return fail("rule hash");
    auto context=snow;context.rules=&rules;
    for(const char* type:{"GHOST","ARCTIC_ALIAS","ARCTIC_DEFAULT"}) {
        object.type=type;const auto* unit=rules.findUnitType("InfantryType",type);
        const bool alternate=std::string(type)=="ARCTIC_ALIAS";
        const auto* sprite=loadRuntimeObjectShpSpriteFromCache(object,context,*cache);
        if(!unit||unit->alternateArcticArt!=alternate||!sprite
            ||!equalAsciiNoCase(sprite->resolvedImageId,alternate?"SEALA":"SEAL"))
            return fail("explicit disable, default or custom image alias");
    }
    return true;
}
