#include "runtime/menu/RuntimeCampaignMenuController.h"
#include <iostream>
#include <algorithm>
#include <type_traits>
using namespace concord::campaign_menu;
struct Clock final : RuntimeUiClock { mutable unsigned reads=0; std::uint64_t nowMs() const noexcept override {++reads;return 417;} };
struct Effects final : RuntimeCampaignMenuServices {
    RuntimeCampaignMenuController* owner=nullptr;
    std::vector<std::string> calls;
    CampaignDatabase input;
    CampaignProgressData progressInput;
    std::vector<CampaignSaveInfo> campaignInput;
    std::vector<SkirmishSaveInfo> skirmishInput;
    bool saveOk=true,pathsOk=true,progressOk=true,shift=false,nestedCapture=false;
    int color=0; RuntimeInputPixelRect rect{0,0,800,600}; std::wstring incompatible;
    const CampaignMissionDefinition* savedMission=nullptr;
    const CampaignMapSelectionStage* stage=nullptr;
    CampaignProgressData savedProgress;
    std::wstring text(const std::string&,const std::wstring& fallback) const override{return fallback;}
    std::wstring widen(const std::string& value) const override{return {value.begin(),value.end()};}
    std::wstring missionLabel(const CampaignMissionDefinition& m) const override{return widen(m.id);}
    std::wstring editionSuffix(const CampaignDefinition*) const override{return L"";}
    std::string loadingScreenAsset(const std::string& country) const override{return "load:"+country;}
    std::string savePath(const std::string& root,const std::string& campaign,const std::string& mission) const override{return root+"/"+campaign+"/"+mission;}
    const CampaignMapSelectionStage* findMapStage(const CampaignDefinition&,const std::string&) const override{return stage;}
    const CampaignMissionDefinition* findMission(const std::string&,const std::string&) const override{return savedMission;}
    bool saveProgress(const std::string&,const CampaignProgressData& p,std::string& error) override{calls.push_back("save");savedProgress=p;error="disk";return saveOk;}
    bool validateSavePaths(const RuntimeConfig&,const SkirmishSaveInfo& save,std::string& error) const override{error="unsafe";return pathsOk&&save.mapPath!="reject";}
    void refreshBackground() override{calls.push_back("background");}
    void refreshPresentation() override{calls.push_back("presentation");}
    void releaseMapCapture() override{calls.push_back("release-map");}
    void stopVoice() override{calls.push_back("voice-stop");}
    void stopMapEffect() override{calls.push_back("effect-stop");}
    void playMapSound(const CampaignMapSelectionSound& sound) override{calls.push_back("sound:"+sound.sample);}
    void repaintValidWindow() override{calls.push_back("repaint-valid");}
    void destroyWindow() override{calls.push_back("destroy");}
    int mapColorAt(int,int) const override{return color;}
    std::wstring saveTitle(const LoadSaveEntry& entry) const override{return widen(entry.kind==LoadSaveKind::Skirmish?entry.skirmish.path:entry.campaign.path);}
    void updatePresentation() override{calls.push_back("update");}
    RuntimeInputPixelRect eventClientRect() const override{return rect;}
    bool shiftPressed() const override{return shift;}
    void repaintEvent(bool valid) override{calls.push_back(valid?"repaint-event-valid":"repaint-event");}
    void trackMouseLeave() override{calls.push_back("track");}
    void setHandCursor(bool hand) override{calls.push_back(hand?"hand":"arrow");}
    void focusEvent() override{calls.push_back("focus");}
    void captureEvent() override{calls.push_back("capture");}
    void releaseEventCapture() override{calls.push_back("release-event");if(nestedCapture&&owner)campaignCaptureChanged(*owner);}
    CampaignDatabase loadDatabase(const CampaignDatabaseOptions&) override{calls.push_back("database");return input;}
    bool loadProgress(const std::string&,CampaignProgressData& p,std::string& error) override{calls.push_back("progress");p=progressInput;error=progressOk?"":"read";return progressOk;}
    std::vector<CampaignSaveInfo> campaignSaves(const std::string&) override{calls.push_back("campaign-saves");return campaignInput;}
    std::vector<SkirmishSaveInfo> skirmishSaves(const std::string&) override{calls.push_back("skirmish-saves");return skirmishInput;}
    std::wstring incompatibleSaveWarning(const std::string&) const override{return incompatible;}
};
struct Fixture {Clock clock;Effects effects;RuntimeCampaignMenuController state;Fixture():state(effects,clock){effects.owner=&state;}};
CampaignDatabase database(){CampaignDatabase db;db.loaded=true;CampaignDefinition c;c.id="allied";c.sourceId="ra2";c.displayName="Allied";
 for(int n=0;n<12;++n){CampaignMissionDefinition m;m.id="m"+std::to_string(n);m.campaignId=c.id;m.ordinal=n+1;m.available=true;m.mapPath="maps/"+m.id;m.playerCountry="Americans";m.startingCredits=1234;c.missions.push_back(m);}db.campaigns.push_back(c);return db;}
