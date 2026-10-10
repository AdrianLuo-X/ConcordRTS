#include "runtime/replay/RuntimeMatchReplay.h"

#include <algorithm>
#include <array>
#include <istream>
#include <limits>
#include <ostream>
#include <span>
#include <type_traits>

namespace {
using Bytes = std::vector<std::uint8_t>;
constexpr std::array<std::uint8_t,8> magic{'C','R','T','S','R','P','L','Y'};
bool fail(std::string& error, const char* message) { error = message; return false; }
void number(Bytes& bytes, std::uint64_t value, int width) {
    for (int shift = (width-1)*8; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}
void blob(Bytes& bytes, std::span<const std::uint8_t> value) {
    number(bytes,value.size(),4); bytes.insert(bytes.end(),value.begin(),value.end());
}
void string(Bytes& bytes, const std::string& value) {
    blob(bytes,{reinterpret_cast<const std::uint8_t*>(value.data()),value.size()});
}
class Cursor {
public:
    explicit Cursor(std::span<const std::uint8_t> bytes): bytes_(bytes) {}
    bool number(std::uint64_t& value, int width) {
        if (bytes_.size()-offset_ < static_cast<std::size_t>(width)) return false;
        value=0; for(int n=0;n<width;++n) value=(value<<8)|bytes_[offset_++]; return true;
    }
    bool blob(std::span<const std::uint8_t>& value, std::size_t limit) {
        std::uint64_t size=0;
        if(!number(size,4) || size>limit || size>bytes_.size()-offset_) return false;
        value=bytes_.subspan(offset_,static_cast<std::size_t>(size)); offset_+=size; return true;
    }
    bool string(std::string& value, std::size_t limit) {
        std::span<const std::uint8_t> data;
        if(!blob(data,limit)) return false;
        value.assign(reinterpret_cast<const char*>(data.data()),data.size());
        return value.find('\0')==std::string::npos;
    }
    bool end() const { return offset_==bytes_.size(); }
private:
    std::span<const std::uint8_t> bytes_; std::size_t offset_=0;
};
bool textValid(const std::string& value, std::size_t maximum, bool required=true) {
    return (!required || !value.empty()) && value.size()<=maximum && value.find('\0')==std::string::npos;
}
bool validHeader(const RuntimeMatchReplayHeader& header) {
    return header.initialTick>=0 && header.fixedTickHz>=1 && header.fixedTickHz<=240
        && header.compatibility && header.initialHash
        && textValid(header.config,1024*1024) && textValid(header.initialState,runtimeMatchReplayMaximumRecordBytes-1024*1024-64);
}
// Offline Stop/Guard/Scatter can address a selection larger than a network
// packet. Preserve that one group operation and its order in the replay;
// splitting Scatter into packets would change its formation/destinations.
bool replayWireCommand(const RuntimeMultiplayerGameplayCommand& command,
    RuntimeMultiplayerGameplayCommand& wire, std::vector<std::int32_t>& group,
    std::string& error) {
    wire=command;group.clear();
    std::visit([&](auto& body) {
        using Body=std::decay_t<decltype(body)>;
        if constexpr(std::is_same_v<Body,RuntimeMultiplayerGameplayStopCommand>
            || std::is_same_v<Body,RuntimeMultiplayerGameplayGuardCommand>
            || std::is_same_v<Body,RuntimeMultiplayerGameplayScatterCommand>) {
            if(body.objectIds.size()>runtimeMultiplayerGameplayMaximumObjectIds) {
                group=std::move(body.objectIds);body.objectIds={group.front()};
            }
        }
    },wire.body);
    if(group.size()>65536) return fail(error,"Replay command selection exceeds the limit.");
    auto sorted=group;std::sort(sorted.begin(),sorted.end());
    if((!sorted.empty() && sorted.front()<=0) || std::adjacent_find(sorted.begin(),sorted.end())!=sorted.end())
        return fail(error,"Replay command selection is invalid.");
    return validateRuntimeMultiplayerGameplayCommand(wire,error);
}
bool validRecord(const RuntimeMatchReplayRecord& record, int currentTick, std::string& error) {
    if(record.tick<0) return fail(error,"Replay tick is negative.");
    if(record.kind==RuntimeMatchReplayRecordKind::Reset) {
        return (record.stateHash && textValid(record.config,1024*1024)
            && textValid(record.state,runtimeMatchReplayMaximumRecordBytes-1024*1024-64))
            || fail(error,"Replay reset state is invalid.");
    }
    if(record.kind==RuntimeMatchReplayRecordKind::Step) {
        return (currentTick<std::numeric_limits<int>::max() && record.tick==currentTick+1)
            || fail(error,"Replay simulation steps are not consecutive.");
    }
    if(record.tick!=currentTick) return fail(error,"Replay command/checkpoint is at the wrong tick.");
    switch(record.kind) {
    case RuntimeMatchReplayRecordKind::Command: {
        RuntimeMultiplayerGameplayCommand wire;std::vector<std::int32_t> group;
        return (textValid(record.owner,512) && replayWireCommand(record.command,wire,group,error))
            || fail(error,"Replay gameplay command is invalid.");
    }
    case RuntimeMatchReplayRecordKind::Finish:
        return record.stateHash!=0 || fail(error,"Replay final state hash is missing.");
    case RuntimeMatchReplayRecordKind::Selection: {
        if(record.selection.size()>65536) return fail(error,"Replay selection exceeds the object limit.");
        // Preserve selection order; group operations can consume this order.
        auto sorted=record.selection; std::sort(sorted.begin(),sorted.end());
        return (std::all_of(sorted.begin(),sorted.end(),[](int id){return id>0;})
            && std::adjacent_find(sorted.begin(),sorted.end())==sorted.end())
            || fail(error,"Replay selection is invalid.");
    }
    default: return fail(error,"Replay record type is unsupported.");
    }
}
Bytes encodeRecord(const RuntimeMatchReplayRecord& record) {
    Bytes bytes; number(bytes,static_cast<std::uint32_t>(record.tick),4);
    switch(record.kind) {
    case RuntimeMatchReplayRecordKind::Command: {
        number(bytes,record.applied?1:0,1); string(bytes,record.owner);
        Bytes payload; std::string error;
        RuntimeMultiplayerGameplayCommand wire;std::vector<std::int32_t> group;
        if(!replayWireCommand(record.command,wire,group,error)
            || !encodeRuntimeMultiplayerGameplayCommand(wire,payload,error)) return {};
        blob(bytes,payload);number(bytes,group.size(),4);
        for(int id:group) number(bytes,static_cast<std::uint32_t>(id),4);
        break;
    }
    case RuntimeMatchReplayRecordKind::Step:
        number(bytes,record.lightweight?1:0,1); number(bytes,record.stateHash,8); break;
    case RuntimeMatchReplayRecordKind::Reset:
        number(bytes,record.stateHash,8); string(bytes,record.config); string(bytes,record.state); break;
    case RuntimeMatchReplayRecordKind::Finish: number(bytes,record.stateHash,8); break;
    case RuntimeMatchReplayRecordKind::Selection:
        number(bytes,record.selection.size(),4);
        for(int id:record.selection) number(bytes,static_cast<std::uint32_t>(id),4);
        break;
    default: return {};
    }
    return bytes;
}
bool decodeRecord(RuntimeMatchReplayRecordKind kind, const Bytes& bytes, RuntimeMatchReplayRecord& record, std::string& error) {
    Cursor cursor(bytes); std::uint64_t tick=0,value=0;
    record.kind=kind;
    if(!cursor.number(tick,4) || tick>std::numeric_limits<int>::max()) return false;
    record.tick=static_cast<int>(tick);
    switch(kind) {
    case RuntimeMatchReplayRecordKind::Command: {
        std::span<const std::uint8_t> payload;
        if(!cursor.number(value,1) || value>1 || !cursor.string(record.owner,512)
            || !cursor.blob(payload,runtimeMultiplayerGameplayMaximumPayloadBytes)
            || !decodeRuntimeMultiplayerGameplayCommand(payload,record.command,error)) return false;
        record.applied=value!=0;
        if(!cursor.number(value,4) || value>65536) return false;
        if(value) {
            if(value<=runtimeMultiplayerGameplayMaximumObjectIds) return false;
            std::vector<std::int32_t> group;
            for(std::uint64_t n=0,count=value;n<count;++n) {
                if(!cursor.number(value,4) || value>std::numeric_limits<int>::max()) return false;
                group.push_back(static_cast<std::int32_t>(value));
            }
            bool expanded=false;
            std::visit([&](auto& body) {
                using Body=std::decay_t<decltype(body)>;
                if constexpr(std::is_same_v<Body,RuntimeMultiplayerGameplayStopCommand>
                    || std::is_same_v<Body,RuntimeMultiplayerGameplayGuardCommand>
                    || std::is_same_v<Body,RuntimeMultiplayerGameplayScatterCommand>) {
                    if(body.objectIds.size()==1 && body.objectIds.front()==group.front()) {
                        body.objectIds=std::move(group);expanded=true;
                    }
                }
            },record.command.body);
            if(!expanded) return false;
        }
        break;
    }
    case RuntimeMatchReplayRecordKind::Step:
        if(!cursor.number(value,1) || value>1 || !cursor.number(record.stateHash,8)) return false;
        record.lightweight=value!=0; break;
    case RuntimeMatchReplayRecordKind::Reset:
        if(!cursor.number(record.stateHash,8) || !cursor.string(record.config,1024*1024)
            || !cursor.string(record.state,runtimeMatchReplayMaximumRecordBytes)) return false;
        break;
    case RuntimeMatchReplayRecordKind::Finish:
        if(!cursor.number(record.stateHash,8)) return false;
        break;
    case RuntimeMatchReplayRecordKind::Selection:
        if(!cursor.number(value,4) || value>65536) return false;
        record.selection.reserve(static_cast<std::size_t>(value));
        for(std::uint64_t n=0,count=value;n<count;++n) {
            if(!cursor.number(value,4) || value>std::numeric_limits<int>::max()) return false;
            record.selection.push_back(static_cast<int>(value));
        }
        break;
    default: return false;
    }
    return cursor.end();
}
bool readBytes(std::istream& stream, std::span<std::uint8_t> bytes) {
    return static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())));
}
bool writeBytes(std::ostream& stream, std::span<const std::uint8_t> bytes) {
    return static_cast<bool>(stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())));
}
}

