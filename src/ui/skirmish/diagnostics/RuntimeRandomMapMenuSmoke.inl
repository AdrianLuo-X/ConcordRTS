bool runRandomMapMenuSmoke(MenuState& state,const fs::path& output,std::string& error) {
    const auto fail=[&](const std::string& why){error="Random map menu: "+why;return false;};
    fs::create_directories(output.parent_path());
    state.phase=ShellMenuPhase::Idle;
    const auto battle=std::find_if(state.modes.begin(),state.modes.end(),[](const auto& m){return m.key=="Battle";});
    if (battle==state.modes.end()) return fail("Battle missing");
    selectSkirmishMode(state,static_cast<int>(battle-state.modes.begin()));
    HDC reference=GetDC(nullptr);
    const bool buffer=ensureShellBackBuffer(state,reference); ReleaseDC(nullptr,reference);
    if (!buffer) return fail("shell buffer missing");
    const auto capture=[&](const char* suffix) {
        state.backDirty=true;
        if (!renderShellBackBuffer(state)) return false;
        const auto path=output.parent_path()/(output.stem().string()+suffix+".bmp");
        return writeBgraBmp(path.string(),state.composeFrame.bytes().data(),kBaseWidth,kBaseHeight,error);
    };
    activateShellTarget(state,{HitKind::ChooseMap,-1},1);
    if (!capture("_entry") || !hasHitKind(state,HitKind::CreateRandomMap)) return fail("no original map-generator entrance");
    activateShellTarget(state,{HitKind::CreateRandomMap,-1},1);
    state.randomOptions.environment=2; state.randomOptions.resources=1; state.randomOptions.timeOfDay=3;
    state.randomOptions.players=4; state.randomOptions.theater=0; state.randomOptions.size=1;
    activateShellTarget(state,{HitKind::RandomGenerate,-1},1);
    const auto deadline=GetTickCount64()+30000;
    while (state.pendingRandomMap.valid() && GetTickCount64()<deadline) {
        pollRandomMap(state);
        if (state.pendingRandomMap.valid()) MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
    if (!state.randomResult || state.randomDirty || state.pendingRandomMap.valid()) return fail("generate action did not finish");
    const auto generated=state.randomResult->iniText;
    if (!capture("_generated") || !hasHitKind(state,HitKind::RandomUse) || !hasHitKind(state,HitKind::RandomSave)) return fail("preview cannot be used or saved");
    activateShellTarget(state,{HitKind::RandomPreview,-1},1);
    if (state.page!=SkirmishPage::RandomMapPreview || !capture("_preview")) return fail("full preview missing");
    activateShellTarget(state,{HitKind::RandomCancel,-1},1);
    if (state.page!=SkirmishPage::RandomMap) return fail("preview did not return to generator");
    activateShellTarget(state,{HitKind::RandomOption,1},1);
    if (!renderShellBackBuffer(state) || hasHitKind(state,HitKind::RandomUse) || hasHitKind(state,HitKind::RandomSave)) return fail("changed choices reused the old battlefield");
    state.randomOptions=state.randomResult->options; state.randomDirty=false;
    setRandomMapName(state,"夜间群岛 检查");
    // Save/use/load exercise the real actions, in an isolated output directory.
    state.config.localDataRootPath=(output.parent_path()/"menu-data").string();
    activateShellTarget(state,{HitKind::RandomSave,-1},1);
    const auto named=state.randomResult->iniText;
    const auto saved=runtimeSavedRandomMapDirectory(state.config)/state.randomResult->fileName;
    if (!regularFile(saved) || named==generated) return fail("named save not written");
    state.randomResult.reset();
    activateShellTarget(state,{HitKind::RandomLoad,-1},1);
    if (!capture("_saved") || state.savedRandomMaps.empty()) return fail("saved map not listed");
    activateShellTarget(state,{HitKind::RandomLoadSelected,-1},1);
    if (!state.randomResult || state.randomResult->iniText!=named || state.randomDirty) return fail("load changed the saved battlefield");
    activateShellTarget(state,{HitKind::RandomUse,-1},1);
    if (state.page!=SkirmishPage::Lobby || state.selection.mapPath.empty() || !regularFile(state.selection.mapPath)
        || !capture("_lobby")) return fail("use did not return the generated map to the match lobby");
    for (std::size_t i=0;i<state.modes.size();++i) {
        state.page=SkirmishPage::MapSelection; selectSkirmishMode(state,static_cast<int>(i));
        if (!renderShellBackBuffer(state) || hasHitKind(state,HitKind::CreateRandomMap)!=state.modes[i].randomMapsAllowed)
            return fail("mode ignored original randomMapsAllowed");
    }
    std::cout<<"Random map menu: generation, preview, changed settings, named save/load, mode eligibility and match selection passed.\n";
    return true;
}
