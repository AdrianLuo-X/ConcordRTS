// Local presentation only; the notice core owns admission and expiration.
std::wstring runtimePlayerDefeatNoticeText(const RuntimeWindowState& state,
    const std::string& owner) {
    const std::string ownerKey=runtimeCanonicalHumanHouseOwner(owner);
    std::wstring name;
    for(std::size_t slotIndex=0;slotIndex<state.config.playerSlots.size();++slotIndex) {
        const auto& slot=state.config.playerSlots[slotIndex];
        if(!slot.enabled || runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(slotIndex))!=ownerKey)
            continue;
        // Offline menus use a zero-based placeholder; multiplayer launch uses
        // the one-based slot number. Human/custom names retain their UTF-8 spelling.
        const bool defaultComputer=!slot.human && (slot.name.empty()
            || slot.name=="Computer "+std::to_string(slotIndex)
            || (state.config.multiplayerActive && slot.name=="Computer "+std::to_string(slotIndex+1)));
        if(defaultComputer) {
            const std::string difficulty=toLowerAsciiLocal(trimAsciiLocal(slot.difficulty));
            if(difficulty=="easy") name=runtimeMenuLabel(state,"GUI:AIEasy",L"Easy Enemy");
            else if(difficulty=="hard") name=runtimeMenuLabel(state,"GUI:AIHard",L"Brutal Enemy");
            else {
                const auto medium=runtimeMenuLabel(state,"GUI:Medium",L"Medium Enemy");
                name=runtimeMenuLabel(state,"GUI:AINormal",medium.c_str());
            }
        } else name=slot.name.empty()
            ? runtimeMenuLabel(state,"GUI:Player"+std::to_string(slotIndex+1),L"玩家")
            : widenUtf8(slot.name);
        break;
    }
    if(name.empty()) name=widenUtf8(owner);
    return name+L"已被击败！";
}

// Fit only the name. The exact defeat suffix survives a long player name,
// and every retained prefix ends at a complete UTF-16 scalar boundary.
std::wstring runtimePlayerDefeatNoticeFitText(HDC dc,const std::wstring& text,int width) {
    const auto fits=[&](const std::wstring& value) {
        SIZE extent{};
        return GetTextExtentPoint32W(dc,value.c_str(),static_cast<int>(value.size()),&extent)
            && extent.cx<=width;
    };
    if(fits(text)) return text;
    const std::wstring suffix=L"已被击败！";
    if(text.size()<suffix.size() || text.compare(text.size()-suffix.size(),suffix.size(),suffix)!=0)
        return text;
    const std::wstring name=text.substr(0,text.size()-suffix.size());
    const std::wstring ending=L"…"+suffix;
    if(!fits(ending)) return suffix;
    std::vector<std::size_t> boundaries{0};
    for(std::size_t pos=0;pos<name.size();) {
        const wchar_t first=name[pos];
        if(first>=0xd800 && first<=0xdbff && pos+1<name.size()
            && name[pos+1]>=0xdc00 && name[pos+1]<=0xdfff) pos+=2;
        else ++pos;
        boundaries.push_back(pos);
    }
    std::size_t low=0,high=boundaries.size();
    while(low+1<high) {
        const std::size_t middle=low+(high-low)/2;
        if(fits(name.substr(0,boundaries[middle])+ending)) low=middle;
        else high=middle;
    }
    return name.substr(0,boundaries[low])+ending;
}

int runtimePlayerDefeatNoticeVisibleLineCount(const RuntimeWindowState& state,
    const RECT& viewport,std::uint64_t nowMs) {
    if(viewport.right-viewport.left<=20) return 0;
    LONG top=viewport.top+12;
    int count=0;
    for(const auto& notice:state.playerDefeatNotices.lines) {
        if(notice.untilMs<=nowMs) continue;
        if(top+25>viewport.bottom) break;
        ++count;top+=27;
    }
    return count;
}

int runtimePlayerDefeatNoticeStackHeight(const RuntimeWindowState& state,
    const RECT& viewport,std::uint64_t nowMs) {
    return runtimePlayerDefeatNoticeVisibleLineCount(state,viewport,nowMs)*27;
}

void drawRuntimePlayerDefeatNotices(HDC dc,RuntimeWindowState& state,const RECT& clientRect) {
    if(state.playerDefeatNotices.lines.empty()) return;
    const RECT viewport=runtimeMapViewport(clientRect);
    const std::uint64_t now=runtimeUiClockNow(state);
    const int rowCount=runtimePlayerDefeatNoticeVisibleLineCount(state,viewport,now);
    if(rowCount==0) return;
    const LONG left=viewport.left+10,right=viewport.right-10;
    LONG top=viewport.top+12;
    const int saved=SaveDC(dc);
    if(saved==0) return;
    HFONT font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
    if(font==nullptr) {RestoreDC(dc,saved);return;}
    SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);
    IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);
    constexpr UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX;
    int rendered=0;
    for(const auto& notice:state.playerDefeatNotices.lines) {
        if(notice.untilMs<=now) continue;
        if(rendered==rowCount) break;
        const auto text=runtimePlayerDefeatNoticeFitText(dc,
            runtimePlayerDefeatNoticeText(state,notice.owner),static_cast<int>(right-left));
        RECT bounds{left,top,right,top+25};
        RECT shadow=bounds;OffsetRect(&shadow,1,1);
        SetTextColor(dc,RGB(0,0,0));
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&shadow,flags);
        SetTextColor(dc,runtimeSuperWeaponTimerColor(state,notice.owner));
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&bounds,flags);
        top+=27;++rendered;
    }
    RestoreDC(dc,saved);DeleteObject(font);
}
