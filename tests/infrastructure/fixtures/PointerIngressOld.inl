namespace frozenPointerIngress {
using Contract::Model;
using POINT=RuntimeInputPixelPoint;
using RECT=RuntimeInputPixelRect;
using RuntimeDiplomacyRow=RuntimeDiplomacyPointerRow;
using UINT=std::uint32_t;
using WPARAM=std::uint64_t;
using LPARAM=std::uint64_t;
using HWND=int;
constexpr int FALSE=0,WHEEL_DELTA=120,GWLP_USERDATA=0,RDW_INVALIDATE=1,RDW_NOERASE=2,VK_RETURN=13;
constexpr UINT WM_MOUSEMOVE=0x200,WM_LBUTTONDOWN=0x201,WM_LBUTTONUP=0x202,WM_LBUTTONDBLCLK=0x203;
constexpr UINT WM_RBUTTONDOWN=0x204,WM_RBUTTONUP=0x205,WM_RBUTTONDBLCLK=0x206;
constexpr UINT WM_MBUTTONDOWN=0x207,WM_MBUTTONUP=0x208,WM_MBUTTONDBLCLK=0x209;
constexpr UINT WM_MOUSEWHEEL=0x20A,WM_XBUTTONDOWN=0x20B,WM_XBUTTONUP=0x20C,WM_XBUTTONDBLCLK=0x20D,WM_MOUSEHWHEEL=0x20E;
constexpr UINT WM_CAPTURECHANGED=0x215,WM_CANCELMODE=0x1F,WM_KILLFOCUS=8,WM_ACTIVATEAPP=0x1C,WM_KEYDOWN=0x100;
enum class B{Finished,Campaign,Banner,Menu,Chat,Diplomacy,Beacon,Explicit,Taunts,LeftActive,LeftMoved,RightActive,RightMoved,Pressed,Hovered,Repaint};
struct BoolSlot {
 Model* m;B tag;
 operator bool()const{
  switch(tag){
  case B::Finished:return m->finished();case B::Campaign:return m->campaignMode();case B::Banner:return m->bannerVisible();
  case B::Menu:return m->menuOpen();case B::Chat:return m->chatOpen();case B::Diplomacy:return m->diplomacyOpen();
  case B::Explicit:return m->chatRecipientsExplicit();case B::Taunts:return m->tauntsEnabled();case B::Pressed:return m->scoreContinuePressed();
  default:throw std::logic_error("unexpected legacy Boolean read");
  }
 }
 bool operator=(bool v){
  switch(tag){
  case B::Chat:m->setChatOpen(v);break;case B::Diplomacy:m->setDiplomacyOpen(v);break;
  case B::Beacon:m->setBeaconEditing(v);break;case B::Explicit:m->setChatRecipientsExplicit(v);break;case B::Taunts:m->setTauntsEnabled(v);break;
  case B::LeftActive:m->setLeftActive(v);break;case B::LeftMoved:m->setLeftMoved(v);break;
  case B::RightActive:m->setRightActive(v);break;case B::RightMoved:m->setRightMoved(v);break;
  case B::Pressed:m->setScoreContinuePressed(v);break;case B::Hovered:m->setScoreContinueHovered(v);break;
  case B::Repaint:if(!v)throw std::logic_error("unexpected false repaint");m->requestRepaint();break;
  default:throw std::logic_error("unexpected legacy Boolean write");
  }return v;
 }
};
enum class I{Pressed,Hovered,Row};
struct IntSlot {
 Model* m;I tag;
 operator int()const{if(tag==I::Pressed)return m->diplomacyPressedControl();throw std::logic_error("unexpected legacy integer read");}
 int operator=(int v){if(tag==I::Pressed)m->setDiplomacyPressedControl(v);else if(tag==I::Hovered)m->setDiplomacyHoveredControl(v);else m->setDiplomacyRow(v);return v;}
};
struct MaskSlot {
 Model* m;
 operator std::uint8_t()const{return m->chatRecipients();}
 std::uint8_t operator=(std::uint8_t v){m->setChatRecipients(v);return v;}
 void operator^=(std::uint8_t v){m->setChatRecipients(static_cast<std::uint8_t>(m->chatRecipients()^v));}
 void operator|=(std::uint8_t v){m->setChatRecipients(static_cast<std::uint8_t>(m->chatRecipients()|v));}
};
struct DraftSlot{Model* m;void clear(){m->clearChatDraft();}};
struct Ui {
 BoolSlot diplomacyOpen,chatOpen,beaconEditing,chatRecipientsExplicit,tauntsEnabled;
 IntSlot diplomacyRow,diplomacyPressedControl,diplomacyHoveredControl;
 MaskSlot chatRecipients;DraftSlot chatDraft;
 explicit Ui(Model& m):diplomacyOpen{&m,B::Diplomacy},chatOpen{&m,B::Chat},beaconEditing{&m,B::Beacon},
  chatRecipientsExplicit{&m,B::Explicit},tauntsEnabled{&m,B::Taunts},diplomacyRow{&m,I::Row},
  diplomacyPressedControl{&m,I::Pressed},diplomacyHoveredControl{&m,I::Hovered},chatRecipients{&m},chatDraft{&m}{}
};
struct Pointer{BoolSlot active,moved;};
struct Presentation{BoolSlot bannerVisible,continuePressed,continueHovered;};
struct EnumSlot{Model* m;operator RuntimeBuildKind()const{return m->activeTab();}};
struct ArraySlot {
 Model* m;bool wheel;int index;
 operator int()const{return wheel?m->wheelAt(index):m->pageAt(index);}
 int operator=(int v){if(wheel)m->setWheelAt(index,v);else m->setPageAt(index,v);return v;}
};
struct Array {
 Model* m;bool wheel;
 std::size_t size()const{return static_cast<std::size_t>(wheel?m->wheelSlotCount():m->pageSlotCount());}
 ArraySlot operator[](int index)const{return {m,wheel,index};}
};
struct Status {Model* m;void operator=(const std::string& text){m->status(text);}};
struct RuntimeWindowState {
 Model& host;Ui playerCommunication;Pointer leftPointer,rightPointer;Presentation resultPresentation;
 BoolSlot skirmishMatchFinished,campaignMode,runtimeMenuOpen,repaintRequested;
 EnumSlot activeBuildTab;Array buildPageByTab,buildWheelRemainderByTab;Status interactionStatus;
 struct Economy{std::string owner="Self";}economy;
 explicit RuntimeWindowState(Model& m):host(m),playerCommunication(m),
  leftPointer{{&m,B::LeftActive},{&m,B::LeftMoved}},rightPointer{{&m,B::RightActive},{&m,B::RightMoved}},
  resultPresentation{{&m,B::Banner},{&m,B::Pressed},{&m,B::Hovered}},
  skirmishMatchFinished{&m,B::Finished},campaignMode{&m,B::Campaign},runtimeMenuOpen{&m,B::Menu},repaintRequested{&m,B::Repaint},
  activeBuildTab{&m},buildPageByTab{&m,false},buildWheelRemainderByTab{&m,true},interactionStatus{&m}{}
};
thread_local Model* current=nullptr;
thread_local RuntimeWindowState* currentState=nullptr;
struct Binding {
 Model* previous;RuntimeWindowState* previousState;
 Binding(Model& m,RuntimeWindowState& s):previous(current),previousState(currentState){current=&m;currentState=&s;}
 ~Binding(){current=previous;currentState=previousState;}
};
UINT nativeMessage(RuntimePointerIngressEvent event){
 switch(event){
 case RuntimePointerIngressEvent::PointerMove:return WM_MOUSEMOVE;
 case RuntimePointerIngressEvent::LeftButtonDown:return WM_LBUTTONDOWN;
 case RuntimePointerIngressEvent::LeftButtonUp:return WM_LBUTTONUP;
 case RuntimePointerIngressEvent::LeftButtonDoubleClick:return WM_LBUTTONDBLCLK;
 case RuntimePointerIngressEvent::RightButtonDown:return WM_RBUTTONDOWN;
 case RuntimePointerIngressEvent::RightButtonUp:return WM_RBUTTONUP;
 case RuntimePointerIngressEvent::RightButtonDoubleClick:return WM_RBUTTONDBLCLK;
 case RuntimePointerIngressEvent::MiddleButtonDown:return WM_MBUTTONDOWN;
 case RuntimePointerIngressEvent::MiddleButtonUp:return WM_MBUTTONUP;
 case RuntimePointerIngressEvent::MiddleButtonDoubleClick:return WM_MBUTTONDBLCLK;
 case RuntimePointerIngressEvent::AuxiliaryButtonDown:return WM_XBUTTONDOWN;
 case RuntimePointerIngressEvent::AuxiliaryButtonUp:return WM_XBUTTONUP;
 case RuntimePointerIngressEvent::AuxiliaryButtonDoubleClick:return WM_XBUTTONDBLCLK;
 case RuntimePointerIngressEvent::VerticalWheel:return WM_MOUSEWHEEL;
 case RuntimePointerIngressEvent::HorizontalWheel:return WM_MOUSEHWHEEL;
 case RuntimePointerIngressEvent::CaptureChanged:return WM_CAPTURECHANGED;
 case RuntimePointerIngressEvent::CancelMode:return WM_CANCELMODE;
 case RuntimePointerIngressEvent::FocusLost:return WM_KILLFOCUS;
 case RuntimePointerIngressEvent::ApplicationDeactivated:return WM_ACTIVATEAPP;
 default:return 0;
 }
}
unsigned LOWORD(LPARAM v){return static_cast<unsigned>(v&0xffffU);}
unsigned HIWORD(LPARAM v){return static_cast<unsigned>((v>>16U)&0xffffU);}
POINT pointFromMouseMessage(LPARAM v){return {static_cast<short>(LOWORD(v)),static_cast<short>(HIWORD(v))};}
LPARAM packedPoint(POINT p){return static_cast<std::uint16_t>(p.x)|(static_cast<LPARAM>(static_cast<std::uint16_t>(p.y))<<16U);}
bool pointInRectInclusive(RECT r,POINT p){return p.x>=r.left&&p.x<=r.right&&p.y>=r.top&&p.y<=r.bottom;}
int PtInRect(const RECT* r,POINT p){return p.x>=r->left&&p.x<r->right&&p.y>=r->top&&p.y<r->bottom;}
HWND GetCapture(){return current->ownsCapture()?1:0;}
void ReleaseCapture(){current->releaseCapture();}
void SetCapture(HWND){current->setCapture();}
void legacyScoreClientRect(HWND,RECT* r){*r=current->scoreClientRect();}
void legacyDiplomacyClientRect(HWND,RECT* r){*r=current->diplomacyClientRect();}
void legacyWheelClientRect(HWND,RECT*){}
RECT runtimeMatchScoreContinueRect(RECT r){return current->scoreContinueRect(r);}
void SendMessageW(HWND,UINT,int,int){current->sendScoreReturn();}
void InvalidateRect(HWND,std::nullptr_t,int){current->invalidate();}
void RedrawWindow(HWND,std::nullptr_t,std::nullptr_t,int){current->redraw();}
std::intptr_t GetWindowLongPtrW(HWND,int){return reinterpret_cast<std::intptr_t>(currentState);}
int DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 1;}
int GET_WHEEL_DELTA_WPARAM(WPARAM){return current->wheelDelta();}
RECT runtimeSidebarRect(RECT){return current->sidebarRect();}
RECT runtimeDiplomacyControlRect(RECT client,int id){return current->controlRect(client,id);}
RECT runtimeDiplomacyCheckboxRect(RECT client,int id){return current->checkboxRect(client,id);}
std::vector<RuntimeDiplomacyRow> runtimeDiplomacyRows(const RuntimeWindowState& s){return s.host.rows();}
bool runtimePlayerChatRecipientsAvailable(const RuntimeWindowState& s){return s.host.chatRecipientsAvailable();}
std::uint8_t runtimePlayerChatSlotsMask(const RuntimeWindowState& s){return s.host.chatSlotsMask();}
std::optional<std::size_t> runtimePlayerChatSlot(const RuntimeWindowState& s,const std::string&){return s.host.ownChatSlot();}
void runtimePlayerToggleAlliance(RuntimeWindowState& s,const std::string& owner){s.host.toggleAlliance(owner);}
std::vector<int> runtimeFilteredBuildCatalogIndices(const RuntimeWindowState& s){return std::vector<int>(static_cast<std::size_t>(s.host.filteredCatalogCount()));}
std::vector<int> runtimeVisibleSuperWeapons(const RuntimeWindowState& s){return std::vector<int>(static_cast<std::size_t>(s.host.visibleSuperWeaponCount()));}
int runtimeBuildButtonsPerPage(RECT r){return current->buttonsPerPage(r);}
constexpr std::array<int,7> runtimeDiplomacyChatControls={1750,1751,1752,1753,1762,1763,1764};

