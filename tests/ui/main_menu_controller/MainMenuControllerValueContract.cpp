#include "runtime/menu/RuntimeMainMenuController.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>
using namespace concord::main_menu;
static_assert(std::is_same_v<decltype(RuntimeMainMenuController::mousePoint), RuntimeInputPixelPoint>);
static_assert(!std::is_copy_constructible_v<RuntimeMainMenuController>);
namespace {
struct Clock final : RuntimeUiClock { std::uint64_t value=1000; mutable unsigned reads=0; std::uint64_t nowMs() const noexcept override {++reads;return value;} };
struct Host final : RuntimeMainMenuServices {
    mutable std::vector<std::string> events;
    mutable unsigned textReads=0;
    bool ready=true,save=true,played=true,changed=false,shift=false,captured=false;
    unsigned warnings=3;int volume=-1;RuntimeConfig saved;
    std::wstring text(const char* key,const wchar_t* fallback) const override { (void)key;++textReads;return fallback; }
    std::wstring widen(const std::string& value) const override { return std::wstring(value.begin(),value.end()); }
    void playSound(const std::string& e) override {events.push_back("sound:"+e);}
    void stopVideo() override {events.push_back("stop-video");}
    void stopMusic() override {events.push_back("stop-music");}
    void setMusicVolumePercent(int v) override {volume=v;events.push_back("volume");}
    void destroyWindow() override {events.push_back("destroy");}
    void repaintValidWindow() override {events.push_back("repaint-valid");}
    void repaintPresentWindow() override {events.push_back("repaint-present");}
    void repaintEventWindow() override {events.push_back("repaint-event");}
    void prewarmSkirmish() override {events.push_back("prewarm");}
    bool skirmishReady() const override {events.push_back("ready");return ready;}
    bool savePreferences(const RuntimeConfig& c,std::string& error) override {saved=c;events.push_back("save");error=save?"":"owned save failure";return save;}
    void resizeHost() override {events.push_back("resize");}
    void startPageMusic() override {events.push_back("start-music");}
    bool playMovie(const std::filesystem::path& p,std::string& error) override {events.push_back("movie:"+p.generic_string());error=played?"":"owned decode failure";return played;}
    void prepareMoviePlayback() override {events.push_back("prepare-movie");}
    void restartMainVideo() override {events.push_back("restart-video");}
    void focusAndRepaint() override {events.push_back("focus-repaint");}
    bool pollVideo() override {events.push_back("poll");return changed;}
    std::size_t warningFrames() const override {return warnings;}
    void trackMouseLeave() override {events.push_back("track");}
    void capture() override {captured=true;events.push_back("capture");}
    void releaseCaptureIfOwned() override {captured=false;events.push_back("release-owned");}
    bool shiftDown() const override {events.push_back("shift");return shift;}
};
unsigned checks=0,failures=0;
void require(bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"Main controller: "<<why<<"\n";}}
void idle(RuntimeMainMenuController& s){s.phase=MenuPhase::Idle;s.closing=false;s.pending=MenuAction::None;s.backDirty=false;}
constexpr std::uint64_t duration=(kSlotCount-1+kAnimationFrames)*kAnimationFrameMs;
void complete(RuntimeMainMenuController& s,Clock& clock,MenuAction action) {
    idle(s);beginAction(s,action);clock.value=s.phaseStarted+duration;
    require(updateAnimation(s,clock.value),"original transition completion changed");
}
}
int main(){
 Clock clock;Host h;RuntimeMainMenuController s(h,clock);
 require(s.result==RuntimeMainMenuResult::Exit && !s.closing && s.page==MenuPage::Home && s.phase==MenuPhase::SlideIn,"original initial policy defaults");
 require(s.focus==0&&s.hover==-1&&s.pressed==-1&&!s.keyboardTooltip&&!s.mouseVisible,"original input defaults");
 RuntimeConfig config;config.windowWidth=1280;config.windowHeight=720;config.targetFps=120;config.gameSpeed=4;config.musicVolume=145;
 s.moviePaths={"one.bik","two.bik"};s.movieIndex=1;s.creditLines.resize(38);s.creditsPage=2;
 resetMainMenuForEntry(s,config,RuntimeMainMenuStartPage::SinglePlayer);
 require(s.page==MenuPage::SinglePlayer&&s.optionWindowWidth==1280&&s.optionWindowHeight==720&&s.optionTargetFps==120&&s.optionGameSpeed==4&&h.volume==100,"entry resets options and original volume clamp");
 require(s.movieIndex==1&&s.moviePaths.size()==2&&s.creditsPage==2&&s.creditLines.size()==38,"prewarmed movie and credit selection retained");
 for(auto page:{MenuPage::Home,MenuPage::SinglePlayer,MenuPage::Movies,MenuPage::Credits,MenuPage::Options}) {
  s.page=page;idle(s);auto specs=buttons(s);
  require(!specs.empty(),"every original page has buttons");
  updateViewport(s,800,600);
  for(std::size_t n=0;n<specs.size();++n) {
   const int y=specs[n].slot<0?kBottomHitTop:buttonsTop(s)+specs[n].slot*slotHeight(s);
   require(hitButton(s,{644,y})==static_cast<int>(n),"inclusive original hit left/top");
   require(hitButton(s,{789,y+1})==static_cast<int>(n),"original inner right");
   require(hitButton(s,{643,y})==-1&&hitButton(s,{790,y})==-1,"exclusive sidebar outer x");
  }
  require(hitButton(s,{-1,210})==-1,"outside point not a button");
  s.phase=MenuPhase::SlideOut;require(hitButton(s,{670,210})==-1,"busy menu does not hit");
  idle(s);s.focus=0;h.shift=false;h.events.clear();require(handleMainMenuKey(s,MainMenuKey::Up)&&s.focus==static_cast<int>(specs.size())-1,"up wraps to final logical button");
  require(std::find(h.events.begin(),h.events.end(),"shift")==h.events.end(),"up short circuits shift query");
  require(handleMainMenuKey(s,MainMenuKey::Down)&&s.focus==0,"down wraps to first");
  h.shift=true;require(handleMainMenuKey(s,MainMenuKey::Tab)&&s.focus==static_cast<int>(specs.size())-1,"shift-tab moves backward");
  h.shift=false;require(handleMainMenuKey(s,MainMenuKey::Tab)&&s.focus==0,"tab moves forward");
  require(!handleMainMenuKey(s,MainMenuKey::Other),"unhandled key forwarded to native default");
  s.phase=MenuPhase::SlideIn;auto reads=h.textReads;require(handleMainMenuKey(s,MainMenuKey::Other)&&h.textReads>reads,"original key count/text evaluated before busy gate");
 }
 for(auto size:std::array<std::pair<int,int>,6>{{{800,600},{1024,640},{1600,1200},{400,300},{1,1},{0,0}}}) {
  updateViewport(s,size.first,size.second);
  const auto a=clientToBase(s,{s.viewLeft,s.viewTop});require(a.x==0&&a.y==0,"viewport inclusive corner");
  const auto b=clientToBase(s,{s.viewLeft+s.viewWidth,s.viewTop});require(b.x==-1&&b.y==-1,"viewport right exclusive");
  const auto c=clientToBase(s,{s.viewLeft,s.viewTop+s.viewHeight});require(c.x==-1&&c.y==-1,"viewport bottom exclusive");
 }
 s.page=MenuPage::Home;idle(s);updateViewport(s,800,600);h.events.clear();
 handleMainMenuMouseMove(s,{670,210});require(s.mouseVisible&&s.hover==0&&!s.keyboardTooltip&&h.events.front()=="track","mouse hover and tracking order");
 h.events.clear();handleMainMenuLeftDown(s,{670,210});
 require(h.captured&&s.focus==0&&s.pressed==0&&h.events==std::vector<std::string>{"capture","sound:MenuClick","repaint-event"},"mouse down capture-state-sound-repaint order");
 h.events.clear();handleMainMenuLeftUp(s,{670,210});
 require(!h.captured&&s.pressed==-1&&s.pending==MenuAction::OpenSinglePlayer,"matching release activates");
 require(h.events.front()=="release-owned"&&h.events.back()=="repaint-event","release precedes activation and original unconditional repaint");
 idle(s);s.pressed=0;handleMainMenuLeftUp(s,{0,0});require(s.pending==MenuAction::None&&s.pressed==-1,"release elsewhere cancels press");
 s.pressed=1;handleMainMenuCaptureLost(s);require(s.pressed==-1&&s.backDirty,"lost capture clears pressed");
 s.mouseVisible=true;s.hover=1;s.pressed=-1;handleMainMenuMouseLeave(s);require(!s.mouseVisible&&s.hover==-1,"mouse leave clears visual state");
 const std::array<std::pair<MenuAction,RuntimeMainMenuResult>,6> exits{{
  {MenuAction::Campaign,RuntimeMainMenuResult::Campaign},{MenuAction::LoadGame,RuntimeMainMenuResult::LoadGame},{MenuAction::Skirmish,RuntimeMainMenuResult::Skirmish},
  {MenuAction::Network,RuntimeMainMenuResult::Network},{MenuAction::Internet,RuntimeMainMenuResult::Internet},{MenuAction::Exit,RuntimeMainMenuResult::Exit}}};
 for(const auto& item:exits) {
  s.smoke=false;h.ready=true;idle(s);h.events.clear();beginAction(s,item.first);const auto start=s.phaseStarted;
  require(s.pending==item.first&&s.phase==MenuPhase::SlideOut&&s.pressed==-1,"terminal action starts original phase");
  updateAnimation(s,start+duration-1);require(!s.closing,"terminal action stays open before duration");
  h.events.clear();updateAnimation(s,start+duration);require(s.closing&&s.result==item.second,"original terminal result");
  require(h.events.size()>=3&&std::vector<std::string>(h.events.end()-3,h.events.end())==std::vector<std::string>{"stop-video","stop-music","destroy"},"stop video then music then destroy");
  const auto count=h.events.size();finishMenu(s,RuntimeMainMenuResult::Exit);require(h.events.size()==count&&s.result==item.second,"closing finish is idempotent");
 }
 idle(s);s.smoke=false;h.ready=false;h.events.clear();beginAction(s,MenuAction::Skirmish);auto start=s.phaseStarted;require(h.events.front()=="prewarm","prewarm before phase and sound");
 require(updateAnimation(s,start+duration)&&!s.closing&&s.pending==MenuAction::Skirmish&&!s.pageStatus.empty(),"skirmish waits without consuming action");h.ready=true;updateAnimation(s,start+duration+1);require(s.closing&&s.result==RuntimeMainMenuResult::Skirmish,"ready skirmish completes");
 s.smoke=true;
 for(const auto& item:std::array<std::pair<MenuAction,MenuPage>,5>{{{MenuAction::OpenSinglePlayer,MenuPage::SinglePlayer},{MenuAction::OpenMovies,MenuPage::Movies},{MenuAction::OpenCredits,MenuPage::Credits},{MenuAction::OpenOptions,MenuPage::Options},{MenuAction::OpenHome,MenuPage::Home}}}) {
  complete(s,clock,item.first);require(s.page==item.second&&s.phase==MenuPhase::SlideIn&&s.focus==0&&s.hover==-1&&!s.keyboardTooltip,"original page transition reset");
  require(updateAnimation(s,clock.value+duration)&&s.phase==MenuPhase::Idle,"slide-in completes");
 }
 complete(s,clock,MenuAction::OpenOptions);s.optionWindowWidth=1600;s.optionWindowHeight=900;s.optionTargetFps=144;s.optionGameSpeed=6;h.save=false;h.events.clear();complete(s,clock,MenuAction::ApplyOptions);
 require(s.page==MenuPage::Options&&s.config.windowWidth==1280&&s.pageStatus==L"Could not save options: owned save failure","failed apply retains config and visible error");
 require(std::find(h.events.begin(),h.events.end(),"resize")==h.events.end(),"failed apply does not resize");
 h.save=true;h.events.clear();complete(s,clock,MenuAction::ApplyOptions);
 require(s.page==MenuPage::Home&&s.config.windowWidth==1600&&s.config.windowHeight==900&&s.config.targetFps==144&&s.config.gameSpeed==6&&s.pageStatus.empty(),"successful apply commits all selected preferences");
 require(std::find(h.events.begin(),h.events.end(),"save")<std::find(h.events.begin(),h.events.end(),"resize"),"save before native resize");
 complete(s,clock,MenuAction::OpenOptions);s.optionWindowWidth=800;complete(s,clock,MenuAction::OpenHome);require(s.config.windowWidth==1600,"cancel discards edited option");
 s.optionWindowWidth=1600;s.optionWindowHeight=900;cycleResolution(s);require(s.optionWindowWidth==800&&s.optionWindowHeight==600,"resolution cycle wraps");
 s.optionTargetFps=144;cycleTargetFps(s);require(s.optionTargetFps==30,"fps wraps");
 s.optionGameSpeed=6;performImmediateAction(s,MenuAction::CycleGameSpeed);require(s.optionGameSpeed==0,"game speed wraps");
 s.moviePaths.clear();h.events.clear();playSelectedMovie(s);require(s.pageStatus==L"No installed movie is available."&&h.events.empty(),"empty movie does not touch devices");
 s.moviePaths={"one.bik","two.bik"};s.movieIndex=0;performImmediateAction(s,MenuAction::PreviousMovie);require(s.movieIndex==1,"previous movie wraps");
 performImmediateAction(s,MenuAction::NextMovie);require(s.movieIndex==0,"next movie wraps");
 h.played=false;h.events.clear();playSelectedMovie(s);require(s.pageStatus==L"Playback unavailable: owned decode failure","movie failure propagated");
 require(h.events==std::vector<std::string>{"prepare-movie","movie:one.bik","restart-video","start-music","focus-repaint"},"same movie resource lifecycle effect order");
 s.creditsPage=0;performImmediateAction(s,MenuAction::PreviousCreditsPage);require(s.creditsPage==2,"credits previous wraps 38 lines");performImmediateAction(s,MenuAction::NextCreditsPage);require(s.creditsPage==0,"credits next wraps");
 idle(s);clock.value=225;h.changed=false;h.warnings=3;s.warningFrame=0;h.events.clear();require(!updateDynamicState(s)&&s.warningFrame==0,"unchanged warning and idle retain changed=false");
 // Explicit warning/dynamic edge: preserve one phase read, poll before animation, closing short circuit.
 s.warningFrame=1;h.events.clear();auto reads=clock.reads;require(updateDynamicState(s)&&s.warningFrame==0&&clock.reads==reads+1&&h.events.front()=="poll","dynamic warning/read/poll order");
 s.closing=true;h.events.clear();reads=clock.reads;require(!updateDynamicState(s)&&clock.reads==reads&&h.events.empty(),"closing returns before source/device effects");
 require(escapeActionForPage(MenuPage::Home)==MenuAction::Exit&&escapeActionForPage(MenuPage::Credits)==MenuAction::OpenMovies&&escapeActionForPage(MenuPage::Options)==MenuAction::OpenHome,"original escape routes");
 RuntimeConfig published;publishMenuConfig(s,published);require(published.windowWidth==s.config.windowWidth&&published.gameSpeed==s.config.gameSpeed,"public config publication");
 std::cout<<"Main controller contract: checks="<<checks<<" failures="<<failures<<" skipped=0 pages=5 nativeHeaders=0 sameProductionController=1\n";
 return failures?1:0;
}
