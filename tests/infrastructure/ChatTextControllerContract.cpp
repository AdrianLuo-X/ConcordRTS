#include "ui/input/RuntimeChatTextController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(WINAPI)
#error Chat text standard contract must not include native window headers.
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_same_v<decltype(RuntimePlayerCommunicationState::chatDraft), std::u16string>);
static_assert(std::is_same_v<decltype(RuntimeChatTextCharacterInput::rawCharacter), std::uint64_t>);
static_assert(std::is_same_v<decltype(RuntimeChatTextKeyInput::key), std::int32_t>);
static_assert(std::has_virtual_destructor_v<RuntimeChatTextHost>);
namespace {
using State = RuntimePlayerCommunicationState;
using Command = RuntimeMultiplayerGameplayPlayerCommunicationCommand;
using Cell = RuntimeMultiplayerGameplayCell;
struct Statistics { std::size_t checks=0, failures=0, cases=0, characters=0, keys=0, codec=0, traceRows=0, dynamic=0, nested=0, exceptions=0; } stats;
void check(bool ok, const std::string& label) {
    ++stats.checks; if (!ok) { ++stats.failures; std::cerr << "FAIL chat-text: " << label << '\n'; }
}
std::string hex(std::string_view value) {
    constexpr char digits[]="0123456789abcdef"; std::string out;
    for (unsigned char c:value) { out+=digits[c/16]; out+=digits[c%16]; } return out;
}
std::string units(std::u16string_view value) {
    std::ostringstream out; for (char16_t c:value) out<<static_cast<unsigned>(c)<<','; return out.str();
}
std::string cell(Cell value) { return std::to_string(value.x)+","+std::to_string(value.y); }
std::string command(const Command& value) {
    return std::to_string(static_cast<int>(value.kind))+":"+hex(value.text)+":"+cell(value.cell)+":"+std::to_string(value.value);
}
// Independent two-pass reference. It first validates all UTF-16 into scalar
// values, then emits base-64 digits. It never calls the production encoder.
std::string legacyStrictUtf8(std::u16string_view source) {
    std::vector<std::uint32_t> scalars;
    for (std::size_t i=0;i<source.size();++i) {
        const auto first=static_cast<std::uint32_t>(source[i]);
        if (first>=0xdc00 && first<=0xdfff) return {};
        if (first>=0xd800 && first<=0xdbff) {
            if (i+1>=source.size()) return {};
            const auto second=static_cast<std::uint32_t>(source[++i]);
            if (second<0xdc00 || second>0xdfff) return {};
            scalars.push_back(65536+(first-55296)*1024+second-56320);
        } else scalars.push_back(first);
    }
    std::string output;
    const auto continuation=[&](std::uint32_t digit){output.push_back(static_cast<char>(128+digit%64));};
    for (auto scalar:scalars) {
        if (scalar<128) output.push_back(static_cast<char>(scalar));
        else if (scalar<2048) {output.push_back(static_cast<char>(192+scalar/64));continuation(scalar);}
        else if (scalar<65536) {output.push_back(static_cast<char>(224+scalar/4096));continuation(scalar/64);continuation(scalar);}
        else {output.push_back(static_cast<char>(240+scalar/262144));continuation(scalar/4096);continuation(scalar/64);continuation(scalar);}
    }
    return output.size()>runtimeMultiplayerGameplayMaximumIdentifierBytes ? std::string{} : output;
}
std::string trimAsciiLocal(const std::string& value) {
    std::size_t first=0;
    while(first<value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last=value.size();
    while(last>first && std::isspace(static_cast<unsigned char>(value[last-1]))) --last;
    return value.substr(first,last-first);
}
struct Fixture {
    State playerCommunication;
    bool menu=false, valid=true, accepted=true, repaint=false, invalidated=false, diplomacyReturn=false;
    std::uint8_t mask=0xa5;
    std::vector<Command> commands;
    std::array<std::size_t,7> calls{};
    std::vector<bool> nestedResults;
    std::uint64_t authority=0xfedcba9876543210ULL;
    RuntimeChatTextCharacterInput characterInput{65};
    RuntimeChatTextKeyInput keyInput{13,false};
    int callback=0, depth=0, throwAt=0, eventCount=0;
};
Fixture seed() {
    Fixture f; auto& u=f.playerCommunication;
    u.chatOpen=true; u.chatAllies=true; u.chatDraft=u" old draft ";
    u.diplomacyOpen=true;u.diplomacyRow=-7;u.diplomacyPressedControl=23;u.diplomacyHoveredControl=31;
    u.chatRecipientsExplicit=true;u.chatRecipients=0xa5;u.tauntsEnabled=false;
    u.diplomacyBackgroundSource="background";u.diplomacyControlSources={"first","second"};
    u.beaconTargeting=true;u.selectedBeacon=Cell{9,-11};
    u.lines.push_back({"owner","line",true,73});u.beacons.push_back({"owner",{9,-11},-1,"beacon",4});
    u.cheerUntilTick.emplace(7,99);u.nextFeedbackTick.emplace(std::make_pair("owner",RuntimePlayerCommunicationKind::Taunt),123);
    return f;
}
std::string snapshot(const Fixture& f) {
    const auto& u=f.playerCommunication;std::ostringstream o;
    o<<u.chatOpen<<u.chatAllies<<':'<<units(u.chatDraft)<<':'<<u.diplomacyOpen<<','<<u.diplomacyRow<<','<<u.diplomacyPressedControl<<','<<u.diplomacyHoveredControl
     <<','<<u.chatRecipientsExplicit<<','<<static_cast<unsigned>(u.chatRecipients)<<','<<u.tauntsEnabled<<':'<<hex(u.diplomacyBackgroundSource)<<':';
    for(const auto& v:u.diplomacyControlSources)o<<hex(v)<<',';
    o<<u.beaconTargeting<<':'<<(u.selectedBeacon?cell(*u.selectedBeacon):"none")<<':'<<u.beaconEditing<<';';
    for(const auto& v:u.lines)o<<"l"<<hex(v.owner)<<':'<<hex(v.text)<<':'<<v.allies<<':'<<v.untilTick<<';';
    for(const auto& v:u.beacons)o<<"b"<<hex(v.owner)<<':'<<cell(v.cell)<<':'<<v.untilTick<<':'<<hex(v.text)<<':'<<v.level<<';';
    std::vector<std::pair<int,int>> sorted(u.cheerUntilTick.begin(),u.cheerUntilTick.end());std::sort(sorted.begin(),sorted.end());
    for(const auto& v:sorted)o<<"h"<<v.first<<':'<<v.second<<';';
    for(const auto& v:u.nextFeedbackTick)o<<"n"<<hex(v.first.first)<<':'<<static_cast<unsigned>(v.first.second)<<':'<<v.second<<';';
    o<<"host="<<f.menu<<f.valid<<f.accepted<<f.repaint<<f.invalidated<<f.diplomacyReturn<<':'<<static_cast<unsigned>(f.mask)<<';';
    for(const auto& v:f.commands)o<<"c"<<command(v)<<';';
    for(auto v:f.calls)o<<v<<',';
    for(bool v:f.nestedResults)o<<"nested"<<v<<';';
    o<<"opaque="<<f.authority<<':'<<f.characterInput.rawCharacter<<':'<<f.keyInput.key<<','<<f.keyInput.repeated
     <<':'<<f.callback<<':'<<f.depth<<':'<<f.throwAt<<':'<<f.eventCount;
    return o.str();
}
struct Event {
    std::string name,argument,result,before,after;
    bool threw=false;
    bool operator==(const Event&) const=default;
};
class Host;
bool oldModal(Fixture&,int,bool,Host&);
bool oldCharacter(RuntimeChatTextCharacterInput,Fixture&,Host&);
class Host final:public RuntimeChatTextHost {
public:
    Fixture& f;bool old;std::vector<Event> trace;
    Host(Fixture& fixture,bool original):f(fixture),old(original){}
    void callback(const std::string& name);
    template<class F> auto invoke(const std::string& name,const std::string& arg,F effect)->decltype(effect()) {
        const auto index=trace.size();trace.push_back({name,arg,{},snapshot(f),{},false});
        try {
            ++f.eventCount;
            if(f.throwAt==f.eventCount)throw std::runtime_error("primitive:"+name);
            callback(name);
            if constexpr(std::is_void_v<decltype(effect())>) {
                effect();trace[index].result="void";trace[index].after=snapshot(f);
            } else {
                auto value=effect();
                if constexpr(std::is_same_v<decltype(value),Cell>)trace[index].result=cell(value);
                else trace[index].result=std::to_string(value);
                trace[index].after=snapshot(f);return value;
            }
        } catch(...) {trace[index].threw=true;trace[index].result="exception";trace[index].after=snapshot(f);throw;}
    }
    bool menuOpen()override{return invoke("menu","",[&]{++f.calls[0];return f.menu;});}
    bool selectedBeaconValid()override{return invoke("selectedValid","",[&]{++f.calls[1];if(!f.valid)f.playerCommunication.selectedBeacon.reset();return f.valid;});}
    Cell selectedBeaconCell()override{return invoke("selectedCell","",[&]{++f.calls[2];const auto value=*f.playerCommunication.selectedBeacon;if(f.callback==8){f.playerCommunication.selectedBeacon.reset();f.playerCommunication.chatDraft.clear();}return value;});}
    std::uint8_t outgoingChatMask()override{return invoke("mask","",[&]{++f.calls[3];return f.mask;});}
    bool dispatchCommunication(Command value)override{
        return invoke("dispatch",command(value),[&]{++f.calls[4];f.commands.push_back(std::move(value));return f.accepted;});
    }
    void requestRepaint()override{invoke("repaint","",[&]{++f.calls[5];f.repaint=true;});}
    void invalidate()override{invoke("invalidate","",[&]{++f.calls[6];f.invalidated=true;});}
    bool diplomacy(int key,bool repeated){return invoke("diplomacy",std::to_string(key)+","+std::to_string(repeated),[&]{return f.diplomacyReturn;});}
};
bool currentModal(RuntimeChatTextKeyInput input,Fixture& f,Host& host) {
    if(handleRuntimeChatTextKey(input,f.playerCommunication,host))return true;
    return host.diplomacy(input.key,input.repeated);
}
void Host::callback(const std::string& name) {
    auto& u=f.playerCommunication;
    if(f.callback==1 && name=="menu") {u.chatOpen=false;u.chatDraft=u"new after menu";}
    if(f.callback==2 && name=="selectedValid")u.selectedBeacon=Cell{std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()};
    if(f.callback==3 && name=="selectedValid"){u.beaconEditing=false;u.chatAllies=false;u.chatDraft=u"changed";f.mask=0x39;}
    if(f.callback==4 && name=="mask"){u.chatDraft=u"owned text must survive";u.beaconEditing=true;f.keyInput={27,true};}
    if(f.callback==5 && name=="dispatch"){u=State{};u.chatOpen=true;u.chatDraft=u"replacement during dispatch";}
    if(f.callback==6 && name=="repaint")u.chatDraft=u"repaint effect";
    if(f.callback==7 && name=="menu"){f.characterInput.rawCharacter=66;f.keyInput={27,true};}
    if(f.callback==9 && name=="dispatch"){u.beacons.clear();u.selectedBeacon.reset();f.accepted=false;}
    if(f.callback==10 && name=="selectedValid"){u=seed().playerCommunication;u.beaconEditing=true;u.selectedBeacon=Cell{-23,41};}
    if(f.callback==11 && name=="dispatch"){u.chatAllies=!u.chatAllies;f.mask=0;f.characterInput={0xffffffffffffffffULL};}
    if((f.callback==12 && name=="invalidate") || (f.callback==13 && name=="dispatch")
        || (f.callback==14 && name=="repaint")) {
        if(f.depth==0){++f.depth;
            if(f.callback==12)f.nestedResults.push_back(old?oldCharacter({0x41},f,*this):handleRuntimeChatTextCharacter({0x41},u,*this));
            else f.nestedResults.push_back(old?oldModal(f,8,true,*this):currentModal({8,true},f,*this));
            --f.depth;
        }
    }
}
// Frozen old source is retained literally below. The executable oracle only
// substitutes borrowed owner ports, char16_t for Win32 wchar_t, and key constants.

[[maybe_unused]] constexpr char frozenOldModal[]=R"OLD(bool handleRuntimePlayerModalKey(RuntimeWindowState& state, int key, bool repeated) {
    auto& ui = state.playerCommunication;
    if (ui.chatOpen) {
        if (key == VK_ESCAPE) { ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear(); }
        else if (key == VK_BACK && !ui.chatDraft.empty()) {
            const wchar_t last = ui.chatDraft.back(); ui.chatDraft.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !ui.chatDraft.empty()
                && ui.chatDraft.back() >= 0xd800 && ui.chatDraft.back() <= 0xdbff)
                ui.chatDraft.pop_back();
        } else if (key == VK_RETURN && !repeated) {
            const auto text = trimAsciiLocal(runtimePlayerChatUtf8(ui.chatDraft));
            if (ui.beaconEditing && !runtimePlayerSelectedBeacon(state)) {
                ui.chatOpen=ui.beaconEditing=false;ui.chatDraft.clear();
            } else if (text.empty() && !ui.beaconEditing) { ui.chatOpen = false; ui.chatDraft.clear(); }
            else {
                RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
                command.kind = ui.beaconEditing ? RuntimePlayerCommunicationKind::BeaconText
                    : ui.chatAllies ? RuntimePlayerCommunicationKind::ChatAllies : RuntimePlayerCommunicationKind::ChatAll;
                if (ui.beaconEditing) command.cell=*ui.selectedBeacon;
                else command.value=runtimePlayerOutgoingChatMask(state);
                command.text = text;
                if (dispatchRuntimePlayerCommunicationFromUi(state, command)) {
                    ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear();
                }
            }
        }
        state.repaintRequested = true;
        return true;
    }
    RuntimeDiplomacyNavigationUiHost host(state);
    return handleRuntimeDiplomacyNavigationKey({key, repeated}, ui, host);
})OLD";
[[maybe_unused]] constexpr char frozenOldCharacter[]=R"OLD(    case WM_CHAR:
    {
        auto* state = reinterpret_cast<RuntimeWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state && state->playerCommunication.chatOpen && !state->runtimeMenuOpen) {
            auto& draft = state->playerCommunication.chatDraft;
            if (wParam >= 32 && wParam != 127 && draft.size() < 80)
                draft.push_back(static_cast<wchar_t>(wParam));
            state->repaintRequested = true; InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    })OLD";
