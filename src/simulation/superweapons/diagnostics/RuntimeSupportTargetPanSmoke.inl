    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPPORT_TARGET_PAN_ONLY")) {
        const HINSTANCE instance=GetModuleHandleW(nullptr);
        const wchar_t* className=L"ConcordSupportTargetPanSmoke";
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc=runtimeWindowProc;
        windowClass.hInstance=instance;
        windowClass.lpszClassName=className;
        if (!RegisterClassW(&windowClass) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
            return fail("target pan input class");
        struct ClassCleanup {
            HINSTANCE instance; const wchar_t* name;
            ~ClassCleanup(){UnregisterClassW(name,instance);}
        } classCleanup{instance,className};
        std::vector<std::pair<std::string,bool>> abilities;
        for (const auto& id:expected) abilities.emplace_back(id,false);
        abilities.emplace_back("ChronoSphereSpecial",true);
        int cases=0, drags=0, interruptions=0, cancels=0, launches=0, images=0;
        for (const auto& [id,secondStage]:abilities) for (bool routed:{false,true})
            for (bool coasting:{false,true}) {
            const std::string scene=id+(secondStage?"-second":"-first")
                +(routed?"-routed":"-direct")+(coasting?"-coasting":"-immediate");
            auto storage=fixture(); auto& s=*storage;
            const auto bad=[&](const std::string& why){
                const auto& intent=s.multiplayerIntentRouter.superWeapon();
                return fail("target pan "+scene+": "+why+" ui="+s.interactionStatus
                    +" intent="+(intent?intent->superWeaponId:"none")
                    +" state="+std::to_string(static_cast<int>(s.superWeapons.stateOf("Commander",id))));
            };
            s.config.noCooldown=false; s.config.multiplayerActive=false;
            s.runtimeScrollCoastingEnabled=coasting; s.runtimeScrollRate=3;
            s.runtimeSoundsEnabled=false; s.visionEnabled=false;
            const int tank=addUnit(s,"MTNK",WorldObjectKind::Unit,90,90,"Commander");
            initializeRuntimeNavigation(s);
            s.selectedObjectIds={tank};
            RuntimeHumanHouseAuthority authority;
            authority.owner="Commander"; authority.canonicalOwner="commander";
            authority.country="Americans"; authority.localFacade=true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            int commands=0;
            if (routed) {
                initializeRuntimeOfflineGameplayRouting(s);
                s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command,std::string&) {
                    const auto* body=std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
                    if (!body) return false;
                    ++commands;
                    return applyRuntimeGameplaySuperWeaponCommand(s,s.economy.owner,*body);
                });
            }
            RuntimeSnapshotActiveStateScope activeScope(s);
            const RECT client{0,0,1024,768};
            const RECT viewport=runtimeMapViewport(client), sidebar=runtimeSidebarRect(client);
            centerRuntimeCameraOnCell(s,client,{93,93});
            ensureRuntimeCameraForViewport(s,viewport);
            HWND window=CreateWindowExW(0,className,L"Support target pan check",WS_POPUP,
                0,0,1024,768,nullptr,nullptr,instance,nullptr);
            if (!window) return bad("input window");
            struct WindowCleanup {
                HWND window;
                ~WindowCleanup() {
                    SetWindowLongPtrW(window,GWLP_USERDATA,0);
                    SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
                    if (GetCapture()==window) ReleaseCapture();
                    DestroyWindow(window);
                }
            } windowCleanup{window};
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
            const auto send=[&](UINT message,POINT p,WPARAM buttons=0) {
                SendMessageW(window,message,buttons,MAKELPARAM(p.x,p.y));
            };
            const auto left=[&](POINT p) {
                send(WM_LBUTTONDOWN,p,MK_LBUTTON); send(WM_LBUTTONUP,p);
            };
            const auto screen=[&](int x,int y) {
                return runtimeScreenPointForCell(s,viewport,{x,y},0);
            };
            auto context=runtimeSuperWeaponContext(s);
            if (!s.superWeapons.forceReady("Commander",id,&context)) return bad("ready");
            // forceReady updates the instance; the normal tick publishes its
            // sidebar snapshot, which routed UI selection checks separately.
            context.tick=++s.tick;
            s.superWeapons.tick(context);
            const auto* ready=s.superWeapons.findSidebarSnapshot("Commander",id);
            if (!ready || ready->state!=SuperWeaponRuntimeState::Ready) return bad("ready sidebar snapshot");
            s.activeBuildTab=RuntimeBuildKind::Defense;
            std::optional<POINT> sidebarPoint;
            for (int page=0;page<runtimeBuildPageCount(s,sidebar) && !sidebarPoint;++page) {
                runtimeSetActiveBuildPage(s,page);
                const int offset=static_cast<int>(runtimeVisibleBuildCatalogIndices(s,sidebar).size());
                const auto visible=runtimeVisibleSuperWeaponsOnBuildPage(s,sidebar);
                for (std::size_t n=0;n<visible.size();++n) if (visible[n]->superWeaponId==id) {
                    const RECT rect=runtimeBuildButtonRect(sidebar,offset+static_cast<int>(n));
                    sidebarPoint=POINT{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};
                    break;
                }
            }
            if (!sidebarPoint) return bad("sidebar icon");
            const auto selected=[&]() {
                const auto* active=runtimeActiveLocalSuperWeaponTargeting(s);
                return active && active->superWeaponId==id;
            };
            const auto select=[&]() {
                left(*sidebarPoint);
                if (!selected()) return false;
                if (secondStage) {
                    left(screen(90,90));
                    if (routed) {
                        const auto& intent=s.multiplayerIntentRouter.superWeapon();
                        if (!intent || intent->targets.size()!=1
                            || intent->targets.front().cell!=RuntimeMultiplayerGameplayCell{90,90}) return false;
                    } else if (s.superWeapons.stateOf("Commander",id)!=SuperWeaponRuntimeState::AwaitingSecondTarget
                        || s.superWeapons.findSidebarSnapshot("Commander",id)->firstTargetCell!=SuperWeaponCell{90,90}) return false;
                }
                return selected();
            };
            if (!select()) return bad("sidebar selection/first chrono target");
            const auto savedHash=s.superWeapons.stateHash();
            const auto savedIntent=s.multiplayerIntentRouter.superWeapon();
            const auto unchanged=[&]() {
                return selected() && s.superWeapons.stateHash()==savedHash
                    && s.multiplayerIntentRouter.superWeapon()==savedIntent && commands==0
                    && s.selectedObjectIds==std::vector<int>{tank};
            };
            POINT start=screen(96,95), end{start.x+80,start.y+40};
            send(WM_MOUSEMOVE,start);
            const auto initialCursor=runtimeMouseCursorFrameForPoint(s,client,start);
            if (!initialCursor || initialCursor->frameIndex==0
                || !loadRuntimeUiShpFrame(s,"",initialCursor->atlasId,initialCursor->frameIndex))
                return bad("native ability cursor");
            const bool takeImages=id=="ForceShieldSpecial" && !routed && !coasting;
            const auto image=[&](const std::string& stage) {
                if (!takeImages || root.empty()) return true;
                std::vector<std::uint32_t> pixels;
                if (!drawRuntimeStateOffscreenAtCell(s,{93,93},1024,768,error,"",
                        false,true,-1,&pixels,true,&s.camera)) return false;
                HDC dc=CreateCompatibleDC(nullptr);
                BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                info.bmiHeader.biWidth=1024; info.bmiHeader.biHeight=-768;
                info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
                void* bits=nullptr;
                HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
                if (!dc || !bitmap || !bits) {
                    if (bitmap) DeleteObject(bitmap);
                    if (dc) DeleteDC(dc);
                    return false;
                }
                const auto old=SelectObject(dc,bitmap);
                std::memcpy(bits,pixels.data(),pixels.size()*sizeof(std::uint32_t));
                drawRuntimeMouseCursorOverlay(dc,client,s); GdiFlush();
                const bool ok=writeRuntimeSmokeBmp(root+"/target-pan-"+stage+".bmp",bits,1024,768,error);
                SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ++images;
                return ok;
            };
            if (!image("before")) return bad("before image: "+error);
            const int cameraX=s.camera.worldOffsetX, cameraY=s.camera.worldOffsetY;
            send(WM_RBUTTONDOWN,start,MK_RBUTTON);
            const auto panCursor=runtimeMouseCursorFrameForPoint(s,client,start);
            if (!s.rightPointer.active || s.rightPointer.moved || GetCapture()!=window
                || !unchanged() || !panCursor || panCursor->frameIndex!=385)
                return bad("button-down canceled targeting or did not start pan");
            send(WM_MOUSEMOVE,end,MK_RBUTTON);
            if (coasting && !advanceRuntimeRightMousePan(s)) return bad("coasting did not pan");
            if (!s.rightPointer.moved || !unchanged()
                || (s.camera.worldOffsetX==cameraX && s.camera.worldOffsetY==cameraY))
                return bad("drag lost targeting or failed to move camera");
            if (!image("during")) return bad("during image: "+error);
            send(WM_RBUTTONUP,end);
            const auto afterCursor=runtimeMouseCursorFrameForPoint(s,client,end);
            if (s.rightPointer.active || s.rightPointer.moved || GetCapture()==window || !unchanged()
                || !afterCursor || afterCursor->frameIndex!=initialCursor->frameIndex
                || afterCursor->atlasId!=initialCursor->atlasId) return bad("release did not restore ability cursor/stage");
            if (!image("after")) return bad("after image: "+error);
            ++drags;
            // Moving away and returning, reaching a map edge, or releasing
            // outside the viewport are still drags, never cancel clicks.
            for (int variant=0;variant<3;++variant) {
                centerRuntimeCameraOnCell(s,client,{93,93});
                start=screen(96,95); end={start.x+80,start.y+40};
                if (variant==1) {
                    s.camera.worldOffsetX=100000; s.camera.worldOffsetY=100000;
                    clampRuntimeMapCamera(s,viewport);
                }
                send(WM_RBUTTONDOWN,start,MK_RBUTTON);
                send(WM_MOUSEMOVE,end,MK_RBUTTON);
                if (coasting) advanceRuntimeRightMousePan(s);
                if (variant==0) { end=start; send(WM_MOUSEMOVE,end,MK_RBUTTON); }
                if (variant==2) { end={-20,-20}; send(WM_MOUSEMOVE,end,MK_RBUTTON); }
                send(WM_RBUTTONUP,end);
                if (s.rightPointer.active || !unchanged()) return bad("return/edge/outside release");
                ++drags;
            }
            centerRuntimeCameraOnCell(s,client,{93,93});
            start=screen(96,95); end={start.x+80,start.y+40};
            for (int interruption=0;interruption<4;++interruption) {
                send(WM_RBUTTONDOWN,start,MK_RBUTTON);
                send(WM_MOUSEMOVE,end,MK_RBUTTON);
                if (interruption==0) ReleaseCapture();
                else if (interruption==1) SendMessageW(window,WM_KILLFOCUS,0,0);
                else if (interruption==2) SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
                else SendMessageW(window,WM_CANCELMODE,0,0);
                if (s.rightPointer.active || s.rightPointer.moved || GetCapture()==window || !unchanged())
                    return bad("capture/focus cancellation lost selection or kept panning");
                send(WM_RBUTTONUP,end);
                if (!unchanged()) return bad("stray release canceled retained ability");
                ++interruptions;
            }
            centerRuntimeCameraOnCell(s,client,{93,93}); start=screen(96,95);
            send(WM_RBUTTONDOWN,start,MK_RBUTTON);
            if (!unchanged()) return bad("plain click canceled before release");
            send(WM_MOUSEMOVE,{start.x+3,start.y},MK_RBUTTON);
            send(WM_RBUTTONUP,{start.x+3,start.y});
            if (selected() || commands!=0 || s.selectedObjectIds!=std::vector<int>{tank}
                || s.superWeapons.stateOf("Commander",id)!=SuperWeaponRuntimeState::Ready)
                return bad("small-jitter click did not cancel only the ability");
            ++cancels;
            if (!select()) return bad("reselect for sidebar cancel");
            send(WM_RBUTTONDOWN,*sidebarPoint,MK_RBUTTON);
            send(WM_RBUTTONUP,*sidebarPoint);
            if (selected() || s.rightPointer.active || commands!=0 || s.selectedObjectIds!=std::vector<int>{tank})
                return bad("sidebar cancel caused a map gesture or cleared units");
            ++cancels;
            if (!select()) return bad("reselect for launch");
            if (id=="ChronoSphereSpecial" && !secondStage) left(screen(90,90));
            left(screen(96,95));
            if (selected() || s.superWeapons.stateOf("Commander",id)!=SuperWeaponRuntimeState::Launching
                || commands!=(routed?1:0)) return bad("ability did not launch exactly once");
            ++launches; ++cases;
            std::cout << "Support target pan: " << scene << " sidebar/drag/cursor/stage/cancel/launch=ok\n";
        }
        std::cout << "Support target pan: passed cases=" << cases << " drags=" << drags
            << " interruptions=" << interruptions << " cancelClicks=" << cancels
            << " launches=" << launches << " images=" << images << "\n";
        return true;
    }
