// Complete player-facing command catalog, using retail CSF names and categories.
int runtimeKeyboardCategoryForId(const std::string& id) {
    if (id.starts_with("Team")) return 2;
    if (id == "TypeSelect" || id == "CombatantSelect" || id == "VeterancyNav"
        || id == "HealthNav" || id == "PreviousObject" || id == "NextObject") return 1;
    if (id == "StopObject" || id == "GuardObject" || id == "ScatterObject" || id == "DeployObject"
        || id == "PlanningMode" || id == "SetPrimaryFactory" || id == "SetRallyPoint") return 0;
    if (id == "Diplomacy" || id == "ToggleAlliance" || id == "PlaceBeacon" || id == "DeleteBeacon"
        || id == "AllToCheer" || id.starts_with("ChatTo") || id.starts_with("Taunt_")) return 4;
    return 3;
}

std::wstring runtimeKeyboardCategoryLabel(const RuntimeWindowState& state, int category) {
    constexpr const char* keys[]{"TXT_CONTROL","TXT_SELECTION","TXT_TEAM","TXT_INTERFACE","TXT_MULTIPLAYER_GAME"};
    constexpr const wchar_t* labels[]{L"控制",L"选择",L"队伍",L"界面",L"多人连线游戏"};
    const int index=std::clamp(category,0,4);
    return runtimeMenuLabel(state,keys[index],labels[index]);
}

std::wstring runtimeFullKeyboardBindingLabel(const RuntimeWindowState& state,const std::string& id) {
    struct Label { const char* id; const char* key; const wchar_t* fallback; };
    static const Label labels[]{
        {"Options","TXT_OPTIONS",L"游戏选项"}, {"StopObject","TXT_STOP_OBJECT",L"停止"},
        {"GuardObject","TXT_GUARD",L"警戒"}, {"ScatterObject","TXT_SCATTER",L"散开"},
        {"DeployObject","TXT_DEPLOY_OBJECT",L"部署／卸载"}, {"CenterView","TXT_CENTER_VIEW",L"选择置中"},
        {"TypeSelect","Cmnd:TypeSelect",L"选择同类"}, {"CombatantSelect","Cmnd:CombatantSelect",L"选择战斗部队"},
        {"VeterancyNav","Cmnd:VetNavigation",L"按等级选择"}, {"HealthNav","Cmnd:HealthNavigation",L"按生命值选择"},
        {"PreviousObject","TXT_PREV_OBJECT",L"前一个单位"}, {"NextObject","TXT_NEXT_OBJECT",L"下一个单位"},
        {"PlanningMode","Cmnd:PlanningMode",L"路径规划"}, {"Follow","TXT_FOLLOW",L"镜头跟随"},
        {"CenterOnRadarEvent","TXT_RADAR_EVENT",L"定位雷达事件"}, {"CenterBase","TXT_CENTER_BASE",L"基地置中"},
        {"StructureTab","TXT_STRUCTURE_TAB",L"建筑页"}, {"DefenseTab","TXT_DEFENSE_TAB",L"防御页"},
        {"InfantryTab","TXT_INFANTRY_TAB",L"步兵页"}, {"UnitTab","TXT_UNIT_TAB",L"车辆页"},
        {"ToggleRepair","TXT_REPAIR_MODE",L"维修工具"}, {"ToggleSell","TXT_SELL_MODE",L"变卖工具"},
        {"ShowCoordinates","",L"显示地图坐标"}, {"Diplomacy","",L"外交面板"},
        {"ToggleAlliance","TXT_ALLIANCE",L"结盟／解除"}, {"PlaceBeacon","TXT_PLACE_BEACON",L"放置信标"},
        {"DeleteBeacon","Cmnd:Delete",L"删除信标／路径点"}, {"AllToCheer","Cmnd:AllCheer",L"部队欢呼"},
        {"ChatToAll","",L"所有人聊天／信标文字"}, {"ChatToAllies","",L"盟友聊天"},
        {"SetPrimaryFactory","",L"设置主工厂"}, {"SetRallyPoint","",L"设置工厂集结点"}
    };
    for (const auto& label:labels) if (id==label.id)
        return *label.key?runtimeMenuLabel(state,label.key,label.fallback):label.fallback;
    const auto numbered=[&](const char* key,const wchar_t* fallback,const std::string& number) {
        std::wstring text=runtimeMenuLabel(state,key,fallback);
        for (const auto* format:{L"%2d",L"%d"}) {
            const auto at=text.find(format);
            if (at!=std::wstring::npos) {text.replace(at,std::wcslen(format),widenUtf8(number));return text;}
        }
        return text+L" "+widenUtf8(number);
    };
    if (id.starts_with("TeamSelect_")) return numbered("TXT_SELECT_TEAM",L"选择第 %d 队",id.substr(11));
    if (id.starts_with("TeamAddSelect_")) return numbered("TXT_ADD_SELECT_TEAM",L"追加选择第 %d 队",id.substr(14));
    if (id.starts_with("TeamCreate_")) return numbered("TXT_CREATE_TEAM",L"建立第 %d 队",id.substr(11));
    if (id.starts_with("TeamCenter_")) return numbered("TXT_CENTER_TEAM",L"选择并定位第 %d 队",id.substr(11));
    if (id.starts_with("SetView")) return runtimeMenuLabel(state,"TXT_SET_BOOKMARK"+id.substr(7),(L"保存书签 "+widenUtf8(id.substr(7))).c_str());
    if (id.starts_with("View")) return runtimeMenuLabel(state,"TXT_VIEW_BOOKMARK"+id.substr(4),(L"查看书签 "+widenUtf8(id.substr(4))).c_str());
    if (id.starts_with("Taunt_")) return numbered("TXT_TAUNT_NUMBER",L"叫嚣 %d",id.substr(6));
    return widenUtf8(id);
}

