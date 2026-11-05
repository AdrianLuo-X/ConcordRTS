#include "simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "data/art/ArtDatabase.h"
#include "world/model/GameWorld.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using runtime_save_text::parseRuntimeInt;
using runtime_save_text::splitRuntimeTokenList;
using runtime_save_text::toLowerAsciiLocal;
int checks=0,observations=0,scenarios=0;std::ostringstream retained;
void need(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
template<class F>std::string flh(const F& f){return std::to_string(f.forwardLeptons)+","+std::to_string(f.lateralLeptons)+","+std::to_string(f.heightLeptons)+","+std::to_string(f.authored);}
template<class T>std::string flhResult(const T& value){return value?"some:"+flh(*value):"none";}
struct Probe final:RuntimeOpenTransportMuzzleQueries {
    std::vector<WorldObjectInstance> values;std::vector<ArtType> arts;std::vector<bool> missing;
    mutable std::vector<std::string> trace;mutable int calls=0;int throwAt=0;
    void record(std::string value)const {trace.push_back(std::move(value));if(++calls==throwAt)throw std::runtime_error("query-fault-"+std::to_string(calls));}
    const std::vector<WorldObjectInstance>& world()const {record("world");return values;}
    std::span<const WorldObjectInstance> objects()const override{return world();}
    const ArtType* artTypeForObject(const WorldObjectInstance& object)const override {
        const auto found=std::find_if(values.begin(),values.end(),[&](const auto& item){return &item==&object;});
        if(found==values.end())throw std::runtime_error("art query did not borrow actual world element");
        const auto index=static_cast<std::size_t>(found-values.begin());record("art:"+std::to_string(index)+":"+std::to_string(object.instanceId));
        return missing[index]?nullptr:&arts[index];
    }
};
namespace original {
struct RuntimeWeaponFireFlh {
    int forwardLeptons = 0;
    int lateralLeptons = 0;
    int heightLeptons = 0;
    bool authored = false;
};
struct RuntimeOpenTransportMuzzle {
    const WorldObjectInstance* host = nullptr;
    RuntimeWeaponFireFlh flh;
};
struct WeaponProfile {bool openTransport=false;};
struct WorldView {const Probe& probe;const std::vector<WorldObjectInstance>& getObjects()const{return probe.world();}};
struct OriginalTransportQueryBinding {WorldView world;const Probe& probe;explicit OriginalTransportQueryBinding(const Probe& input):world{input},probe(input){}};
const ArtType* runtimeArtTypeForObject(const OriginalTransportQueryBinding& state,const WorldObjectInstance& object){return state.probe.artTypeForObject(object);}
std::optional<RuntimeWeaponFireFlh> parseRuntimeWeaponFireFlh(
    const std::string& rawFlh
) {
    const std::vector<std::string> components =
        splitRuntimeTokenList(rawFlh);
    if (components.size() < 3U) {
        return std::nullopt;
    }

    RuntimeWeaponFireFlh flh;
    if (!parseRuntimeInt(components[0], flh.forwardLeptons)
        || !parseRuntimeInt(components[1], flh.lateralLeptons)
        || !parseRuntimeInt(components[2], flh.heightLeptons)) {
        return std::nullopt;
    }
    flh.authored = true;
    return flh;
}
std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const OriginalTransportQueryBinding& state, const WorldObjectInstance& passenger,
    const WeaponProfile& profile) {
    if (!profile.openTransport || toLowerAsciiLocal(passenger.mission) != "insidetransport")
        return std::nullopt;
    for (const auto& host : state.world.getObjects()) {
        if (!host.alive || host.health <= 0) continue;
        const auto seat = std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(), passenger.instanceId);
        if (seat == host.transportPassengerIds.end()) continue;
        const auto* art = runtimeArtTypeForObject(state, host);
        if (!art) return std::nullopt;
        const auto index = static_cast<std::size_t>(seat - host.transportPassengerIds.begin());
        if (index >= art->alternateFireFLHs.size()) return std::nullopt;
        const auto flh = parseRuntimeWeaponFireFlh(art->alternateFireFLHs[index]);
        if (flh) return RuntimeOpenTransportMuzzle{&host, *flh};
        return std::nullopt;
    }
    return std::nullopt;
}
}