constexpr int VK_ESCAPE=27,VK_BACK=8,VK_RETURN=13;
bool oldModal(Fixture& state, int key, bool repeated, Host& host) {
    auto& ui = state.playerCommunication;
    if (ui.chatOpen) {
        if (key == VK_ESCAPE) { ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear(); }
        else if (key == VK_BACK && !ui.chatDraft.empty()) {
            const char16_t last = ui.chatDraft.back(); ui.chatDraft.pop_back();
            if (last >= 0xdc00 && last <= 0xdfff && !ui.chatDraft.empty()
                && ui.chatDraft.back() >= 0xd800 && ui.chatDraft.back() <= 0xdbff)
                ui.chatDraft.pop_back();
        } else if (key == VK_RETURN && !repeated) {
            const auto text = trimAsciiLocal(legacyStrictUtf8(ui.chatDraft));
            if (ui.beaconEditing && !host.selectedBeaconValid()) {
                ui.chatOpen=ui.beaconEditing=false;ui.chatDraft.clear();
            } else if (text.empty() && !ui.beaconEditing) { ui.chatOpen = false; ui.chatDraft.clear(); }
            else {
                RuntimeMultiplayerGameplayPlayerCommunicationCommand command;
                command.kind = ui.beaconEditing ? RuntimePlayerCommunicationKind::BeaconText
                    : ui.chatAllies ? RuntimePlayerCommunicationKind::ChatAllies : RuntimePlayerCommunicationKind::ChatAll;
                if (ui.beaconEditing) command.cell=host.selectedBeaconCell();
                else command.value=host.outgoingChatMask();
                command.text = text;
                if (host.dispatchCommunication(command)) {
                    ui.chatOpen = ui.beaconEditing = false; ui.chatDraft.clear();
                }
            }
        }
        host.requestRepaint();
        return true;
    }
    return host.diplomacy(key,repeated);
}

