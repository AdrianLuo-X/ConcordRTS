#include "runtime/menu/RuntimeSkirmishController.h"
#include "ui/skirmish/RuntimeSkirmishSetup.h"
#include <algorithm>
#include <iostream>
#include <tuple>
#include <utility>

using namespace concord::skirmish;
namespace {
int checks=0,failures=0;
void check(bool value,const char* what) {++checks;if(!value){++failures;std::cerr<<what<<'\n';}}
struct Host final : RuntimeSkirmishServices {
    RuntimeSkirmishController* controller=nullptr;
    bool captured=false,closed=false,randomReady=false;
    int repaints=0,previews=0,crateWrites=0,cooldownWrites=0,prewarms=0,warnings=0;
    std::uint64_t now=1000;
    std::vector<std::string> effects;
    CooperativeScenarioDefinition scenario;
    void requestRepaint() override {++repaints;effects.push_back("paint");}
    void playSound(const std::string& s,bool) override {effects.push_back(s);}
    void stopMusic() override {effects.push_back("stop-music");}
    std::uint64_t nowMs() const override {return now;}
    void destroyWindow() override {check(controller->closing,"mark closed before destruction");closed=true;effects.push_back("destroy");}
    void prewarmMainMenu() override {++prewarms;effects.push_back("prewarm-main");}
    bool saveCratePreference(const RuntimeConfig&,bool,std::string&) override {++crateWrites;return true;}
    bool saveNoCooldownPreference(const RuntimeConfig&,bool,std::string&) override {++cooldownWrites;return true;}
    bool ensurePreview(int) override {++previews;return true;}
    void loadDefaults(const RuntimeConfig& c,RuntimeSkirmishSelection& s) override {
        s.startingCredits=c.startingCredits;s.gameSpeed=c.gameSpeed;s.computerAiEnabled=c.computerAiEnabled;
        s.noCooldown=c.noCooldown&&!c.multiplayerActive;
    }
    std::wstring text(const std::string&,const std::wstring& fallback) const override {return fallback;}
    std::wstring label(std::initializer_list<std::string>,const wchar_t* fallback) const override {return fallback;}
    std::wstring widenUtf8(const std::string& text) const override {return std::wstring(text.begin(),text.end());}
    std::string loadingBriefKey(const std::string&) const override {return "fixture-brief";}
    std::string loadingScreenAsset(const std::string& id) const override {return "fixture-"+id;}
    const CooperativeScenarioDefinition* cooperativeScenario(const std::string& id) const override {return id=="coop"?&scenario:nullptr;}
    bool generatedMapFile(const std::string&) const override {return false;}
    int sliderGripWidth() const override {return 12;}
    bool cursorAvailable() const override {return true;}
    bool activateRandomTarget(HitTarget,int) override {return false;}
    bool randomMapReady() const override {return randomReady;}
    void warnInvalidSelection() override {++warnings;}
    void focus() override {effects.push_back("focus");}
    void capture() override {captured=true;effects.push_back("capture");}
    void releaseCaptureIfOwned() override {
        if(captured){captured=false;effects.push_back("release-enter");
            handleSkirmishCaptureLost(*controller);effects.push_back("release-return");}
    }
};
struct Fixture {
    Host host;
    RuntimeSkirmishController state{host};
    RuntimeConfig config;
    Fixture() {
        host.controller=&state;config.fixedTickHz=37;config.simulationRandomSeed=42;
        config.defaultMapPath="map0.map";config.playerCountry="Americans";config.playerColor="Gold";
        config.startingCredits=10000;config.gameSpeed=4;config.noCooldown=false;
        state.modes={{0,"Battle","standard",false,"MPBattleMD.ini",L"Battle"},
            {1,"FreeForAll","standard",false,"MPFreeForAllMD.ini",L"FFA"},
            {2,"TeamGame","standard",false,"MPTeamMD.ini",L"Team"},
            {3,"Cooperative","cooperative",false,"MPCoopMD.ini",L"Coop"}};
        for(int i=0;i<20;++i) {MapInfo map;map.path="map"+std::to_string(i)+".map";
            map.id="map"+std::to_string(i);map.displayName=L"Map";map.filters={"standard"};map.maxPlayers=i==1?2:8;state.maps.push_back(map);}
        MapInfo coop;coop.path="coop.map";coop.id="coop";coop.filters={"cooperative"};coop.maxPlayers=4;state.maps.push_back(coop);
        state.countries={{"Random",L"Random",""},{"Americans",L"Americans",""},{"YuriCountry",L"Yuri",""},{"Observer",L"Observer",""}};
        state.cooperativeDataComplete=true;state.cooperativeValidationByMap["coop"].valid=true;
        host.scenario.mapId="coop";host.scenario.playerCountries={"Americans"};host.scenario.enemyCountries={"YuriCountry"};
        reset();
    }
    void reset() {resetControllerForEntry(state,config);host.captured=false;host.closed=false;}
    RuntimeInputPixelPoint client(int x,int y) const {return {state.viewLeft+x*state.viewWidth/800,state.viewTop+y*state.viewHeight/600};}
    void hit(HitKind kind,int index=-1) {state.hits={{{10,10,40,40},{kind,index}}};}
    void click(HitKind kind,int index=-1) {hit(kind,index);handleSkirmishLeftDown(state,client(20,20));handleSkirmishLeftUp(state,client(20,20));}
};
}
int main() {
    Fixture f;auto& s=f.state;auto& h=f.host;
    check(s.selection.slots.size()==8,"eight-seat reset");
    check(s.selection.slots[0].human&&s.selection.slots[0].enabled&&s.selection.slots[1].enabled,"initial opponents");
    check(s.selection.mapPath=="map0.map"&&s.selection.mapStartingPointLimit==8,"initial map identity");
    check(s.config.fixedTickHz==37&&s.config.simulationRandomSeed==42,"controller does not advance authority");
    const int paints=h.repaints;resetControllerForEntry(s,f.config);check(h.repaints==paints,"reset adds no paint wake");
    for(auto size: {std::pair{800,600},{1024,640},{1600,1200},{400,300},{0,0}}) {
        computeShellViewport(s,size.first,size.second);
        check(s.viewWidth>=1&&s.viewHeight>=1,"nonempty viewport");
        const auto edge=shellClientToBase(s,{s.viewLeft+s.viewWidth,s.viewTop});check(edge.x==-1&&edge.y==-1,"half-open right edge");
        const auto origin=shellClientToBase(s,{s.viewLeft,s.viewTop});check(origin.x==0&&origin.y==0,"viewport origin");
        const auto outside=shellClientToBase(s,{s.viewLeft-1,s.viewTop});check(outside.x==-1,"letterbox outside");
    }
    computeShellViewport(s,1024,640);check(s.viewLeft==112&&s.viewTop==20&&s.viewWidth==800&&s.viewHeight==600,"integer-first viewport");
    handleSkirmishMouseMove(s,f.client(20,20));check(s.mouseVisible&&s.mousePoint.x==20&&s.mousePoint.y==20,"cursor model");
    handleSkirmishMouseLeave(s);check(!s.mouseVisible&&s.mousePoint.x==-1,"cursor leave");
    s.hits={{{0,0,40,40},{HitKind::ToggleCrates,-1}},{{10,10,30,30},{HitKind::ToggleSuperweapons,-1}}};
    check(hitTest(s,{20,20})->target.kind==HitKind::ToggleSuperweapons,"reverse hit order");
    check(hitTest(s,{40,20})==nullptr,"half-open hit edge");
    const bool crates=s.selection.crates;f.click(HitKind::ToggleCrates);
    check(s.selection.crates!=crates&&!h.captured&&s.pressed.kind==HitKind::None,"activation survives synchronous capture loss");
    const auto release=std::find(h.effects.begin(),h.effects.end(),"release-enter");
    check(release!=h.effects.end()&&std::find(release,h.effects.end(),"release-return")!=h.effects.end(),"capture callback returned");
    f.hit(HitKind::ToggleCrates);const bool prior=s.selection.crates;
    handleSkirmishLeftDown(s,f.client(20,20));handleSkirmishLeftUp(s,{-300,-300});check(s.selection.crates==prior,"outside release cancels action");
    s.hits={{{373,200,452,220},{HitKind::SpeedSlider,-1}}};
    handleSkirmishLeftDown(s,f.client(379,210));check(s.draggingSlider==HitKind::SpeedSlider&&h.captured,"slider captured");
    handleSkirmishMouseMove(s,{-999,-999});check(s.selection.gameSpeed==0,"negative slider saturates left");
    handleSkirmishLeftUp(s,f.client(451,210));check(s.selection.gameSpeed==6&&s.draggingSlider==HitKind::None&&!h.captured,"slider release updates then cancels gesture");
    for(auto kind:{HitKind::CreditsSlider,HitKind::UnitsSlider}) {
        setSliderFromBaseX(s,kind,-100);check(kind==HitKind::CreditsSlider?s.selection.startingCredits==5000:s.selection.unitCount==0,"slider minimum");
        setSliderFromBaseX(s,kind,1000);check(kind==HitKind::CreditsSlider?s.selection.startingCredits==200000:s.selection.unitCount==10,"slider maximum");
    }
    f.reset();computeShellViewport(s,800,600);
    f.click(HitKind::ChooseMap);check(s.page==SkirmishPage::MapSelection,"choose map page");
    handleSkirmishWheel(s,-3);check(s.mapScroll==9,"map wheel three rows per notch");
    selectFilteredMap(s,3,false);check(s.selectedMap==3,"map selection");
    check(handleSkirmishKey(s,RuntimeSkirmishKey::Escape)&&s.page==SkirmishPage::Lobby&&s.selectedMap==0,"cancel restores map setup");
    f.click(HitKind::ChooseMap);f.hit(HitKind::Map,4);handleSkirmishDoubleClick(s,f.client(20,20));
    check(s.page==SkirmishPage::Lobby&&s.selectedMap==4&&s.selection.mapPath=="map4.map","double click applies map");
    toggleDropdown(s,DropdownKind::Country,0);check(s.openDropdown==DropdownKind::Country,"country dropdown");
    check(handleSkirmishKey(s,RuntimeSkirmishKey::End)&&s.dropdownHover==3,"dropdown End");
    check(handleSkirmishKey(s,RuntimeSkirmishKey::Return)&&s.selection.slots[0].country=="Observer"&&s.openDropdown==DropdownKind::None,"observer via keyboard");
    check(s.startingSpots[0]==0&&s.selection.slots[0].team=="-"&&!dropdownCanOpen(s,DropdownKind::Start,0)&&!dropdownCanOpen(s,DropdownKind::Team,0),"observer constraints");
    f.reset();toggleDropdown(s,DropdownKind::Color,0);check(handleSkirmishKey(s,RuntimeSkirmishKey::Home)&&s.dropdownHover==0,"dropdown Home");
    check(handleSkirmishKey(s,RuntimeSkirmishKey::Up)&&s.dropdownHover==7,"dropdown wrapping");
    check(handleSkirmishKey(s,RuntimeSkirmishKey::Space)&&s.selection.playerColor=="Magenta","dropdown applies color");
    toggleDropdown(s,DropdownKind::Country,0);check(handleSkirmishKey(s,RuntimeSkirmishKey::Escape)&&s.page==SkirmishPage::Lobby&&s.openDropdown==DropdownKind::None,"Escape first closes dropdown");
    f.reset();s.selection.slots[0].team="4";s.selection.slots[1].team="3";s.startingSpots[0]=2;
    activateTarget(s,{HitKind::Mode,3},1);check(s.selection.gameMode=="Cooperative"&&s.selection.slots[0].team=="1"&&s.selection.slots[1].team=="1"&&s.selection.slots[2].team=="2"&&s.selection.slots[3].team=="2","cooperative forced setup");
    check(!dropdownCanOpen(s,DropdownKind::Team,0)&&!dropdownCanOpen(s,DropdownKind::Player,1),"cooperative locked fields");
    activateTarget(s,{HitKind::Mode,0},1);check(s.selection.slots[0].team=="4"&&s.selection.slots[1].team=="3"&&s.startingSpots[0]==2,"ordinary setup restored");
    s.selection.slots[0].team="1";s.selection.slots[1].team="1";std::wstring reason;
    check(!canStart(s,reason)&&!reason.empty(),"all allied refused");activateTarget(s,{HitKind::Mode,1},1);check(canStart(s,reason),"FFA opponents ignores alliance");
    f.config.multiplayerActive=true;f.config.noCooldown=true;f.reset();f.click(HitKind::ToggleNoCooldown);check(!s.selection.noCooldown,"online noCooldown cannot enable");
    const auto countries=countryOptionIndices(s,0);check(std::find(countries.begin(),countries.end(),3)==countries.end(),"online observer excluded");
    f.config.multiplayerActive=false;f.config.noCooldown=false;f.reset();f.click(HitKind::ToggleNoCooldown);check(s.selection.noCooldown,"offline noCooldown enabled");
    const int crateBefore=h.crateWrites,cooldownBefore=h.cooldownWrites;check(handleSkirmishKey(s,RuntimeSkirmishKey::Return),"Enter consumed");
    check(s.selection.accepted&&s.phase==ShellMenuPhase::SlideOut&&!s.closing,"accepted transition begins before destruction");
    check(h.crateWrites==crateBefore+1&&h.cooldownWrites==cooldownBefore+1,"accepted preference services");
    completeMenu(s,false);check(s.selection.accepted,"close during slide-out cannot overwrite accepted setup");
    check(updateMenuTransition(s,h.now+545)&&!h.closed,"animation not finished early");
    check(updateMenuTransition(s,h.now+546)&&h.closed&&s.closing,"animation exact duration");
    check(h.effects.size()>=2&&h.effects[h.effects.size()-2]=="stop-music"&&h.effects.back()=="destroy","stop music before window destruction");
    RuntimeConfig launch=f.config;applyRuntimeSkirmishSelection(launch,s.selection);check(launch.fixedTickHz==37&&launch.simulationRandomSeed==42&&launch.playerSlots.size()==8&&launch.defaultMapPath==s.selection.mapPath,"same production config projection");
    f.reset();s.phase=ShellMenuPhase::SlideIn;s.phaseStarted=h.now;s.smoke=false;const int prewarms=h.prewarms;
    completeMenu(s,false);check(!s.selection.accepted&&s.phase==ShellMenuPhase::SlideOut&&h.prewarms==prewarms+1,"close while slide-in cancels and prewarms");
    check(!handleSkirmishKey(s,RuntimeSkirmishKey::Other),"unknown lobby key remains unhandled");
    std::cout<<"Skirmish controller: checks="<<checks<<" failures="<<failures<<" pureHost=yes synchronousCapture=yes\n";return failures?1:0;
}
