// Issue25: original UI pixels and actual window input. Native RT_DIALOG115
// dimensions are dialog units; the project's 800x600 mapping is an adaptation.
// This is not a retail scanout, pause-policy, or live network-session experiment.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeDiplomacyPageSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const char* configuredOutput = std::getenv("CONCORD_DIPLOMACY_PAGE_OUTPUT");
    const std::filesystem::path output = configuredOutput && *configuredOutput
        ? configuredOutput : "build/issue21_diplomacy_page";
    std::filesystem::create_directories(output);
    std::ofstream csv(output / "diplomacy-page.csv");
    if (!csv) { error = "Diplomacy page: output unavailable"; return false; }
    csv << "case,event,width,height,control,target,pixelChecks,commands\n";
    int scenes = 0, clicks = 0, keys = 0, paints = 0, pixelsChecked = 0;
    int alliances = 0, chatDeliveries = 0, rejectedMasks = 0, localChecks = 0, captures = 0;
    int originalControls=0, fontPixels=0, layoutChecks=0;
    std::string label, stage; std::vector<std::string> failures;
    const auto fail = [&](const std::string& why) {
        error = "Diplomacy " + label + " [" + stage + "]: " + why; return false;
    };
    if (!live.report.success || !live.world.getSummary().loaded || !live.rulesLoaded
        || !live.artLoaded || !live.theaterLoaded) return fail("original startup resources/world unavailable");
    const auto assetRoot = std::filesystem::path(runtimeAssetRootPath(live.config));
    const std::array<int, 7> chatIds{1750,1751,1752,1753,1762,1763,1764};
    const auto center = [](RECT r) { return POINT{(r.left+r.right)/2,(r.top+r.bottom)/2}; };
    const auto rgb = [](RuntimePaletteColor c) { return (std::uint32_t(c.r)<<16) | (std::uint32_t(c.g)<<8) | c.b; };
    const auto digest = [](const std::vector<unsigned char>& bytes) {
        const auto hash=runtimeMultiplayerSnapshotSha256(bytes); std::string text;
        for (auto byte:hash) { text.push_back("0123456789ABCDEF"[byte>>4]); text.push_back("0123456789ABCDEF"[byte&15]); }
        return text;
    };

    const auto make = [&](const std::string& country) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config=live.config; s.report=live.report; s.world=live.world; s.mapFacts=live.mapFacts;
        s.rules=live.rules; s.art=live.art; s.theater=live.theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=s.mapFactsLoaded=true;
        s.config.playerCountry=country; s.config.fixedTickHz=30; s.config.gameMode="Battle";
        s.config.gameModeRulesOverride.clear(); s.config.computerAiEnabled=false;
        s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=false;
        s.config.keyboardReferenceLayout=false; s.config.keyBindings.clear();
        s.config.configPath=(output/"unused-local-preferences.ini").string();
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false; s.visionEnabled=false;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        // Preserve the actual parsed map and its facts. No synthetic terrain or
        // saved-map substitution is necessary for a six-page UI experiment.
        s.config.playerSlots.resize(8); s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear();
        s.humanHouseAuthorities.clear(); s.playerCommands.alliances.clear();
        const std::array<std::string,8> countries{"Americans","Russians","Americans",country,
            "French","Germans","Africans","YuriCountry"};
        const std::array<std::string,8> names{"同名玩家","AI对手","同名玩家","本地指挥官",
            "法国友军","德国玩家","AI非洲","第八槽玩家"};
        const auto* base=s.rules.findBuildingType("NANRCT");
        if (!base) { fail("original NANRCT unavailable"); return std::unique_ptr<RuntimeWindowState>{}; }
        for (std::size_t i=0;i<8;++i) {
            auto& slot=s.config.playerSlots[i]; slot.enabled=true; slot.human=i!=1 && i!=6;
            slot.local=i==3; slot.name=names[i]; slot.country=countries[i]; slot.team="-";
            slot.peerId="diplomacy-peer-"+std::to_string(i);
            const std::string owner=runtimeSkirmishOwnerForSlot(i), canonical=runtimeCanonicalHumanHouseOwner(owner);
            s.skirmishCountryByOwner[canonical]=countries[i];
            // Authored UI score records, not a new combat/kill-credit test.
            s.matchScores[canonical].unitsDestroyed=static_cast<int>(i);
            s.matchScores[canonical].structuresDestroyed=static_cast<int>(i)+1;
            WorldObjectInstance o; o.instanceId=100+static_cast<int>(i); o.mapId="Diplomacy"+std::to_string(i);
            o.kind=WorldObjectKind::Structure; o.type="NANRCT"; o.owner=owner;
            o.x=52+static_cast<int>(i)*6; o.y=70; o.cell=runtimeCellKey(o.x,o.y);
            o.health=base->strength; o.alive=o.poweredOperational=true; o.mission="Guard"; o.currentAmmo=-1;
            s.world.accessObjects().push_back(o);
            if (slot.human) {
                RuntimeHumanHouseAuthority house; house.owner=owner; house.canonicalOwner=canonical;
                house.country=countries[i]; house.localFacade=slot.local; s.humanHouseAuthorities.push_back(house);
            }
        }
        s.economy.owner=runtimeSkirmishOwnerForSlot(3); s.economy.credits=4321; s.economy.techLevel=10;
        s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        initializeRuntimeLocalProductionLanes(s.economy);
        s.skirmishModeAlliesAllowed=s.skirmishModeAllyChangeAllowed=true;
        s.skirmishModeMustAlly=false;
        initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s); rebuildRuntimeEconomyPower(s);
        initializeRuntimeOfflineGameplayRouting(s);
        return box;
    };

    const auto run = [&](const std::string& country,int width,int height) {
        stage="original fixture"; auto box=make(country); if (!box) return false; auto& s=*box;
        RuntimeSnapshotActiveStateScope active(s); const RECT client{0,0,width,height};
        const auto own=runtimeCanonicalHumanHouseOwner(s.economy.owner);
        HWND window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"Original diplomacy smoke",
            WS_POPUP,-30000,-30000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!window) return fail("hidden Win32 window unavailable");
        struct Cleanup {
            RuntimeWindowState& state; HWND window; WNDPROC previous=nullptr; HDC screen=nullptr;
            ~Cleanup() {
                if (GetCapture()==window) ReleaseCapture();
                SetWindowLongPtrW(window,GWLP_USERDATA,0);
                if (previous) SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
                releaseRuntimeBackBuffer(state.backBuffer); if (screen) ReleaseDC(nullptr,screen); DestroyWindow(window);
            }
        } cleanup{s,window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        cleanup.previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(runtimeWindowProc)));
        cleanup.screen=GetDC(nullptr);
        if (!cleanup.previous || !cleanup.screen || !ensureRuntimeBackBuffer(s.backBuffer,cleanup.screen,width,height))
            return fail("actual WndProc / DIB unavailable");
        centerRuntimeCameraOnCell(s,client,{75,72}); ensureRuntimeCameraForViewport(s,runtimeMapViewport(client));
        const auto key=[&](int code,bool repeated=false) {
            SendMessageW(window,WM_KEYDOWN,code,repeated ? LPARAM(1)|(LPARAM(1)<<30) : LPARAM(1));
            SendMessageW(window,WM_KEYUP,code,LPARAM(1)|(LPARAM(1)<<30)|(LPARAM(1)<<31)); ++keys;
        };
        const auto mouse=[&](POINT p) { SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(p.x,p.y)); };
        const auto click=[&](int id) {
            const POINT p=center(runtimeDiplomacyControlRect(client,id)); mouse(p);
            SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
            SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y)); ++clicks;
        };
        const auto paint=[&]() {
            s.repaintRequested=true; InvalidateRect(window,nullptr,FALSE); SendMessageW(window,WM_PAINT,0,0); GdiFlush(); ++paints;
            return s.backBuffer.pixels && s.backBuffer.width==width && s.backBuffer.height==height;
        };
        const auto localAuthority=[&]() {
            std::ostringstream queues; s.combatDetails.save(queues);
            return std::to_string(s.world.deterministicSimulationStateHash())+'|'+s.playerCommands.serialize()
                +'|'+std::to_string(s.specialMechanisms.stateHash())+'|'+std::to_string(s.economy.credits)+'|'+queues.str();
        };
        std::vector<RuntimeMultiplayerGameplayCommand> commands;
        s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string& reason) {
            const bool accepted=dispatchRuntimeOfflineGameplayCommand(s,command,reason);
            if (accepted) commands.push_back(command); return accepted;
        });
        const auto rows=runtimeDiplomacyRows(s);
        if (rows.size()!=8 || !rows[0].self || rows[0].owner!=own || rows[0].name!=L"本地指挥官"
            || rows[0].slot!=std::optional<std::size_t>(3)) return fail("self-first physical-slot roster mismatch");
        std::set<std::string> rowOwners;
        for (const auto& row:rows) {
            if (!row.slot || !rowOwners.insert(row.owner).second || row.name!=widenUtf8(s.config.playerSlots[*row.slot].name)
                || row.country!=s.config.playerSlots[*row.slot].country || row.human!=s.config.playerSlots[*row.slot].human
                || row.kills!=static_cast<long long>(*row.slot)*2+1 || row.defeated)
                return fail("original roster name/country/canonical target confused");
        }
        stage="real Tab and actual original pixels"; const auto before=localAuthority();
        key(VK_TAB); if (!s.playerCommunication.diplomacyOpen || s.runtimeMenuOpen) return fail("Tab did not open separate diplomacy page");
        if (!paint()) return fail("actual WM_PAINT did not compose page");
        key(VK_TAB,true);
        if (!s.playerCommunication.diplomacyOpen || localAuthority()!=before || !commands.empty()) return fail("Tab-repeat or paint changed authority");
        const bool yuri=country=="YuriCountry", soviet=country=="Russians";
        const auto archive=assetRoot/(yuri ? "ra2md" : "ra2")/(yuri ? "sidec02md.mix" : soviet ? "sidec02.mix" : "sidec01.mix");
        const std::string image=std::string(width==640 ? "bkgdsm" : "bkgdmd")+(yuri ? "y.shp" : ".shp");
        RuntimeMapRenderCache rawCache; RuntimeShpFrameImage rawImage; std::vector<RuntimePaletteColor> palette;
        std::string imageSource,paletteSource;
        if (!loadRuntimeShpFrameFromMixArchive(pathString(archive),rawCache,image,0,rawImage,imageSource,error)
            || !loadRuntimePaletteFromMixArchive(pathString(archive),rawCache,yuri ? "uibkgdy" : "uibkgd",palette,paletteSource,error)
            || palette.size()!=256 || rawImage.width!=(width==640 ? 472 : 632) || rawImage.height!=(width==640 ? 448 : 568))
            return fail("independent original MIX image/palette unavailable: "+error);
        if (s.playerCommunication.diplomacyBackgroundSource.empty()
            || s.playerCommunication.diplomacyBackgroundSource.find(image)==std::string::npos)
            return fail("actual painter lacks expected original background source");
        std::vector<int> controlIds{1684,1059,1057,1431,1061,1058,1840,1062,1819,1685};
        for (int i=0;i<8;++i) { controlIds.push_back(1041+i); controlIds.push_back(1049+i); controlIds.push_back(1754+i); }
        for (int i=0;i<7;++i) { controlIds.push_back(1743+i); controlIds.push_back(chatIds[i]); }
        // Independent declared DU -> 800x600 canvas -> client adaptation.
        // It is not an assertion of retail Windows font-dependent DU pixels.
        const auto nativeRect=[&](int x,int y,int w,int h) {
            const auto sx=[&](int v) { return static_cast<LONG>(std::lround(std::lround(v*800.0/533.0)*width/800.0)); };
            const auto sy=[&](int v) { return static_cast<LONG>(std::lround(std::lround(v*600.0/369.0)*height/600.0)); };
            return RECT{sx(x),sy(y),sx(x+w),sy(y+h)};
        };
        for (const auto& item:std::array<std::array<int,5>,6>{{{1059,425,346,108,23},{1684,425,1,108,10},
                {1057,111,53,110,10},{1431,226,53,35,10},{1061,271,53,35,10},{1058,316,53,60,10}}}) {
            const RECT expected=nativeRect(item[1],item[2],item[3],item[4]), actual=runtimeDiplomacyControlRect(client,item[0]);
            if (!EqualRect(&actual,&expected)) return fail("native DU screen adaptation differs for control "+std::to_string(item[0]));
            ++layoutChecks;
        }
        // Auxiliary wide-window layout checks retain the actual native large
        // background selection; they do not add a screenshot or page scene.
        for (const auto& dimensions:std::array<std::array<int,2>,3>{{{1024,640},{1280,960},{1920,1080}}}) {
            const RECT wide{0,0,dimensions[0],dimensions[1]}, expectedImage{0,0,856,736};
            const auto background=runtimeDiplomacyBackgroundLayout(wide);
            if (std::string(background.imageId)!="bkgdlg.shp" || !EqualRect(&background.imageRect,&expectedImage))
                return fail("wide page changed the original LG artwork/origin");
            for (int id:{1059,1684}) {
                const RECT rect=runtimeDiplomacyControlRect(wide,id);
                if (rect.left<wide.right-runtimeSidebarWidth || rect.right!=wide.right
                    || rect.right-rect.left>runtimeSidebarWidth-6 || rect.right<=rect.left
                    || rect.top<wide.top || rect.bottom>wide.bottom)
                    return fail("wide native right-column control escaped the sidebar: "+std::to_string(id));
                ++layoutChecks;
            }
            if (runtimeDiplomacyHitControl(s,wide,center(runtimeDiplomacyControlRect(wide,1059)))!=1059)
                return fail("wide native Return visual and hit rectangles differ");
            ++layoutChecks;
        }
        const auto outsideControls=[&](POINT p) {
            for (int id:controlIds) { auto rect=runtimeDiplomacyControlRect(client,id); InflateRect(&rect,8,8); if (PtInRect(&rect,p)) return false; }
            return true;
        };
        int probes=0; std::set<std::uint32_t> colors;
        for (int y=16;y<std::min(height-36,rawImage.height);y+=13) for (int x=12;x<std::min(width-180,rawImage.width);x+=17) {
            if (!outsideControls({x,y})) continue;
            const auto index=rawImage.pixels[static_cast<std::size_t>(y)*rawImage.width+x]; if (!index) continue;
            const auto expected=rgb(palette[index]); const auto actual=static_cast<const std::uint32_t*>(s.backBuffer.pixels)[static_cast<std::size_t>(y)*width+x]&0xffffffU;
            if (actual!=expected) return fail("original raw palette pixel differs at "+std::to_string(x)+','+std::to_string(y)
                +" expected="+std::to_string(expected)+" actual="+std::to_string(actual));
            colors.insert(expected); ++probes;
        }
        if (probes<80 || colors.size()<4) return fail("original page interior probes insufficient/solid");
        pixelsChecked+=probes;
        const auto& font=runtimeResultBitFont(s);
        if (font.bytes.empty() || !font.glyph(L'玩') || !font.glyph(L'家')) return fail("original GAME.FNT glyphs unavailable");
        if (s.playerCommunication.diplomacyControlSources.empty()) return fail("original control skin provenance missing");
        const auto pcxProbe=[&](const std::string& id,const RECT& destination,const std::filesystem::path& mix,
                bool returnStrip=false) {
            RuntimeShpFrameImage raw; std::vector<RuntimePaletteColor> colors; std::string source;
            if (!loadRuntimePcxImageFromMixArchive(pathString(mix),rawCache,id,raw,colors,source,error)
                || colors.size()!=256 || raw.width<=0 || raw.height<=0) return fail("native PCX unavailable "+id+": "+error);
            const int dw=destination.right-destination.left, dh=destination.bottom-destination.top;
            int checked=0;
            for (int y=0;y<dh;++y) for (int x=0;x<dw;++x) {
                // Return text is centered in the middle strip; sample its top
                // and bottom authored skin rows independently of the glyphs.
                if (returnStrip && y>2 && y<dh-3) continue;
                const auto index=raw.pixels[static_cast<std::size_t>((y*raw.height)/dh)*raw.width+(x*raw.width)/dw];
                const auto color=colors[index]; if (!index || (color.r>=240 && color.g<=32 && color.b>=240)) continue;
                const int px=destination.left+x, py=destination.top+y;
                if (px<0 || py<0 || px>=width || py>=height) return fail("native PCX control outside canvas "+id);
                const auto actual=static_cast<const std::uint32_t*>(s.backBuffer.pixels)[static_cast<std::size_t>(py)*width+px]&0xffffffU;
                if (actual!=rgb(color)) return fail("native PCX palette differs "+id+" at "+std::to_string(px)+','+std::to_string(py));
                ++checked;
            }
            if (checked<3) return fail("native PCX had no substantive opaque probes "+id);
            pixelsChecked+=checked; ++originalControls; return true;
        };
        const auto localmd=assetRoot/"ra2md"/"localmd.mix", localmix=assetRoot/"ra2"/"local.mix";
        const std::map<std::string,std::string> flagIds{{"Americans","usai"},{"Alliance","japi"},{"French","frai"},
            {"Germans","geri"},{"British","gbri"},{"Africans","djbi"},{"Arabs","arbi"},
            {"Confederation","lati"},{"Russians","rusi"},{"YuriCountry","yrii"}};
        for (const auto& [flagCountry,id]:flagIds) if (runtimeDiplomacyFlagId(flagCountry)!=id)
            return fail("native country flag table differs for "+flagCountry);
        for (std::size_t i=0;i<rows.size();++i) {
            const auto id=flagIds.at(rows[i].country);
            if (!pcxProbe(id,runtimeDiplomacyControlRect(client,1754+static_cast<int>(i)),id=="yrii"?localmd:localmix)) return false;
        }
        const std::size_t firstOther=1;
        if (!pcxProbe("cue_i",runtimeDiplomacyCheckboxRect(client,1742+static_cast<int>(firstOther)),localmd)) return false;
        std::size_t checkedChatRow=1; while (checkedChatRow<rows.size() && !rows[checkedChatRow].human) ++checkedChatRow;
        if (checkedChatRow==rows.size() || !pcxProbe("cce_i",runtimeDiplomacyCheckboxRect(client,chatIds[checkedChatRow-1]),localmd)) return false;
        const RECT returnRect=runtimeDiplomacyControlRect(client,1059);
        const LONG cap=std::max(1L,(returnRect.right-returnRect.left)*8/108);
        const auto returnSkin=[&](const std::string& prefix) {
            return pcxProbe(prefix+"li24",RECT{returnRect.left,returnRect.top,returnRect.left+cap,returnRect.bottom},localmd,true)
                && pcxProbe(prefix+"mi24",RECT{returnRect.left+cap,returnRect.top,returnRect.right-cap,returnRect.bottom},localmd,true)
                && pcxProbe(prefix+"ri24",RECT{returnRect.right-cap,returnRect.top,returnRect.right,returnRect.bottom},localmd,true);
        };
        if (!returnSkin("bue_")) return false;
        RuntimeResultBitFont rawFont;
        if (!rawFont.load(std::filesystem::path(s.config.resourceRootPath)/"GameMD"/"game.fnt")
            || digest(rawFont.bytes)!="9D7A6D5435B3F32973B7EC3913468ECB12D9CEC2EB081122083033E417A12D92")
            return fail("independent original GAME.FNT identity unavailable");
        const auto playerLabel=runtimeMenuLabel(s,"GUI:Player",L"");
        if (playerLabel.empty() || s.runtimeMenuCsfSource.empty()) return fail("native CSF player label unavailable");
        const auto* glyph=rawFont.glyph(playerLabel.front());
        if (!glyph) return fail("native CSF glyph not present in original bitmap font");
        const int baseLeft=static_cast<int>(std::lround(111*800.0/533.0));
        const int baseTop=static_cast<int>(std::lround(53*600.0/369.0));
        const int baseBottom=static_cast<int>(std::lround(63*600.0/369.0));
        const int glyphTop=baseTop+std::max(0,baseBottom-baseTop-static_cast<int>(rawFont.height))/2;
        int inks=0;
        for (unsigned y=0;y<rawFont.rows;++y) for (int x=0;x<glyph[0];++x) {
            if (!(glyph[1+y*rawFont.stride+x/8]&(0x80>>(x%8)))) continue;
            const int px=static_cast<int>(std::lround((baseLeft+x)*width/800.0));
            const int py=static_cast<int>(std::lround((glyphTop+static_cast<int>(y))*height/600.0));
            const int right=static_cast<int>(std::lround((baseLeft+x+1)*width/800.0));
            const int bottom=static_cast<int>(std::lround((glyphTop+static_cast<int>(y)+1)*height/600.0));
            if (right<=px || bottom<=py) continue;
            if ((static_cast<const std::uint32_t*>(s.backBuffer.pixels)[static_cast<std::size_t>(py)*width+px]&0xffffffU)!=0xffd866U)
                return fail("actual original bitmap glyph differs from native font bits");
            ++inks;
        }
        if (inks<6) return fail("native bitmap font pixel probes insufficient"); fontPixels+=inks;
        if (!writeRuntimeSmokeBmp(pathString(output/(label+".bmp")),s.backBuffer.pixels,width,height,error)) return fail(error);
        csv<<label<<",original-page,"<<width<<','<<height<<",0,,"<<probes<<','<<commands.size()<<'\n';

        stage="actual press / release and modal isolation";
        std::size_t aiRow=0,humanRow=0,lastSlotRow=0;
        for (std::size_t i=1;i<rows.size();++i) { if (!rows[i].human && !aiRow) aiRow=i; if (rows[i].human && !humanRow) humanRow=i; if (rows[i].slot==std::optional<std::size_t>(7)) lastSlotRow=i; }
        if (!aiRow || !humanRow || !lastSlotRow) return fail("AI/human/slot7 fixture roster incomplete");
        s.activeSidebarTool=RuntimeSidebarTool::Sell;
        const POINT mapPoint{40,40}; mouse(mapPoint);
        if (runtimeMouseCursorFrameForPoint(s,client,mapPoint)) return fail("map sell cursor leaked into native diplomacy page");
        SendMessageW(window,WM_SETCURSOR,reinterpret_cast<WPARAM>(window),MAKELPARAM(HTCLIENT,WM_MOUSEMOVE));
        if (GetCursor()!=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512))) return fail("modal OS cursor is not the native arrow");
        s.activeSidebarTool=RuntimeSidebarTool::None; ++localChecks;
        const POINT buttonPoint=center(returnRect); mouse(buttonPoint);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(buttonPoint.x,buttonPoint.y));
        if (!paint() || !returnSkin("bde_")) return false;
        const POINT cancelPoint{10,10};
        if (runtimeDiplomacyHitControl(s,client,cancelPoint)!=0) return fail("Return release-outside probe is over a control");
        mouse(cancelPoint);
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(cancelPoint.x,cancelPoint.y));
        if (!s.playerCommunication.diplomacyOpen || s.playerCommunication.diplomacyPressedControl!=0) return fail("Return release-outside activated page close");
        const int allyControl=1742+static_cast<int>(aiRow); const POINT allyPoint=center(runtimeDiplomacyControlRect(client,allyControl));
        const auto inputBefore=localAuthority();
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(allyPoint.x,allyPoint.y));
        mouse(allyPoint); SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(allyPoint.x,allyPoint.y));
        const POINT outside{width-4,height-4}; mouse(outside); SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(outside.x,outside.y));
        if (localAuthority()!=inputBefore || !commands.empty()) return fail("bare UP or release-outside changed alliance");
        click(1041+static_cast<int>(aiRow));
        if (localAuthority()!=inputBefore || !commands.empty()) return fail("player name acted as alliance checkbox");
        click(allyControl);
        if (commands.size()!=1 || !runtimeOwnersAreAllied(s,own,rows[aiRow].owner)) return fail("real AI alliance click failed");
        const auto* offer=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&commands.back().body);
        if (!offer || offer->kind!=RuntimePlayerCommunicationKind::Alliance || offer->text!=rows[aiRow].owner || offer->value!=1)
            return fail("display name/row was submitted instead of canonical ally owner");
        ++alliances; click(allyControl);
        if (commands.size()!=2 || runtimeOwnersAreAllied(s,own,rows[aiRow].owner)) return fail("real AI alliance removal failed");
        ++alliances;
        key(VK_UP); key(VK_DOWN);
        if (s.playerCommunication.diplomacyRow!=static_cast<int>(aiRow)) return fail("actual arrow keys did not retain roster focus");
        key('A'); const auto keyboardCount=commands.size(); const auto keyboardAuthority=localAuthority();
        key('A',true);
        if (!runtimeOwnersAreAllied(s,own,rows[aiRow].owner) || commands.size()!=keyboardCount
            || localAuthority()!=keyboardAuthority) return fail("repeated modal A changed alliance again");
        key('A'); if (runtimeOwnersAreAllied(s,own,rows[aiRow].owner)) return fail("actual modal A removal failed");
        alliances+=2;
        // Mode gates continue to use the actual click path, not a direct call to
        // the authority validator. No world/rules or ally tables are rescued.
        for (const std::string mode:{"FreeForAll","Cooperative"}) {
            s.config.gameMode=mode; const auto hash=localAuthority(); const auto count=commands.size(); click(allyControl);
            if (localAuthority()!=hash || commands.size()!=count) return fail("restricted mode alliance changed authority");
            ++localChecks;
        }
        s.config.gameMode="Battle"; s.campaignMode=true;
        { const auto hash=localAuthority(); const auto count=commands.size(); click(allyControl);
            if (localAuthority()!=hash || commands.size()!=count) return fail("campaign alliance changed authority"); ++localChecks; }
        s.campaignMode=false;
        // AI chat checkboxes are absent; no listener preference changes there.
        const auto listeners=s.playerCommunication.chatRecipients;
        click(chatIds[aiRow-1]);
        if (s.playerCommunication.chatRecipients!=listeners) return fail("AI row has an active chat receiver control");
        key(VK_ESCAPE); if (s.playerCommunication.diplomacyOpen || s.runtimeMenuOpen) return fail("Escape did not close diplomacy locally");
        key(VK_TAB); click(1059);
        if (s.playerCommunication.diplomacyOpen || s.runtimeMenuOpen) return fail("native Return did not close page");
        key(VK_TAB); key(VK_TAB); if (s.playerCommunication.diplomacyOpen) return fail("second real Tab did not close page");
        key(VK_TAB);
        SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(10,10));
        SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(10,10));
        if (s.playerCommunication.diplomacyOpen) return fail("real right click did not close page");
        // Enter the page during an actual map drag, then ensure neither the
        // modal key nor its deferred release leaves a capture or pan cursor.
        SendMessageW(window,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(30,30));
        SendMessageW(window,WM_MOUSEMOVE,MK_RBUTTON,MAKELPARAM(80,55));
        if (!s.rightDragActive || GetCapture()!=window) return fail("actual map right drag fixture did not capture");
        key(VK_TAB);
        if (!s.playerCommunication.diplomacyOpen || s.rightDragActive || GetCapture()==window)
            return fail("Tab retained map drag/capture behind diplomacy page");
        SendMessageW(window,WM_RBUTTONUP,0,MAKELPARAM(80,55));
        if (s.playerCommunication.diplomacyOpen) key(VK_ESCAPE);
        key(VK_TAB); mouse(buttonPoint);
        SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(buttonPoint.x,buttonPoint.y));
        if (s.playerCommunication.diplomacyPressedControl!=1059) return fail("actual Return press not tracked");
        key(VK_ESCAPE);
        if (s.playerCommunication.diplomacyOpen || s.playerCommunication.diplomacyPressedControl!=0 || GetCapture()==window)
            return fail("Escape retained pressed Return/capture");
        SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(buttonPoint.x,buttonPoint.y));
        key(VK_TAB); ++localChecks;

        if (country=="Americans" && width==800) {
            stage="network captured alliance and physical recipient slots";
            RuntimeMultiplayerRuntimeBridge bridge; s.multiplayerBridge=&bridge; s.offlineGameplayIntentRouting=false;
            commands.clear(); s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string& reason) {
                commands.push_back(command); reason.clear(); ++captures; return true;
            });
            const int humanControl=1742+static_cast<int>(humanRow);
            const auto networkBefore=localAuthority(); click(humanControl);
            if (commands.size()!=1 || localAuthority()!=networkBefore) return fail("network checkbox bypassed intent authority");
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s,s.economy.owner,commands.front(),error)
                || !applyRuntimeMultiplayerGameplayCommand(s,s.economy.owner,commands.front())) return fail("authenticated alliance receive: "+error);
            if (runtimeOwnersAreAllied(s,own,rows[humanRow].owner)
                || !s.playerCommands.alliances[{own,rows[humanRow].owner}]) return fail("human one-sided offer became mutual");
            auto reciprocal=commands.front(); auto* back=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&reciprocal.body);
            if (!back) return fail("captured alliance payload unavailable"); back->text=own;
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s,rows[humanRow].owner,reciprocal,error)
                || !applyRuntimeMultiplayerGameplayCommand(s,rows[humanRow].owner,reciprocal)
                || !runtimeOwnersAreAllied(s,own,rows[humanRow].owner)) return fail("reciprocal human authenticated alliance failed");
            ++alliances;
            const auto sendChat=[&](int chatKey,const std::wstring& text,std::uint8_t expected) {
                if (s.playerCommunication.diplomacyOpen) key(VK_ESCAPE);
                key(chatKey);
                if (!s.playerCommunication.chatOpen) return fail("actual chat key did not open editor");
                for (wchar_t c:text) SendMessageW(window,WM_CHAR,c,1);
                const auto count=commands.size(); key(VK_RETURN);
                if (s.playerCommunication.chatOpen || commands.size()!=count+1) return fail("actual Enter did not capture one chat");
                const auto* chat=std::get_if<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(&commands.back().body);
                if (!chat || chat->kind!=(chatKey==VK_HOME?RuntimePlayerCommunicationKind::ChatAllies:RuntimePlayerCommunicationKind::ChatAll)
                    || chat->value!=expected || chat->text!=runtimePlayerChatUtf8(text)) return fail("actual WM chat lost recipient mask/text/kind");
                return true;
            };
            if (!sendChat(VK_RETURN,L"原广播消息",0)) return false;
            key(VK_TAB);
            const auto authorityBeforeListeners=localAuthority(); const auto captureCount=commands.size();
            // Physical slot3 is self, but row0; slot7 remains bit7, never bit(row).
            for (std::size_t i=1;i<rows.size();++i) if (rows[i].human) click(chatIds[i-1]);
            if (!s.playerCommunication.chatRecipientsExplicit || runtimePlayerOutgoingChatMask(s)!=(1U<<3)
                || localAuthority()!=authorityBeforeListeners || commands.size()!=captureCount) return fail("all-off recipients lost self bit or changed authority");
            if (!sendChat(VK_RETURN,L"只给自己",std::uint8_t(1U<<3))) return false;
            key(VK_TAB); const auto selfOnlyCaptures=commands.size();
            click(chatIds[lastSlotRow-1]);
            const std::uint8_t expectedMask=static_cast<std::uint8_t>((1U<<3)|(1U<<7));
            if (runtimePlayerOutgoingChatMask(s)!=expectedMask || commands.size()!=selfOnlyCaptures) return fail("slot7 checkbox used row/name as bit");
            if (!sendChat(VK_RETURN,L"第八槽消息",expectedMask)) return false;
            const auto chatEnvelope=commands.back();
            const auto ownEconomy=s.economy.owner;
            const auto receive=[&](const std::string& viewer,const RuntimeMultiplayerGameplayCommand& envelope,bool visible) {
                s.economy.owner=viewer; const auto beforeLines=s.playerCommunication.lines.size();
                const auto incoming=std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(envelope.body);
                std::string beforeText; for (const auto& line:s.playerCommunication.lines) beforeText+=line.owner+'|'+line.text+';';
                if (!prevalidateRuntimeMultiplayerGameplayCommand(s,ownEconomy,envelope,error)
                    || !applyRuntimeMultiplayerGameplayCommand(s,ownEconomy,envelope)) return fail("authenticated masked chat receive: "+error);
                std::string afterText; for (const auto& line:s.playerCommunication.lines) afterText+=line.owner+'|'+line.text+';';
                if (visible ? (s.playerCommunication.lines.size()!=std::min<std::size_t>(8,beforeLines+1)
                        || s.playerCommunication.lines.back().text!=incoming.text || s.playerCommunication.lines.back().owner!=own)
                    : (afterText!=beforeText)) return fail("recipient privacy differs for "+viewer);
                ++chatDeliveries; return true;
            };
            if (!receive(runtimeSkirmishOwnerForSlot(7),chatEnvelope,true)
                || !receive(runtimeSkirmishOwnerForSlot(0),chatEnvelope,false)
                || !receive(ownEconomy,chatEnvelope,true)) return false;
            s.economy.owner=ownEconomy;
            // Receiver's own outgoing checks are irrelevant to incoming privacy.
            s.playerCommunication.chatRecipients=0;
            if (!receive(runtimeSkirmishOwnerForSlot(7),chatEnvelope,true)) return false;
            s.economy.owner=ownEconomy;
            auto selfOnly=chatEnvelope; std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(selfOnly.body).value=1U<<3;
            if (!receive(runtimeSkirmishOwnerForSlot(7),selfOnly,false) || !receive(ownEconomy,selfOnly,true)) return false;
            s.economy.owner=ownEconomy;
            auto legacy=chatEnvelope; std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(legacy.body).value=0;
            if (!receive(runtimeSkirmishOwnerForSlot(0),legacy,true)) return false;
            s.economy.owner=ownEconomy;
            auto alliesOnly=chatEnvelope; auto& alliedChat=std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(alliesOnly.body);
            alliedChat.kind=RuntimePlayerCommunicationKind::ChatAllies;
            alliedChat.value=static_cast<std::uint8_t>((1U<<3)|(1U<<*rows[humanRow].slot)|(1U<<7));
            if (!receive(rows[humanRow].owner,alliesOnly,true) || !receive(runtimeSkirmishOwnerForSlot(7),alliesOnly,false)) return false;
            s.economy.owner=ownEconomy;
            if (!sendChat(VK_HOME,L"盟友会谈",std::uint8_t(1U<<3))) return false;
            for (std::uint8_t invalid:{std::uint8_t(1U<<7),std::uint8_t((1U<<3)|(1U<<1))}) {
                auto malformed=chatEnvelope; std::get<RuntimeMultiplayerGameplayPlayerCommunicationCommand>(malformed.body).value=invalid;
                if (prevalidateRuntimeMultiplayerGameplayCommand(s,ownEconomy,malformed,error)) return fail("invalid sender/AI mask passed authentication");
                ++rejectedMasks; error.clear();
            }
            key(VK_TAB); const auto tauntAuthority=localAuthority(); const auto tauntCaptures=commands.size(); click(1840);
            if (s.playerCommunication.tauntsEnabled || localAuthority()!=tauntAuthority || commands.size()!=tauntCaptures)
                return fail("native taunts preference was authoritative or not toggled");
            RuntimeMultiplayerGameplayCommand taunt; RuntimeMultiplayerGameplayPlayerCommunicationCommand payload;
            payload.kind=RuntimePlayerCommunicationKind::Taunt; payload.value=1; taunt.body=payload;
            const auto lines=s.playerCommunication.lines.size(); const auto sounds=s.runtimeSoundEvents;
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s,rows[humanRow].owner,taunt,error)
                || !applyRuntimeMultiplayerGameplayCommand(s,rows[humanRow].owner,taunt)
                || s.playerCommunication.lines.size()!=lines || s.runtimeSoundEvents!=sounds) return fail("disabled taunt still presented");
            ++localChecks; s.multiplayerBridge=nullptr;
        }
        if (country=="YuriCountry" && width==640) {
            stage="actual resize with original native background and shared hit rectangles";
            SetWindowPos(window,nullptr,0,0,800,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            s.repaintRequested=true; InvalidateRect(window,nullptr,FALSE); SendMessageW(window,WM_PAINT,0,0); GdiFlush(); ++paints;
            if (!s.playerCommunication.diplomacyOpen || s.backBuffer.width!=800 || s.backBuffer.height!=600
                || s.playerCommunication.diplomacyBackgroundSource.find("bkgdmdy")==std::string::npos)
                return fail("resized page did not adopt actual MD artwork/client buffer");
            RuntimeShpFrameImage resized; std::string source;
            if (!loadRuntimeShpFrameFromMixArchive(pathString(archive),rawCache,"bkgdmdy.shp",0,resized,source,error)) return fail(error);
            int resizePixels=0;
            for (int y=16;y<60;y+=7) for (int x=12;x<80;x+=9) {
                const auto index=resized.pixels[static_cast<std::size_t>(y)*resized.width+x]; if (!index) continue;
                if ((static_cast<const std::uint32_t*>(s.backBuffer.pixels)[static_cast<std::size_t>(y)*800+x]&0xffffffU)!=rgb(palette[index]))
                    return fail("resized actual MD raw palette pixel changed");
                ++resizePixels;
            }
            if (resizePixels<20) return fail("resized native probes insufficient"); pixelsChecked+=resizePixels;
            const RECT resizedClient{0,0,800,600}; const POINT resizedButton=center(runtimeDiplomacyControlRect(resizedClient,1059));
            SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(resizedButton.x,resizedButton.y));
            SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(resizedButton.x,resizedButton.y));
            SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(resizedButton.x,resizedButton.y)); ++clicks;
            if (s.playerCommunication.diplomacyOpen) return fail("resized native Return had a stale hit rectangle");
            ++localChecks;
        }
        csv<<label<<",input-complete,"<<width<<','<<height<<",1059,,"<<probes<<','<<commands.size()<<'\n';
        return true;
    };
    const char* filter=std::getenv("CONCORD_DIPLOMACY_PAGE_CASE");
    for (const std::string country:{"Americans","Russians","YuriCountry"}) for (int width:{640,800}) {
        const int height=width==640 ? 480:600; label=country+'-'+std::to_string(width)+'x'+std::to_string(height);
        if (filter && *filter && label!=filter) continue;
        error.clear(); if (!run(country,width,height)) { failures.push_back(error); std::cerr<<error<<'\n'; }
        else { ++scenes; std::cout<<"Diplomacy "<<label<<" PASS\n"; }
    }
    if (!failures.empty()) { error=failures.front(); return false; }
    if (!scenes) return fail("case filter selected no page");
    error.clear(); std::cout<<"Diplomacy original pages: scenes="<<scenes<<" actualPaints="<<paints
        <<" rawPalettePixels="<<pixelsChecked<<" originalControls="<<originalControls<<" fontPixels="<<fontPixels
        <<" nativeLayouts="<<layoutChecks<<" WMkeys="<<keys<<" clicks="<<clicks
        <<" alliances="<<alliances<<" captured="<<captures<<" receiveChecks="<<chatDeliveries
        <<" invalidMasks="<<rejectedMasks<<" localChecks="<<localChecks
        <<"; original indexed assets, actual UI and authenticated capture/receive, not desktop scanout or live-room transport\n";
    return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