static_assert(std::tuple_size_v<decltype(ArtType{}.alternateFireFLHs)> == 5);
static_assert(sizeof(RuntimeWeaponFireFlh)==sizeof(original::RuntimeWeaponFireFlh));
static_assert(alignof(RuntimeWeaponFireFlh)==alignof(original::RuntimeWeaponFireFlh));
static_assert(sizeof(RuntimeOpenTransportMuzzle)==sizeof(original::RuntimeOpenTransportMuzzle));
static_assert(alignof(RuntimeOpenTransportMuzzle)==alignof(original::RuntimeOpenTransportMuzzle));
std::string complete(const Probe& q){std::ostringstream o;for(std::size_t i=0;i<q.values.size();++i){const auto& x=q.values[i];o<<i<<':'<<x.instanceId<<'|'<<x.alive<<'|'<<x.health<<'|'<<x.mission<<'|'<<x.owner<<'|'<<x.type<<'|'<<x.x<<'|'<<x.y<<'|'<<x.level<<'|';for(int id:x.transportPassengerIds)o<<id<<',';o<<"art="<<q.missing[i]<<'|';for(const auto& value:q.arts[i].alternateFireFLHs)o<<value.size()<<':'<<value;}return o.str();}
template<class R>std::string result(const Probe& q,const R& r){
    if(!r)return "none";
    const auto found=std::find_if(q.values.begin(),q.values.end(),[&](const auto& item){return &item==r->host;});
    need(found!=q.values.end(),"result must borrow live provider element");
    return "host="+std::to_string(found-q.values.begin())+"|"+flh(r->flh);
}
std::string trace(const Probe& q){std::string out;for(const auto& line:q.trace)out+=std::to_string(line.size())+":"+line;return out;}
Probe fixture(unsigned seed){
    Probe q;q.values.resize(6);q.arts.resize(6);q.missing.resize(6);
    for(std::size_t i=0;i<q.values.size();++i){auto& host=q.values[i];host.instanceId=static_cast<int>(i)+90;host.health=static_cast<int>((seed+i)%4u)-1;host.alive=(seed+3u*i)%5u!=0;host.kind=static_cast<WorldObjectKind>(i%3u);host.owner="owner-"+std::to_string(i);host.type="type-"+std::to_string(i);host.mission="Guard";host.x=static_cast<int>(i)+12;host.y=static_cast<int>(i)+20;
        for(unsigned k=0;k<(seed+i)%5u;++k)host.transportPassengerIds.push_back(k%2u?77:static_cast<int>(k));
        q.missing[i]=(seed+i)%7u==0;for(unsigned k=0;k<(seed+2u*i)%6u;++k)q.arts[i].alternateFireFLHs[k]=(seed+i+k)%6u==0?"bad,2,3":std::to_string(seed%100u)+", "+std::to_string(static_cast<int>(i)-2)+"; "+std::to_string(k)+",ignored";
    }return q;
}
template<class F>std::string observe(Probe& q,F&& call){const auto before=complete(q);std::string value;try{value=result(q,call());}catch(const std::exception& error){value="exception:"+std::string(error.what());}need(complete(q)==before,"original input world/art view immutable");return value+"|"+trace(q);}
void equal(const std::string& label,const std::string& a,const std::string& b){need(a==b,label);++observations;retained<<label<<'|'<<a.size()<<':'<<a<<"\n";}
int main(int argc,char** argv)try {
    need(flh(RuntimeWeaponFireFlh{})==flh(original::RuntimeWeaponFireFlh{}),"all four original defaults");
    const std::vector<std::string> tokens{"0","1","-1","+17","2147483647","-2147483648","2147483648","-2147483649","0x10","1x",""," ","\t-8\n","1.5","+","--1"};
    for(const auto& a:tokens)for(const auto& b:tokens)for(const auto& c:tokens){
        const auto value=a+","+b+";"+c;
        equal("parse-"+std::to_string(observations),flhResult(parseRuntimeWeaponFireFlh(value)),flhResult(original::parseRuntimeWeaponFireFlh(value)));
    }
    for(const auto& value:std::vector<std::string>{"1,,2;3,ignored"," 1 ; 2 ; 3 ; 4 ","1,2","1","",std::string("1,2,3\0junk",10)})equal("parse-special-"+std::to_string(observations),flhResult(parseRuntimeWeaponFireFlh(value)),flhResult(original::parseRuntimeWeaponFireFlh(value)));
    for(unsigned byte=0;byte<256;++byte){std::string value="1,";value.push_back(static_cast<char>(byte));value+="2,3";equal("parse-byte-"+std::to_string(byte),flhResult(parseRuntimeWeaponFireFlh(value)),flhResult(original::parseRuntimeWeaponFireFlh(value)));}
    for(unsigned seed=0;seed<512u;++seed)for(bool open:{false,true})for(int failure=0;failure<4;++failure){
        auto q=fixture(seed),r=fixture(seed);q.throwAt=r.throwAt=failure;
        WorldObjectInstance passenger;passenger.instanceId=77;passenger.alive=false;passenger.mission=std::array<std::string,4>{"InsideTransport","INSIDETRANSPORT","insideTransport","Guard"}[seed%4u];
        original::OriginalTransportQueryBinding state(r);original::WeaponProfile profile{open};
        equal("query-"+std::to_string(seed)+"-"+std::to_string(open)+"-"+std::to_string(failure),observe(q,[&]{return runtimeOpenTransportMuzzle(q,passenger,open);}),observe(r,[&]{return original::runtimeOpenTransportMuzzle(state,passenger,profile);}));++scenarios;
    }
    for(unsigned variant=0;variant<11u;++variant){
        auto q=fixture(2u),r=fixture(2u);
        for(auto* f:{&q,&r}){for(auto& host:f->values){host.alive=true;host.health=20;host.transportPassengerIds={77,77};}for(std::size_t i=0;i<f->arts.size();++i){f->missing[i]=false;f->arts[i].alternateFireFLHs={"10,20,30","40,50,60"};}
            if(variant==1)f->missing[0]=true;if(variant==2)f->values[0].transportPassengerIds={1,2,3,4,5,77};if(variant==3)f->arts[0].alternateFireFLHs[0]="bad";if(variant==4)f->values[0].alive=false;if(variant==5)f->values[0].health=0;if(variant==6)f->values[0].transportPassengerIds={1,77,77};if(variant==7)f->values[0].instanceId=77;if(variant==8)f->values[0].transportPassengerIds={1,2};if(variant==9)f->values[0].transportPassengerIds={1,2,3,4,77};if(variant==10){f->values[0].transportPassengerIds={1,2,3,4,77,77};f->arts[0].alternateFireFLHs[4]="-4,+5,6";}}
        WorldObjectInstance passenger;passenger.instanceId=77;passenger.mission="InsideTransport";original::OriginalTransportQueryBinding old(r);original::WeaponProfile profile{true};
        equal("priority-"+std::to_string(variant),observe(q,[&]{return runtimeOpenTransportMuzzle(q,passenger,true);}),observe(r,[&]{return original::runtimeOpenTransportMuzzle(old,passenger,profile);}));
    }
    {
        auto q=fixture(2u),r=fixture(2u);
        for(auto* f:{&q,&r}) {f->values[0].instanceId=77;f->values[0].alive=true;f->values[0].health=20;f->values[0].mission="InsideTransport";f->values[0].transportPassengerIds={77};f->missing[0]=false;f->arts[0].alternateFireFLHs={"1,2,3"};}
        original::OriginalTransportQueryBinding state(r);original::WeaponProfile profile{true};
        equal("passenger-aliases-world-host",observe(q,[&]{return runtimeOpenTransportMuzzle(q,q.values[0],true);}),observe(r,[&]{return original::runtimeOpenTransportMuzzle(state,r.values[0],profile);}));
    }
    Probe q;q.values.resize(2);q.arts.resize(2);q.missing.resize(2);for(auto& host:q.values){host.alive=true;host.health=1;host.transportPassengerIds={77};}
    q.arts[0].alternateFireFLHs={"1,,2;3,ignored"};q.arts[1].alternateFireFLHs={"9,8,7"};WorldObjectInstance passenger;passenger.instanceId=77;passenger.mission="InsideTransport";
    const auto first=runtimeOpenTransportMuzzle(q,passenger,true);need(first&&first->host==&q.values[0]&&first->flh.forwardLeptons==1&&first->flh.lateralLeptons==2&&first->flh.heightLeptons==3&&first->flh.authored,"original first match/empty component skip/extra token accepted");
    q.missing[0]=true;q.trace.clear();q.calls=0;need(!runtimeOpenTransportMuzzle(q,passenger,true)&&q.trace==std::vector<std::string>{"world","art:0:-1"},"first matched missing art stops before later legal host");
    q.trace.clear();q.calls=0;need(!runtimeOpenTransportMuzzle(q,passenger,false)&&q.trace.empty(),"false permission short circuit before world view");
    passenger.mission="Guard";need(!runtimeOpenTransportMuzzle(q,passenger,true)&&q.trace.empty(),"mission gate before world view");
    if(argc>1){std::filesystem::create_directories(argv[1]);std::ofstream file(std::filesystem::path(argv[1])/"complete-observations.txt",std::ios::binary);file<<retained.str();need(static_cast<bool>(file),"retained complete observations");}
    std::cout<<"Open transport source: "<<checks<<" checks, "<<observations<<" complete observations, "<<scenarios<<" query/exception scenarios, four-field FLH and live pointer/priority preserved, skipped=0\n";return 0;
}catch(const std::exception& error){std::cerr<<"Open transport source failed: "<<error.what()<<'\n';return 1;}