static_assert(std::is_same_v<decltype(RuntimeInputPixelRect::left),std::int32_t>);
static_assert(!std::is_copy_constructible_v<RuntimeCampaignMenuController>);
int main(){unsigned checks=0,failures=0;auto require=[&](bool v,const char* why){++checks;if(!v){++failures;std::cerr<<"Campaign controller: "<<why<<"\n";}};
 {Fixture f;auto& s=f.state;require(s.page==CampaignPage::Campaigns&&s.difficultyIndex==1&&!s.closing&&!s.accepted,"entry defaults");require(s.hoveredControl==-1&&s.pressedControl==-1&&s.focusedControl==-1,"input defaults");require(!s.selection.accepted&&s.presentationClock==&f.clock,"result/borrow defaults");require(!selectedCampaign(s)&&!selectedMission(s)&&!selectedSave(s),"empty selection");
  require(lowerAscii("AbC")=="abc"&&trimAscii(" \t a \r\n")=="a","query case trim");require(trimAscii(" \t").empty(),"empty trim");
  for(int k=-1;k<15;++k)require(campaignListStart(k,12)==std::clamp(k-4,0,3),"nine-row list bounds");
  s.database=database();require(missionUnlocked(s,s.database.campaigns[0],s.database.campaigns[0].missions[0]),"first unlocked");require(!missionUnlocked(s,s.database.campaigns[0],s.database.campaigns[0].missions[1]),"second locked");
  s.progress.campaignId="ALLIED";s.progress.currentMissionId="M2";chooseFirstUnlockedMission(s);require(s.missionIndex==2,"casefold progress current");s.progress.currentMissionId="missing";s.progress.completedMissionIds={"M3"};chooseFirstUnlockedMission(s);require(s.missionIndex==3,"completed reverse fallback");
  require(missionIndexById(s.database.campaigns[0]," M1 ")==1,"trimmed mission lookup");
  s.page=CampaignPage::Missions;for(int selected=0;selected<12;++selected){s.missionIndex=selected;const auto controls=campaignControls(s);unsigned rows=0;for(const auto& c:controls)if(c.action==CampaignControlAction::Row){++rows;require(c.index>=0&&c.index<12,"row actual index");require(c.enabled,"available row stays enabled even locked");}require(rows==9,"nine visible mission rows");}
  s.page=CampaignPage::Campaigns;const auto controls=campaignControls(s);for(std::size_t n=0;n<controls.size();++n){const auto& c=controls[n];if(!c.enabled)continue;require(campaignControlAt(s,{c.rect.left,c.rect.top})>=0,"left/top included");require(!campaignPointInRect(c.rect,{c.rect.right,c.rect.bottom}),"right/bottom excluded");}
 }
 {Fixture f;auto& s=f.state;auto& e=f.effects;e.input=database();RuntimeConfig cfg;cfg.localDataRootPath="local";initializeCampaignMenu(s,cfg,false);require(e.calls==std::vector<std::string>{"database","progress"},"entry service order");require(s.page==CampaignPage::Campaigns&&s.missionIndex==0,"new campaign entry");s.difficultyIndex=9;e.saveOk=false;acceptMission(s);require(s.selection.accepted&&!s.accepted&&!s.closing,"save failure retains filled result but window open");require(s.status==L"Progress could not be saved: disk","progress error");require(e.savedProgress.difficulty=="Brutal"&&e.savedProgress.currentMissionId=="m0","clamped difficulty saved");e.saveOk=true;e.calls.clear();acceptMission(s);require(s.accepted&&s.closing,"accept success closes");require(e.calls==std::vector<std::string>{"save","release-map","voice-stop","effect-stop","destroy"},"accept exact effects");e.calls.clear();closeMenu(s,false);require(e.calls.empty()&&s.accepted,"close idempotent");require(f.clock.reads==0,"non-presentation navigation does not consume clock");}
 {Fixture f;auto& s=f.state;auto& e=f.effects;e.input=database();CampaignSaveInfo a;a.path="z";a.tick=20;CampaignSaveInfo b=a;b.path="A";e.campaignInput={a,b};SkirmishSaveInfo sk;sk.path="sk";sk.tick=1;sk.mapPath="ok";SkirmishSaveInfo bad=sk;bad.path="bad";bad.mapPath="reject";e.skirmishInput={sk,bad};RuntimeConfig cfg;initializeCampaignMenu(s,cfg,true);require(s.page==CampaignPage::Saves&&s.loadGame,"load entry");require(s.saves.size()==3,"reject unsafe save before sort");require(s.saves[0].kind==LoadSaveKind::Skirmish&&s.saves[1].campaign.path=="A"&&s.saves[2].campaign.path=="z","kind then descending tick then casefold path");require(!s.skirmishSaveWarning.empty()&&s.status==s.skirmishSaveWarning,"warning survives compatible entries");e.pathsOk=false;acceptSave(s);require(!s.closing&&!s.selection.accepted,"unsafe chosen save rejected");e.pathsOk=true;acceptSave(s);require(s.closing&&s.accepted&&s.selection.loadedSkirmishSave,"skirmish load result");}
 {Fixture f;auto& s=f.state;auto& e=f.effects;e.input=database();RuntimeConfig cfg;e.progressOk=false;initializeCampaignMenu(s,cfg,false);require(s.status==L"Progress read failed: read","progress read error retained");e.calls.clear();s.page=CampaignPage::Campaigns;require(campaignKeyDown(s,CampaignMenuKey::Other,false)==false&&e.calls.empty(),"unknown key default");require(!campaignKeyDown(s,CampaignMenuKey::Left,false)||s.difficultyIndex==0,"left clamps");campaignKeyDown(s,CampaignMenuKey::Left,false);require(s.difficultyIndex==0,"left clamp repeat");s.page=CampaignPage::Missions;require(!campaignKeyDown(s,CampaignMenuKey::Left,false),"left in mission delegates");s.page=CampaignPage::Campaigns;campaignKeyDown(s,CampaignMenuKey::Confirm,true);require(!s.closing&&s.page==CampaignPage::Campaigns,"repeat enter ignored");campaignKeyDown(s,CampaignMenuKey::Escape,true);require(!s.closing,"repeat escape ignored");s.hoveredControl=3;s.pressedControl=2;s.focusedControl=1;e.calls.clear();resetCampaignMenuInput(s);require(e.calls==std::vector<std::string>{"release-map","repaint-valid"},"reset ownership then repaint");require(s.hoveredControl==-1&&s.pressedControl==-1&&s.focusedControl==-1,"reset all highlights");
  e.calls.clear();campaignMouseMove(s,{700,555});require(s.hoveredControl==0&&e.calls.front()=="track"&&e.calls.back()=="repaint-event","hover back repaint");e.calls.clear();campaignMouseMove(s,{701,555});require(e.calls==std::vector<std::string>{"track","hand"},"unchanged hover stays idle");e.calls.clear();campaignMouseLeave(s);require(s.hoveredControl==-1&&e.calls.back()=="repaint-event","leave clears hover");
  s.page=CampaignPage::Missions;s.missionIndex=0;campaignWheel(s,false);require(s.missionIndex==1,"zero/negative wheel maps +1");campaignWheel(s,true);require(s.missionIndex==0,"positive wheel -1");
  e.calls.clear();s.page=CampaignPage::Campaigns;campaignTimer(s,true);require(e.calls.empty(),"landing timer idle");s.page=CampaignPage::Missions;campaignTimer(s,false);require(e.calls.empty(),"wrong timer idle");campaignTimer(s,true);require(e.calls==std::vector<std::string>{"update"},"mission update no static repaint");
  s.page=CampaignPage::Campaigns;s.pressedControl=-1;e.calls.clear();campaignMouseUp(s,{700,555});require(!s.closing,"release only cannot activate");require(e.calls.front()=="release-event","release first");e.nestedCapture=true;campaignMouseDown(s,{700,555});require(s.pressedControl==0&&e.calls.back()=="repaint-event","press captured");campaignMouseUp(s,{50,300});require(!s.closing&&s.pressedControl==-1,"nested capture change drag-out no action");
  s.mapPresentation.pressedColor=7;s.pressedControl=-1;e.calls.clear();campaignCaptureChanged(s);require(e.calls.empty()&&s.mapPresentation.pressedColor==0,"map-only capture loss no new repaint");
 }
 {Fixture f;auto& s=f.state;auto& e=f.effects;s.database=database();s.page=CampaignPage::Missions;CampaignMapSelectionStage stage;stage.id="stage";stage.choices={{3,"","","m1"},{9,"","","m2"}};e.stage=&stage;s.mapPresentation.stageSection="stage";s.mapPresentation.phase=CampaignMapPresentationPhase::Interactive;s.mapPresentation.vqaFrame.width=1;s.mapPresentation.vqaFrame.height=1;s.mapPresentation.vqaFrame.bgra.resize(4);s.mapPresentation.clickMap.width=1;s.mapPresentation.clickMap.height=1;s.mapPresentation.clickMap.pixels.resize(1);require(campaignShowsMap(s),"CPU valid classic map shown");require(!confirmCampaignMapChoice(s,0)&&!confirmCampaignMapChoice(s,256),"invalid palette choices");cycleCampaignMapChoice(s,1);require(s.mapPresentation.keyboardColor==3,"first choice");cycleCampaignMapChoice(s,1);require(s.mapPresentation.keyboardColor==9,"next choice");cycleCampaignMapChoice(s,1);require(s.mapPresentation.keyboardColor==3,"choice wrap");require(confirmCampaignMapChoice(s,9)&&s.missionIndex==2&&s.page==CampaignPage::Difficulty,"map choice original available mission route");}
 std::cout<<"Campaign standard controller: checks="<<checks<<" failures="<<failures<<" skipped=0 sameProductionController=1\n";return failures?1:0;}
