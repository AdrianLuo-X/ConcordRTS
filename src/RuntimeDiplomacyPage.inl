// GAMEMD RT_DIALOG 115: full menu background and original control positions.
// The 533x369 dialog-unit template is mapped to the runtime's 800x600 canvas;
// this mapping is shared by text, controls and input, rather than treating DU as pixels.
struct RuntimeDiplomacyRow {
    std::string owner;
    std::wstring name;
    std::string country;
    long long kills = 0;
    bool self = false, human = false, defeated = false;
    std::optional<std::size_t> slot;
};

RECT runtimeDiplomacyBaseRect(int x, int y, int width, int height) {
    const auto sx = [](int value) { return static_cast<LONG>(std::lround(value * 800.0 / 533.0)); };
    const auto sy = [](int value) { return static_cast<LONG>(std::lround(value * 600.0 / 369.0)); };
    return {sx(x), sy(y), sx(x + width), sy(y + height)};
}

constexpr std::array<int, 7> runtimeDiplomacyChatControls{{1750,1751,1752,1753,1762,1763,1764}};
RECT runtimeDiplomacyControlBase(int id) {
    if (id == 1684) return runtimeDiplomacyBaseRect(425,1,108,10);
    if (id == 1059) return runtimeDiplomacyBaseRect(425,346,108,23);
    if (id == 1057) return runtimeDiplomacyBaseRect(111,53,110,10);
    if (id == 1431) return runtimeDiplomacyBaseRect(226,53,35,10);
    if (id == 1061) return runtimeDiplomacyBaseRect(271,53,35,10);
    if (id == 1058) return runtimeDiplomacyBaseRect(316,53,60,10);
    if (id >= 1041 && id <= 1056) {
        const int row = (id - 1041) % 8;
        constexpr int heights[]{10,10,9,8,8,8,8,9};
        return runtimeDiplomacyBaseRect(id <= 1048 ? 111 : 316, 68 + row*20,
            id <= 1048 ? 110 : 41, heights[row]);
    }
    if (id >= 1754 && id <= 1761) return runtimeDiplomacyBaseRect(69,66+(id-1754)*20,32,12);
    if (id >= 1743 && id <= 1749) {
        constexpr int heights[]{10,9,8,10,8,8,9};
        return runtimeDiplomacyBaseRect(226,88+(id-1743)*20,10,heights[id-1743]);
    }
    const auto chat = std::find(runtimeDiplomacyChatControls.begin(), runtimeDiplomacyChatControls.end(), id);
    if (chat != runtimeDiplomacyChatControls.end()) {
        const int index = static_cast<int>(chat-runtimeDiplomacyChatControls.begin());
        return runtimeDiplomacyBaseRect(271,88+index*20,10,index==5?8:10);
    }
    if (id == 1840) return runtimeDiplomacyBaseRect(69,232,227,15);
    if (id == 1062) return runtimeDiplomacyBaseRect(69,247,263,10);
    if (id == 1819) return runtimeDiplomacyBaseRect(69,262,263,10);
    if (id == 1685) return runtimeDiplomacyBaseRect(2,355,303,12);
    return {};
}
RECT runtimeDiplomacyControlRect(const RECT& client, int id) {
    RECT rect=runtimeMatchScoreScaledRect(client, runtimeDiplomacyControlBase(id));
    if (id==1059 || id==1684) {
        // Keep the right-column controls inside the fixed-width runtime sidebar
        // on wide windows. The two native 640/800 canvases retain their mapping.
        const LONG width=std::min(rect.right-rect.left,static_cast<LONG>(runtimeSidebarWidth-6));
        rect.left=client.right-width;rect.right=client.right;
    }
    return rect;
}
RuntimeMenuBackgroundLayout runtimeDiplomacyBackgroundLayout(const RECT& client) {
    auto layout=runtimeMenuBackgroundLayout(client);
    const LONG width=client.right-client.left;
    // Retail 72F59F/72F5CC selects small at 640, medium at 800, otherwise large.
    layout.imageId=width==640?"bkgdsm.shp":width==800?"bkgdmd.shp":"bkgdlg.shp";
    const LONG w=width==640?472:width==800?632:856;
    const LONG h=width==640?448:width==800?568:736;
    layout.imageRect={client.left,client.top,client.left+w,client.top+h};
    return layout;
}
RECT runtimeDiplomacyCheckboxRect(const RECT& client,int id) {
    const RECT control=runtimeDiplomacyControlRect(client,id);
    const RECT size=runtimeMatchScoreScaledRect(client,RECT{0,0,18,18});
    const LONG w=size.right-size.left,h=size.bottom-size.top;
    const LONG cx=(control.left+control.right)/2,cy=(control.top+control.bottom)/2;
    return {cx-w/2,cy-h/2,cx-w/2+w,cy-h/2+h};
}

