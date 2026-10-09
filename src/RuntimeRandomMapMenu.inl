// Included in the original skirmish shell, after its drawing helpers.
std::uint32_t nextRandomMapSeed() {
    static std::uint32_t serial = 0;
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return static_cast<std::uint32_t>(now.QuadPart) ^ static_cast<std::uint32_t>(GetTickCount64()) ^ ++serial;
}

std::string randomNameUtf8(const std::wstring& name) {
    if (name.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8,0,name.data(),static_cast<int>(name.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,name.data(),static_cast<int>(name.size()),result.data(),size,nullptr,nullptr);
    return result;
}

LRESULT CALLBACK randomMapNameWindowProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
    const auto original=reinterpret_cast<WNDPROC>(GetPropW(window,L"ConcordRandomMapNameProc"));
    if (message==WM_KEYDOWN && (wParam==VK_ESCAPE || wParam==VK_RETURN)) {
        SendMessageW(GetParent(window),WM_KEYDOWN,wParam,lParam); return 0;
    }
    const LRESULT result=original?CallWindowProcW(original,window,message,wParam,lParam):DefWindowProcW(window,message,wParam,lParam);
    if (message==WM_NCDESTROY) RemovePropW(window,L"ConcordRandomMapNameProc");
    return result;
}

void updateRandomMapNameWindow(MenuState& state) {
    const bool visible = state.page == SkirmishPage::RandomMap && state.phase == ShellMenuPhase::Idle;
    if (!visible) {
        if (state.randomNameWindow) {
            if (GetFocus()==state.randomNameWindow && state.hwnd && IsWindow(state.hwnd)) SetFocus(state.hwnd);
            ShowWindow(state.randomNameWindow,SW_HIDE);
        }
        return;
    }
    if (!state.hwnd || !IsWindow(state.hwnd)) return;
    if (!state.randomNameWindow || !IsWindow(state.randomNameWindow)) {
        state.randomNameWindow=CreateWindowExW(0,L"EDIT",state.randomName.c_str(),
            WS_CHILD|WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,0,0,10,10,state.hwnd,
            reinterpret_cast<HMENU>(47014),GetModuleHandleW(nullptr),nullptr);
        if (!state.randomNameWindow) return;
        const LONG_PTR original=GetWindowLongPtrW(state.randomNameWindow,GWLP_WNDPROC);
        SetPropW(state.randomNameWindow,L"ConcordRandomMapNameProc",reinterpret_cast<HANDLE>(original));
        SetWindowLongPtrW(state.randomNameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(randomMapNameWindowProc));
        SendMessageW(state.randomNameWindow,EM_SETLIMITTEXT,60,0);
        SendMessageW(state.randomNameWindow,WM_SETFONT,reinterpret_cast<WPARAM>(state.normalFont),FALSE);
    }
    SetWindowPos(state.randomNameWindow,nullptr,
        state.viewLeft+138*state.viewWidth/kBaseWidth,state.viewTop+508*state.viewHeight/kBaseHeight,
        368*state.viewWidth/kBaseWidth,25*state.viewHeight/kBaseHeight,SWP_NOZORDER|SWP_NOACTIVATE);
    EnableWindow(state.randomNameWindow,!state.pendingRandomMap.valid());
    ShowWindow(state.randomNameWindow,SW_SHOW);
}

void setRandomMapName(MenuState& state, const std::string& text) {
    state.randomName=utf8ToWide(text);
    if (state.randomNameWindow && IsWindow(state.randomNameWindow)) SetWindowTextW(state.randomNameWindow,state.randomName.c_str());
}

void requestRandomMap(MenuState& state, bool surprise) {
    if (state.pendingRandomMap.valid()) return;
    state.randomOptions.seed=nextRandomMapSeed();
    if (surprise) {
        std::uint32_t choice=state.randomOptions.seed;
        const auto choose=[&](int count) { choice=choice*1664525U+1013904223U; return static_cast<int>((choice>>8)%count); };
        state.randomOptions.environment=choose(5); state.randomOptions.theater=choose(2);
        state.randomOptions.size=choose(4); state.randomOptions.resources=choose(4);
        state.randomOptions.timeOfDay=choose(4);
    }
    state.randomOptions.name=randomNameUtf8(state.randomName);
    std::string error;
    if (!runtimeValidateRandomMapOptions(state.randomOptions,error)) { state.status=utf8ToWide(error); invalidate(state); return; }
    const auto config=state.config; const auto options=state.randomOptions;
    state.pendingRandomMap=std::async(std::launch::async,[config,options]() {
        RandomMapJob job;
        job.success=runtimeGenerateRandomMap(config,options,job.result,job.error);
        return job;
    });
    state.status=label(state,{"GUI:WorkingPleaseWait"},L"正在生成地图……");
    invalidate(state);
}

