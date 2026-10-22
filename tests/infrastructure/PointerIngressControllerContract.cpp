#include "ui/input/RuntimePointerIngressController.h"
#include "ui/sidebar/RuntimeSidebarPageController.h"
#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINDEF_) || defined(_MINWINDEF_)
#error Public pointer/sidebar headers introduced Windows declarations
#endif
#include "ui/input/RuntimeReferencePresentationController.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Contract {
using Point=RuntimeInputPixelPoint;
using Rect=RuntimeInputPixelRect;
using Row=RuntimeDiplomacyPointerRow;
using Event=RuntimePointerIngressEvent;
constexpr std::array<int,7> independentChatControls={1750,1751,1752,1753,1762,1763,1764};
std::string value(bool v){return v?"1":"0";}
std::string value(int v){return std::to_string(v);}
std::string value(std::uint8_t v){return std::to_string(static_cast<unsigned>(v));}
std::string value(RuntimeBuildKind v){return std::to_string(static_cast<int>(v));}
std::string value(const std::string& v){return v;}
std::string value(Point v){return std::to_string(v.x)+","+std::to_string(v.y);}
std::string value(Rect v){return std::to_string(v.left)+","+std::to_string(v.top)+","+std::to_string(v.right)+","+std::to_string(v.bottom);}
std::string value(std::optional<std::size_t> v){return v?std::to_string(*v):"none";}
std::string value(const Row& r){return r.owner+":"+value(r.self)+":"+value(r.defeated)+":"+value(r.human)+":"+value(r.slot);}
std::string value(const std::vector<Row>& rows){std::string s;for(const auto& r:rows)s+="["+value(r)+"]";return s;}
struct State {
 bool finished=false,campaign=false,banner=false,menu=false,chat=false,diplomacy=false,capture=false;
 bool pressed=false,hovered=false,leftActive=true,leftMoved=true,rightActive=true,rightMoved=true;
 bool taunts=true,beaconEditing=true,recipientsExplicit=false,repaint=false,recipientsAvailable=true,allianceAllowed=true;
 bool overlap=false;
 int diplomacyPressed=0,diplomacyHovered=0,diplomacyRow=4;
 std::uint8_t recipients=7,slotsMask=7;
 std::optional<std::size_t> ownSlot=std::size_t{0};
 std::u16string draft=u"draft";
 std::vector<Row> roster;
 RuntimeBuildKind tab=RuntimeBuildKind::Building;
 std::vector<int> pages{0,0,0,0},wheel{0,0,0,0};
 int filtered=13,superWeapons=2,perPage=4,delta=120;
 int invalidates=0,redraws=0,captureSets=0,captureReleases=0,returns=0,tick=21;
 Rect client{0,0,800,600},score{100,100,120,120},sidebar{632,0,800,600};
 Rect close{100,100,120,120},taunt{100,140,160,160};
 std::vector<std::string> commands;
};
State initialState(){
 State s;
 for(std::size_t i=0;i<8;++i)s.roster.push_back(Row{i==0?"Self":"Peer"+std::to_string(i),i==0,false,true,i});
 return s;
}
std::string stateValue(const State& s){
 std::ostringstream o;
 o<<s.finished<<s.campaign<<s.banner<<s.menu<<s.chat<<s.diplomacy<<s.capture
  <<s.pressed<<s.hovered<<s.leftActive<<s.leftMoved<<s.rightActive<<s.rightMoved
  <<s.taunts<<s.beaconEditing<<s.recipientsExplicit<<s.repaint<<s.recipientsAvailable<<s.allianceAllowed<<s.overlap
  <<";ui="<<s.diplomacyPressed<<','<<s.diplomacyHovered<<','<<s.diplomacyRow
  <<";mask="<<static_cast<unsigned>(s.recipients)<<','<<static_cast<unsigned>(s.slotsMask)<<','<<value(s.ownSlot)
  <<";draft=";for(char16_t c:s.draft)o<<static_cast<unsigned>(c)<<',';
 o<<";rows="<<value(s.roster)<<";tab="<<value(s.tab)<<";pages=";
 for(int p:s.pages)o<<p<<',';o<<";wheel=";for(int r:s.wheel)o<<r<<',';
 o<<";catalog="<<s.filtered<<','<<s.superWeapons<<','<<s.perPage<<','<<s.delta
  <<";effects="<<s.invalidates<<','<<s.redraws<<','<<s.captureSets<<','<<s.captureReleases<<','<<s.returns<<','<<s.tick
  <<";rects="<<value(s.client)<<';'<<value(s.score)<<';'<<value(s.sidebar)<<';'<<value(s.close)<<';'<<value(s.taunt);
 for(const auto& c:s.commands)o<<";command="<<c;
 return o.str();
}
struct Trace {
 int depth=0;std::string name,args,result,before,after;bool threw=false;
 bool operator==(const Trace&) const=default;
};
struct Model final:RuntimePointerIngressHost,RuntimeSidebarWheelHost {
 std::array<State,2> roots;
 int root=0,depth=0,ordinal=0,throwAt=0,hookAt=1,hookSeen=0;
 bool throwAfter=false,hookFired=false;
 RuntimePointerIngressInput carrier{};
 Row rowCarrier{};
 int inputDelta=0;
 std::string hookName;
 std::function<void(Model&)> hook;
 std::vector<Trace> trace;
 explicit Model(const State& s):roots{s,s}{}
 State& s(){return roots[static_cast<std::size_t>(root)];}
 const State& s()const{return roots[static_cast<std::size_t>(root)];}
 std::string dump()const{return "root="+std::to_string(root)+";A="+stateValue(roots[0])+";B="+stateValue(roots[1])
  +";carrier="+std::to_string(static_cast<unsigned>(carrier.event))+":"+value(carrier.point)
  +";rowCarrier="+value(rowCarrier)+";inputDelta="+value(inputDelta);}
 template<class T,class F>T query(const std::string& name,const std::string& args,F fn){
  const std::size_t at=trace.size();trace.push_back({depth,name,args,"<pending>",dump(),"",false});++ordinal;
  const int thisOrdinal=ordinal;
  if(throwAt==thisOrdinal&&!throwAfter){trace[at].threw=true;trace[at].after=dump();throw std::runtime_error("primitive-before");}
  try{
   T result=fn();trace[at].result=value(result);
   if(name==hookName&&++hookSeen==hookAt&&!hookFired){hookFired=true;if(hook)hook(*this);}
   if(throwAt==thisOrdinal&&throwAfter)throw std::runtime_error("primitive-after");
   trace[at].after=dump();return result;
  }catch(...){trace[at].threw=true;trace[at].after=dump();throw;}
 }
 template<class F>void effect(const std::string& name,const std::string& args,F fn){
  (void)query<int>(name,args,[&]{fn();return 0;});
 }
 std::vector<Row> rows()override{return query<std::vector<Row>>("rows","",[&]{return s().roster;});}
 Rect controlRect(Rect client,int id)override{return query<Rect>("controlRect",value(client)+":"+value(id),[&]{
  if(s().overlap)return Rect{0,0,20,20};return id==1059?s().close:id==1840?s().taunt:Rect{-100,-100,-90,-90};});}
 Rect checkboxRect(Rect client,int id)override{return query<Rect>("checkboxRect",value(client)+":"+value(id),[&]{
  if(s().overlap)return Rect{0,0,20,20};
  if(id>=1743&&id<=1749){const int x=20+(id-1743)*20;return Rect{x,20,x+10,30};}
  const auto it=std::find(independentChatControls.begin(),independentChatControls.end(),id);
  if(it!=independentChatControls.end()){const int x=20+static_cast<int>(it-independentChatControls.begin())*20;return Rect{x,60,x+10,70};}
  return Rect{-100,-100,-90,-90};});}
 bool allianceEnabled(const Row& row)override{return query<bool>("allianceEnabled",value(row),[&]{return !row.self&&!row.defeated&&s().allianceAllowed;});}
 void toggleAlliance(std::string target)override{effect("toggleAlliance",target,[&]{s().commands.push_back("Alliance:"+target+":"+std::to_string(s().tick));});}
 bool chatRecipientsAvailable()override{return query<bool>("chatRecipientsAvailable","",[&]{return s().recipientsAvailable;});}
 std::uint8_t chatSlotsMask()override{return query<std::uint8_t>("chatSlotsMask","",[&]{return s().slotsMask;});}
 std::optional<std::size_t> ownChatSlot()override{return query<std::optional<std::size_t>>("ownChatSlot","",[&]{return s().ownSlot;});}
 bool chatRecipientsExplicit()override{return query<bool>("chatRecipientsExplicit","",[&]{return s().recipientsExplicit;});}
 void setChatRecipientsExplicit(bool v)override{effect("setChatRecipientsExplicit",value(v),[&]{s().recipientsExplicit=v;});}
 std::uint8_t chatRecipients()override{return query<std::uint8_t>("chatRecipients","",[&]{return s().recipients;});}
 void setChatRecipients(std::uint8_t v)override{effect("setChatRecipients",value(v),[&]{s().recipients=v;});}
 void setDiplomacyOpen(bool v)override{effect("setDiplomacyOpen",value(v),[&]{s().diplomacy=v;});}
 void setDiplomacyRow(int v)override{effect("setDiplomacyRow",value(v),[&]{s().diplomacyRow=v;});}
 int diplomacyPressedControl()override{return query<int>("diplomacyPressedControl","",[&]{return s().diplomacyPressed;});}
 void setDiplomacyPressedControl(int v)override{effect("setDiplomacyPressedControl",value(v),[&]{s().diplomacyPressed=v;});}
 void setDiplomacyHoveredControl(int v)override{effect("setDiplomacyHoveredControl",value(v),[&]{s().diplomacyHovered=v;});}
 bool tauntsEnabled()override{return query<bool>("tauntsEnabled","",[&]{return s().taunts;});}
 void setTauntsEnabled(bool v)override{effect("setTauntsEnabled",value(v),[&]{s().taunts=v;});}
 void setChatOpen(bool v)override{effect("setChatOpen",value(v),[&]{s().chat=v;});}
 void setBeaconEditing(bool v)override{effect("setBeaconEditing",value(v),[&]{s().beaconEditing=v;});}
 void clearChatDraft()override{effect("clearChatDraft","",[&]{s().draft.clear();});}
 void setLeftActive(bool v)override{effect("setLeftActive",value(v),[&]{s().leftActive=v;});}
 void setLeftMoved(bool v)override{effect("setLeftMoved",value(v),[&]{s().leftMoved=v;});}
 void setRightActive(bool v)override{effect("setRightActive",value(v),[&]{s().rightActive=v;});}
 void setRightMoved(bool v)override{effect("setRightMoved",value(v),[&]{s().rightMoved=v;});}
 void requestRepaint()override{effect("requestRepaint","",[&]{s().repaint=true;});}
 bool finished()override{return query<bool>("finished","",[&]{return s().finished;});}
 bool campaignMode()override{return query<bool>("campaignMode","",[&]{return s().campaign;});}
 bool bannerVisible()override{return query<bool>("bannerVisible","",[&]{return s().banner;});}
 bool menuOpen()override{return query<bool>("menuOpen","",[&]{return s().menu;});}
 bool chatOpen()override{return query<bool>("chatOpen","",[&]{return s().chat;});}
 bool diplomacyOpen()override{return query<bool>("diplomacyOpen","",[&]{return s().diplomacy;});}
 bool ownsCapture()override{return query<bool>("ownsCapture","",[&]{return s().capture;});}
 void releaseCapture()override{effect("releaseCapture","",[&]{s().capture=false;++s().captureReleases;});}
 void setCapture()override{effect("setCapture","",[&]{s().capture=true;++s().captureSets;});}
 Rect scoreClientRect()override{return query<Rect>("scoreClientRect","",[&]{return s().client;});}
 Rect diplomacyClientRect()override{return query<Rect>("diplomacyClientRect","",[&]{return s().client;});}
 Rect scoreContinueRect(Rect c)override{return query<Rect>("scoreContinueRect",value(c),[&]{return s().score;});}
 bool scoreContinuePressed()override{return query<bool>("scoreContinuePressed","",[&]{return s().pressed;});}
 void setScoreContinuePressed(bool v)override{effect("setScoreContinuePressed",value(v),[&]{s().pressed=v;});}
 void setScoreContinueHovered(bool v)override{effect("setScoreContinueHovered",value(v),[&]{s().hovered=v;});}
 void sendScoreReturn()override{effect("sendScoreReturn","WM_KEYDOWN:VK_RETURN:0",[&]{++s().returns;s().commands.push_back("Return");});}
 void invalidate()override{effect("invalidate","FALSE",[&]{++s().invalidates;});}
 RuntimeBuildKind activeTab()override{return query<RuntimeBuildKind>("activeTab","",[&]{return s().tab;});}
 int pageSlotCount()override{return query<int>("pageSlotCount","",[&]{return static_cast<int>(s().pages.size());});}
 int pageAt(int i)override{return query<int>("pageAt",value(i),[&]{return s().pages.at(static_cast<std::size_t>(i));});}
 int filteredCatalogCount()override{return query<int>("filteredCatalogCount","",[&]{return s().filtered;});}
 int visibleSuperWeaponCount()override{return query<int>("visibleSuperWeaponCount","",[&]{return s().superWeapons;});}
 int buttonsPerPage(Rect r)override{return query<int>("buttonsPerPage",value(r),[&]{return s().perPage;});}
 void setPageAt(int i,int v)override{effect("setPageAt",value(i)+":"+value(v),[&]{s().pages.at(static_cast<std::size_t>(i))=v;});}
 int wheelSlotCount()override{return query<int>("wheelSlotCount","",[&]{return static_cast<int>(s().wheel.size());});}
 int wheelAt(int i)override{return query<int>("wheelAt",value(i),[&]{return s().wheel.at(static_cast<std::size_t>(i));});}
 void setWheelAt(int i,int v)override{effect("setWheelAt",value(i)+":"+value(v),[&]{s().wheel.at(static_cast<std::size_t>(i))=v;});}
 void status(const std::string& text)override{effect("status",text,[&]{s().commands.push_back("status:"+text);});}
 int wheelDelta()override{return query<int>("wheelDelta","",[&]{return inputDelta;});}
 Rect sidebarRect()override{return query<Rect>("sidebarRect","",[&]{return s().sidebar;});}
 void redraw()override{effect("redraw","RDW_INVALIDATE|RDW_NOERASE",[&]{++s().redraws;});}
};
enum class Owner{Ingress,Open,Close,ChatEnabled,ToggleChat,Hit,Mouse,Tab,Active,Set,Count,Clamp,Scroll,Wheel};
struct Action {
 Owner owner=Owner::Ingress;
 RuntimePointerIngressInput input{Event::Other,{25,25}};
 Row row{"Peer1",false,false,true,std::size_t{1}};
 int number=0,delta=120;
 RuntimeBuildKind kind=RuntimeBuildKind::Building;
 Rect rect{632,0,800,600};
};
std::string execute(Model&,const Action&,bool);
} // namespace Contract

