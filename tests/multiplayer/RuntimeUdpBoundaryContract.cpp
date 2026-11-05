// Native boundary oracle: same production UDP provider, owned loopback only.
#include "RuntimeLoopbackTestSocket.h"
#include "network/session/RuntimeLanTransport.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <utility>
#include <vector>
#ifdef __linux__
#include <chrono>
#include <csignal>
#include <pthread.h>
#include <thread>
#endif
namespace {
using namespace concord::test::loopback;

int checks=0,failures=0;
#ifdef _WIN32
constexpr auto socketErrorLabel = " WSA=";
#else
constexpr auto socketErrorLabel = " errno=";
#endif
bool expect(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<text<<socketErrorLabel<<lastSocketError()<<std::endl;}return value;}
struct RawDatagram {
    NativeSocket value=invalidSocket;
    RawDatagram()=default;
    RawDatagram(const RawDatagram&)=delete;RawDatagram& operator=(const RawDatagram&)=delete;
    ~RawDatagram(){close();}
    void close(){if(value!=invalidSocket)closeSocket(value);value=invalidSocket;}
    bool bind(std::uint16_t port=0,bool wildcard=false){
        close();value=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(value==invalidSocket)return false;
#ifdef _WIN32
        const BOOL exclusive=TRUE;
        if(setsockopt(value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))!=0)return false;
#endif
        if(!setReceiveTimeout(value))return false;
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(wildcard?INADDR_ANY:INADDR_LOOPBACK);address.sin_port=htons(port);
        return ::bind(value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0;
    }
    std::uint16_t port()const{sockaddr_in address{};NativeSocketLength size=sizeof(address);return getsockname(value,reinterpret_cast<sockaddr*>(&address),&size)==0?ntohs(address.sin_port):0;}
    bool send(std::span<const std::uint8_t> bytes,const sockaddr_in& destination)const{
        return sendto(value,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),0,reinterpret_cast<const sockaddr*>(&destination),sizeof(destination))==static_cast<int>(bytes.size());
    }
    bool receive(std::vector<std::uint8_t>& bytes,sockaddr_in& source)const{
        std::array<std::uint8_t,2048> buffer{};NativeSocketLength size=sizeof(source);
        const auto n=recvfrom(value,reinterpret_cast<char*>(buffer.data()),static_cast<int>(buffer.size()),0,reinterpret_cast<sockaddr*>(&source),&size);
        if(n<0)return false;bytes.assign(buffer.begin(),buffer.begin()+n);return true;
    }
    bool readable(int milliseconds)const{
        fd_set reads;FD_ZERO(&reads);FD_SET(value,&reads);timeval timeout{};timeout.tv_sec=milliseconds/1000;timeout.tv_usec=(milliseconds%1000)*1000;
        return select(selectWidth(value),&reads,nullptr,nullptr,&timeout)>0;
    }
};
sockaddr_in endpoint(std::uint16_t port){sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);return address;}
RuntimeLanRoomAdvertisement advertisement(const char* id){
    RuntimeLanRoomAdvertisement value;value.roomId=id;value.roomName="Owned UDP boundary";value.hostName="OwnedHost";value.contentFingerprint="unchanged-wire-fixture";
    value.sessionPort=17816;value.playerCount=1;value.maximumPlayers=2;value.hostAddress="203.0.113.9";return value;
}
constexpr std::array<std::uint8_t,8> originalProbe{0x43,0x4c,0x41,0x4e,0,1,1,0};
constexpr std::array<std::uint8_t,8> originalAdvertisementPrefix{0x43,0x4c,0x41,0x4e,0,1,2,0};
void closedAndInvalid(){
    std::string error;RuntimeLanDiscoveryHost host;RuntimeLanDiscoveryBrowser browser;
    expect(!host.isOpen()&&host.port()==0,"new host is closed");
    expect(!browser.isOpen()&&browser.rooms().empty(),"new browser is closed");
    expect(host.poll(0,error)==-1&&!error.empty(),"closed host poll reports original error");
    expect(browser.poll(0,error)==-1&&!error.empty(),"closed browser poll reports original error");
    expect(!browser.sendProbe("127.0.0.1",error)&&!error.empty(),"closed browser cannot send");
    expect(!browser.open(0,error)&&!error.empty(),"zero discovery target rejected");
    auto invalid=advertisement("invalid");invalid.playerCount=3;invalid.maximumPlayers=2;
    expect(!host.open(0,invalid,error)&&!host.isOpen(),"original player limit validates before live host");
    expect(!host.updateAdvertisement(advertisement("closed"),error),"closed host update rejected");
    host.close();host.close();browser.close();browser.close();
    expect(!host.isOpen()&&!browser.isOpen(),"repeated inactive close");
}
bool rawDatagramErrors(){
    std::string error;RuntimeLanDiscoveryHost host;const auto advertised=advertisement("wire-room");RawDatagram raw;
    if(!expect(raw.bind(),"owned raw loopback endpoint")||!expect(host.open(0,advertised,error),"production discovery host open"))return false;
    const auto target=endpoint(host.port());
    expect(host.poll(0,error)==0&&error.empty(),"empty host nonblocking poll");
    std::vector<std::vector<std::uint8_t>> invalidProbes;
    invalidProbes.emplace_back(originalProbe.begin(),originalProbe.end());invalidProbes.back()[0]^=0xffU;
    invalidProbes.emplace_back(originalProbe.begin(),originalProbe.end());invalidProbes.back()[5]=2;
    invalidProbes.emplace_back(originalProbe.begin(),originalProbe.end());invalidProbes.back()[6]=2;
    invalidProbes.emplace_back(originalProbe.begin(),originalProbe.end()-1);
    invalidProbes.emplace_back(originalProbe.begin(),originalProbe.end());invalidProbes.back().push_back(0);
    invalidProbes.emplace_back(1501,0);
    for(const auto& bytes:invalidProbes)expect(raw.send(bytes,target),"raw invalid/oversized probe sent only loopback");
    expect(host.poll(100,error)==0&&error.empty()&&host.isOpen(),"bad and oversized probes ignored without closing healthy host");
    expect(!raw.readable(30),"no advertisement emitted for invalid probes");
    expect(raw.send(originalProbe,target),"original fixed probe wire sent");
    expect(host.poll(1000,error)==1&&error.empty(),"healthy probe still answered after bad datagrams");
    std::vector<std::uint8_t> captured;sockaddr_in source{};
    if(!expect(raw.receive(captured,source),"capture actual production advertisement"))return false;
    expect(captured.size()>=8&&std::equal(originalAdvertisementPrefix.begin(),originalAdvertisementPrefix.end(),captured.begin()),"actual production advertisement header exact original wire");
    expect(ntohs(source.sin_port)==host.port()&&source.sin_addr.s_addr==htonl(INADDR_LOOPBACK),"actual host reply source endpoint");
    RuntimeLanDiscoveryBrowser browser;RawDatagram responder;
    if(!expect(responder.bind(),"owned raw responder endpoint")||!expect(browser.open(responder.port(),error),"production browser to owned raw responder"))return false;
    expect(browser.poll(0,error)==0&&error.empty(),"empty browser nonblocking poll");
    expect(!browser.sendProbe("",error)&&!error.empty()&&browser.isOpen(),"empty destination rejected without browser teardown");
    expect(browser.sendProbe("127.0.0.1",error),"browser original probe sends to own responder");
    std::vector<std::uint8_t> probe;sockaddr_in browserAddress{};
    if(!expect(responder.receive(probe,browserAddress),"capture original browser source endpoint"))return false;
    expect(probe.size()==originalProbe.size()&&std::equal(probe.begin(),probe.end(),originalProbe.begin()),"browser entire original probe wire exact");
    std::vector<std::vector<std::uint8_t>> badReplies;
    badReplies.emplace_back(captured.begin(),captured.begin()+7);
    badReplies.push_back(captured);badReplies.back()[0]^=0xffU;
    badReplies.push_back(captured);badReplies.back()[5]=2;
    badReplies.push_back(captured);badReplies.back()[6]=1;
    badReplies.push_back(captured);badReplies.back().pop_back();
    badReplies.push_back(captured);badReplies.back().push_back(0);
    badReplies.emplace_back(1501,0);
    for(const auto& bytes:badReplies)expect(responder.send(bytes,browserAddress),"raw bad/oversized advertisement sent");
    expect(browser.poll(100,error)==0&&error.empty()&&browser.isOpen()&&browser.rooms().empty(),"malformed advertisements never publish a room or destroy browser");
    expect(responder.send(captured,browserAddress),"unchanged actual production advertisement replayed");
    expect(browser.poll(1000,error)==1&&error.empty(),"healthy native advertisement accepted after malformed datagrams");
    const auto rooms=browser.rooms();expect(rooms.size()==1,"exactly one real decoded room");
    if(rooms.size()==1){const auto& room=rooms.front();
        expect(room.roomId==advertised.roomId&&room.roomName==advertised.roomName&&room.hostName==advertised.hostName,"actual production text fields unchanged");
        expect(room.sessionPort==advertised.sessionPort&&room.playerCount==1&&room.maximumPlayers==2&&room.contentFingerprint==advertised.contentFingerprint,"actual production scalar and fingerprint fields unchanged");
        expect(room.hostAddress=="127.0.0.1"&&room.hostAddress!=advertised.hostAddress,"peer address comes from native source rather than claimed field");
    }
    browser.close();host.close();expect(!browser.isOpen()&&!host.isOpen(),"healthy endpoints close after invalid input");return true;
}
void hostMoveClose(){
    std::string error;RuntimeLanDiscoveryHost destination,source;
    if(!expect(destination.open(0,advertisement("old-owner"),error),"active host destination")||!expect(source.open(0,advertisement("new-owner"),error),"active host source"))return;
    const auto oldPort=destination.port(),newPort=source.port();
    {RawDatagram busy;expect(!busy.bind(oldPort,true),"qualified exclusive bind cannot take an active owned host port");}
    destination=std::move(source);
    expect(destination.isOpen()&&destination.port()==newPort&&!source.isOpen()&&source.port()==0,"active host move transfers only source owner");
    {RawDatagram released;expect(released.bind(oldPort,true),"move assignment releases old native host port");}
    auto* same=&destination;destination=std::move(*same);
    expect(destination.isOpen()&&destination.port()==newPort,"host self move preserves active session");
    expect(source.open(0,advertisement("reused-source"),error),"moved from host can reopen");source.close();
    destination.close();destination.close();expect(!destination.isOpen()&&destination.port()==0,"active host repeated close resets public state");
    {RawDatagram released;expect(released.bind(newPort,true),"explicit host close releases native port");}
    std::uint16_t destroyedPort=0;
    {RuntimeLanDiscoveryHost owned;expect(owned.open(0,advertisement("scope-owner"),error),"scope owned host open");destroyedPort=owned.port();}
    {RawDatagram released;expect(destroyedPort!=0&&released.bind(destroyedPort,true),"host destructor releases native port");}
}
void browserMoveClose(){
    std::string error;RawDatagram responder;if(!expect(responder.bind(),"browser life raw responder"))return;
    RuntimeLanDiscoveryBrowser destination,source;
    if(!expect(destination.open(responder.port(),error),"active browser destination")||!expect(source.open(responder.port(),error),"active browser source"))return;
    std::vector<std::uint8_t> probe;sockaddr_in oldAddress{},newAddress{},movedAddress{};
    expect(destination.sendProbe("127.0.0.1",error),"old browser probe");if(!expect(responder.receive(probe,oldAddress),"old native browser endpoint captured"))return;
    expect(source.sendProbe("127.0.0.1",error),"source browser probe");if(!expect(responder.receive(probe,newAddress),"source native browser endpoint captured"))return;
    const auto oldPort=ntohs(oldAddress.sin_port),newPort=ntohs(newAddress.sin_port);expect(oldPort!=newPort,"two active browser endpoints distinct");
    {RawDatagram busy;expect(!busy.bind(oldPort,true),"qualified exclusive bind cannot take active browser port");}
    destination=std::move(source);expect(destination.isOpen()&&!source.isOpen(),"browser move transfers session");
    {RawDatagram released;expect(released.bind(oldPort,true),"browser move releases old native endpoint");}
    expect(destination.sendProbe("127.0.0.1",error),"moved browser can still send");
    expect(responder.receive(probe,movedAddress)&&ntohs(movedAddress.sin_port)==newPort,"moved browser preserves actual source socket");
    auto* same=&destination;destination=std::move(*same);expect(destination.isOpen(),"browser self move preserves session");
    expect(source.open(responder.port(),error),"moved from browser reopens");source.close();
    destination.close();destination.close();expect(!destination.isOpen()&&destination.rooms().empty(),"browser repeated close clears public state");
    {RawDatagram released;expect(released.bind(newPort,true),"browser close releases real native port");}
    std::uint16_t destroyedPort=0;
    {RuntimeLanDiscoveryBrowser owned;expect(owned.open(responder.port(),error)&&owned.sendProbe("127.0.0.1",error),"scope browser open and send");sockaddr_in address{};expect(responder.receive(probe,address),"scope browser endpoint captured");destroyedPort=ntohs(address.sin_port);}
    {RawDatagram released;expect(destroyedPort!=0&&released.bind(destroyedPort,true),"browser destructor releases native port");}
}
}
#ifdef __linux__
namespace {
volatile std::sig_atomic_t interrupted = 0;

void recordInterrupt(int) {
    interrupted = 1;
}

void posixTimeoutInterruptAndAge() {
    using namespace std::chrono_literals;
    RuntimeLanDiscoveryHost host;
    RuntimeLanDiscoveryBrowser browser;
    std::string error;
    if (!expect(host.open(0, advertisement("posix-aging"), error), "open own POSIX discovery host")) return;
    expect(host.poll(20, error) == 0 && error.empty() && host.isOpen(),
           "bounded POSIX poll timeout preserves its socket");

    struct sigaction action{}, previous{};
    action.sa_handler = recordInterrupt;
    sigemptyset(&action.sa_mask);
    if (!expect(sigaction(SIGUSR1, &action, &previous) == 0, "install owned test signal handler")) return;
    const auto pollingThread = pthread_self();
    std::jthread sender([pollingThread](std::stop_token stop) {
        for (int attempt = 0; attempt < 10 && !stop.stop_requested(); ++attempt) {
            std::this_thread::sleep_for(20ms);
            if (!stop.stop_requested()) pthread_kill(pollingThread, SIGUSR1);
        }
    });
    const int result = host.poll(1000, error);
    sender.request_stop();
    sender.join();
    expect(sigaction(SIGUSR1, &previous, nullptr) == 0, "restore prior signal handler");
    expect(interrupted && result == -1 && host.isOpen() && error.starts_with("select(discovery host)"),
           "real interrupted POSIX poll reports error and retains its endpoint");

    if (!expect(browser.open(host.port(), error), "open owned POSIX discovery browser")) return;
    expect(browser.sendProbe("127.0.0.1", error) && host.poll(1000, error) == 1
               && browser.poll(1000, error) == 1 && browser.rooms(30000).size() == 1,
           "discovery succeeds on the same endpoint after interruption");
    std::this_thread::sleep_for(20ms);
    expect(browser.rooms(1).empty() && browser.rooms(30000).size() == 1,
           "actual monotonic age filters the same received advertisement");
}
} // namespace
#endif

int main() {
    SocketSystem sockets;
#ifdef _WIN32
    if (!expect(sockets.ready, "owned WinSock2 startup")) return 1;
#else
    if (!expect(sockets.ready, "owned POSIX socket startup")) return 1;
#endif
    closedAndInvalid();
    rawDatagramErrors();
    hostMoveClose();
    browserMoveClose();
#ifdef __linux__
    posixTimeoutInterruptAndAge();
#endif
    std::cout << "UDP native boundary: checks=" << checks << " failures=" << failures
              << " rawInvalidAndOversize=yes actualWire=yes sourceAddress=native nonblocking=yes"
              << " moveClose=nativePort destructorRelease=yes trafficLoopbackOnly=yes" << std::endl;
    return failures ? 1 : 0;
}