bool pollRandomMap(MenuState& state) {
    if (!state.pendingRandomMap.valid() || state.pendingRandomMap.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    try {
        auto job=state.pendingRandomMap.get();
        if (job.success) {
            state.randomResult=std::move(job.result); state.randomDirty=false;
            state.status=L"地图已生成，可以预览、保存或使用。";
        } else state.status=utf8ToWide(job.error);
    } catch (const std::exception& error) { state.status=L"地图生成失败："+utf8ToWide(error.what()); }
    state.backDirty=true; return true;
}

int* randomOption(MenuState& state, int index) {
    switch (index) {
    case 0:return &state.randomOptions.players;
    case 1:return &state.randomOptions.size;
    case 2:return &state.randomOptions.environment;
    case 3:return &state.randomOptions.theater;
    case 4:return &state.randomOptions.timeOfDay;
    case 5:return &state.randomOptions.resources;
    default:return nullptr;
    }
}

std::wstring randomOptionValue(MenuState& state, int index) {
    const auto& o=state.randomOptions;
    if (index==0) return std::to_wstring(o.players);
    if (index==1) {
        const std::array<const char*,4> keys{"TXT_MAPSIZE_SMALL","TXT_MAPSIZE_MEDIUM","TXT_MAPSIZE_LARGE","TXT_MAPSIZE_VERY_LARGE"};
        const int size=runtimeRandomMapDimension(o);
        return label(state,{keys[o.size]},L"地图大小")+L"  "+std::to_wstring(size)+L" × "+std::to_wstring(size);
    }
    if (index==2) {
        const std::array<const char*,5> keys{"TXT_MAP_ARCHIPELAGO","TXT_MAP_CONTINENT","TXT_MAP_TEAM_CONTINENTS","TXT_MAP_INLAND","TXT_MAP_MOUNTAINOUS"};
        return label(state,{keys[o.environment]},L"地形");
    }
    if (index==3) return label(state,{o.theater==0?"TXT_THEATER_TEMPERATE":"TXT_THEATER_SNOW"},L"气候");
    if (index==4) {
        const std::array<const char*,4> keys{"TXT_TIME_MORNING","TXT_TIME_AFTERNOON","TXT_TIME_DUSK","TXT_TIME_NIGHT"};
        return label(state,{keys[o.timeOfDay]},L"时段");
    }
    const std::array<const char*,4> keys{"TXT_RESOURCE_LOW","TXT_RESOURCE_MODERATE","TXT_RESOURCE_HIGH","TXT_RESOURCE_EXTREME"};
    return label(state,{keys[o.resources]},L"矿量");
}

void randomButton(MenuState& state, ShellCanvas& canvas, HitKind kind, int y,
    std::initializer_list<std::string> keys, const wchar_t* fallback, bool enabled=true, bool bottom=false) {
    drawShellButton(state,canvas,kind,y,label(state,keys,fallback),bottom);
    if (!enabled) {
        state.hits.erase(std::remove_if(state.hits.begin(),state.hits.end(),[&](const HitRegion& h){return h.target.kind==kind;}),state.hits.end());
        shellDarken(canvas,RECT{644,y+8,790,y+35},130);
    }
}

void drawGeneratedMapPreview(MenuState& state, ShellCanvas& canvas, const RECT& rect) {
    shellFill(canvas,rect,RGB(2,2,2)); shellFrame(canvas,rect,RGB(117,17,11));
    if (!state.randomResult) return;
    const auto& map=*state.randomResult; const auto& preview=map.preview;
    const double scale=std::min(static_cast<double>(rect.right-rect.left-4)/preview.width,
        static_cast<double>(rect.bottom-rect.top-4)/preview.height);
    const int width=static_cast<int>(preview.width*scale),height=static_cast<int>(preview.height*scale);
    const int cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2;
    RECT dest{cx-width/2,cy-height/2,cx+(width+1)/2,cy+(height+1)/2};
    shellBlitScaled(canvas,preview.pixels.data(),preview.width,preview.height,dest);
    for (std::size_t i=0;i<map.starts.size();++i) {
        const auto& p=map.starts[i];
        const int x=dest.left+(p.x-p.y+map.width)*width/(map.width*2);
        const int y=dest.top+(p.x+p.y-map.width)*height/(map.height*2);
        shellText(state,canvas,std::to_wstring(i+1),RECT{x-8,y-9,x+8,y+9},true,RGB(255,232,0));
    }
}

void drawRandomMapPage(MenuState& state, ShellCanvas& canvas) {
    shellText(state,canvas,label(state,{"GUI:CreateRandomMap"},L"建立随机地图"),RECT{86,85,506,112},true);
    const bool busy=state.pendingRandomMap.valid();
    const std::array<const char*,6> labels{"GUI:Players","GUI:MapSize","GUI:Environment","GUI:Theater","GUI:TimeOfDay","GUI:Resources"};
    for (int i=0;i<6;++i) {
        const int y=121+i*31;
        shellText(state,canvas,label(state,{labels[i]},L"设置"),RECT{87,y,229,y+27},false);
        shellFill(canvas,RECT{238,y,506,y+27},RGB(5,4,4)); shellFrame(canvas,RECT{238,y,506,y+27},RGB(117,17,11));
        shellText(state,canvas,L"<",RECT{238,y,269,y+27},true);
        shellText(state,canvas,randomOptionValue(state,i),RECT{270,y,474,y+27},true);
        shellText(state,canvas,L">",RECT{475,y,506,y+27},true);
        if (!busy) {
            addHit(state,RECT{238,y,269,y+27},HitKind::RandomOption,i*2);
            addHit(state,RECT{270,y,506,y+27},HitKind::RandomOption,i*2+1);
        }
    }
    const RECT previewRect{86,318,506,500};
    drawGeneratedMapPreview(state,canvas,previewRect);
    drawGeneratedMapPreview(state,canvas,RECT{644,40,790,152});
    if (!state.randomResult) shellText(state,canvas,L"选择参数后，点击“产生地图”。",previewRect,true);
    else addHit(state,previewRect,HitKind::RandomPreview);
    shellText(state,canvas,label(state,{"GUI:Name"},L"名称"),RECT{87,508,136,533},false);
    shellFill(canvas,RECT{138,508,506,533},RGB(5,4,4)); shellFrame(canvas,RECT{138,508,506,533},RGB(117,17,11));
    shellText(state,canvas,state.randomName,RECT{143,508,501,533},false);
    randomButton(state,canvas,HitKind::RandomGenerate,157,{"GUI:GenerateMap"},L"产生地图",!busy);
    randomButton(state,canvas,HitKind::RandomSurprise,199,{"GUI:SurpriseMe"},L"替我挑选",!busy);
    const bool ready=state.randomResult.has_value() && !busy && !state.randomDirty;
    randomButton(state,canvas,HitKind::RandomUse,241,{"GUI:UseMap"},L"使用地图",ready);
    randomButton(state,canvas,HitKind::RandomSave,283,{"GUI:SaveMap"},L"保存地图",ready);
    randomButton(state,canvas,HitKind::RandomLoad,325,{"GUI:LoadMap"},L"载入地图",!busy);
    randomButton(state,canvas,HitKind::RandomPreview,367,{"GUI:PreviewMap"},L"预览地图",ready);
    randomButton(state,canvas,HitKind::RandomCancel,kShellBottomButtonTop,{"GUI:Cancel"},L"取消",true,true);
}

void drawRandomMapPreviewPage(MenuState& state, ShellCanvas& canvas) {
    shellText(state,canvas,label(state,{"GUI:PreviewMap"},L"预览地图"),RECT{86,85,586,112},true);
    drawGeneratedMapPreview(state,canvas,RECT{70,120,600,540});
    drawGeneratedMapPreview(state,canvas,RECT{644,40,790,152});
    randomButton(state,canvas,HitKind::RandomUse,241,{"GUI:UseMap"},L"使用地图",state.randomResult.has_value()&&!state.randomDirty);
    randomButton(state,canvas,HitKind::RandomCancel,kShellBottomButtonTop,{"GUI:Back"},L"返回",true,true);
}

void scanSavedRandomMaps(MenuState& state) {
    state.savedRandomMaps=scanMaps(runtimeSavedRandomMapDirectory(state.config),{},state.csf);
    state.savedRandomMaps.erase(std::remove_if(state.savedRandomMaps.begin(),state.savedRandomMaps.end(),[](const MapInfo& map) {
        RuntimeRandomMapOptions options; return !runtimeRandomMapOptionsFromFileName(map.path.filename().string(),options);
    }),state.savedRandomMaps.end());
    state.selectedSavedRandomMap=std::clamp(state.selectedSavedRandomMap,0,std::max(0,static_cast<int>(state.savedRandomMaps.size())-1));
    state.randomSavedScroll=std::clamp(state.randomSavedScroll,0,std::max(0,static_cast<int>(state.savedRandomMaps.size())-11));
}

void drawSavedRandomMapPage(MenuState& state, ShellCanvas& canvas) {
    shellText(state,canvas,label(state,{"GUI:LoadMap"},L"载入随机地图"),RECT{86,85,506,112},true);
    shellFill(canvas,RECT{86,121,506,500},RGB(3,3,3)); shellFrame(canvas,RECT{86,121,506,500},RGB(117,17,11));
    for (int i=0;i<11;++i) {
        const int index=state.randomSavedScroll+i;
        if (index>=static_cast<int>(state.savedRandomMaps.size())) break;
        RECT row{88,125+i*31,504,156+i*31};
        if (index==state.selectedSavedRandomMap) shellFill(canvas,row,RGB(74,7,5));
        shellText(state,canvas,state.savedRandomMaps[index].displayName,RECT{96,row.top,497,row.bottom},false);
        addHit(state,row,HitKind::RandomSaved,index);
    }
    if (state.savedRandomMaps.empty()) shellText(state,canvas,L"还没有保存的随机地图。",RECT{87,230,505,300},true);
    shellText(state,canvas,L"<",RECT{87,473,123,498},true); addHit(state,RECT{87,473,123,498},HitKind::RandomSavedScroll,-1);
    shellText(state,canvas,L">",RECT{470,473,505,498},true); addHit(state,RECT{470,473,505,498},HitKind::RandomSavedScroll,1);
    randomButton(state,canvas,HitKind::RandomLoadSelected,241,{"GUI:LoadMap"},L"载入地图",!state.savedRandomMaps.empty());
    randomButton(state,canvas,HitKind::RandomDelete,283,{"GUI:DeleteMap"},L"删除地图",!state.savedRandomMaps.empty());
    randomButton(state,canvas,HitKind::RandomCancel,kShellBottomButtonTop,{"GUI:Cancel"},L"取消",true,true);
}

int insertRandomMap(MenuState& state,const fs::path& path) {
    for (int i=0;i<static_cast<int>(state.maps.size());++i) if (state.maps[i].path.filename()==path.filename()) {
        state.maps[i].path=path; return i;
    }
    MapInfo map; map.path=path; if (!readMapMetadata(path,map)) return -1;
    map.filters={"standard"};
    state.maps.push_back(std::move(map)); return static_cast<int>(state.maps.size())-1;
}

bool activateRandomMapTarget(MenuState& state,HitTarget target,int direction) {
    const bool busy=state.pendingRandomMap.valid();
    if (target.kind==HitKind::CreateRandomMap) {
        if (state.selectedMode<0 || state.selectedMode>=static_cast<int>(state.modes.size()) || !state.modes[state.selectedMode].randomMapsAllowed) return true;
        state.page=SkirmishPage::RandomMap; state.status=L"选择参数，生成自己的战场。";
    } else if (target.kind==HitKind::RandomCancel) {
        state.page=(state.page==SkirmishPage::RandomMapLoad || state.page==SkirmishPage::RandomMapPreview)?SkirmishPage::RandomMap:SkirmishPage::MapSelection;
        state.status=label(state,{"GUI:Ready"},L"就绪");
    } else if (target.kind==HitKind::RandomPreview && !busy) {
        if (state.randomResult) state.page=SkirmishPage::RandomMapPreview;
    } else if (target.kind==HitKind::RandomOption && !busy) {
        const int i=target.index/2; int* value=randomOption(state,i);
        if (!value) return true;
        const int low=i==0?2:0,high=i==0?8:i==2?4:i==3?1:3;
        const int step=(target.index%2==0?-1:1)*(direction<0?-1:1);
        *value=low+(*value-low+step+(high-low+1))%(high-low+1);
        state.randomDirty=true; state.status=L"参数已更改，请重新产生地图。";
    } else if ((target.kind==HitKind::RandomGenerate || target.kind==HitKind::RandomSurprise) && !busy) {
        requestRandomMap(state,target.kind==HitKind::RandomSurprise);
    } else if ((target.kind==HitKind::RandomSave || target.kind==HitKind::RandomUse) && !busy) {
        if (!state.randomResult || state.randomDirty) return true;
        std::string error; fs::path path;
        const bool save=target.kind==HitKind::RandomSave;
        if (!runtimeRenameRandomMap(*state.randomResult,randomNameUtf8(state.randomName),error)
            || !runtimeStoreRandomMap(*state.randomResult,save?runtimeSavedRandomMapDirectory(state.config):runtimeRandomMapCacheDirectory(state.config),path,error)) {
            state.status=utf8ToWide(error);
        } else {
            const int index=insertRandomMap(state,path);
            if (index<0) state.status=L"无法载入刚生成的地图。";
            else if (save) state.status=L"随机地图已保存，可从“载入地图”再次使用。";
            else {
                // Cooperative is a fixed mission sequence, so generated maps
                // return to Battle only when opened from that mission mode.
                if (state.selectedMode>=0 && isUnavailableCooperativeMode(state.modes[state.selectedMode])) {
                    const auto battle=std::find_if(state.modes.begin(),state.modes.end(),[](const auto& mode){return equalsAsciiNoCase(mode.key,"Battle");});
                    if (battle!=state.modes.end()) selectSkirmishMode(state,static_cast<int>(battle-state.modes.begin()));
                }
                rebuildFilteredMaps(state,false); state.selectedMap=index; applyMapSlotLimit(state);
                syncSelection(state); state.page=SkirmishPage::Lobby; state.status=label(state,{"GUI:Ready"},L"就绪");
            }
        }
    } else if (target.kind==HitKind::RandomLoad && !busy) {
        scanSavedRandomMaps(state); state.page=SkirmishPage::RandomMapLoad;
    } else if (target.kind==HitKind::RandomSaved) {
        state.selectedSavedRandomMap=target.index;
    } else if (target.kind==HitKind::RandomSavedScroll) {
        state.randomSavedScroll=std::clamp(state.randomSavedScroll+target.index*11,0,std::max(0,static_cast<int>(state.savedRandomMaps.size())-11));
    } else if (target.kind==HitKind::RandomLoadSelected || target.kind==HitKind::RandomDelete) {
        const int i=state.selectedSavedRandomMap;
        if (i<0 || i>=static_cast<int>(state.savedRandomMaps.size())) return true;
        const MapInfo saved=state.savedRandomMaps[i]; std::string error;
        if (target.kind==HitKind::RandomDelete) {
            if (state.smoke || MessageBoxW(state.hwnd,(L"删除“"+saved.displayName+L"”？").c_str(),L"删除地图",MB_YESNO|MB_ICONQUESTION)!=IDYES) return true;
            if (saved.path.parent_path()!=runtimeSavedRandomMapDirectory(state.config)) return true;
            std::error_code ec;
            if (!fs::remove(saved.path,ec) || ec) state.status=L"未能删除这张地图。";
            else {
                for (int m=static_cast<int>(state.maps.size())-1;m>=0;--m) if (state.maps[m].path==saved.path) {
                    state.maps.erase(state.maps.begin()+m);
                    if (state.selectedMap>=m) --state.selectedMap;
                    if (state.mapPageOriginalMap>=m) --state.mapPageOriginalMap;
                }
                rebuildFilteredMaps(state,false); scanSavedRandomMaps(state); state.status=L"地图已删除。";
            }
        } else {
            RuntimeRandomMapResult map;
            if (!runtimeLoadRandomMap(saved.path,map,error)) state.status=utf8ToWide(error);
            else {
                state.randomOptions=map.options; setRandomMapName(state,map.options.name);
                state.randomResult=std::move(map); state.randomDirty=false;
                state.page=SkirmishPage::RandomMap; state.status=L"已载入保存的地图。";
            }
        }
    } else return false;
    state.audio.play("MenuClick"); invalidate(state); return true;
}
