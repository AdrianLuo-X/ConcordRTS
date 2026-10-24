// Native Windows fixture: only owned loopback endpoints and sockets.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "network/session/RuntimeLanTransport.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
namespace {
int checks=0,failures=0;
bool expect(bool value,const char* text){++checks;if(!value){++failures;std::cerr<<text<<'\n';}return value;}
struct Sockets {
    bool ready=false;
    Sockets(){WSADATA data{};ready=WSAStartup(MAKEWORD(2,2),&data)==0;}
    ~Sockets(){if(ready)WSACleanup();}
};
struct Socket {
    SOCKET value=INVALID_SOCKET;
    Socket()=default;
    ~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}
    Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete;
    void close(){if(value!=INVALID_SOCKET)closesocket(value);value=INVALID_SOCKET;}
    bool connect(std::uint16_t port){
        value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(value==INVALID_SOCKET)return false;
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        const DWORD timeout=1000;setsockopt(value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
        return ::connect(value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0;
    }
};
bool sendBytes(SOCKET socket,std::span<const std::uint8_t> bytes){
    std::size_t sent=0;
    while(sent<bytes.size()){
        const int n=send(socket,reinterpret_cast<const char*>(bytes.data()+sent),static_cast<int>(bytes.size()-sent),0);
        if(n<=0)return false;sent+=static_cast<std::size_t>(n);
    }
    return true;
}
bool receiveBytes(SOCKET socket,std::vector<std::uint8_t>& bytes,std::size_t count){
    bytes.clear();while(bytes.size()<count){
        std::array<std::uint8_t,32> buffer{};const auto amount=std::min(count-bytes.size(),buffer.size());
        const int n=recv(socket,reinterpret_cast<char*>(buffer.data()),static_cast<int>(amount),0);
        if(n<=0)return false;bytes.insert(bytes.end(),buffer.begin(),buffer.begin()+n);
    }
    return true;
}
std::uint64_t peer(const std::vector<RuntimeTcpEvent>& events){
    for(const auto& e:events)if(e.kind==RuntimeTcpEventKind::Connected)return e.peerId;return 0;
}
bool has(const std::vector<RuntimeTcpEvent>& events,RuntimeTcpEventKind kind,std::uint64_t id=0){
    return std::any_of(events.begin(),events.end(),[&](const auto& e){return e.kind==kind&&(id==0||e.peerId==id);});
}
bool packet(const std::vector<RuntimeTcpEvent>& events,std::uint16_t type,std::span<const std::uint8_t> payload){
    return std::any_of(events.begin(),events.end(),[&](const auto& e){return e.kind==RuntimeTcpEventKind::Packet&&e.packet.type==type&&e.packet.payload.size()==payload.size()&&std::equal(e.packet.payload.begin(),e.packet.payload.end(),payload.begin());});
}
std::vector<RuntimeTcpEvent> collect(RuntimeTcpServer& server,RuntimeTcpEventKind kind,std::uint64_t id=0){
    std::vector<RuntimeTcpEvent> all;std::string error;
    for(int i=0;i<10;++i){auto now=server.poll(100,error);all.insert(all.end(),now.begin(),now.end());if(has(all,kind,id)||!error.empty())break;}return all;
}
bool fragmentedAndBadPeer(){
    std::string error;RuntimeTcpServer server;
    if(!expect(server.open(0,3,RuntimeTcpBindScope::LoopbackIpv4,error),"open owned loopback server"))return false;
    RuntimeTcpClient healthy;if(!expect(healthy.connect("127.0.0.1",server.port(),1000,error),"healthy peer connect"))return false;
    const auto healthyId=peer(server.poll(1000,error));if(!expect(healthyId!=0,"healthy peer accepted"))return false;
    Socket raw;if(!expect(raw.connect(server.port()),"raw fragmented peer connect"))return false;
    const auto rawId=peer(server.poll(1000,error));if(!expect(rawId!=0&&rawId!=healthyId,"raw peer independent identity"))return false;
    RuntimeNetworkPacket value;value.type=42;value.payload={0,1,2,3,0xff};std::vector<std::uint8_t> frame;
    const std::vector<std::uint8_t> golden={0x43,0x52,0x54,0x53,0,1,0,42,0,0,0,5,0,1,2,3,0xff};
    expect(encodeRuntimeNetworkPacket(value,frame,error)&&frame==golden,"complete original wire golden");
    if(!expect(sendBytes(raw.value,std::span<const std::uint8_t>(frame).first(5)),"send partial header"))return false;
    auto events=server.poll(1000,error);
    expect(error.empty()&&!has(events,RuntimeTcpEventKind::Packet)&&server.clientCount()==2,"partial header publishes no packet and keeps both peers");
    if(!expect(sendBytes(raw.value,std::span<const std::uint8_t>(frame).subspan(5,9)),"send rest header plus partial body"))return false;
    events=server.poll(1000,error);
    expect(error.empty()&&!has(events,RuntimeTcpEventKind::Packet),"partial body publishes no packet");
    if(!expect(sendBytes(raw.value,std::span<const std::uint8_t>(frame).subspan(14)),"complete fragmented payload"))return false;
    events=collect(server,RuntimeTcpEventKind::Packet,rawId);
    expect(packet(events,42,value.payload)&&!has(events,RuntimeTcpEventKind::Error),"exact fragmented real packet received");
    auto corrupt=frame;corrupt[0]^=0xff;
    if(!expect(sendBytes(raw.value,corrupt),"send corrupt framing to own server"))return false;
    events=collect(server,RuntimeTcpEventKind::Disconnected,rawId);
    expect(has(events,RuntimeTcpEventKind::Error,rawId)&&has(events,RuntimeTcpEventKind::Disconnected,rawId)&&server.clientCount()==1,"bad frame errors and disconnects only culprit");
    value.type=43;value.payload={9,8,7};expect(healthy.send(value,error),"healthy sender continues after malformed peer");
    healthy.poll(1000,error);events=collect(server,RuntimeTcpEventKind::Packet,healthyId);
    expect(packet(events,43,value.payload)&&!has(events,RuntimeTcpEventKind::Error),"healthy packet/order survives bad peer");
    raw.close();Socket truncated;if(!expect(truncated.connect(server.port()),"partial EOF peer connect"))return false;
    const auto partialId=peer(server.poll(1000,error));
    expect(partialId!=0,"partial EOF peer accepted");expect(sendBytes(truncated.value,std::span<const std::uint8_t>(frame).first(6)),"partial EOF bytes");
    shutdown(truncated.value,SD_SEND);events=collect(server,RuntimeTcpEventKind::Disconnected,partialId);
    expect(has(events,RuntimeTcpEventKind::Disconnected,partialId)&&!has(events,RuntimeTcpEventKind::Packet,partialId)&&server.clientCount()==1,"partial EOF never publishes candidate and isolates peer");
    expect(server.send(healthyId,value,error),"healthy return packet queues");value.payload.assign(20,0xee);
    server.poll(1000,error);const auto response=healthy.poll(1000,error);
    const std::array<std::uint8_t,3> prior{9,8,7};
    expect(packet(response,43,prior),"server send owns copy after caller mutation");
    healthy.close();server.close();server.close();expect(!server.isOpen()&&server.clientCount()==0&&server.port()==0,"close idempotently releases all endpoints");
    return true;
}
bool exactOutgoing(){
    Socket listener;listener.value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(!expect(listener.value!=INVALID_SOCKET,"raw owned listener socket"))return false;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(!expect(bind(listener.value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==0&&listen(listener.value,1)==0,"raw loopback bind/listen"))return false;
    int size=sizeof(address);getsockname(listener.value,reinterpret_cast<sockaddr*>(&address),&size);
    RuntimeTcpClient client;std::string error;
    if(!expect(client.connect("127.0.0.1",ntohs(address.sin_port),1000,error),"production client to raw listener"))return false;
    Socket accepted;accepted.value=accept(listener.value,nullptr,nullptr);
    if(!expect(accepted.value!=INVALID_SOCKET,"accept own production client"))return false;
    const DWORD timeout=1000;setsockopt(accepted.value,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    RuntimeNetworkPacket original;original.type=42;original.payload={0,1,2,3,0xff};
    expect(client.send(original,error),"queue original caller packet");original.type=99;original.payload.assign(20,0xee);
    client.poll(1000,error);std::vector<std::uint8_t> wire;
    const std::vector<std::uint8_t> golden={0x43,0x52,0x54,0x53,0,1,0,42,0,0,0,5,0,1,2,3,0xff};
    expect(receiveBytes(accepted.value,wire,golden.size())&&wire==golden,"physical outgoing full wire exact and owns caller payload");
    client.close();client.close();expect(!client.isConnected(),"explicit client cancellation idempotent");
    return true;
}
bool lifetimeAndFailure(){
    std::string error;RuntimeTcpServer first,second;
    if(!expect(first.open(0,3,RuntimeTcpBindScope::LoopbackIpv4,error)&&second.open(0,3,RuntimeTcpBindScope::LoopbackIpv4,error),"two owned move-target listeners"))return false;
    RuntimeTcpClient oldClient,newClient;
    if(!expect(oldClient.connect("127.0.0.1",first.port(),1000,error)&&newClient.connect("127.0.0.1",second.port(),1000,error),"two live listener clients"))return false;
    first.poll(1000,error);second.poll(1000,error);const auto transferredPort=second.port();
    first=std::move(second);expect(first.isOpen()&&first.port()==transferredPort&&!second.isOpen()&&second.port()==0,"server move transfers owner and empties source");
    auto events=oldClient.poll(1000,error);expect(has(events,RuntimeTcpEventKind::Disconnected)&&!oldClient.isConnected(),"server move assignment closes previous live listener/peers");
    RuntimeNetworkPacket p;p.type=77;p.payload={7,7,7};newClient.send(p,error);newClient.poll(1000,error);auto incoming=collect(first,RuntimeTcpEventKind::Packet);
    expect(packet(incoming,77,p.payload),"moved listener retains new active connection");
    expect(second.open(0,2,RuntimeTcpBindScope::LoopbackIpv4,error),"moved-from listener reusable");
    RuntimeTcpClient a,b;if(!expect(a.connect("127.0.0.1",second.port(),1000,error),"move clientA connect"))return false;
    const auto peerA=peer(second.poll(1000,error));
    if(!expect(b.connect("127.0.0.1",second.port(),1000,error),"move clientB connect"))return false;
    const auto peerB=peer(second.poll(1000,error));a=std::move(b);
    expect(a.isConnected()&&!b.isConnected()&&b.peerAddress().empty(),"client move transfers and empties source");
    events=collect(second,RuntimeTcpEventKind::Disconnected,peerA);
    expect(has(events,RuntimeTcpEventKind::Disconnected,peerA)&&second.clientCount()==1,"client move assignment closes prior live socket");
    p.type=78;a.send(p,error);a.poll(1000,error);events=collect(second,RuntimeTcpEventKind::Packet,peerB);
    expect(packet(events,78,p.payload),"moved client preserves owned session");
    auto* self=&a;a=std::move(*self);expect(a.isConnected(),"self-move leaves current owner active");
    expect(b.connect("127.0.0.1",second.port(),1000,error),"moved-from client reconnects");
    b.close();a.close();second.close();newClient.close();first.close();
    RuntimeTcpServer temporary;expect(temporary.open(0,1,RuntimeTcpBindScope::LoopbackIpv4,error),"reserve owned failure port");
    const auto closedPort=temporary.port();temporary.close();RuntimeTcpClient refused;
    expect(!refused.connect("127.0.0.1",closedPort,100,error)&&!refused.isConnected()&&!error.empty(),"real refused loopback connect returns error and no socket owner");
    expect(!refused.connect("",1,0,error)&&error=="TCP host and port are required","invalid endpoint rejected without DNS");
    const std::array<std::uint8_t,65> tooLong{};
    expect(!refused.connect("127.0.0.1",closedPort,0,tooLong,error)&&error=="local TCP proxy prelude exceeds 64 bytes","prelude bound unchanged");
    refused.close();expect(!refused.send(p,error)&&error=="TCP client is not connected","closed endpoint send error retained");
    expect(!first.open(0,0,error)&&!first.open(0,runtimeLanMaximumTcpClients+1,error),"zero/above-capacity listener limits reject");
    return true;
}
}
int main(){
    Sockets sockets;if(!expect(sockets.ready,"native socket startup"))return 1;
    fragmentedAndBadPeer();exactOutgoing();lifetimeAndFailure();
    std::cout<<"TCP native boundary: checks="<<checks<<" failures="<<failures<<" partialHeaderBody=exact outgoingWire=exact callerOwnedCopy=yes badPeer=isolated partialEOF=noPublish moveClose=owned refusalCancelReconnect=yes loopbackOnly=yes\n";
    return failures?1:0;
}