bool RuntimeMatchReplayWriter::begin(std::ostream& stream, const RuntimeMatchReplayHeader& header, std::string& error) {
    if(stream_ || !validHeader(header)) return fail(error,"Replay initial state/configuration is invalid.");
    stream_=&stream; tick_=header.initialTick;
    Bytes prefix(magic.begin(),magic.end()); number(prefix,runtimeMatchReplayFormatVersion,2);
    number(prefix,runtimeMultiplayerGameplayCommandVersion,2);
    if(!writeBytes(stream,prefix)) {failed_=true;return fail(error,"Cannot write replay header.");}
    bytes_=prefix.size(); chain_=runtimeMultiplayerSnapshotSha256(prefix);
    Bytes payload; number(payload,header.initialTick,4); number(payload,header.fixedTickHz,4);
    number(payload,header.compatibility,8); number(payload,header.initialHash,8);
    string(payload,header.config); string(payload,header.initialState);
    return envelope(RuntimeMatchReplayRecordKind::Initial,payload,error);
}
bool RuntimeMatchReplayWriter::envelope(RuntimeMatchReplayRecordKind kind, const Bytes& payload, std::string& error) {
    if(!stream_ || failed_ || finished_ || payload.size()>runtimeMatchReplayMaximumRecordBytes
        || bytes_+45+payload.size()>runtimeMatchReplayMaximumFileBytes)
        return fail(error,"Replay stream is closed, failed or too large.");
    Bytes data; data.reserve(13+payload.size()); number(data,static_cast<std::uint8_t>(kind),1);
    number(data,sequence_,8); number(data,payload.size(),4); data.insert(data.end(),payload.begin(),payload.end());
    Bytes chained(chain_.begin(),chain_.end()); chained.insert(chained.end(),data.begin(),data.end());
    const auto digest=runtimeMultiplayerSnapshotSha256(chained);
    if(!writeBytes(*stream_,data) || !writeBytes(*stream_,digest)) {
        failed_=true;return fail(error,"Replay write failed; recording is incomplete.");
    }
    chain_=digest; bytes_+=data.size()+digest.size(); ++sequence_; error.clear(); return true;
}
bool RuntimeMatchReplayWriter::append(const RuntimeMatchReplayRecord& record, std::string& error) {
    if(!validRecord(record,tick_,error)) return false;
    const auto payload=encodeRecord(record);
    if(payload.empty() || !envelope(record.kind,payload,error)) return false;
    tick_=record.tick;
    if(record.kind==RuntimeMatchReplayRecordKind::Finish) {
        stream_->flush(); if(!*stream_) {failed_=true;return fail(error,"Replay final flush failed.");}
        finished_=true;
    }
    return true;
}