// BEGIN FROZEN diplomacy-local-hit-mouse
void closeRuntimeDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=false; ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    state.rightPointer.active=state.rightPointer.moved=false;
    state.repaintRequested=true;
}
void openRuntimeDiplomacyPage(RuntimeWindowState& state) {
    auto& ui=state.playerCommunication;
    ui.diplomacyOpen=true; ui.diplomacyRow=1;
    ui.diplomacyPressedControl=ui.diplomacyHoveredControl=0;
    ui.chatOpen=ui.beaconEditing=false; ui.chatDraft.clear();
    state.leftPointer.active=state.leftPointer.moved=false;
    state.rightPointer.active=state.rightPointer.moved=false;
    state.repaintRequested=true;
}

bool runtimeDiplomacyAllianceEnabled(const RuntimeWindowState& state, const RuntimeDiplomacyRow& row) {
    return state.host.allianceEnabled(row);
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
// END FROZEN diplomacy-local-hit-mouse

// BEGIN FROZEN sidebar-tab-index
int runtimeBuildTabIndex(RuntimeBuildKind kind) {
    switch (kind) {
    case RuntimeBuildKind::Building:
        return 0;
    case RuntimeBuildKind::Defense:
        return 1;
    case RuntimeBuildKind::Infantry:
        return 2;
    case RuntimeBuildKind::Unit:
        return 3;
    }
    return 0;
}
// END FROZEN sidebar-tab-index

// BEGIN FROZEN sidebar-page-family
int runtimeActiveBuildPage(const RuntimeWindowState& state) {
    const int tab = runtimeBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildPageByTab.size())) {
        return 0;
    }
    return std::max(0, static_cast<int>(state.buildPageByTab[tab]));
}

