#include "simulation/navigation/pathfinding/PathGoalCandidates.h"
#include <algorithm>
#include <iostream>
#include <type_traits>

class Terrain final : public PathGoalTerrainView {
public:
    std::vector<PathTerrainCell> cells;
    const PathTerrainCell* nodeAt(PathCell cell) const override {
        for(const auto& node:cells) if(node.cell.x==cell.x&&node.cell.y==cell.y) return &node;
        return nullptr;
    }
    const PathTerrainCell& nodeAtIndex(int i) const override {return cells[i];}
    std::vector<int> indicesInBounds(std::int64_t x1,std::int64_t y1,std::int64_t x2,std::int64_t y2) const override {
        std::vector<int> result;for(std::size_t i=0;i<cells.size();++i){auto c=cells[i].cell;if(c.x>=x1&&c.x<=x2&&c.y>=y1&&c.y<=y2)result.push_back(static_cast<int>(i));}return result;
    }
};
bool equal(PathCell a,PathCell b){return a.x==b.x&&a.y==b.y;}
int main(){
    static_assert(sizeof(PathCell)==8);
    static_assert(std::is_standard_layout_v<PathTerrainCell>);
    Terrain terrain;for(int y=0;y<12;++y)for(int x=0;x<12;++x){PathTerrainCell c;c.cell={x,y};terrain.cells.push_back(c);}
    PathGoalCandidateInput request;request.start={0,0};request.goal={2,2};
    auto plain=generatePathGoalCandidates(request,terrain);
    if(plain.requiresFallbackRing||plain.cells.size()!=1||!equal(plain.cells[0],{2,2}))return 1;
    int checks=0;
    for(int mode=0;mode<4;++mode)for(int bits=0;bits<32;++bits){
        auto& c=terrain.cells[26];c.passable=(bits&1)!=0;c.cliff=(bits&2)!=0;c.water=(bits&4)!=0;c.shore=(bits&8)!=0;c.bridge=(bits&16)!=0;
        request.movementMode=static_cast<PathMovementMode>(mode);
        const bool usable=mode==3||(c.passable&&!c.cliff&&(mode==1?(c.water||c.bridge):(mode==0?(!c.water||c.bridge||c.shore):true)));
        const auto result=generatePathGoalCandidates(request,terrain);
        if(result.requiresFallbackRing==usable||result.cells.size()!=(usable?1U:0U))return 2;
        ++checks;
    }
    terrain.cells[26]=PathTerrainCell{};terrain.cells[26].cell={2,2};request.movementMode=PathMovementMode::Ground;
    const std::vector<PathCell> approach={{3,2},{2,3},{1,2},{0,0},{30,30}};
    request.mapQueryApproach=true;request.approachCells=approach;
    auto mapped=generatePathGoalCandidates(request,terrain);
    if(!mapped.mapQueryApproachUsed||mapped.cells.size()!=3||!equal(mapped.cells[0],{1,2})||!equal(mapped.cells[1],{3,2})||!equal(mapped.cells[2],{2,3}))return 3;
    request.mapQueryApproach=false;request.approachCells={};request.alternateGoalCells=approach;
    auto alternate=generatePathGoalCandidates(request,terrain);
    if(alternate.cells.size()!=3||!equal(alternate.cells[0],{1,2})||!equal(alternate.cells[1],{3,2})||!equal(alternate.cells[2],{2,3}))return 4;
    request.alternateGoalCells={};request.goalSlotCount=4;
    auto nearby=generatePathGoalCandidates(request,terrain);
    if(nearby.cells.size()!=16||!equal(nearby.cells[0],{2,2}))return 5;
    terrain.cells[26].passable=false;request.goalSlotCount=0;
    const std::vector<PathCell> footprint={{2,2}};
    auto ring=generatePathGoalFallbackCandidates(request,terrain,{true,true,2,2,2,2,footprint});
    if(ring.cells.size()!=32||!equal(ring.cells[0],{1,1})||ring.distanceQueries==0||ring.componentCellChecks!=0)return 6;
    auto irregular=generatePathGoalFallbackCandidates(request,terrain,{true,false,2,2,2,2,footprint});
    if(irregular.distanceQueries!=ring.distanceQueries||irregular.componentCellChecks!=ring.distanceQueries||irregular.cells.size()!=ring.cells.size())return 7;
    for(std::size_t i=0;i<ring.cells.size();++i)if(!equal(irregular.cells[i],ring.cells[i]))return 8;
    request.goal={30,30};auto missing=generatePathGoalCandidates(request,terrain);
    auto outside=generatePathGoalFallbackCandidates(request,terrain,{});
    if(!missing.requiresFallbackRing||!outside.cells.empty())return 9;
    std::cout<<"Independent candidate module passed movementTruthTable="<<checks<<" nearbyCap=16 ringCap=32 authoredOrder=yes lifetime=lexical ownedResults=yes noQueueWorldRulesHostLink=yes\n";
}