std::vector<RuntimeDiplomacyRow> runtimeDiplomacyRows(const RuntimeWindowState& state) {
    std::vector<RuntimeDiplomacyRow> rows;
    std::set<std::string> added;
    const auto self = runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto add = [&](const std::string& owner, const std::wstring& name,
                         std::optional<std::size_t> slot) {
        const auto key = runtimeCanonicalHumanHouseOwner(owner);
        if (key.empty() || runtimeOwnerIsNeutralForAutoTarget(key) || runtimeObserverOwner(state,key)
            || !added.insert(key).second) return;
        RuntimeDiplomacyRow row;
        row.owner=key; row.name=name.empty()?widenUtf8(owner):name;
        row.country=runtimeCountryForOwner(state,key); row.self=key==self;
        row.slot=slot;
        row.human=slot ? state.config.playerSlots[*slot].human
            : std::any_of(state.humanHouseAuthorities.begin(),state.humanHouseAuthorities.end(),
                [&](const auto& authority){return authority.canonicalOwner==key;});
        row.defeated=state.multiplayerDefeatedOwners.contains(key)
            || !std::any_of(state.world.getObjects().begin(),state.world.getObjects().end(),
                [&](const auto& object) {
                    return runtimeCanonicalHumanHouseOwner(object.owner)==key
                        && runtimeSkirmishObjectSurvives(state,object)
                        && (!state.config.shortGame || runtimeSkirmishObjectCountsForShortGame(state,object));
                });
        const auto score=state.matchScores.find(key);
        if (score!=state.matchScores.end()) row.kills=static_cast<long long>(score->second.unitsDestroyed)
            + score->second.structuresDestroyed;
        rows.push_back(std::move(row));
    };
    for (std::size_t i=0;i<state.config.playerSlots.size();++i) {
        const auto& slot=state.config.playerSlots[i];
        if (slot.enabled) add(runtimeSkirmishOwnerForSlot(i),widenUtf8(slot.name),i);
    }
    if (rows.empty()) {
        if (!state.multiplayerLocalSpectator) add(self,{},{});
        for (const auto& owner:runtimePlayerDiplomacyOwners(state)) add(owner,{},{});
    }
    const auto own=std::find_if(rows.begin(),rows.end(),[](const auto& row){return row.self;});
    if (own!=rows.end()) std::rotate(rows.begin(),own,own+1);
    return rows;
}

void closeRuntimeDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=false; ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    state.rightDragActive=state.rightDragMoved=false;
    state.repaintRequested=true;
}
void openRuntimeDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=true; ui.diplomacyRow=1;
    ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    ui.chatOpen=ui.beaconEditing=false; ui.chatDraft.clear();
    state.selectionDragActive=state.selectionDragMoved=false;
    state.rightDragActive=state.rightDragMoved=false;
    state.repaintRequested=true;
}

bool runtimeDiplomacyAllianceEnabled(const RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
    command.kind=RuntimePlayerCommunicationKind::Alliance; command.text=row.owner;
    const auto source=runtimeCanonicalHumanHouseOwner(state.economy.owner);
    const auto offer=state.playerCommands.alliances.find({source,row.owner});
    const bool offered=offer==state.playerCommands.alliances.end()
        ? runtimeOwnersAreAllied(state,source,row.owner):offer->second;
    command.value=offered?0:1;
    std::string error;
    return !row.self && !row.defeated && validateRuntimePlayerCommunication(state,source,command,error);
}
bool runtimeDiplomacyChatEnabled(const RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    return !row.self && row.human && row.slot && *row.slot<8 && runtimePlayerChatRecipientsAvailable(state);
}
void runtimeDiplomacyToggleChat(RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    if (!runtimeDiplomacyChatEnabled(state,row)) return;
    auto& ui=state.playerCommunication;
    if (!ui.chatRecipientsExplicit) ui.chatRecipients=runtimePlayerChatSlotsMask(state);
    ui.chatRecipientsExplicit=true;
    ui.chatRecipients^=static_cast<std::uint8_t>(1U<<*row.slot);
    ui.chatRecipients|=static_cast<std::uint8_t>(1U<<*runtimePlayerChatSlot(state,state.economy.owner));
    state.repaintRequested=true;
}