bool RuntimeMatchReplayReader::begin(std::istream& stream, RuntimeMatchReplayHeader& output, std::string& error) {
    if(stream_) return fail(error,"Replay reader is already open.");
    stream_=&stream; std::array<std::uint8_t,12> prefix{};
    if(!readBytes(stream,prefix) || !std::equal(magic.begin(),magic.end(),prefix.begin())) {
        failed_=true;return fail(error,"Not a ConcordRTS match replay.");
    }
    Cursor versions(std::span<const std::uint8_t>(prefix).subspan(8)); std::uint64_t format=0,command=0;
    if(!versions.number(format,2) || !versions.number(command,2)
        || format!=runtimeMatchReplayFormatVersion || command!=runtimeMultiplayerGameplayCommandVersion) {
        failed_=true;return fail(error,"Replay format or gameplay version is incompatible.");
    }
    bytes_=prefix.size(); chain_=runtimeMultiplayerSnapshotSha256(prefix);
    Bytes bytes; RuntimeMatchReplayRecordKind kind;
    if(!envelope(kind,bytes,error)) return false;
    RuntimeMatchReplayHeader header; Cursor cursor(bytes); std::uint64_t tick=0,hz=0;
    if(kind!=RuntimeMatchReplayRecordKind::Initial || !cursor.number(tick,4) || tick>std::numeric_limits<int>::max()
        || !cursor.number(hz,4) || hz>240 || !cursor.number(header.compatibility,8)
        || !cursor.number(header.initialHash,8) || !cursor.string(header.config,1024*1024)
        || !cursor.string(header.initialState,runtimeMatchReplayMaximumRecordBytes) || !cursor.end()) {
        failed_=true;return fail(error,"Replay initial record is malformed.");
    }
    header.initialTick=static_cast<int>(tick); header.fixedTickHz=static_cast<int>(hz);
    if(!validHeader(header)) {failed_=true;return fail(error,"Replay initial state/configuration is invalid.");}
    tick_=header.initialTick; output=std::move(header); error.clear(); return true;
}
bool RuntimeMatchReplayReader::envelope(RuntimeMatchReplayRecordKind& kind, Bytes& payload, std::string& error) {
    if(!stream_ || failed_ || finished_) return fail(error,"Replay reader is closed or failed.");
    std::array<std::uint8_t,13> prefix{};
    if(!readBytes(*stream_,prefix)) {failed_=true;return fail(error,"Replay is truncated or has no final record.");}
    Cursor cursor(prefix); std::uint64_t tag=0,sequence=0,size=0;
    if(!cursor.number(tag,1) || tag<1 || tag>6 || !cursor.number(sequence,8) || sequence!=sequence_
        || !cursor.number(size,4) || size>runtimeMatchReplayMaximumRecordBytes
        || bytes_+45+size>runtimeMatchReplayMaximumFileBytes) {
        failed_=true;return fail(error,"Replay record type, sequence or length is invalid.");
    }
    Bytes data(static_cast<std::size_t>(size)); RuntimeMultiplayerSnapshotSha256 digest{};
    if(!readBytes(*stream_,data) || !readBytes(*stream_,digest)) {
        failed_=true;return fail(error,"Replay record is truncated.");
    }
    Bytes chained(chain_.begin(),chain_.end()); chained.insert(chained.end(),prefix.begin(),prefix.end());
    chained.insert(chained.end(),data.begin(),data.end());
    if(runtimeMultiplayerSnapshotSha256(chained)!=digest) {
        failed_=true;return fail(error,"Replay integrity check failed.");
    }
    kind=static_cast<RuntimeMatchReplayRecordKind>(tag); payload=std::move(data);
    chain_=digest; bytes_+=45+size; ++sequence_; error.clear(); return true;
}
bool RuntimeMatchReplayReader::next(RuntimeMatchReplayRecord& output, std::string& error) {
    RuntimeMatchReplayRecordKind kind; Bytes payload;
    if(!envelope(kind,payload,error)) return false;
    RuntimeMatchReplayRecord record;
    if(!decodeRecord(kind,payload,record,error) || !validRecord(record,tick_,error)) {
        failed_=true;return fail(error,"Replay record payload or timeline is invalid.");
    }
    if(kind==RuntimeMatchReplayRecordKind::Finish) {
        if(stream_->peek()!=std::char_traits<char>::eof() || stream_->bad()) {
            failed_=true;return fail(error,"Replay contains trailing data or a read failure.");
        }
        finished_=true;
    }
    tick_=record.tick; output=std::move(record); error.clear(); return true;
}
bool validateRuntimeMatchReplay(std::istream& stream, RuntimeMatchReplayHeader& header,
    std::uint64_t& steps, std::uint64_t& commands, std::string& error
) {
    RuntimeMatchReplayReader reader; RuntimeMatchReplayHeader candidate;
    if(!reader.begin(stream,candidate,error)) return false;
    std::uint64_t stepCount=0,commandCount=0;
    while(!reader.finished()) {
        RuntimeMatchReplayRecord record;
        if(!reader.next(record,error)) return false;
        stepCount+=record.kind==RuntimeMatchReplayRecordKind::Step;
        commandCount+=record.kind==RuntimeMatchReplayRecordKind::Command;
    }
    header=std::move(candidate); steps=stepCount; commands=commandCount; error.clear(); return true;
}