RuntimeMenuAction runtimeKeyboardActionForIndex(int index) {
    const auto& definitions=runtimeKeyboardBindingDefinitions();
    return index<static_cast<int>(definitions.size())?definitions[static_cast<std::size_t>(index)].action
        :static_cast<RuntimeMenuAction>(4096+index);
}

std::vector<int> runtimeKeyboardCategoryBindings(const RuntimeWindowState& state) {
    std::vector<int> result;
    for (std::size_t i=0;i<state.runtimeMenuKeyboardBindings.size();++i)
        if (runtimeKeyboardCategoryForId(state.runtimeMenuKeyboardBindings[i].first)==state.runtimeMenuKeyboardCategory)
            result.push_back(static_cast<int>(i));
    return result;
}
int runtimeKeyboardCategoryPages(const RuntimeWindowState& state) {
    return std::max(1,(static_cast<int>(runtimeKeyboardCategoryBindings(state).size())+4)/5);
}
int runtimeKeyboardDefaultAt(int index) {
    const auto& definitions=runtimeKeyboardBindingDefinitions();
    if (index<0) return 0;
    if (index<static_cast<int>(definitions.size())) return definitions[static_cast<std::size_t>(index)].defaultKey;
    const auto bindings=runtimePlayerCommandBindings();
    index-=static_cast<int>(definitions.size());
    return index<static_cast<int>(bindings.size())?bindings[static_cast<std::size_t>(index)].second:0;
}

void runtimeKeyboardChangePage(RuntimeWindowState& state,int delta,bool category) {
    state.runtimeMenuKeyCaptureIndex=-1;
    if (category) {state.runtimeMenuKeyboardCategory=(state.runtimeMenuKeyboardCategory+delta+5)%5;state.runtimeMenuKeyboardPage=0;}
    else {const int count=runtimeKeyboardCategoryPages(state);state.runtimeMenuKeyboardPage=(state.runtimeMenuKeyboardPage+delta+count)%count;}
    state.runtimeMenuHover=state.runtimeMenuPressed=RuntimeMenuAction::None;
    state.runtimeMenuFocus=0;
    state.runtimeMenuStatus=L"选命令改键 · Esc取消 · Backspace默认";
    state.repaintRequested=true;
}