void runtimePlayerToggleAlliance(RuntimeWindowState&, const std::string&);
int runtimeDiplomacyHitControl(const RuntimeWindowState& state, const RECT& client, POINT point) {
    if (pointInRectInclusive(runtimeDiplomacyControlRect(client,1059),point)) return 1059;
    const auto rows=runtimeDiplomacyRows(state);
    for (std::size_t i=1;i<rows.size() && i<8;++i) {
        if (runtimeDiplomacyAllianceEnabled(state,rows[i])
            && pointInRectInclusive(runtimeDiplomacyCheckboxRect(client,1742+static_cast<int>(i)),point))
            return 1742+static_cast<int>(i);
        if (runtimeDiplomacyChatEnabled(state,rows[i])
            && pointInRectInclusive(runtimeDiplomacyCheckboxRect(client,runtimeDiplomacyChatControls[i-1]),point))
            return runtimeDiplomacyChatControls[i-1];
    }
    if (pointInRectInclusive(runtimeDiplomacyControlRect(client,1840),point)) return 1840;
    return 0;
}
void handleRuntimeDiplomacyMouse(RuntimeWindowState& state,const RECT& client,UINT message,POINT point) {
    auto& ui=state.playerCommunication;
    const int hit=runtimeDiplomacyHitControl(state,client,point);
    ui.diplomacyHoveredControl=hit;
    if (message==WM_LBUTTONDOWN) ui.diplomacyPressedControl=hit;
    else if (message==WM_LBUTTONUP) {
        const bool activate=hit!=0 && hit==ui.diplomacyPressedControl;
        ui.diplomacyPressedControl=0;
        if (activate) {
            if (hit==1059) closeRuntimeDiplomacyPage(state);
            else if (hit==1840) ui.tauntsEnabled=!ui.tauntsEnabled;
            else {
                const auto rows=runtimeDiplomacyRows(state);
                if (hit>=1743 && hit<=1749) {
                    const auto row=static_cast<std::size_t>(hit-1742);
                    if (row<rows.size()) {ui.diplomacyRow=static_cast<int>(row);runtimePlayerToggleAlliance(state,rows[row].owner);}
                } else {
                    const auto found=std::find(runtimeDiplomacyChatControls.begin(),runtimeDiplomacyChatControls.end(),hit);
                    const auto row=static_cast<std::size_t>(found-runtimeDiplomacyChatControls.begin()+1);
                    if (row<rows.size()) {ui.diplomacyRow=static_cast<int>(row);runtimeDiplomacyToggleChat(state,rows[row]);}
                }
            }
        }
    } else if (message==WM_RBUTTONUP) closeRuntimeDiplomacyPage(state);
    state.repaintRequested=true;
}

std::string runtimeDiplomacyFlagId(const std::string& country) {
    // Retail 4E3560 table (the African/Arab entries differ from the old lobby table).
    static const std::map<std::string,std::string> ids{{"AMERICANS","usai"},{"ALLIANCE","japi"},
        {"FRENCH","frai"},{"GERMANS","geri"},{"BRITISH","gbri"},{"AFRICANS","djbi"},
        {"ARABS","arbi"},{"CONFEDERATION","lati"},{"RUSSIANS","rusi"},
        {"YURICOUNTRY","yrii"},{"YURI","yrii"}};
    const auto found=ids.find(toUpperAsciiLocal(country));
    return found==ids.end()?"rani":found->second;
}
RuntimeUiShpCacheEntry* runtimeDiplomacyPcx(RuntimeWindowState& state,const std::string& id) {
    if (auto* loose=loadRuntimeUiShpFrame(state,"",id,0); loose && loose->pcxFallback
        && !loose->embeddedPalette.empty()) return loose;
    static std::map<std::string,RuntimeUiShpCacheEntry> entries;
    const std::string key=runtimeAssetRootPath(state.config)+"|"+id;
    auto& entry=entries[key];
    if (!entry.loadAttempted) {
        entry.loadAttempted=true;
        const std::filesystem::path root(runtimeAssetRootPath(state.config));
        for (const auto& path:{root/"ra2md"/"localmd.mix",root/"ra2"/"local.mix"})
            if (loadRuntimePcxImageFromMixArchive(pathString(path),state.renderCache,id,
                    entry.image,entry.embeddedPalette,entry.sourcePath,entry.errorMessage)) {
                entry.loaded=entry.pcxFallback=true;break;
            }
    }
    return entry.loaded?&entry:nullptr;
}

