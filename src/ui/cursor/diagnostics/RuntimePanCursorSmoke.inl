bool runRuntimePanCursorSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Pan cursor: "+why;return false;};
    const char* output=std::getenv("CONCORD_PAN_CURSOR_OUTPUT");
    const std::string root=output?output:"";
    if(!root.empty())std::filesystem::create_directories(root);
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    const wchar_t* className=L"ConcordPanCursorSmoke";
    WNDCLASSW wc{};wc.lpfnWndProc=runtimeWindowProc;wc.hInstance=instance;wc.lpszClassName=className;
    if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return fail("window class");
    struct ClassCleanup { HINSTANCE instance;const wchar_t* name;
        ~ClassCleanup(){UnregisterClassW(name,instance);} } classCleanup{instance,className};
    constexpr int width=960,height=640;
    constexpr std::uint32_t background=0x00283840U;
    const RECT client{0,0,width,height},viewport=runtimeMapViewport(client);
    int drags=0,pixelsCompared=0,boundaries=0,interruptions=0,captures=0;
    for(bool coasting:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        seedRuntimeLoadScratch(s,live,false);s.world=live.world;
        s.config.computerAiEnabled=false;s.config.multiplayerActive=false;s.config.fixedTickHz=30;
        s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimePreferencePersistenceEnabled=false;s.visionEnabled=false;
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.economy.owner="Americans";s.config.playerCountry="Americans";s.config.playerSlots.clear();
        s.world.accessObjects().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear();s.terrainInfoByCell.clear();s.terrainByCell.clear();
        s.mapFacts={};s.mapFactsLoaded=false;
        for(int y=40;y<=100;++y)for(int x=40;x<=100;++x) {
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=0;
            s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s);RuntimeSnapshotActiveStateScope active(s);
        s.runtimeScrollCoastingEnabled=coasting;s.runtimeScrollRate=3;
        HWND window=CreateWindowExW(0,className,L"Pan cursor check",WS_POPUP,0,0,width,height,
            nullptr,nullptr,instance,nullptr);
        if(!window)return fail("input window");
        struct WindowCleanup { HWND window;
            ~WindowCleanup(){SetWindowLongPtrW(window,GWLP_USERDATA,0);
                SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(DefWindowProcW));
                if(GetCapture()==window)ReleaseCapture();DestroyWindow(window);} } windowCleanup{window};
        SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        const auto send=[&](UINT message,POINT point,WPARAM buttons=0){
            SendMessageW(window,message,buttons,MAKELPARAM(point.x,point.y));};
        const POINT start{(viewport.left+viewport.right)/2,(viewport.top+viewport.bottom)/2};
        const auto begin=[&]{centerRuntimeCameraOnCell(s,client,{70,70});
            send(WM_MOUSEMOVE,start);send(WM_RBUTTONDOWN,start,MK_RBUTTON);};
        struct Surface { RuntimeWindowState& s;HDC screen=GetDC(nullptr);
            ~Surface(){releaseRuntimeBackBuffer(s.backBuffer);if(screen)ReleaseDC(nullptr,screen);} } surface{s};
        if(!surface.screen||!ensureRuntimeBackBuffer(s.backBuffer,surface.screen,width,height))return fail("surface");
        std::vector<RuntimePaletteColor> palette;std::string path;
        const auto cacheMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"cache.mix");
        const auto mouseMix=pathString(std::filesystem::path(runtimeAssetRootPath(s.config))/"ra2"/"conquer.mix");
        if(!loadRuntimePaletteFromMixArchive(cacheMix,s.renderCache,"mousepal",palette,path,error))return false;
        std::set<int> captured;
        const auto check=[&](int frame,bool compare=false) {
            const auto cursor=runtimeMouseCursorFrameForPoint(s,client,s.mouseClientPoint);
            if(!cursor||cursor->frameIndex!=frame||cursor->atlasId!="mouse")
                return fail("frame expected="+std::to_string(frame)+" actual="+(cursor?std::to_string(cursor->frameIndex):"none"));
            if(!compare)return true;
            if(cursor->offsetX!=27||cursor->offsetY!=21)return fail("center hotspot");
            GdiFlush();auto* pixels=static_cast<std::uint32_t*>(s.backBuffer.pixels);
            std::fill_n(pixels,width*height,background);
            drawRuntimeMouseCursorOverlay(s.backBuffer.memoryDc,client,s);GdiFlush();
            RuntimeShpFrameImage sprite;
            if(!loadRuntimeShpFrameFromMixArchive(mouseMix,s.renderCache,"mouse",frame,sprite,path,error))return false;
            std::vector<std::uint32_t> expected(width*height,background);
            for(int y=0;y<sprite.height;++y)for(int x=0;x<sprite.width;++x) {
                const auto index=sprite.pixels[y*sprite.width+x];if(!index)continue;
                const int dx=s.mouseClientPoint.x-27+sprite.offsetX+x,dy=s.mouseClientPoint.y-21+sprite.offsetY+y;
                if(dx<0||dx>=width||dy<0||dy>=height)continue;
                const auto c=palette[index];expected[dy*width+dx]=(c.r<<16)|(c.g<<8)|c.b;
            }
            for(int i=0;i<width*height;++i)if((pixels[i]&0xffffffU)!=expected[i])return fail("native pixels "+std::to_string(frame));
            ++pixelsCompared;
            if(!coasting&&!root.empty()&&captured.insert(frame).second) {
                if(!writeRuntimeSmokeBmp(root+"/pan-"+std::to_string(frame)+".bmp",pixels,width,height,error))return false;
                ++captures;
            }
            return true;
        };
        const auto worldHash=s.world.deterministicSimulationStateHash();
        constexpr POINT directions[]={{0,-80},{80,-80},{80,0},{80,80},{0,80},{-80,80},{-80,0},{-80,-80}};
        for(int n=0;n<8;++n) {
            begin();if(!s.rightDragActive||s.rightDragMoved||GetCapture()!=window||!check(385,true))return fail("neutral/down "+error);
            const int cx=s.camera.worldOffsetX,cy=s.camera.worldOffsetY;
            const POINT end{start.x+directions[n].x,start.y+directions[n].y};
            send(WM_MOUSEMOVE,end,MK_RBUTTON);
            if(coasting&&!advanceRuntimeRightMousePan(s))return fail("coasting movement");
            if(!s.rightDragMoved||(cx==s.camera.worldOffsetX&&cy==s.camera.worldOffsetY)||!check(386+n,true))return fail("direction/movement "+error);
            send(WM_RBUTTONUP,end);
            if(s.rightDragActive||GetCapture()==window||!check(0))return fail("release "+error);
            ++drags;
        }
        begin();send(WM_MOUSEMOVE,{start.x+3,start.y},MK_RBUTTON);
        if(s.rightDragMoved||!check(385,true))return fail("jitter "+error);
        send(WM_RBUTTONUP,{start.x+3,start.y});if(!check(0))return false;++boundaries;
        begin();send(WM_MOUSEMOVE,{start.x+80,start.y},MK_RBUTTON);
        send(WM_MOUSEMOVE,{start.x+40,start.y},MK_RBUTTON);
        // Direct panning reverses at once; coasting continues toward the
        // cursor's displacement from the held origin until it crosses it.
        if(!check(coasting?388:392,true))return fail("reverse "+error);++boundaries;
        send(WM_MOUSEMOVE,start,MK_RBUTTON);
        if(!check(coasting?385:392,true))return fail("return to origin "+error);++boundaries;
        send(WM_MOUSEMOVE,{start.x-40,start.y},MK_RBUTTON);
        if(!check(392,true))return fail("cross origin "+error);
        send(WM_RBUTTONUP,{start.x-40,start.y});++boundaries;
        begin();if(!check(385))return fail("new gesture retained direction");
        send(WM_MOUSEMOVE,{start.x+80,start.y},MK_RBUTTON);
        const auto camera=s.camera;s.camera.worldOffsetX=100000;s.camera.worldOffsetY=100000;
        clampRuntimeMapCamera(s,viewport);
        if(!check(388,true))return fail("edge lost drag direction");s.camera=camera;
        send(WM_RBUTTONUP,{start.x+80,start.y});++boundaries;
        for(int mode=0;mode<4;++mode) {
            begin();send(WM_MOUSEMOVE,{start.x+80,start.y},MK_RBUTTON);
            if(mode==0)ReleaseCapture();
            else if(mode==1)SendMessageW(window,WM_KILLFOCUS,0,0);
            else if(mode==2)SendMessageW(window,WM_ACTIVATEAPP,FALSE,0);
            else SendMessageW(window,WM_CANCELMODE,0,0);
            send(WM_MOUSEMOVE,start);
            if(s.rightDragActive||GetCapture()==window||!check(0))return fail("interrupted gesture "+error);
            send(WM_RBUTTONUP,start);++interruptions;
        }
        begin();send(WM_MOUSEMOVE,{-20,-20},MK_RBUTTON);
        if(runtimeMouseCursorFrameForPoint(s,client,{-20,-20}))return fail("cursor outside viewport");
        send(WM_RBUTTONUP,{-20,-20});send(WM_MOUSEMOVE,start);
        if(s.rightDragActive||GetCapture()==window||!check(0))return fail("outside release "+error);++boundaries;
        if(s.world.deterministicSimulationStateHash()!=worldHash)return fail("camera UI mutated simulation");
    }
    std::cout<<"Pan cursor passed: drags="<<drags<<" nativePixelComparisons="<<pixelsCompared
        <<" boundaries="<<boundaries<<" interruptions="<<interruptions<<" captures="<<captures<<'\n';
    return true;
}