void runtimeSetActiveBuildPage(RuntimeWindowState& state, int page) {
    const int tab = runtimeBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildPageByTab.size())) {
        return;
    }
    state.buildPageByTab[tab] = std::max(0, page);
}

int runtimeBuildPageCount(
    const RuntimeWindowState& state,
    const RECT& sidebar
) {
    const std::vector<int> filtered = runtimeFilteredBuildCatalogIndices(state);
    const int superWeaponCount = state.activeBuildTab == RuntimeBuildKind::Defense
        ? static_cast<int>(runtimeVisibleSuperWeapons(state).size())
        : 0;
    const int perPage = runtimeBuildButtonsPerPage(sidebar);
    return std::max(
        1,
        (static_cast<int>(filtered.size()) + superWeaponCount + perPage - 1)
            / perPage
    );
}

void runtimeClampActiveBuildPage(
    RuntimeWindowState& state,
    const RECT& sidebar
) {
    runtimeSetActiveBuildPage(
        state,
        std::min(
            runtimeActiveBuildPage(state),
            runtimeBuildPageCount(state, sidebar) - 1
        )
    );
}

bool scrollRuntimeSidebarWheel(RuntimeWindowState& state, const RECT& sidebar, int delta) {
    const int tab = runtimeBuildTabIndex(state.activeBuildTab);
    if (tab < 0 || tab >= static_cast<int>(state.buildWheelRemainderByTab.size())) return false;
    const int before = runtimeActiveBuildPage(state);
    runtimeClampActiveBuildPage(state, sidebar);
    auto remainder = state.buildWheelRemainderByTab[tab];
    const int pages = runtimeBuildPageCount(state, sidebar);
    if (pages <= 1) {
        remainder = 0;
        return before != runtimeActiveBuildPage(state);
    }
    const int accumulated = remainder + delta;
    const int steps = accumulated / WHEEL_DELTA;
    remainder = accumulated % WHEEL_DELTA;
    const int previous = runtimeActiveBuildPage(state);
    const int next = std::clamp(previous - steps, 0, pages - 1);
    if (steps != 0 && next == previous) remainder = 0;
    runtimeSetActiveBuildPage(state, next);
    if (next == before) return false;
    state.interactionStatus = "build page " + std::to_string(next + 1) + "/" + std::to_string(pages);
    return true;
}
// END FROZEN sidebar-page-family

