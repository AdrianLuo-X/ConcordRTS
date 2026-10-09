#include "RuntimeMatchReplay.h"

#include <iostream>
#include <sstream>
#include <stdexcept>

int main() {
    try {
        const auto require=[](bool value,const std::string& reason){if(!value)throw std::runtime_error(reason);};
        RuntimeMatchReplayHeader header;
        header.initialTick=17; header.fixedTickHz=60; header.compatibility=0x123456789abcdef0ULL;
        header.initialHash=0xfedcba9876543210ULL;
        header.config="[ReplayConfig]\nMap=地图.map\n";
        header.initialState="[RuntimeSave]\nTick=17\n";
        std::ostringstream saved(std::ios::binary); RuntimeMatchReplayWriter writer; std::string error;
        require(writer.begin(saved,header,error),error);
        std::vector<RuntimeMatchReplayRecord> expected;
        const auto append=[&](RuntimeMatchReplayRecord record){
            require(writer.append(record,error),error); expected.push_back(std::move(record));
        };
        RuntimeMatchReplayRecord record; record.kind=RuntimeMatchReplayRecordKind::Selection;
        record.tick=17; record.selection={3,1,2}; append(record);
        // A long match has more than the old diagnostic ring's 128 events.
        for(int n=0;n<2048;++n) {
            record={}; record.kind=RuntimeMatchReplayRecordKind::Command; record.tick=17+n;
            record.owner=n%2 ? "Alpha" : "Beta"; record.applied=n%3!=0;
            record.command.body=RuntimeMultiplayerGameplayStopCommand{{1,2,3}};
            if(n==0) {
                auto& ids=std::get<RuntimeMultiplayerGameplayStopCommand>(record.command.body).objectIds;
                ids.clear();for(int id=512;id>0;--id)ids.push_back(id);
            }
            append(record);
            record={}; record.kind=RuntimeMatchReplayRecordKind::Step; record.tick=18+n;
            record.lightweight=n%11==0; record.stateHash=n%60==0?1000+n:0; append(record);
        }
        record={};record.kind=RuntimeMatchReplayRecordKind::Reset;record.tick=100;
        record.config=header.config;record.state=header.initialState;record.stateHash=987;append(record);
        record={};record.kind=RuntimeMatchReplayRecordKind::Step;record.tick=101;append(record);
        record={};record.kind=RuntimeMatchReplayRecordKind::Finish;record.tick=101;record.stateHash=456;append(record);
        require(writer.finished(),"Missing writer final state");
        require(!writer.append(record,error),"Accepted a record after Finish");
        const auto bytes=saved.str();
        std::istringstream input(bytes,std::ios::binary); RuntimeMatchReplayHeader decoded;
        std::uint64_t steps=0,commands=0;
        require(validateRuntimeMatchReplay(input,decoded,steps,commands,error),error);
        require(decoded==header && steps==2049 && commands==2048,"Full recording count/header mismatch");
        std::istringstream replay(bytes,std::ios::binary);RuntimeMatchReplayReader reader;
        require(reader.begin(replay,decoded,error),error);
        for(const auto& wanted:expected) {
            RuntimeMatchReplayRecord actual;
            require(reader.next(actual,error),error);require(actual==wanted,"Record/order/command changed on disk");
        }
        require(reader.finished(),"Reader never reached Finish");
        const auto reject=[&](const std::string& damaged,const char* reason){
            std::istringstream test(damaged,std::ios::binary);
            RuntimeMatchReplayHeader sentinel=header;std::uint64_t a=91,b=92;
            require(!validateRuntimeMatchReplay(test,sentinel,a,b,error),reason);
            require(sentinel==header && a==91 && b==92,"Invalid replay changed validated output");
        };
        for(std::size_t length:{0U,1U,8U,12U,20U,80U}) reject(bytes.substr(0,length),"Accepted truncated header");
        reject(bytes.substr(0,bytes.size()-1),"Accepted truncated checksum");
        reject(bytes.substr(0,bytes.size()-57),"Accepted missing Finish");
        reject(bytes+"junk","Accepted trailing bytes");
        for(std::size_t offset:{0U,8U,10U,12U,13U,21U,31U,98U,210U,1024U}) {
            auto changed=bytes;changed[offset]^=0x40;reject(changed,"Accepted changed version, length, sequence or content");
        }
        {
            auto oversized=bytes;
            for(int n=21;n<25;++n)oversized[n]=static_cast<char>(255);
            reject(oversized,"Accepted oversized record length");
        }
        {
            std::ostringstream out;RuntimeMatchReplayWriter invalid;
            auto bad=header;bad.fixedTickHz=0;require(!invalid.begin(out,bad,error),"Accepted invalid clock");
            require(out.str().empty(),"Invalid header wrote bytes");
            require(invalid.begin(out,header,error),error);
            auto badRecord=expected[1];badRecord.tick=18;
            const auto before=out.str();require(!invalid.append(badRecord,error),"Accepted command at wrong tick");
            badRecord={};badRecord.kind=RuntimeMatchReplayRecordKind::Selection;badRecord.tick=17;badRecord.selection={1,1};
            require(!invalid.append(badRecord,error),"Accepted duplicate selected ID");
            require(before==out.str(),"Rejected record changed recording");
            out.setstate(std::ios::badbit);badRecord=expected[1];
            require(!invalid.append(badRecord,error),"Ignored output failure");
            out.clear();require(!invalid.append(badRecord,error),"Resumed a failed recording silently");
        }
        std::cout<<"Match replay codec: passed; 2048 commands, 2049 frames, reset, final state, SHA-256 chain, version/length/order, truncation and I/O failure\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"Match replay codec: "<<error.what()<<'\n';return 1;
    }
}
