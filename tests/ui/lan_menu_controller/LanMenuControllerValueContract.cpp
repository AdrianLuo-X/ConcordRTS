#include "runtime/menu/RuntimeLanMenuController.h"
#include <array>
#include <iostream>
#include <map>
#include <type_traits>
#include <utility>
using namespace concord::lan_menu;
static_assert(!std::is_copy_constructible_v<RuntimeMultiplayerMenuResult>);
static_assert(std::is_nothrow_move_constructible_v<RuntimeMultiplayerMenuResult>);
static_assert(std::is_nothrow_move_assignable_v<RuntimeMultiplayerMenuResult>);
struct Flow final : RuntimeLanMenuFlow {
    RuntimeMultiplayerLobbyModel model;
    bool present=false,active=false,authority=false,canonical=false,resume=false,resumeOK=true,canStartOK=false,initializedFlag=true;
    bool browseOK=true,broadcastOK=true,loopbackOK=true,hostOK=true,joinOK=true,slotOK=true,verifyOK=true,mapVerified=false,downloadOK=true;
    unsigned browses=0,hosts=0,joins=0,slotWrites=0,swaps=0,optionWrites=0,requests=0,kicks=0,verifies=0,downloads=0,polls=0,readies=0,starts=0,closes=0,leaves=0;
    std::uint16_t lastPort=0;std::size_t lastIndex=99;std::uint8_t lastPosition=99;int lastTick=-1;bool lastReady=false,resumePassed=false;
    std::string local="host",lastPeer,lastCountry;RuntimeMultiplayerLanRole role=RuntimeMultiplayerLanRole::Host;RuntimeMultiplayerLanPhase phase=RuntimeMultiplayerLanPhase::Inactive;
    RuntimeMultiplayerLanHostOptions hostOptions;RuntimeMultiplayerLobbyHostCreateOptions hostLobby;RuntimeMultiplayerLanJoinOptions joinOptions;
    std::vector<RuntimeMultiplayerRoomMember> roster;std::vector<RuntimeLanRoomAdvertisement> rooms;std::vector<std::string> probes;std::vector<std::filesystem::path> verifiedPaths;
    RuntimeMultiplayerLobbyFlowPollResult polled;RuntimeMultiplayerLanOperationResult operation;
    bool startHost(RuntimeMultiplayerLanHostOptions&& options, const RuntimeMultiplayerLobbyHostCreateOptions& lobby, std::string& error, const RuntimeMultiplayerResumeSave* resume)  override { (void)options;(void)lobby;(void)error;(void)resume;hostOptions=std::move(options);hostLobby=lobby;resumePassed=resume!=nullptr;++hosts;if(!hostOK){error="host denied";return false;}present=true;active=true;authority=true;return true; }
    bool startBrowsing(std::uint16_t port, std::string& error)  override { (void)port;(void)error;++browses;lastPort=port;if(!browseOK){error="browse denied";return false;}present=true;phase=RuntimeMultiplayerLanPhase::Browsing;return true; }
    bool join(RuntimeMultiplayerLanJoinOptions&& options, std::string& error)  override { (void)options;(void)error;joinOptions=std::move(options);++joins;if(!joinOK){error="join denied";return false;}present=true;return true; }
    bool sendDiscoveryProbe(const std::string& address, std::string& error)  override { (void)address;(void)error;probes.push_back(address);if(address=="255.255.255.255"){if(!broadcastOK)error="broadcast denied";return broadcastOK;}if(!loopbackOK)error="loopback denied";return loopbackOK; }
    std::vector<RuntimeLanRoomAdvertisement> discoveredRooms(std::uint64_t age) const override { (void)age;return rooms; }
    RuntimeMultiplayerLobbyFlowPollResult poll(int timeout)  override { (void)timeout;++polls;return polled; }
    RuntimeMultiplayerLobbyEditResult setSlot(std::size_t index, const RuntimeMultiplayerMatchSlot& slot, std::string& error)  override { (void)index;(void)slot;(void)error;++slotWrites;RuntimeMultiplayerLobbyEditResult r;r.accepted=slotOK;r.reason="slot denied";return r; }
    RuntimeMultiplayerLobbyEditResult setSlotController(std::size_t index, RuntimeMultiplayerMatchSlotController kind, std::string_view peer, std::string& error)  override { (void)index;(void)kind;(void)peer;(void)error;++swaps;lastIndex=index;lastPeer=std::string(peer);RuntimeMultiplayerLobbyEditResult r;r.accepted=true;return r; }
    RuntimeMultiplayerLobbyEditResult setSlotOptions(std::size_t index, std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position, std::string& error)  override { (void)index;(void)country;(void)color;(void)team;(void)difficulty;(void)position;(void)error;++optionWrites;lastIndex=index;lastPosition=position;lastCountry=country;RuntimeMultiplayerLobbyEditResult r;r.accepted=slotOK;r.reason="options denied";return r; }
    RuntimeMultiplayerLanOperationResult requestLocalSlotOptions(std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position)  override { (void)country;(void)color;(void)team;(void)difficulty;(void)position;++requests;lastPosition=position;return operation; }
    RuntimeMultiplayerLanOperationResult kickPeer(const std::string& peer, std::string reason)  override { (void)peer;(void)reason;++kicks;lastPeer=peer;return operation; }
    bool verifyLocalMap(const std::filesystem::path& path, std::string& error)  override { (void)path;(void)error;++verifies;verifiedPaths.push_back(path);if(!verifyOK)error="bad bytes";return verifyOK; }
    bool localMapVerified() const override { return mapVerified; }
    bool downloadMissingMap(const std::filesystem::path& root, std::filesystem::path& output, std::string& error)  override { (void)root;(void)output;(void)error;++downloads;output="downloaded.map";return downloadOK; }
    bool hostAuthoritative() const override { return authority; }
    bool canonicalSetupPublished() const override { return canonical; }
    bool resuming() const override { return resume; }
    bool resumeReady(std::string& error) const override { (void)error;return resumeOK; }
    bool canStart(std::string& error) const override { (void)error;return canStartOK; }
    bool initialized() const override { return initializedFlag; }
    RuntimeMultiplayerLanOperationResult setReady(bool value)  override { (void)value;++readies;lastReady=value;return operation; }
    RuntimeMultiplayerLanOperationResult startMatch(int tick)  override { (void)tick;++starts;lastTick=tick;return operation; }
    void close()  override { ++closes;present=false; }
    const RuntimeMultiplayerLobbyModel& lobby() const override { return model; }
    bool bridgePresent() const override{return present;}
    bool sessionActive() const override{return active;}
    const std::string& localPeerId() const override{return local;}
    const std::vector<RuntimeMultiplayerRoomMember>& members() const override{return roster;}
    RuntimeMultiplayerLanRole lanRole() const override{return role;}
    RuntimeMultiplayerLanPhase lanPhase() const override{return phase;}
    void leave() override{++leaves;}
};
struct View final : RuntimeLanMenuView {
    std::map<RuntimeLanMenuWidget,std::string> texts;std::map<RuntimeLanMenuWidget,bool> visible,enabled;
    std::map<RuntimeLanMenuWidget,std::vector<std::string>> lists;std::map<RuntimeLanMenuWidget,std::int64_t> selected;
    std::vector<std::string> calls;bool widgetPresent=true,acceptedFlag=false,checkedFlag=false,hostSetupOK=true,resumeOK=true,nameOK=true,entropyOK=true,compatOK=true,resolveOK=true,joinOptionsOK=true,projectionOK=true;
    unsigned clears=0,destroys=0,finishes=0,legacyBegins=0,legacyPolls=0,hostSetups=0,resumes=0,entropyReads=0,resolves=0;bool label=false;std::string error,kicked;
    RuntimeMultiplayerHostSelectionProjection projection;
    void showWidget(RuntimeLanMenuWidget w,bool b) override{visible[w]=b;}
    void enableWidget(RuntimeLanMenuWidget w,bool b) override{enabled[w]=b;}
    std::string text(RuntimeLanMenuWidget w) const override{auto p=texts.find(w);return p==texts.end()?std::string{}:p->second;}
    void setText(RuntimeLanMenuWidget w,const std::string& s) override{texts[w]=s;}
    void listClear(RuntimeLanMenuWidget w) override{lists[w].clear();}
    void listAdd(RuntimeLanMenuWidget w,const std::string& s) override{lists[w].push_back(s);}
    std::int64_t listSelected(RuntimeLanMenuWidget w) const override{auto p=selected.find(w);return p==selected.end()?-1:p->second;}
    void listSelect(RuntimeLanMenuWidget w,std::int64_t s) override{selected[w]=s;}
    bool checked(RuntimeLanMenuWidget) const override{return checkedFlag;}
    bool hasWidget(RuntimeLanMenuWidget) const override{return widgetPresent;}
    bool parseEndpoint(std::string_view,RuntimeMultiplayerDirectEndpoint& e,std::string&) override{e.port=7777;return true;}
    bool makeJoinOptions(const RuntimeLanRoomAdvertisement&,std::string&& name,const std::string& peer,bool spectator,std::string&& password,const RuntimeMultiplayerCompatibility&,RuntimeMultiplayerLanJoinOptions& o,std::string& e) override{if(!joinOptionsOK){e="bad advertisement";return false;}o.displayName=std::move(name);o.peerId=peer;o.participation=spectator?RuntimeMultiplayerParticipationRole::Spectator:RuntimeMultiplayerParticipationRole::Combatant;o.password=std::move(password);return true;}
    void clearPassword() override{++clears;texts[RuntimeLanMenuWidget::passwordEdit].clear();}
    void readyLabel(bool b) override{label=b;}
    bool runHostSetup(const RuntimeConfig&,RuntimeSkirmishSelection& s,std::string& e) override{++hostSetups;calls.push_back("setup");s.accepted=hostSetupOK;if(!hostSetupOK)e="cancelled";return hostSetupOK;}
    bool selectResume(const RuntimeConfig&,const RuntimeMultiplayerCompatibility&,const std::string&,const std::string&,RuntimeMultiplayerResumeSave&,RuntimeMultiplayerHostSelectionProjection& p,std::string& e) override{++resumes;calls.push_back("resume");p=projection;if(!resumeOK)e="cancelled save";return resumeOK;}
    bool normalizeName(std::string&,std::size_t n,std::string& e) override{calls.push_back("normalize:"+std::to_string(n));if(!nameOK)e="bad name";return nameOK;}
    std::string trim(const std::string& s) const override{const auto a=s.find_first_not_of(' ');return a==std::string::npos?std::string{}:s.substr(a,s.find_last_not_of(' ')-a+1);}
    bool secureU64(std::uint64_t& value,std::string& e) override{++entropyReads;calls.push_back("u64");value=100+entropyReads;if(!entropyOK)e="entropy denied";return entropyOK;}
    bool secureId(std::string_view prefix,std::string& value,std::string& e) override{calls.push_back(std::string(prefix));value=std::string(prefix)+"fixture";if(!entropyOK)e="id denied";return entropyOK;}
    bool projectHost(const RuntimeConfig&,const RuntimeSkirmishSelection&,const std::string&,const std::string&,std::uint64_t,RuntimeMultiplayerHostSelectionProjection& p,std::string& e) override{calls.push_back("project");p=projection;if(!projectionOK)e="projection denied";return projectionOK;}
    bool resolveMap(const RuntimeConfig&,const RuntimeMultiplayerMatchMap&,std::filesystem::path& p,std::string& e) override{++resolves;if(!resolveOK){e="map unavailable";return false;}p="resolved.map";return true;}
    bool makeCompatibility(const RuntimeConfig&,RuntimeMultiplayerCompatibility&,std::string& e) override{calls.push_back("compatibility");if(!compatOK)e="compatibility denied";return compatOK;}
    bool accepted() const override{return acceptedFlag;}
    std::string& resultError() override{return error;}
    void showKickedMessage(const std::string& s) override{kicked=s;calls.push_back("kicked");}
    void destroyWindow() override{++destroys;calls.push_back("destroy");}
    void finishStartedMatch() override{++finishes;}
    void beginDirectDiscovery() override{++legacyBegins;}
    void pollDirectDiscovery() override{++legacyPolls;}
};
RuntimeMultiplayerLobbyHostCreateOptions options(){
    RuntimeMultiplayerLobbyHostCreateOptions o;o.map.canonicalId="maps/official/2peaks.map";o.map.contentFingerprint=0x1020304050607080ULL;
    o.rules.gameMode="battle";o.rules.rulesOverride="mpbattlemd.ini";o.rules.randomSeed=17;o.rules.fixedTickHz=30;o.rules.startingCredits=10000;o.rules.startingUnits=10;o.rules.techLevel=10;o.rules.gameSpeed=2;
    o.hostMember.peerId="host";o.hostMember.displayName="Host";o.hostMember.role=RuntimeMultiplayerRoomMemberRole::Host;o.slotCount=8;return o;
}
int main(){unsigned checks=0,failures=0;auto require=[&](bool b,const char* why){++checks;if(!b){++failures;std::cerr<<why<<'\n';}};
 {Flow f;View v;RuntimeLanMenuController s(f,v);require(s.network==RuntimeMultiplayerMenuNetwork::Lan&&s.page==MultiplayerMenuPage::Browser&&!s.done&&!s.hosting&&!s.compatibilityReady,"original defaults");require(s.flow.initialized(),"borrow actual supplied flow");
  std::string e;v.compatOK=false;require(initializeLanMenuData(s,e)&&!s.compatibilityReady&&e=="compatibility denied"&&s.peerId=="peer-fixture","compat failure still reads peer entropy");require(v.calls==std::vector<std::string>{"compatibility","peer-"},"initialization ordering");initializeLanBrowser(s,e);require(v.text(RuntimeLanMenuWidget::statusText)==e&&f.browses==0,"compat failure avoids browse");v.entropyOK=false;require(!initializeLanMenuData(s,e)&&v.error=="id denied","peer entropy failure returned");
  showPage(s,MultiplayerMenuPage::Browser);require(v.visible[RuntimeLanMenuWidget::roomList]&&!v.visible[RuntimeLanMenuWidget::directAddressEdit]&&v.visible[RuntimeLanMenuWidget::passwordEdit],"LAN browser widgets");
  s.hosting=true;f.resume=true;s.displayedSetupRevision=99;showPage(s,MultiplayerMenuPage::Lobby);require(v.visible[RuntimeLanMenuWidget::resumeSwapButton]&&!v.visible[RuntimeLanMenuWidget::roomList]&&s.displayedSetupRevision==0,"resume lobby visibility and revision reset");
 }
 {Flow f;View v;RuntimeLanMenuController s(f,v);std::string e;f.broadcastOK=false;require(beginLanBrowsing(s,e)&&e.empty()&&f.browses==1&&f.lastPort==runtimeLanDefaultDiscoveryPort,"broadcast failure loopback fallback");require(f.probes==std::vector<std::string>{"255.255.255.255","127.0.0.1"},"both original probes ordered");require(beginLanBrowsing(s,e)&&f.browses==1,"already browsing avoids restart");f.loopbackOK=false;require(!beginLanBrowsing(s,e)&&e=="broadcast denied","both probe failures preserve first error");f.present=false;f.browseOK=false;auto n=f.probes.size();require(!beginLanBrowsing(s,e)&&f.probes.size()==n,"browse failure avoids probes");
  RuntimeLanRoomAdvertisement r;r.roomName="Room";r.playerCount=2;r.maximumPlayers=8;r.hostName="Host";r.hostAddress="127.0.0.1";r.sessionPort=5000;r.passwordProtected=true;f.rooms={r};updateRoomList(s);require(s.rooms.size()==1&&s.rooms[0].roomName==r.roomName&&s.rooms[0].playerCount==r.playerCount&&s.rooms[0].maximumPlayers==r.maximumPlayers&&s.rooms[0].hostName==r.hostName&&s.rooms[0].hostAddress==r.hostAddress&&s.rooms[0].sessionPort==r.sessionPort&&s.rooms[0].passwordProtected==r.passwordProtected&&v.lists[RuntimeLanMenuWidget::roomList]==std::vector<std::string>{"Room  [2/8]  Host  127.0.0.1:5000  [Password]"},"exact original discovery text");
  joinSelectedLanRoom(s);require(f.joins==0&&v.text(RuntimeLanMenuWidget::statusText)=="Select a discovered LAN room first.","no selection");v.selected[RuntimeLanMenuWidget::roomList]=1;joinSelectedLanRoom(s);require(f.joins==0,"selection upper bound");v.selected[RuntimeLanMenuWidget::roomList]=0;v.texts[RuntimeLanMenuWidget::playerNameEdit]="Guest";v.texts[RuntimeLanMenuWidget::passwordEdit]="secret";v.checkedFlag=true;s.peerId="guest";s.localMapPath="old";s.directDiscoveryPending=true;v.joinOptionsOK=false;joinSelectedLanRoom(s);require(f.joins==0&&v.text(RuntimeLanMenuWidget::statusText)=="bad advertisement","ad validation rejects before backend");
  v.joinOptionsOK=true;f.joinOK=false;joinSelectedLanRoom(s);require(s.page==MultiplayerMenuPage::Browser&&v.clears==0,"failed join preserves browser/password");f.joinOK=true;joinSelectedLanRoom(s);require(s.page==MultiplayerMenuPage::Lobby&&!s.hosting&&!s.directDiscoveryPending&&s.localMapPath.empty()&&v.clears==1,"successful join transitions");require(f.joinOptions.displayName=="Guest"&&f.joinOptions.peerId=="guest"&&f.joinOptions.participation==RuntimeMultiplayerParticipationRole::Spectator&&f.joinOptions.password=="secret","spectator/password join fields");
 }
 {Flow f;View v;RuntimeLanMenuController s(f,v);std::string e;require(!ensureLocalMapVerified(s,e)&&e.find("waiting for")!=std::string::npos&&f.verifies==0,"wait for canonical setup");f.mapVerified=true;require(ensureLocalMapVerified(s,e)&&v.resolves==0,"already verified no IO");f.mapVerified=false;f.canonical=true;f.authority=true;v.resolveOK=false;require(!ensureLocalMapVerified(s,e)&&f.downloads==0,"host missing map no download");f.authority=false;require(ensureLocalMapVerified(s,e)&&f.downloads==1&&s.localMapPath=="downloaded.map","client original download owner");s.localMapPath="bad.map";f.verifyOK=false;v.resolveOK=true;require(!ensureLocalMapVerified(s,e)&&s.localMapPath.empty()&&f.verifies==2,"stale path recheck and second invalidation");f.verifyOK=true;require(ensureLocalMapVerified(s,e)&&s.localMapPath=="resolved.map","resolved verified map");
 }
 {Flow f;View v;RuntimeLanMenuController s(f,v);std::string e;require(!startHosting(s,e)&&v.calls.empty(),"unqualified host avoids all effects");s.compatibilityReady=true;v.nameOK=false;require(!startHosting(s,e)&&v.hostSetups==0,"invalid name avoids nested setup");v.nameOK=true;v.hostSetupOK=false;require(!startHosting(s,e)&&f.hosts==0&&v.entropyReads==0,"nested cancellation avoids entropy/backend");v.hostSetupOK=true;v.entropyOK=false;require(!startHosting(s,e)&&f.hosts==0,"entropy failure avoids backend");v.entropyOK=true;v.projectionOK=false;require(!startHosting(s,e)&&f.hosts==0,"projection failure avoids host");v.projectionOK=true;v.projection.lobby=options();v.projection.slots={RuntimeMultiplayerMatchSlot{}};v.projection.localMapPath="selected.map";f.hostOK=false;require(!startHosting(s,e)&&!s.hosting&&v.clears==0,"backend host failure retains password");f.hostOK=true;f.slotOK=false;require(!startHosting(s,e)&&f.closes==1&&!s.hosting,"slot publication failure closes started flow");f.slotOK=true;f.canonical=true;f.mapVerified=true;require(RuntimeMultiplayerLobbyModel::createHost(options(),f.model,e),"real pure model fixture");f.roster={options().hostMember};s.peerId="host";v.texts[RuntimeLanMenuWidget::playerNameEdit]="Host";v.texts[RuntimeLanMenuWidget::passwordEdit]="secret";require(startHosting(s,e)&&s.hosting&&s.page==MultiplayerMenuPage::Lobby&&s.localMapPath=="selected.map","successful host transition");require(f.hostOptions.password=="secret"&&f.hostOptions.passwordProtected&&f.hostOptions.session.hostPeerId=="host"&&f.hostOptions.roomGeneration!=0,"host forwarded options");require(f.hostOptions.sessionPort==runtimeLanDefaultSessionPort&&f.hostOptions.discoveryPort==runtimeLanDefaultDiscoveryPort&&!f.resumePassed,"original LAN ports");
  auto slots=f.slotWrites,setup=v.hostSetups;v.resumeOK=false;require(!startHosting(s,e,true)&&f.slotWrites==slots,"resume cancel no host publication");v.resumeOK=true;require(startHosting(s,e,true)&&f.resumePassed&&f.slotWrites==slots&&v.hostSetups==setup,"resume uses save no fresh setup/slot rewrite");
 }
 {Flow f;View v;RuntimeLanMenuController s(f,v);std::string e;require(RuntimeMultiplayerLobbyModel::createHost(options(),f.model,e),"roster model");f.present=f.active=true;f.roster={options().hostMember};f.roster[0].ready=true;require(localMemberReady(s)&&humanSlotForPeer(s,"host")!=nullptr&&humanSlotForPeer(s,"missing")==nullptr,"ready and human owner query");s.hosting=true;s.selectedRosterPeerId="host";f.mapVerified=true;f.canStartOK=true;updateRoster(s);require(v.label&&s.rosterPeerIds==std::vector<std::string>{"host"}&&v.selected[RuntimeLanMenuWidget::rosterList]==0,"roster selection/readiness");require(v.enabled[RuntimeLanMenuWidget::readyButton]&&v.enabled[RuntimeLanMenuWidget::startButton]&&!v.enabled[RuntimeLanMenuWidget::kickButton],"ready/start/kick gating");require(v.text(RuntimeLanMenuWidget::countryEdit)=="random"&&v.text(RuntimeLanMenuWidget::startPositionEdit)=="0","slot controls original defaults");
  f.resume=true;updateRoster(s);require(!v.enabled[RuntimeLanMenuWidget::countryEdit]&&!v.enabled[RuntimeLanMenuWidget::applySlotButton],"resume locks editing");f.resume=false;v.texts[RuntimeLanMenuWidget::startPositionEdit]="9";applyLocalSlotOptions(s);require(f.optionWrites==0&&v.text(RuntimeLanMenuWidget::statusText).find("0 to 8")!=std::string::npos,"start position reject");for(const auto& p:std::array<std::string,4>{"","00","-1","a"}){v.texts[RuntimeLanMenuWidget::startPositionEdit]=p;applyLocalSlotOptions(s);require(f.optionWrites==0,"invalid start variants");}
  v.texts[RuntimeLanMenuWidget::startPositionEdit]=" 8 ";v.texts[RuntimeLanMenuWidget::countryEdit]=" america ";applyLocalSlotOptions(s);require(f.optionWrites==1&&f.lastPosition==8&&f.lastCountry=="america","host trim and exact upper position");s.hosting=false;f.operation.succeeded=true;applyLocalSlotOptions(s);require(f.requests==1,"client options request");s.hosting=true;s.selectedRosterPeerId="remote";kickSelectedPeer(s);require(f.kicks==1&&f.lastPeer=="remote"&&s.selectedRosterPeerId.empty(),"kick forwards and clears selection");v.selected[RuntimeLanMenuWidget::rosterList]=-1;rosterSelectionChanged(s);require(s.selectedRosterPeerId.empty(),"invalid roster selection cleared");require(handleControlClick(s,ResumeSwapButton)&&f.swaps==0,"swap without human owner rejected");s.selectedRosterPeerId="host";require(handleControlClick(s,ResumeSwapButton)&&f.swaps==1&&f.lastIndex==0,"resume swap reuses flow owner");
 }
 {Flow f;View v;RuntimeLanMenuController s(f,v);f.initializedFlag=false;pollMenu(s);require(f.polls==0,"uninitialized poll ignored");f.initializedFlag=true;f.polled.succeeded=false;f.polled.error="transport error";s.directDiscoveryPending=true;pollMenu(s);require(!s.directDiscoveryPending&&v.text(RuntimeLanMenuWidget::statusText)=="transport error","poll failure status");f.polled.succeeded=true;RuntimeMultiplayerLanEvent event;event.kind=RuntimeMultiplayerLanEventKind::Kicked;f.polled.events={event};s.pendingPassword="secret";pollMenu(s);require(v.kicked=="You were removed from the lobby by the host."&&s.pendingPassword.empty()&&s.done&&f.closes==1&&v.destroys==1,"kick message then full close");require(v.calls==std::vector<std::string>{"kicked","destroy"},"kick notification before destroy");f.polled.events.clear();s.done=false;s.page=MultiplayerMenuPage::Lobby;f.polled.matchStarted=true;pollMenu(s);require(v.finishes==1,"match bridge transfer delegated original owner");f.operation.succeeded=false;f.operation.reason="not ready";startMatch(s);require(f.lastTick==1&&v.text(RuntimeLanMenuWidget::statusText)=="not ready","original start tick and failure");f.operation.succeeded=true;startMatch(s);require(v.text(RuntimeLanMenuWidget::statusText)=="Starting the verified room...","start success status");toggleReady(s);require(f.lastReady&&f.readies==1,"ready toggles missing member false");f.role=RuntimeMultiplayerLanRole::Client;f.phase=RuntimeMultiplayerLanPhase::Lobby;f.present=true;v.acceptedFlag=true;auto closes=f.closes;leaveLobby(s);require(f.leaves==1&&f.closes==closes,"accepted transfer not flow closed");v.widgetPresent=false;auto clear=v.clears;closeMenu(s);require(v.clears==clear,"null original password handle guard");
  auto old=v.text(RuntimeLanMenuWidget::statusText);require(!handleControlClick(s,-99)&&v.text(RuntimeLanMenuWidget::statusText)==old,"unknown control delegates native default");s.network=RuntimeMultiplayerMenuNetwork::Internet;require(handleControlClick(s,JoinButton)&&v.legacyBegins==1,"legacy direct hook retained no production-route credit");
 }
 std::cout<<"LAN same production controller: checks="<<checks<<" failures="<<failures<<" skipped=0\n";return failures?1:0;
}