int ingress(Model& host,RuntimePointerIngressInput input){
 RuntimeWindowState state(host);Binding binding(host,state);
 RuntimeWindowState* campaignState=&state;const HWND window=1;
 const UINT message=nativeMessage(input.event);const WPARAM wParam=FALSE;const LPARAM lParam=packedPoint(input.point);
// BEGIN FROZEN pointer-ingress-outer
    if (campaignState != nullptr && campaignState->skirmishMatchFinished) {
        if (GetCapture() == window) {
            ReleaseCapture();
        }
        if (!campaignState->campaignMode && !campaignState->resultPresentation.bannerVisible
            && (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
            RECT client;
            legacyScoreClientRect(window, &client);
            const RECT button = runtimeMatchScoreContinueRect(client);
            const POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            const bool inside = PtInRect(&button, point) != FALSE;
            auto& presentation = campaignState->resultPresentation;
            if (message == WM_LBUTTONUP) {
                const bool activate = presentation.continuePressed && inside;
                presentation.continuePressed = false;
                campaignState->repaintRequested = true;
                if (activate) SendMessageW(window, WM_KEYDOWN, VK_RETURN, 0);
            } else {
                if (message == WM_LBUTTONDOWN) presentation.continuePressed = inside;
                presentation.continueHovered = inside;
                campaignState->repaintRequested = true;
            }
            return 0;
        }
        switch (message) {
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
            return 0;
        default:
            break;
        }
    }
    if (campaignState && !campaignState->runtimeMenuOpen && !campaignState->skirmishMatchFinished
        && (campaignState->playerCommunication.chatOpen || campaignState->playerCommunication.diplomacyOpen)) {
        if (campaignState->playerCommunication.diplomacyOpen) {
            if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || message == WM_KILLFOCUS
                || (message == WM_ACTIVATEAPP && wParam == FALSE)) {
                campaignState->playerCommunication.diplomacyPressedControl = 0;
                if (message != WM_CAPTURECHANGED && GetCapture() == window) ReleaseCapture();
                return 0;
            }
            switch (message) {
            case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
            case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK:
            case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
                RECT client{}; legacyDiplomacyClientRect(window, &client);
                handleRuntimeDiplomacyMouse(*campaignState, client, message, pointFromMouseMessage(lParam));
                if (message == WM_LBUTTONDOWN) SetCapture(window);
                if (message == WM_LBUTTONUP || message == WM_RBUTTONUP) {
                    if (GetCapture() == window) ReleaseCapture();
                }
                campaignState->leftPointer.active = campaignState->leftPointer.moved = false;
                InvalidateRect(window,nullptr,FALSE); return 0;
            }
            default: break;
            }
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK
            || message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_MOUSEWHEEL) {
            auto& ui = campaignState->playerCommunication;
            if (message == WM_RBUTTONUP) {
                ui.chatOpen = ui.diplomacyOpen = ui.beaconEditing = false; ui.chatDraft.clear();
            }
            campaignState->leftPointer.active = campaignState->leftPointer.moved = false;
            campaignState->repaintRequested = true; InvalidateRect(window,nullptr,FALSE); return 0;
        }
    }
// END FROZEN pointer-ingress-outer
 return 1;
}

int wheel(Model& host){
 RuntimeWindowState state(host);Binding binding(host,state);
 const HWND window=1;const UINT message=WM_MOUSEWHEEL;const WPARAM wParam=0;const LPARAM lParam=0;
 switch(message){
// BEGIN FROZEN sidebar-wheel-native-case
    case WM_MOUSEWHEEL:
    {
        RuntimeWindowState* state = reinterpret_cast<RuntimeWindowState*>(
            GetWindowLongPtrW(window, GWLP_USERDATA)
        );
        if (state != nullptr) {
            if (state->runtimeMenuOpen) {
                return 0;
            }
            const int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT clientRect{};
            legacyWheelClientRect(window, &clientRect);
            if (!scrollRuntimeSidebarWheel(*state, runtimeSidebarRect(clientRect), wheelDelta)) return 0;
            state->repaintRequested = true;
            RedrawWindow(
                window,
                nullptr,
                nullptr,
                    RDW_INVALIDATE | RDW_NOERASE
            );
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
// END FROZEN sidebar-wheel-native-case
 default:return 1;
 }
}
} // namespace frozenPointerIngress