#include "fixtures/PointerIngressOld.inl"
namespace Contract {
std::string execute(Model& m,const Action& a,bool old){
 m.carrier=a.input;m.rowCarrier=a.row;m.inputDelta=a.delta;
 if(old){
  frozenPointerIngress::RuntimeWindowState state(m);
  frozenPointerIngress::Binding binding(m,state);
  switch(a.owner){
  case Owner::Ingress:return value(frozenPointerIngress::ingress(m,m.carrier)==0);
  case Owner::Open:frozenPointerIngress::openRuntimeDiplomacyPage(state);break;
  case Owner::Close:frozenPointerIngress::closeRuntimeDiplomacyPage(state);break;
  case Owner::ChatEnabled:return value(frozenPointerIngress::runtimeDiplomacyChatEnabled(state,m.rowCarrier));
  case Owner::ToggleChat:frozenPointerIngress::runtimeDiplomacyToggleChat(state,m.rowCarrier);break;
  case Owner::Hit:return value(frozenPointerIngress::runtimeDiplomacyHitControl(state,a.rect,m.carrier.point));
  case Owner::Mouse:frozenPointerIngress::handleRuntimeDiplomacyMouse(state,a.rect,frozenPointerIngress::nativeMessage(m.carrier.event),m.carrier.point);break;
  case Owner::Tab:return value(frozenPointerIngress::runtimeBuildTabIndex(a.kind));
  case Owner::Active:return value(frozenPointerIngress::runtimeActiveBuildPage(state));
  case Owner::Set:frozenPointerIngress::runtimeSetActiveBuildPage(state,a.number);break;
  case Owner::Count:return value(frozenPointerIngress::runtimeBuildPageCount(state,a.rect));
  case Owner::Clamp:frozenPointerIngress::runtimeClampActiveBuildPage(state,a.rect);break;
  case Owner::Scroll:return value(frozenPointerIngress::scrollRuntimeSidebarWheel(state,a.rect,a.delta));
  case Owner::Wheel:(void)frozenPointerIngress::wheel(m);break;
  }
 }else{
  switch(a.owner){
  case Owner::Ingress:return value(handleRuntimePointerIngress(m.carrier,m));
  case Owner::Open:openRuntimeDiplomacyPointer(m);break;
  case Owner::Close:closeRuntimeDiplomacyPointer(m);break;
  case Owner::ChatEnabled:return value(runtimeDiplomacyPointerChatEnabled(m.rowCarrier,m));
  case Owner::ToggleChat:toggleRuntimeDiplomacyPointerChat(m.rowCarrier,m);break;
  case Owner::Hit:return value(hitRuntimeDiplomacyPointerControl(a.rect,m.carrier.point,m));
  case Owner::Mouse:handleRuntimeDiplomacyPointer(m.carrier,a.rect,m);break;
  case Owner::Tab:return value(runtimeSidebarPageTabIndex(a.kind));
  case Owner::Active:return value(runtimeSidebarPageActive(m));
  case Owner::Set:runtimeSidebarPageSet(m,a.number);break;
  case Owner::Count:return value(runtimeSidebarPageCount(m,a.rect));
  case Owner::Clamp:runtimeSidebarPageClamp(m,a.rect);break;
  case Owner::Scroll:return value(runtimeSidebarPageScroll(m,a.rect,a.delta));
  case Owner::Wheel:handleRuntimeSidebarWheel(m);break;
  }
 }
 return "void";
}
struct Result{std::string returned,state,error;std::vector<Trace> trace;bool hookFired=false;};
using Setup=std::function<void(Model&,bool)>;
Result run(const State& state,const Action& action,bool old,const Setup& setup={}){
 Model m(state);if(setup)setup(m,old);
 std::string result,error;
 try{result=execute(m,action,old);}catch(const std::exception& e){error=e.what();}catch(...){error="<nonstandard>";}
 return {result,m.dump(),error,m.trace,m.hookFired};
}
std::size_t checks=0,cases=0,traceEvents=0,dynamicCases=0,nestedCases=0,exceptionCases=0;
void need(bool condition,const std::string& label){++checks;if(!condition)throw std::runtime_error(label);}
Result compare(const State& state,const Action& action,const std::string& label,const Setup& setup={}){
 const auto old=run(state,action,true,setup),now=run(state,action,false,setup);
 ++cases;traceEvents+=now.trace.size();
 need(old.returned==now.returned,label+": return");
 need(old.error==now.error,label+": exception");
 need(old.state==now.state,label+": complete owned state");
 if(old.trace!=now.trace){
  std::size_t at=0;while(at<old.trace.size()&&at<now.trace.size()&&old.trace[at]==now.trace[at])++at;
  const std::string a=at<old.trace.size()?old.trace[at].name+":"+old.trace[at].args:"<end>";
  const std::string b=at<now.trace.size()?now.trace[at].name+":"+now.trace[at].args:"<end>";
  need(false,label+": ordered primitive trace "+std::to_string(at)+" old="+a+" new="+b);
 }
 need(old.trace==now.trace,label+": complete before/after/result/depth trace");
 return now;
}
Action ingress(Event event,Point point={105,105}){Action a;a.owner=Owner::Ingress;a.input={event,point};return a;}
Action mouse(Event event,Point point){Action a;a.owner=Owner::Mouse;a.input={event,point};return a;}
Action owner(Owner which){Action a;a.owner=which;return a;}
void expectState(const State& state,const Action& a,const std::function<bool(const Model&)>& predicate,const std::string& label){
 Model m(state);(void)execute(m,a,false);need(predicate(m),label);
}
void ordinaryCases(){
 const std::array<Event,21> events{
  Event::Other,Event::PointerMove,Event::LeftButtonDown,Event::LeftButtonUp,Event::LeftButtonDoubleClick,
  Event::RightButtonDown,Event::RightButtonUp,Event::RightButtonDoubleClick,
  Event::MiddleButtonDown,Event::MiddleButtonUp,Event::MiddleButtonDoubleClick,
  Event::AuxiliaryButtonDown,Event::AuxiliaryButtonUp,Event::AuxiliaryButtonDoubleClick,
  Event::VerticalWheel,Event::HorizontalWheel,Event::CaptureChanged,Event::CancelMode,
  Event::FocusLost,Event::ApplicationDeactivated,static_cast<Event>(255)};
 // Both aliases are legal simultaneous flags; the complete original precedence decides.
 for(int bits=0;bits<128;++bits)for(const auto event:events){
  State s=initialState();s.finished=(bits&1)!=0;s.campaign=(bits&2)!=0;s.banner=(bits&4)!=0;
  s.menu=(bits&8)!=0;s.chat=(bits&16)!=0;s.diplomacy=(bits&32)!=0;s.capture=(bits&64)!=0;
  s.pressed=true;s.diplomacyPressed=1059;
  (void)compare(s,ingress(event),"outer-"+std::to_string(bits)+"-"+std::to_string(static_cast<unsigned>(event)));
 }
 const std::array<Point,10> points{{{99,105},{100,100},{119,119},{120,105},{105,120},{120,120},{-32768,32767},{32767,-32768},{0,0},{105,105}}};
 for(const auto p:points)for(const auto event:{Event::PointerMove,Event::LeftButtonDown,Event::LeftButtonUp}){
  for(bool pressed:{false,true}){
   State s=initialState();s.finished=true;s.pressed=pressed;s.capture=true;
   (void)compare(s,ingress(event,p),"score-edge-"+value(p)+"-"+value(pressed));
  }
 }
 {
  State s=initialState();s.finished=true;s.pressed=true;
  expectState(s,ingress(Event::LeftButtonUp,{120,105}),[](const Model& m){return m.s().returns==0&&!m.s().pressed;},"score right edge is excluded");
  expectState(s,ingress(Event::LeftButtonUp,{119,119}),[](const Model& m){return m.s().returns==1&&!m.s().pressed&&m.s().repaint;},"score inside sends exactly one Return");
 }
 for(Owner which:{Owner::Open,Owner::Close}){
  for(int bits=0;bits<16;++bits){
   State s=initialState();s.diplomacy=(bits&1)!=0;s.chat=(bits&2)!=0;s.leftActive=(bits&4)!=0;s.rightMoved=(bits&8)!=0;
   (void)compare(s,owner(which),"local-open-close-"+std::to_string(bits));
  }
 }
 const std::array<Point,7> controlPoints{{{105,105},{105,145},{25,25},{25,65},{145,25},{145,65},{500,400}}};
 for(std::size_t rows=0;rows<=9;++rows)for(const auto p:controlPoints)for(const auto event:{Event::PointerMove,Event::LeftButtonDown,Event::LeftButtonUp,Event::RightButtonUp}){
  for(int enabled=0;enabled<4;++enabled){
   State s=initialState();s.roster.resize(rows);s.allianceAllowed=(enabled&1)!=0;s.recipientsAvailable=(enabled&2)!=0;
   if(rows==0||rows>8)s.recipientsAvailable=false;
   s.diplomacy=true;s.diplomacyPressed=p.y==25?1743:p.y==65?1750:p.y==145?1840:1059;
   (void)compare(s,mouse(event,p),"diplomacy-"+std::to_string(rows)+"-"+value(p)+"-"+std::to_string(enabled));
  }
 }
 for(int i=0;i<7;++i){
  for(const bool chat:{false,true}){
   const Point p{25+i*20,chat?65:25};
   State s=initialState();s.diplomacy=true;s.diplomacyPressed=chat?std::array<int,7>{1750,1751,1752,1753,1762,1763,1764}[static_cast<std::size_t>(i)]:1743+i;
   (void)compare(s,mouse(Event::LeftButtonUp,p),"each-original-row-"+std::to_string(i)+"-"+value(chat));
  }
 }
 for(int bits=0;bits<64;++bits){
  State s=initialState();s.recipientsAvailable=(bits&1)!=0;s.recipientsExplicit=(bits&2)!=0;s.recipients=static_cast<std::uint8_t>((bits*37)&255);
  Action a=owner(Owner::ChatEnabled);
  a.row.self=(bits&4)!=0;a.row.human=(bits&8)==0;a.row.defeated=(bits&16)!=0;
  if((bits&32)!=0)a.row.slot.reset();
  (void)compare(s,a,"chat-enabled-"+std::to_string(bits));a.owner=Owner::ToggleChat;
  (void)compare(s,a,"chat-toggle-"+std::to_string(bits));
 }
 for(std::size_t slot:{std::size_t{0},std::size_t{1},std::size_t{7},std::size_t{8},std::size_t{255}}){
  State s=initialState();Action a=owner(Owner::ToggleChat);a.row.slot=slot;
  (void)compare(s,a,"chat-slot-boundary-"+std::to_string(slot));
 }
 {
  State s=initialState();s.overlap=true;Action a=owner(Owner::Hit);a.input.point={5,5};
  const auto result=compare(s,a,"close-priority-overlap");need(result.returned=="1059","close precedes rows, alliance, chat and taunts");
  need(result.trace.size()==1&&result.trace.front().name=="controlRect","close hit avoids row queries");
 }
 const std::array<RuntimeBuildKind,5> tabs{RuntimeBuildKind::Building,RuntimeBuildKind::Defense,RuntimeBuildKind::Infantry,RuntimeBuildKind::Unit,static_cast<RuntimeBuildKind>(255)};
 const std::array<int,13> deltas{0,1,-1,119,-119,120,-120,121,-121,240,-240,960,-960};
 for(const auto tab:tabs){
  Action a=owner(Owner::Tab);a.kind=tab;(void)compare(initialState(),a,"tab-index-"+value(tab));
  for(int page:{-3,0,1,4,20})for(int remainder:{-119,0,119})for(const int delta:deltas){
   State s=initialState();s.tab=tab;std::fill(s.pages.begin(),s.pages.end(),page);std::fill(s.wheel.begin(),s.wheel.end(),remainder);
   a=owner(Owner::Scroll);a.delta=delta;(void)compare(s,a,"scroll-"+value(tab)+"-"+std::to_string(page)+"-"+std::to_string(remainder)+"-"+std::to_string(delta));
   a.owner=Owner::Wheel;(void)compare(s,a,"wheel-"+value(tab)+"-"+std::to_string(page)+"-"+std::to_string(remainder)+"-"+std::to_string(delta));
  }
  for(std::size_t slots:{std::size_t{0},std::size_t{1},std::size_t{4}})for(Owner which:{Owner::Active,Owner::Set,Owner::Clamp,Owner::Scroll}){
   State s=initialState();s.tab=tab;s.pages.resize(slots);s.wheel.resize(slots);
   a=owner(which);a.number=-5;(void)compare(s,a,"page-array-boundary-"+value(tab)+"-"+std::to_string(slots)+"-"+std::to_string(static_cast<int>(which)));
  }
  for(int filtered:{0,1,4,5,20})for(int perPage:{1,4,16}){
   State s=initialState();s.tab=tab;s.filtered=filtered;s.perPage=perPage;
   for(Owner which:{Owner::Count,Owner::Clamp,Owner::Scroll}){a=owner(which);(void)compare(s,a,"count-and-single-page-"+value(tab)+"-"+std::to_string(filtered)+"-"+std::to_string(perPage));}
  }
 }
 {
  State s=initialState();s.menu=true;Action a=owner(Owner::Wheel);
  const auto result=compare(s,a,"wheel-menu-short-circuit");need(result.trace.size()==1&&result.trace.front().name=="menuOpen","menu blocks delta/client/page queries");
  s=initialState();s.tab=RuntimeBuildKind::Building;s.filtered=20;s.pages[0]=0;s.wheel[0]=119;
  a=owner(Owner::Scroll);a.delta=1;
  expectState(s,a,[](const Model& m){return m.s().pages[0]==0&&m.s().wheel[0]==0;},"wheel boundary clears unusable positive remainder");
  s.pages[0]=2;s.wheel[0]=0;a.delta=120;
  expectState(s,a,[](const Model& m){return m.s().pages[0]==1&&m.s().commands.back()=="status:build page 2/5";},"positive wheel moves to previous page and formats exact status");
  s=initialState();s.recipients=0;s.slotsMask=15;
  a=owner(Owner::ToggleChat);a.row.slot=std::size_t{1};
  expectState(s,a,[](const Model& m){return m.s().recipients==13&&m.s().recipientsExplicit&&m.s().repaint;},"toggle initializes full slots, flips target and retains local recipient");
 }
}


void nestedCall(Model& m,const Action& action,bool old){
 const int previous=m.depth;m.depth=previous+1;
 try{(void)execute(m,action,old);m.depth=previous;}catch(...){m.depth=previous;throw;}
}
void dynamicAndExceptions(){
 struct Dynamic{std::string name;State state;Action action;std::string port;int occurrence;std::function<void(Model&,bool)> callback;bool nested;};
 std::vector<Dynamic> list;
 {
  State s=initialState();s.finished=true;s.capture=true;s.pressed=true;
  list.push_back({"finished-release-fresh-root",s,ingress(Event::LeftButtonUp),"releaseCapture",1,[](Model& m,bool){m.root=1;m.s().finished=false;m.s().pressed=true;m.s().score={100,100,120,120};},false});
  list.push_back({"score-owned-input",s,ingress(Event::LeftButtonDown),"scoreClientRect",1,[](Model& m,bool){m.carrier={Event::RightButtonUp,{-300,-300}};},false});
  list.push_back({"score-rectangle-fresh-root",s,ingress(Event::LeftButtonDown),"scoreContinueRect",1,[](Model& m,bool){m.root=1;m.s().score={-200,-200,-100,-100};},false});
  list.push_back({"score-return-synchronous-nested",s,ingress(Event::LeftButtonUp),"sendScoreReturn",1,[](Model& m,bool old){nestedCall(m,ingress(Event::Other),old);},true});
 }
 {
  State s=initialState();s.diplomacy=true;s.capture=true;s.diplomacyPressed=1743;
  list.push_back({"diplomacy-owned-rows",s,mouse(Event::LeftButtonUp,{25,25}),"rows",1,[](Model& m,bool){m.root=1;m.s().roster.resize(2);m.s().roster[1].owner="FreshPeer";},false});
  list.push_back({"diplomacy-dispatch-uses-fresh-rows",s,mouse(Event::LeftButtonUp,{25,25}),"setDiplomacyPressedControl",1,[](Model& m,bool){m.root=1;m.s().roster[1].owner="AfterPressedPeer";},false});
  list.push_back({"diplomacy-owned-point",s,mouse(Event::LeftButtonDown,{25,25}),"rows",1,[](Model& m,bool){m.carrier={Event::RightButtonUp,{105,105}};},false});
  list.push_back({"same-window-setcapture-reentry",s,ingress(Event::LeftButtonDown,{25,25}),"setCapture",1,[](Model& m,bool old){nestedCall(m,ingress(Event::CaptureChanged),old);},true});
  list.push_back({"release-cancel-reentry",s,ingress(Event::LeftButtonUp,{25,25}),"releaseCapture",1,[](Model& m,bool old){nestedCall(m,ingress(Event::CaptureChanged),old);},true});
  list.push_back({"cancel-fresh-capture-root",s,ingress(Event::CancelMode),"setDiplomacyPressedControl",1,[](Model& m,bool){m.root=1;m.s().capture=false;},false});
  list.push_back({"close-nested-open",s,owner(Owner::Close),"setDiplomacyOpen",1,[](Model& m,bool old){nestedCall(m,owner(Owner::Open),old);},true});
  list.push_back({"toggle-owned-roster-and-valid-local",s,mouse(Event::LeftButtonUp,{25,65}),"chatRecipientsAvailable",1,[](Model& m,bool){m.root=1;m.s().ownSlot=std::size_t{2};m.s().roster[0].slot=std::size_t{2};m.s().roster[2].slot=std::size_t{0};m.s().slotsMask=15;m.s().recipientsAvailable=true;},false});
 }
 {
  State s=initialState();s.tab=RuntimeBuildKind::Defense;s.pages[1]=2;s.filtered=20;
  Action a=owner(Owner::Scroll);a.delta=121;
  list.push_back({"wheel-fresh-count-root",s,a,"buttonsPerPage",1,[](Model& m,bool){m.root=1;m.s().filtered=9;m.s().pages[1]=1;m.s().wheel[1]=37;},false});
  list.push_back({"wheel-late-remainder-read",s,a,"filteredCatalogCount",2,[](Model& m,bool){m.s().wheel[1]=-119;},false});
  a.owner=Owner::Wheel;
  list.push_back({"wheel-owned-delta",s,a,"sidebarRect",1,[](Model& m,bool){m.s().delta=-960;},false});
  list.push_back({"wheel-nested-close",s,a,"status",1,[](Model& m,bool old){nestedCall(m,owner(Owner::Close),old);},true});
  list.push_back({"wheel-redraw-reentry",s,a,"redraw",1,[](Model& m,bool old){nestedCall(m,owner(Owner::Open),old);},true});
  list.push_back({"page-query-owned-count",s,owner(Owner::Count),"filteredCatalogCount",1,[](Model& m,bool){m.root=1;m.s().tab=RuntimeBuildKind::Unit;m.s().filtered=0;m.s().perPage=1;},false});
 }
 for(const auto& d:list){
  const Setup setup=[&](Model& m,bool old){m.hookName=d.port;m.hookAt=d.occurrence;m.hook=[&,old](Model& h){d.callback(h,old);};};
  const auto result=compare(d.state,d.action,"dynamic-"+d.name,setup);
  need(result.hookFired,"dynamic callback reached: "+d.name);++dynamicCases;if(d.nested)++nestedCases;
 }
 std::vector<std::pair<State,Action>> paths;
 State s=initialState();s.finished=true;s.capture=true;s.pressed=true;
 paths.emplace_back(s,ingress(Event::LeftButtonUp));
 s=initialState();s.diplomacy=true;s.capture=true;s.diplomacyPressed=1743;
 paths.emplace_back(s,ingress(Event::LeftButtonDown,{25,25}));
 paths.emplace_back(s,ingress(Event::LeftButtonUp,{25,25}));
 paths.emplace_back(s,ingress(Event::CancelMode));
 paths.emplace_back(s,owner(Owner::Open));paths.emplace_back(s,owner(Owner::Close));
 paths.emplace_back(s,mouse(Event::LeftButtonUp,{105,145}));
 paths.back().first.diplomacyPressed=1840;
 paths.emplace_back(s,mouse(Event::LeftButtonUp,{25,65}));
 paths.back().first.diplomacyPressed=1750;
 paths.emplace_back(s,owner(Owner::ToggleChat));
 s=initialState();s.chat=true;paths.emplace_back(s,ingress(Event::RightButtonUp));
 s=initialState();s.tab=RuntimeBuildKind::Defense;s.pages[1]=2;s.filtered=20;paths.emplace_back(s,owner(Owner::Wheel));
 for(std::size_t path=0;path<paths.size();++path){
  const auto baseline=run(paths[path].first,paths[path].second,true);
  need(baseline.error.empty(),"exception fixture legal baseline "+std::to_string(path));
  for(std::size_t ordinal=1;ordinal<=baseline.trace.size();++ordinal)for(bool after:{false,true}){
   const Setup setup=[ordinal,after](Model& m,bool){m.throwAt=static_cast<int>(ordinal);m.throwAfter=after;};
   const auto result=compare(paths[path].first,paths[path].second,"exception-"+std::to_string(path)+"-"+std::to_string(ordinal)+"-"+value(after),setup);
   need(!result.error.empty(),"injected primitive exception reached");++exceptionCases;
  }
 }
 // Nested callbacks also unwind through the exact same primitive trace and state prefix.
 for(const auto& d:list){
  if(!d.nested)continue;
  const Setup setup=[&](Model& m,bool old){m.hookName=d.port;m.hookAt=d.occurrence;m.hook=[&,old](Model& h){
   const int before=h.ordinal;h.throwAt=before+1;h.throwAfter=false;d.callback(h,old);
  };};
  const auto result=compare(d.state,d.action,"nested-exception-"+d.name,setup);
  need(!result.error.empty(),"nested exception reached");++exceptionCases;
 }
}
} // namespace Contract
int main(){
 try{
  Contract::ordinaryCases();Contract::dynamicAndExceptions();
  std::cout<<"pointer ingress controller contract passed; checks="<<Contract::checks
   <<"; differentialCases="<<Contract::cases<<"; traceEvents="<<Contract::traceEvents
   <<"; dynamicCases="<<Contract::dynamicCases<<"; nestedCases="<<Contract::nestedCases
   <<"; exceptionCases="<<Contract::exceptionCases
   <<"; independentOldOwners=5; fullState=both-roots-and-inputs; oldPrivatePrimitives=rows-alliance-geometry"
   <<"; numericDomain=bounded-representable; windowsHeader=absent; failures=0\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"pointer ingress controller contract failed: "<<e.what()<<'\n';return 1;}
}

