// Included inside RuntimeCampaignMenu.cpp's anonymous namespace.
constexpr int kCampaignShellWidth = 800;
constexpr int kCampaignShellHeight = 600;
constexpr RECT kCampaignBackButton{653, 543, 779, 569};
constexpr RECT kCampaignMapViewport{8, 40, 624, 502};
constexpr RECT kCampaignSlider{206, 230, 422, 254};

enum class CampaignControlAction { Back, Confirm, Edition, Campaign, Previous, Next, Row, Difficulty };

struct CampaignControl {
    RECT rect{};
    CampaignControlAction action = CampaignControlAction::Back;
    int index = 0;
    std::wstring label;
    bool enabled = true;
    bool selected = false;
};

bool campaignShowsMap(const CampaignMenuState& state) {
    return state.page == CampaignPage::Missions
        && state.mapPresentation.usesClassicSurface()
        && !state.mapPresentation.authoredReconstruction
        && state.mapPresentation.vqaFrame.valid();
}

RECT campaignShellClientRect(const RECT& client) {
    const int width = std::max(1L, client.right - client.left);
    const int height = std::max(1L, client.bottom - client.top);
    const double scale = std::min(width / 800.0, height / 600.0);
    const int fittedWidth = std::max(1, static_cast<int>(800 * scale));
    const int fittedHeight = std::max(1, static_cast<int>(600 * scale));
    const int left = client.left + (width - fittedWidth) / 2;
    const int top = client.top + (height - fittedHeight) / 2;
    return RECT{left, top, left + fittedWidth, top + fittedHeight};
}

POINT campaignShellPoint(const RECT& client, int x, int y) {
    const RECT fit = campaignShellClientRect(client);
    if (x < fit.left || x >= fit.right || y < fit.top || y >= fit.bottom) {
        return POINT{-1, -1};
    }
    return POINT{(x - fit.left) * 800 / std::max(1L, fit.right - fit.left),
                 (y - fit.top) * 600 / std::max(1L, fit.bottom - fit.top)};
}

POINT campaignMapPoint(POINT shell) {
    if (!PtInRect(&kCampaignMapViewport, shell)) return POINT{-1, -1};
    return POINT{(shell.x - kCampaignMapViewport.left) * kBaseWidth
                     / (kCampaignMapViewport.right - kCampaignMapViewport.left),
                 (shell.y - kCampaignMapViewport.top) * kBaseHeight
                     / (kCampaignMapViewport.bottom - kCampaignMapViewport.top)};
}

int campaignListStart(int selected, int count) {
    return std::clamp(selected - 4, 0, std::max(0, count - 9));
}

std::vector<CampaignControl> campaignControls(const CampaignMenuState& state) {
    using Action = CampaignControlAction;
    std::vector<CampaignControl> controls;
    controls.push_back({kCampaignBackButton, Action::Back, 0, L"返回"});
    const CampaignDefinition* campaign = selectedCampaign(state);
    const CampaignMissionDefinition* mission = selectedMission(state);
    const auto button = [&](int slot, Action action, int index,
                            const std::wstring& label, bool enabled = true,
                            bool selected = false) {
        controls.push_back({RECT{653, 207 + slot * 42, 779, 233 + slot * 42},
                            action, index, label, enabled, selected});
    };
    bool canContinue = campaign != nullptr;
    if (state.page == CampaignPage::Saves) canContinue = !state.saves.empty();
    else if (state.page != CampaignPage::Campaigns && !campaignShowsMap(state)) {
        canContinue = campaign && mission && missionUnlocked(state, *campaign, *mission);
    }
    button(state.page == CampaignPage::Campaigns ? 6 : 0, Action::Confirm, 0,
        state.page == CampaignPage::Briefing ? L"开始任务"
        : state.page == CampaignPage::Saves ? L"载入"
        : campaignShowsMap(state)
            && state.mapPresentation.phase != CampaignMapPresentationPhase::Interactive
            ? L"跳过演出" : L"继续", canContinue);

    if (state.page == CampaignPage::Campaigns) {
        std::set<std::string> sources;
        int sourceSlot = 0;
        int iconSlot = 0;
        for (int index = 0; index < static_cast<int>(state.database.campaigns.size()); ++index) {
            const auto& candidate = state.database.campaigns[static_cast<std::size_t>(index)];
            if (!isTutorialCampaign(&candidate) && sources.insert(lowerAscii(candidate.sourceId)).second) {
                button(sourceSlot++, Action::Edition, index,
                    campaignSourceIs(&candidate, "yr") ? L"尤里的复仇"
                    : campaignSourceIs(&candidate, "ra2") ? L"红色警戒 2" : widen(candidate.sourceId),
                    true, campaign && lowerAscii(campaign->sourceId) == lowerAscii(candidate.sourceId));
            }
            if (!campaign || lowerAscii(candidate.sourceId) != lowerAscii(campaign->sourceId)) continue;
            if (isTutorialCampaign(&candidate)) {
                button(3, Action::Campaign, index, L"新兵训练");
            } else {
                const int top = 51 + iconSlot++ * 286;
                controls.push_back({RECT{187, top, 448, top + 130}, Action::Campaign,
                    index, resolvedCampaignName(state, &candidate), true, index == state.campaignIndex});
            }
        }
    } else if (state.page == CampaignPage::Missions && !campaignShowsMap(state) && campaign) {
        const int count = static_cast<int>(campaign->missions.size());
        const int start = campaignListStart(state.missionIndex, count);
        for (int row = 0; row < 9 && start + row < count; ++row) {
            const int index = start + row;
            const auto& item = campaign->missions[static_cast<std::size_t>(index)];
            const bool unlocked = missionUnlocked(state, *campaign, item);
            controls.push_back({RECT{42, 66 + row * 34, 590, 98 + row * 34}, Action::Row,
                index, std::to_wstring(item.ordinal) + L". " + missionLabel(item, state.csf)
                    + (unlocked ? L"" : L"  [未解锁]"), item.available, index == state.missionIndex});
        }
        button(1, Action::Previous, 0, L"上一任务", count > 1);
        button(2, Action::Next, 0, L"下一任务", count > 1);
    } else if (state.page == CampaignPage::Saves) {
        const int count = static_cast<int>(state.saves.size());
        const int start = campaignListStart(state.saveIndex, count);
        for (int row = 0; row < 9 && start + row < count; ++row) {
            const int index = start + row;
            controls.push_back({RECT{42, 66 + row * 34, 590, 98 + row * 34}, Action::Row,
                index, loadSaveTitle(state.saves[static_cast<std::size_t>(index)]), true,
                index == state.saveIndex});
        }
        button(1, Action::Previous, 0, L"上一存档", count > 1);
        button(2, Action::Next, 0, L"下一存档", count > 1);
    } else if (state.page == CampaignPage::Briefing) {
        button(1, Action::Previous, 0, L"向上滚动");
        button(2, Action::Next, 0, L"向下滚动");
    }
    if (state.page == CampaignPage::Campaigns || state.page == CampaignPage::Difficulty) {
        for (int index = 0; index < static_cast<int>(difficultyNames().size()); ++index) {
            const int x = 178 + index * 71;
            controls.push_back({RECT{x, 267, x + 69, 295}, Action::Difficulty, index,
                difficultyDisplayName(state, index), true, index == state.difficultyIndex});
        }
        // Four contiguous slider regions share the same actions as the labels.
        for (int index = 0; index < 4; ++index) {
            const LONG left = kCampaignSlider.left + index * 54;
            controls.push_back({RECT{left, kCampaignSlider.top, left + 54, kCampaignSlider.bottom},
                Action::Difficulty, index, L"", true, index == state.difficultyIndex});
        }
    }
    return controls;
}