void drawRuntimeDiplomacyPage(HDC dc,const RECT& client,RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    const int saved=SaveDC(dc);
    SetBkMode(dc,TRANSPARENT);
    FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    GdiFlush(); // Submit GDI before directly writing into the same DIB.
    const auto layout=runtimeDiplomacyBackgroundLayout(client);
    const auto* background=loadRuntimeUiShpFrame(state,"sidebar",layout.imageId,0);
    const auto* palette=runtimeUiPaletteForEntry(state,background,"uibkgd");
    const auto target=runtimePixelSurfaceTargetForDc(state,dc);
    ui.diplomacyBackgroundSource.clear();ui.diplomacyControlSources.clear();
    if (background && palette) {
        // Native 72FC92 uses the artwork at the origin, not a centered small popup.
        const RECT rect{client.left,client.top,client.left+background->image.globalWidth,
            client.top+background->image.globalHeight};
        if (drawRuntimeIndexedImage(dc,rect,background->image,*palette,true,false,true,
                target.pixels,target.width,target.height,false,runtimeIndexedOpaqueAlpha,
                background->pcxFallback,background->pcxFallback)) ui.diplomacyBackgroundSource=background->sourcePath;
    }
    drawRuntimeMenuSidebarBackground(dc,client,state);
    const auto rows=runtimeDiplomacyRows(state);
    const auto pcx=[&](const std::string& id,const RECT& rect,bool stretch=true) {
        auto* image=runtimeDiplomacyPcx(state,id);
        if (!image || image->embeddedPalette.empty()) return false;
        GdiFlush();
        const bool drawn=drawRuntimeIndexedImage(dc,rect,image->image,image->embeddedPalette,true,stretch,true,
            target.pixels,target.width,target.height,false,runtimeIndexedOpaqueAlpha,true,true);
        if (drawn) ui.diplomacyControlSources.push_back(image->sourcePath);
        return drawn;
    };
    const auto& font=runtimeResultBitFont(state);
    HFONT fallback=font.bytes.empty()?CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,
        DEFAULT_PITCH,L"Microsoft YaHei UI"):nullptr;
    const auto text=[&](int id,const std::wstring& value,COLORREF color,bool center=false) {
        RECT base=runtimeDiplomacyControlBase(id);
        // Some template STATICs are only eight DU high. Keep all installed
        // bitmap rows visible inside the 20-DU row spacing of this canvas mapping.
        if (!font.bytes.empty()) base.bottom=std::max(base.bottom,base.top+static_cast<LONG>(font.height));
        RECT textClient=client;
        if (id==1059 || id==1684) {
            const double scale=std::min((client.right-client.left)/800.0,(client.bottom-client.top)/600.0);
            const RECT mapped=runtimeMatchScoreScaledRect(client,base);
            const LONG shift=client.right-mapped.right;
            textClient.left+=shift;textClient.right+=shift;
            if (scale>0) base.left=std::max(base.left,base.right-static_cast<LONG>(std::floor((runtimeSidebarWidth-6)/scale)));
        }
        if (!font.bytes.empty()) font.draw(dc,textClient,base,value,color,center);
        else {RECT rect=runtimeMatchScoreScaledRect(textClient,base);auto old=SelectObject(dc,fallback);
            SetTextColor(dc,color);DrawTextW(dc,value.c_str(),static_cast<int>(value.size()),&rect,
                DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS|(center?DT_CENTER:DT_LEFT));SelectObject(dc,old);}
    };
    const COLORREF gold=RGB(255,216,102);
    for (const auto& item:std::array<std::pair<int,const char*>,5>{{{1684,"GUI:DiplomacyMenu"},
            {1057,"GUI:Player"},{1431,"GUI:Allies"},{1061,"GUI:Chat"},{1058,"GUI:Kills"}}})
        text(item.first,runtimeMenuLabel(state,item.second,L""),gold,item.first==1684);
    const auto check=[&](int id,bool checked,bool enabled) {
        RECT rect=runtimeDiplomacyCheckboxRect(client,id);
        if (!pcx(checked?(enabled?"cce_i":"ccd_i"):(enabled?"cue_i":"cud_i"),rect))
            DrawFrameControl(dc,&rect,DFC_BUTTON,DFCS_BUTTONCHECK|(checked?DFCS_CHECKED:0)|(enabled?0:DFCS_INACTIVE));
    };
    for (std::size_t i=0;i<rows.size() && i<8;++i) {
        const auto& row=rows[i];const COLORREF color=runtimeRadarColorForOwner(state,row.owner);
        text(1041+static_cast<int>(i),row.name,color);
        text(1049+static_cast<int>(i),std::to_wstring(row.kills),color);
        pcx(runtimeDiplomacyFlagId(row.country),runtimeDiplomacyControlRect(client,1754+static_cast<int>(i)));
        if (i>0 && !row.self) {
            const auto offer=state.playerCommands.alliances.find({runtimeCanonicalHumanHouseOwner(state.economy.owner),row.owner});
            const bool offered=offer==state.playerCommands.alliances.end()
                ?runtimeOwnersAreAllied(state,state.economy.owner,row.owner):offer->second;
            check(1742+static_cast<int>(i),offered,runtimeDiplomacyAllianceEnabled(state,row));
            if (row.human) check(runtimeDiplomacyChatControls[i-1],!ui.chatRecipientsExplicit
                || (row.slot && *row.slot<8 && (runtimePlayerOutgoingChatMask(state)&(1U<<*row.slot))),
                runtimeDiplomacyChatEnabled(state,row));
        }
    }
    RECT taunt=runtimeDiplomacyControlRect(client,1840);
    const RECT tauntBoxBase=runtimeDiplomacyBaseRect(69,232,12,12);
    pcx(ui.tauntsEnabled?"cce_i":"cue_i",runtimeMatchScoreScaledRect(client,tauntBoxBase));
    const RECT tauntText=runtimeDiplomacyBaseRect(83,232,213,15);
    if (!font.bytes.empty()) font.draw(dc,client,tauntText,runtimeMenuLabel(state,"GUI:TauntsOn",L"叫嚣开启"),gold,false);
    else {taunt.left+=24;auto old=SelectObject(dc,fallback);SetTextColor(dc,gold);
        const auto label=runtimeMenuLabel(state,"GUI:TauntsOn",L"叫嚣开启");DrawTextW(dc,label.c_str(),-1,&taunt,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);}
    const RECT button=runtimeDiplomacyControlRect(client,1059);
    const std::string prefix=ui.diplomacyPressedControl==1059?"bde_":"bue_";
    const LONG cap=std::max(1L,(button.right-button.left)*8/108);
    pcx(prefix+"li24",RECT{button.left,button.top,button.left+cap,button.bottom});
    pcx(prefix+"mi24",RECT{button.left+cap,button.top,button.right-cap,button.bottom});
    pcx(prefix+"ri24",RECT{button.right-cap,button.top,button.right,button.bottom});
    text(1059,runtimeMenuLabel(state,"GUI:Return",L"继续"),gold,true);
    if (std::find(runtimeDiplomacyChatControls.begin(),runtimeDiplomacyChatControls.end(),
            ui.diplomacyHoveredControl)!=runtimeDiplomacyChatControls.end())
        text(1062,runtimeMenuLabel(state,"STT:DiplomacyCBoxListeners",L"让您的讯息能传送给这名玩家。"),gold);
    // More than the native eight participants is a mod extension: disclose omitted rows.
    if (rows.size()>8) text(1685,L"8 / "+std::to_wstring(rows.size()),gold);
    if (fallback) DeleteObject(fallback);
    RestoreDC(dc,saved);
}
