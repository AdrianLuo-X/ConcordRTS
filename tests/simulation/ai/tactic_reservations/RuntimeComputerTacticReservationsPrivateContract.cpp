// Standard-only live query borrow checks; real private-state qualification uses
// the existing recovery host and its original private dependencies.
#include "simulation/ai/RuntimeComputerTacticReservations.h"
#include "simulation/buildings/RuntimeGrindingSystem.h"
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
class LiveQueries final:public concord::simulation::ai::RuntimeComputerTacticObjectReservationQueries {
public:
    std::set<int> transport,engineer,occupy;
    std::vector<RuntimeGrindingRelation> grinding;
    std::map<std::string,RuntimeComputerTacticsState> owners;
    mutable std::vector<std::string> reads;
    bool pendingTransportTarget(int id)const override {reads.push_back("transport");return transport.contains(id);}
    bool pendingEngineerCapture(int id)const override {reads.push_back("engineer");return engineer.contains(id);}
    bool pendingOccupyTarget(int id)const override {reads.push_back("occupy");return occupy.contains(id);}
    const std::vector<RuntimeGrindingRelation>& grindingRelations()const override {reads.push_back("grinding");return grinding;}
    const std::map<std::string,RuntimeComputerTacticsState>& ownerTactics()const override {reads.push_back("owners");return owners;}
};
}
int runRuntimeComputerTacticReservationsLiveBorrowContract(){
    int checks=0;LiveQueries q;
    const std::vector<std::string> order={"transport","engineer","occupy","grinding","owners"};
    const auto expect=[&checks](bool value){++checks;if(!value)throw std::runtime_error("live reservation query borrow/ordered read");};
    const auto probe=[&](int id,bool expected,std::size_t readCount){q.reads.clear();expect(concord::simulation::ai::runtimeComputerTacticReservesObject(q,id)==expected);expect(q.reads.size()==readCount);expect(q.reads==std::vector<std::string>(order.begin(),order.begin()+static_cast<std::ptrdiff_t>(readCount)));};
    probe(7,false,5);
    q.transport.insert(7);probe(7,true,1);q.transport.clear();
    q.engineer.insert(7);probe(7,true,2);q.engineer.clear();
    q.occupy.insert(7);probe(7,true,3);q.occupy.clear();
    RuntimeGrindingRelation relation;relation.sourceId=7;q.grinding.push_back(relation);probe(7,true,4);q.grinding.clear();
    q.owners["live"].teams[3].objectIds={7};probe(7,true,5);
    q.owners.at("live").teams.at(3).objectIds.clear();probe(7,false,5);
    q.transport.insert(-1);probe(-1,true,1);
    return checks;
}
