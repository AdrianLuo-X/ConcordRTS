using namespace concord::campaign_menu::win32;

void blitCampaignShellImage(std::vector<unsigned char>& canvas, const CampaignImage& image,
                            const RECT& rect) {
    if (!image.valid() || rect.right <= rect.left || rect.bottom <= rect.top) return;
    for (int y = std::max(0L, rect.top); y < std::min(600L, rect.bottom); ++y) {
        const int sy = (y - rect.top) * image.height / (rect.bottom - rect.top);
        for (int x = std::max(0L, rect.left); x < std::min(800L, rect.right); ++x) {
            const int sx = (x - rect.left) * image.width / (rect.right - rect.left);
            const auto src = static_cast<std::size_t>((sy * image.width + sx) * 4);
            const auto dst = static_cast<std::size_t>((y * 800 + x) * 4);
            const unsigned alpha = image.bgra[src + 3];
            for (int channel = 0; channel < 3; ++channel) {
                canvas[dst + channel] = static_cast<unsigned char>(
                    (image.bgra[src + channel] * alpha + canvas[dst + channel] * (255 - alpha)) / 255);
            }
            canvas[dst + 3] = 255;
        }
    }
}

void presentCampaignMenuFrame(HDC destination, HDC shell, const RECT& client) {
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    if (width <= 0 || height <= 0) return;
    concord::ui::win32::CampaignDeviceBitmapWin32 presentationOwner;
    if (!presentationOwner.ensure(destination, width, height)) return;
    const HDC presentation = presentationOwner.dc();
    const RECT bounds{0, 0, width, height};
    const RECT fit = campaignShellClientRect(bounds);
    // Letterboxing belongs to the completed frame. Clearing the window DC
    // before StretchBlt exposes a black frame even with an offscreen shell.
    FillRect(presentation, &bounds, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SetStretchBltMode(presentation, COLORONCOLOR);
    if (StretchBlt(presentation, fit.left, fit.top, fit.right - fit.left, fit.bottom - fit.top,
            shell, 0, 0, kCampaignShellWidth, kCampaignShellHeight, SRCCOPY)) {
        BitBlt(destination, client.left, client.top, width, height, presentation, 0, 0, SRCCOPY);
    }
    presentationOwner.release();
}

void drawCampaignMenu(CampaignMenuState& state, HDC dc, const RECT& client) {
    updateCampaignMapPresentation(state);
    const auto controls = campaignControls(state);
    std::vector<unsigned char> surface(800 * 600 * 4, 0);
    const auto asset = [&](const std::string& id, int x, int y) {
        const auto found = state.shellImages.find(id);
        if (found == state.shellImages.end()) return;
        const auto& image = found->second;
        blitCampaignShellImage(surface, image, RECT{x, y, x + image.width, y + image.height});
    };
    blitCampaignShellImage(surface, state.campaignShell, RECT{0, 0, 632, 568});
    asset("lwscrnl.shp", 0, 568);
    asset("sdtp.shp", 632, 0);
    asset("sdwrnanm.shp", 670, 48);
    for (int slot = 0; slot < 8; ++slot) {
        asset("sdbtnbkgd.shp", 632, 199 + slot * 42);
        asset("sdbtnanm.shp", 644, 199 + slot * 42);
    }
    asset("sdbtm.shp", 632, 535);
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const auto& control = controls[index];
        if (control.rect.left >= 632) {
            const int frame = !control.enabled ? 0
                : state.pressedControl == static_cast<int>(index) ? 1
                : state.hoveredControl == static_cast<int>(index) ? 2 : 0;
            asset("button" + std::to_string(frame), control.rect.left, control.rect.top);
            if (!control.enabled) {
                for (int y = control.rect.top; y < std::min<long>(600L, control.rect.bottom); ++y) {
                    for (int x = control.rect.left; x < std::min<long>(800L, control.rect.right); ++x) {
                        const auto at = static_cast<std::size_t>((y * 800 + x) * 4);
                        for (int channel = 0; channel < 3; ++channel) surface[at + channel] /= 2;
                    }
                }
            }
        } else if (control.action == CampaignControlAction::Campaign) {
            const auto& campaign = state.database.campaigns[static_cast<std::size_t>(control.index)];
            if (state.campaignLogoFrames.size() >= 2) {
                blitCampaignShellImage(surface, state.campaignLogoFrames[isSovietCampaign(&campaign) ? 1 : 0],
                    campaignNativeRect(control.rect));
            }
        }
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 800;
    info.bmiHeader.biHeight = -600;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    concord::ui::win32::CampaignDeviceBitmapWin32 bufferOwner;
    if (!bufferOwner.ensure(dc, 800, 600)) return;
    const HDC buffer = bufferOwner.dc();
    (void)bufferOwner.upload({surface, 800, 600, 800U * 4U, 0});
    concord::ui::CampaignSystemFontsWin32 systemFonts;
    systemFonts.initialize();
    const auto label = [&](const std::wstring& value, RECT rect, bool center = false,
                            COLORREF color = RGB(255, 222, 25), bool wrap = false) {
        const UINT format=DT_NOPREFIX | (center ? DT_CENTER : DT_LEFT)
                | (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        traceCampaignSystemFont(wrap?1U:0U,value,campaignPixelRect(rect),color,format);
        systemFonts.draw(buffer,wrap ? concord::ui::CampaignSystemFontRole::Small
            : concord::ui::CampaignSystemFontRole::Body,
            concord::ui::CampaignSystemWideTextRequest{value,campaignPixelRect(rect),format,
                concord::ui::CampaignSystemFontColor::fromPacked(color)});
    };
    label(campaignPageTitle(state), RECT{640, 0, 791, 24}, true);
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    if (campaignShowsMap(state)) {
        auto mapPixels = composeCampaignSurface(state);
        BITMAPINFO mapInfo = info;
        mapInfo.bmiHeader.biWidth = kBaseWidth;
        mapInfo.bmiHeader.biHeight = -kBaseHeight;
        StretchDIBits(buffer, kCampaignMapViewport.left, kCampaignMapViewport.top,
            kCampaignMapViewport.right - kCampaignMapViewport.left,
            kCampaignMapViewport.bottom - kCampaignMapViewport.top,
            0, 0, kBaseWidth, kBaseHeight, mapPixels.data(), &mapInfo, DIB_RGB_COLORS, SRCCOPY);
        drawCampaignMapTargets(state, buffer, campaignNativeRect(kCampaignMapViewport));
    }
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const auto& control = controls[index];
        const bool focused = state.hoveredControl == static_cast<int>(index)
            || state.focusedControl == static_cast<int>(index);
        RECT rect = campaignNativeRect(control.rect);
        if (control.action == CampaignControlAction::Campaign && rect.left < 632) {
            rect.top = rect.bottom + 3;
            rect.bottom = rect.top + 22;
            rect.left = 30;
            rect.right = 600;
        } else if (control.action == CampaignControlAction::Row) {
            HBRUSH fill = CreateSolidBrush(control.selected ? RGB(74, 13, 9) : RGB(8, 8, 8));
            FillRect(buffer, &rect, fill);
            DeleteObject(fill);
            rect.left += 8;
        }
        if (!control.label.empty()
            && (focused || (control.selected && control.action != CampaignControlAction::Campaign))) {
            HBRUSH edge = CreateSolidBrush(focused ? RGB(255, 222, 25) : RGB(142, 28, 18));
            campaignFrameControl(buffer, control.rect, edge);
            DeleteObject(edge);
        }
        label(control.label, rect, control.action != CampaignControlAction::Row,
            !control.enabled ? RGB(105, 105, 105)
                : focused ? RGB(255, 244, 168) : RGB(255, 222, 25));
    }
    if (state.page == CampaignPage::Campaigns || state.page == CampaignPage::Difficulty) {
        label(L"游戏难度", RECT{206, 209, 422, 229}, true);
        RECT track{206, 239, 422, 245};
        HBRUSH edge = CreateSolidBrush(RGB(122, 111, 83));
        FrameRect(buffer, &track, edge);
        const int x = 206 + state.difficultyIndex * 66;
        RECT grip{x, 231, x + 18, 253};
        FillRect(buffer, &grip, edge);
        DeleteObject(edge);
        if (state.page == CampaignPage::Difficulty && mission) {
            label(missionLabel(*mission, state.csf), RECT{44, 105, 590, 145}, true);
            label(L"选择难度后，单击右侧“继续”。", RECT{60, 334, 572, 375}, true);
        }
    }
    if (state.page == CampaignPage::Missions || state.page == CampaignPage::Briefing) {
        label(resolvedCampaignName(state, campaign), RECT{42, 20, 590, 52});
        if (mission && state.page == CampaignPage::Missions) {
            if (campaignShowsMap(state)) {
                label(missionLabel(*mission, state.csf), RECT{42, 510, 590, 542}, false, kBrightGold);
            } else {
                label(missionLabel(*mission, state.csf), RECT{42, 406, 590, 446}, false, kBrightGold);
                label(L"选择任务后单击“继续”。未解锁的任务需要先完成前序任务。",
                    RECT{42, 458, 590, 532}, false, kText, true);
            }
        }
        if (mission && state.page == CampaignPage::Briefing) {
            label(missionLabel(*mission, state.csf), RECT{42, 64, 590, 100}, false, kBrightGold);
            const std::wstring briefing = state.csf.get(mission->briefingKey,
                state.csf.get(mission->loadBriefingKey, L"该任务没有可用的本地化简报。"));
            RECT bounds=campaignNativeRect(systemFonts.measure(buffer,
                concord::ui::CampaignSystemFontRole::Small,
                concord::ui::CampaignSystemWideTextRequest{briefing,{48,118,585,118},
                    DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX,{}}));
            traceCampaignSystemFont(1U,briefing,{48,118,585,118},0U,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX,true,campaignPixelRect(bounds));
            state.briefingScroll = std::clamp(state.briefingScroll, 0,
                static_cast<int>(std::max(0L, bounds.bottom - bounds.top - 408)));
            const int saved = SaveDC(buffer);
            IntersectClipRect(buffer, 42, 112, 590, 536);
            label(briefing, RECT{48, 118 - state.briefingScroll, 585,
                std::max(536L, bounds.bottom) - state.briefingScroll}, false, kText, true);
            RestoreDC(buffer, saved);
        }
    } else if (state.page == CampaignPage::Saves) {
        label(L"选择存档", RECT{42, 20, 590, 52});
        if (state.saves.empty()) {
            label(L"没有找到可兼容的存档。", RECT{42, 112, 590, 164}, true, kText);
        } else if (const auto* entry = selectedSave(state)) {
            label(loadSaveSummary(*entry), RECT{42, 414, 590, 536}, false, kText, true);
        }
    }
    const std::wstring hint = state.status.empty()
        ? L"单击选择  ·  Enter 确认  ·  Esc / 右下角返回" : state.status;
    traceCampaignSystemFont(1U,hint,{12,570,620,596},kBrightGold,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    systemFonts.draw(buffer,concord::ui::CampaignSystemFontRole::Small,
        concord::ui::CampaignSystemWideTextRequest{hint,{12,570,620,596},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX,
            concord::ui::CampaignSystemFontColor::fromPacked(kBrightGold)});
    presentCampaignMenuFrame(dc, buffer, client);
    systemFonts.reset();
    bufferOwner.release();
}

struct CampaignPaintAudit {
    POINT size{};
    int copies = 0;
    bool completeFrameOnly = true;
};

int CALLBACK auditCampaignPaintRecord(HDC, HANDLETABLE*, const ENHMETARECORD* record,
                                      int, LPARAM data) {
    auto& audit = *reinterpret_cast<CampaignPaintAudit*>(data);
    if (record->iType == EMR_BITBLT) {
        const auto& copy = *reinterpret_cast<const EMRBITBLT*>(record);
        ++audit.copies;
        audit.completeFrameOnly = audit.completeFrameOnly && copy.dwRop == SRCCOPY
            && copy.cbBmiSrc > 0 && copy.cbBitsSrc > 0
            && copy.xDest == 0 && copy.yDest == 0
            && copy.cxDest == audit.size.x && copy.cyDest == audit.size.y;
    } else if (record->iType != EMR_HEADER && record->iType != EMR_EOF) {
        // All clearing, scaling, text and artwork must stay in offscreen DCs.
        // Inspecting the drawing command stream catches intermediate black
        // frames that a screenshot of the finished paint cannot reveal.
        audit.completeFrameOnly = false;
    }
    return 1;
}

bool runCampaignMenuPaintSmoke(CampaignMenuState& state, std::string& error) {
    HDC screen = GetDC(nullptr);
    if (!screen) { error = "paint audit could not acquire a reference DC"; return false; }
    bool valid = true;
    for (const POINT size : std::array<POINT, 3>{{{800, 600}, {1024, 640}, {1280, 720}}}) {
        HDC recording = CreateEnhMetaFileW(screen, nullptr, nullptr, nullptr);
        if (!recording) { valid = false; break; }
        drawCampaignMenu(state, recording, RECT{0, 0, size.x, size.y});
        HENHMETAFILE frame = CloseEnhMetaFile(recording);
        CampaignPaintAudit audit{size};
        const bool recorded = frame && EnumEnhMetaFile(nullptr, frame,
            auditCampaignPaintRecord, &audit, nullptr);
        if (frame) DeleteEnhMetaFile(frame);
        if (!recorded || !audit.completeFrameOnly || audit.copies != 1) {
            error = "paint exposed an intermediate frame at " + std::to_string(size.x)
                + "x" + std::to_string(size.y) + " (copies=" + std::to_string(audit.copies) + ")";
            valid = false;
            break;
        }
    }
    ReleaseDC(nullptr, screen);
    if (!valid && error.empty()) error = "paint audit could not record the frame";
    return valid;
}