bool oldCharacter(RuntimeChatTextCharacterInput input,Fixture& state,Host& host) {
    if(state.playerCommunication.chatOpen && !host.menuOpen()){
        auto& draft=state.playerCommunication.chatDraft;
        if(input.rawCharacter>=32 && input.rawCharacter!=127 && draft.size()<80)
            draft.push_back(static_cast<char16_t>(input.rawCharacter));
        host.requestRepaint();host.invalidate();return true;
    }
    return false; // Original DefWindowProc fallback is the Native contract.
}
enum class Entry {Character,Modal};
struct Result {Fixture state;std::vector<Event> trace;bool handled=false;std::string exception;};
Result run(Fixture f,Entry entry,bool old) {
    Host host(f,old);bool handled=false;std::string exception;
    try {
        if(entry==Entry::Character)handled=old?oldCharacter(f.characterInput,f,host):handleRuntimeChatTextCharacter(f.characterInput,f.playerCommunication,host);
        else handled=old?oldModal(f,f.keyInput.key,f.keyInput.repeated,host):currentModal(f.keyInput,f,host);
    } catch(const std::exception& e){exception=e.what();}
    return {std::move(f),std::move(host.trace),handled,std::move(exception)};
}
Result exercise(Fixture f,Entry entry,const std::string& label) {
    auto before=run(f,entry,true),after=run(std::move(f),entry,false);
    ++stats.cases;if(entry==Entry::Character)++stats.characters;else ++stats.keys;
    check(before.handled==after.handled,label+": exact handled return");
    check(before.exception==after.exception,label+": exact exception");
    check(snapshot(before.state)==snapshot(after.state),label+": every communication/host/input field");
    check(before.trace==after.trace,label+": complete ordered query-effect/state/result trace");
    stats.traceRows+=after.trace.size();return after;
}
void codecCases() {
    const std::vector<std::pair<std::u16string,std::string>> golden{
        {u"",{}},{u"A","41"},{std::u16string{0,65,0},"004100"},
        {std::u16string{0x7f,0x80,0x7ff,0x800},"7fc280dfbfe0a080"},
        {u"\u4e2d\u6587","e4b8ade69687"},
        {std::u16string{0xd800,0xdc00},"f0908080"},
        {std::u16string{0xdbff,0xdfff},"f48fbfbf"},
        {std::u16string{0xfffe,0xffff},"efbfbeefbfbf"}
    };
    for(const auto& [input,expected]:golden){
        check(hex(encodeRuntimeChatTextUtf8(input))==expected,"explicit UTF-8 golden");
        check(hex(legacyStrictUtf8(input))==expected,"independent oracle golden");++stats.codec;
    }
    for(std::uint32_t c=0;c<=65535;++c){
        const std::u16string input(1,static_cast<char16_t>(c));
        check(encodeRuntimeChatTextUtf8(input)==legacyStrictUtf8(input),"all single UTF-16 units including every surrogate");++stats.codec;
    }
    const std::vector<std::u16string> malformed{
        {0xd800},{0xdc00},{0xdbff},{0xdfff},{0xd800,65},{0xd800,0xd800},{65,0xdc00},
        {0xd800,0xdc00,0xdc00},{0xdc00,0xd800},{0xdbff,0xdfff,0xd800}
    };
    for(const auto& value:malformed){check(encodeRuntimeChatTextUtf8(value).empty(),"strict malformed sequence rejects whole result");++stats.codec;}
    for(auto hi:{0xd800,0xd801,0xdbfe,0xdbff})for(auto lo:{0xdc00,0xdc01,0xdffe,0xdfff}){
        const std::u16string value{65,static_cast<char16_t>(hi),static_cast<char16_t>(lo),0};
        check(encodeRuntimeChatTextUtf8(value)==legacyStrictUtf8(value),"supplementary pair and embedded NUL boundaries");++stats.codec;
    }
    for(std::size_t count:{79U,80U,81U,85U,86U,127U,128U,129U,255U,256U,257U}){
        for(char16_t value:{char16_t{65},char16_t{0x7ff},char16_t{0x800}}){
            const std::u16string input(count,value);const auto expected=legacyStrictUtf8(input);
            check(encodeRuntimeChatTextUtf8(input)==expected,"whole encoded-byte limit independent of draft-unit limit");++stats.codec;
        }
    }
    check(encodeRuntimeChatTextUtf8(std::u16string(256,u'A')).size()==256,"exact 256 bytes allowed");
    check(encodeRuntimeChatTextUtf8(std::u16string(257,u'A')).empty(),"257 bytes rejected");
}
void characterCases() {
    const std::vector<std::uint64_t> raw{0,8,13,31,32,33,126,127,128,0x7ff,0x800,0xd800,0xdc00,
        0xffff,0x10000,0x1007f,0x100000000ULL,0x10000007fULL,std::numeric_limits<std::uint64_t>::max()};
    for(bool open:{false,true})for(bool menu:{false,true})for(std::size_t count:{0U,1U,78U,79U,80U,81U})for(auto code:raw){
        auto f=seed();f.playerCommunication.chatOpen=open;f.menu=menu;f.playerCommunication.chatDraft.assign(count,u'x');f.characterInput={code};
        const auto r=exercise(f,Entry::Character,"raw64/control/menu/unit-capacity");
        const bool active=open&&!menu;const bool appended=active&&code>=32&&code!=127&&count<80;
        check(r.handled==active && r.state.calls[0]==(open?1U:0U)
            && r.state.calls[5]==(active?1U:0U) && r.state.calls[6]==(active?1U:0U),"character guard short-circuit and unconditional eligible repaint/invalidate");
        check(r.state.playerCommunication.chatDraft.size()==count+(appended?1U:0U),"raw filter before narrowing and exact 80-unit cap");
        if(appended)check(r.state.playerCommunication.chatDraft.back()==static_cast<char16_t>(code),"raw64 low UTF-16 unit stored unchanged");
    }
    auto f=seed();f.playerCommunication.chatDraft.assign(79,u'x');f.characterInput={0xd800};
    auto r=exercise(f,Entry::Character,"pair first half at capacity");
    r.state.characterInput={0xdc00};r=exercise(r.state,Entry::Character,"pair second half blocked at capacity");
    check(r.state.playerCommunication.chatDraft.size()==80 && r.state.playerCommunication.chatDraft.back()==0xd800,"original unit capacity can retain unmatched high surrogate");
}
std::vector<std::u16string> drafts() {
    return {{},u"hello",u" \t\r\n ",u"  hello\t ",u"\u4e2d\u6587",
        {0xd800},{0xdc00},{0xd800,0xdc00},{65,0xd800,0xdc00},{65,0xdc00},{65,0xd800},
        {0,65,0},std::u16string(256,u'a'),std::u16string(257,u'a'),std::u16string(86,0x800)};
}
void keyCases() {
    for(const auto& draft:drafts())for(bool open:{false,true})for(bool editing:{false,true})
        for(bool repeated:{false,true})for(int key:{8,13,27,0,65,-1,std::numeric_limits<int>::max()}){
            auto f=seed();f.playerCommunication.chatOpen=open;f.playerCommunication.beaconEditing=editing;
            f.playerCommunication.chatDraft=draft;f.keyInput={key,repeated};
            const auto r=exercise(f,Entry::Modal,"complete original modal key policy");
            check(r.state.calls[6]==0,"modal never invalidates");
            if(open)check(r.handled && r.state.calls[5]==1,"all open chat keys consume and repaint");
            else check(!r.handled && r.trace.size()==1 && r.trace.front().name=="diplomacy","closed chat delegates once to unchanged diplomacy boundary");
            if(open&&key==27)check(!r.state.playerCommunication.chatOpen&&!r.state.playerCommunication.beaconEditing&&r.state.playerCommunication.chatDraft.empty(),"Escape clears on repeat too");
            if(open&&key==13&&repeated)check(r.state.commands.empty()&&r.state.playerCommunication.chatDraft==draft,"repeat Enter does not encode/query/dispatch");
        }
    for(bool allied:{false,true})for(bool valid:{false,true})for(bool accepted:{false,true})
        for(bool editing:{false,true})for(const auto& draft:drafts()){
            auto f=seed();auto& u=f.playerCommunication;u.chatAllies=allied;u.beaconEditing=editing;u.chatDraft=draft;f.valid=valid;f.accepted=accepted;
            const auto r=exercise(f,Entry::Modal,"submit exact kind/text/cell/recipient/acceptance");
            const auto encoded=trimAsciiLocal(legacyStrictUtf8(draft));const bool submit=(!editing||valid)&&(!encoded.empty()||editing);
            check(r.state.commands.size()==(submit?1U:0U),"original empty/invalid/selected branch decides dispatch");
            if(submit){const auto& c=r.state.commands.front();
                check(c.kind==(editing?RuntimePlayerCommunicationKind::BeaconText:allied?RuntimePlayerCommunicationKind::ChatAllies:RuntimePlayerCommunicationKind::ChatAll)
                    &&c.text==encoded&&c.cell==(editing?*u.selectedBeacon:Cell{})&&c.value==(editing?0:f.mask),"all owned command fields independent of captured production output");
                check(r.state.playerCommunication.chatOpen==!accepted && r.state.playerCommunication.beaconEditing==(editing&&!accepted),"accepted clears; rejected preserves editing flags");
            }
        }
    auto f=seed();f.playerCommunication.chatOpen=false;Host host(f,false);
    check(!handleRuntimeChatTextKey({13,false},f.playerCommunication,host)&&host.trace.empty(),"new chat-only entry closed return makes no child query");
    for(const auto& value:std::vector<std::u16string>{{65,0xd800,0xdc00},{65,0xdc00},{65,0xd800}}){
        f=seed();f.playerCommunication.chatDraft=value;f.keyInput={8,true};const auto r=exercise(f,Entry::Modal,"backspace exact paired/lone unit deletion");
        check(r.state.playerCommunication.chatDraft==u"A","Backspace removes one scalar pair or one unmatched unit without repairing other text");
    }
}
void callbackCases() {
    for(int callback=1;callback<=14;++callback){
        auto f=seed();f.callback=callback;f.playerCommunication.beaconEditing=callback==2||callback==3||callback==8||callback==10;
        const auto entry=callback==1||callback==7||callback==12?Entry::Character:Entry::Modal;
        const auto r=exercise(f,entry,"controlled synchronous primitive mutation/reentry "+std::to_string(callback));++stats.dynamic;
        if(callback>=12){check(!r.state.nestedResults.empty(),"configured nested callback actually entered");++stats.nested;}
        if(callback==2)check(r.state.commands.front().cell==Cell{std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()},"cell copied after valid query changes current selection");
        if(callback==3)check(r.state.commands.front().kind==RuntimePlayerCommunicationKind::ChatAll && r.state.commands.front().text=="old draft"&&r.state.commands.front().value==0x39,"fresh mode/mask after selected query but encoded text owned earlier");
        if(callback==4)check(r.state.commands.front().text=="old draft"&&r.state.commands.front().kind==RuntimePlayerCommunicationKind::ChatAllies,"mask callback cannot alter owned text or earlier kind");
        if(callback==8)check(r.state.commands.front().cell==Cell{9,-11},"owned cell survives source reset inside completed query");
        if(callback==9)check(r.state.playerCommunication.chatOpen&&r.state.commands.size()==1,"rejected dispatch retains its own synchronous changes");
    }
    for(Entry entry:{Entry::Character,Entry::Modal})for(bool editing:{false,true}){
        auto f=seed();f.playerCommunication.beaconEditing=editing;
        const auto baseline=run(f,entry,true);
        for(std::size_t index=0;index<baseline.trace.size();++index){
            f.throwAt=static_cast<int>(index+1);
            const auto r=exercise(f,entry,"every reached primitive exception "+std::to_string(index));
            check(!r.exception.empty()&&r.trace.back().threw&&r.trace.size()==index+1,"exception stops at original primitive without extra sink");++stats.exceptions;
        }
    }
}
} // namespace
int main() {
    codecCases();characterCases();keyCases();callbackCases();
    std::cout<<"Chat text contract: checks="<<stats.checks<<" differentialCases="<<stats.cases
        <<" characterCases="<<stats.characters<<" keyCases="<<stats.keys<<" codecCases="<<stats.codec
        <<" dynamicCases="<<stats.dynamic<<" nestedCases="<<stats.nested<<" exceptionCases="<<stats.exceptions
        <<" orderedTraceRows="<<stats.traceRows<<" failures="<<stats.failures
        <<" sameProductionCpp=1 frozenOldModalAndCharacter=1 independentStrictUtf16Oracle=1 completeCommunicationState=1"
        <<" nativeWinApiCodec=not-covered nativeWindow=not-covered bridgeAuthority=not-covered physicalModifiers=not-run\n";
    return stats.failures==0?0:1;
}