int campaignControlAt(const CampaignMenuState& state, POINT point) {
    const auto controls = campaignControls(state);
    for (int index = 0; index < static_cast<int>(controls.size()); ++index) {
        if (controls[static_cast<std::size_t>(index)].enabled
            && PtInRect(&controls[static_cast<std::size_t>(index)].rect, point)) return index;
    }
    return -1;
}

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
    HDC presentation = CreateCompatibleDC(destination);
    HBITMAP bitmap = CreateCompatibleBitmap(destination, width, height);
    if (!presentation || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (presentation) DeleteDC(presentation);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(presentation, bitmap);
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
    SelectObject(presentation, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(presentation);
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
                for (int y = control.rect.top; y < std::min(600L, control.rect.bottom); ++y) {
                    for (int x = control.rect.left; x < std::min(800L, control.rect.right); ++x) {
                        const auto at = static_cast<std::size_t>((y * 800 + x) * 4);
                        for (int channel = 0; channel < 3; ++channel) surface[at + channel] /= 2;
                    }
                }
            }
        } else if (control.action == CampaignControlAction::Campaign) {
            const auto& campaign = state.database.campaigns[static_cast<std::size_t>(control.index)];
            if (state.campaignLogoFrames.size() >= 2) {
                blitCampaignShellImage(surface, state.campaignLogoFrames[isSovietCampaign(&campaign) ? 1 : 0],
                    control.rect);
            }
        }
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 800;
    info.bmiHeader.biHeight = -600;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, 800, 600);
    if (!buffer || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (buffer) DeleteDC(buffer);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(buffer, bitmap);
    SetDIBitsToDevice(buffer, 0, 0, 800, 600, 0, 0, 0, 600, surface.data(), &info, DIB_RGB_COLORS);
    HFONT body = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT smallFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    const auto label = [&](const std::wstring& value, RECT rect, bool center = false,
                            COLORREF color = RGB(255, 222, 25), bool wrap = false) {
        drawText(buffer, wrap ? smallFont : body, value, rect, color,
            DT_NOPREFIX | (center ? DT_CENTER : DT_LEFT)
                | (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS));
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
        drawCampaignMapTargets(state, buffer, kCampaignMapViewport);
    }
    for (std::size_t index = 0; index < controls.size(); ++index) {
        const auto& control = controls[index];
        const bool focused = state.hoveredControl == static_cast<int>(index)
            || state.focusedControl == static_cast<int>(index);
        RECT rect = control.rect;
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
            FrameRect(buffer, &control.rect, edge);
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
            RECT bounds{48, 118, 585, 118};
            HGDIOBJ oldFont = SelectObject(buffer, smallFont);
            DrawTextW(buffer, briefing.c_str(), static_cast<int>(briefing.size()), &bounds,
                DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(buffer, oldFont);
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
    drawText(buffer, smallFont, hint, RECT{12, 570, 620, 596}, kBrightGold,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    presentCampaignMenuFrame(dc, buffer, client);
    DeleteObject(body);
    DeleteObject(smallFont);
    SelectObject(buffer, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(buffer);
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
