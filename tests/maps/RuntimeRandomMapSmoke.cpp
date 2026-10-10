#include "data/maps/RuntimeRandomMap.h"
#include "runtime/host/RuntimeShell.h"
#include "core/io/IniFile.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>

int main(int argc,char** argv) {
    RuntimeConfig config;
    config.localDataRootPath="local_data";
    config.resourceRootPath="RedAlert2";
    const std::filesystem::path output=argc>1?argv[1]:"build/smoke/random-map-probe/maps";
    std::string error; int checked=0;
    const auto fail=[&](const std::string& reason){std::cerr<<"Random map: "<<reason<<'\n';return 1;};
    for (int environment=0;environment<5;++environment) for (int climate=0;climate<2;++climate)
        for (int resource=0;resource<4;++resource) {
        RuntimeRandomMapOptions options;
        options.environment=environment; options.theater=climate; options.resources=resource;
        options.players=2+((environment*2+climate)%7); options.timeOfDay=resource;
        options.size=resource; options.seed=0x914000U+environment*100+climate*10+resource;
        RuntimeRandomMapResult result;
        if (!runtimeGenerateRandomMap(config,options,result,error)) return fail("generation "+std::to_string(environment)+"/"+std::to_string(climate)+"/"+std::to_string(resource)+": "+error);
        if (result.starts.size()!=static_cast<std::size_t>(options.players) || result.landCells<100
            || result.oreCells<50*options.players || ((environment<3)&&result.waterCells==0)) return fail("missing gameplay terrain or starts");
        IniFile ini; MapDatabase map;
        if (!ini.loadFromString(result.iniText) || !map.loadFromIni(ini)
            || !map.getOverlayPackSummary().payloadDecoded || !map.getOverlayDataPackSummary().payloadDecoded)
            return fail("original map/ore decode failed");
        std::map<int,MapIsoTile> tiles;
        for (const auto& tile:map.getIsoTiles()) {
            if (!tiles.emplace(tile.y*512+tile.x,tile).second) return fail("overlapping terrain cells");
        }
        for (const auto& start:result.starts) for (int dy=-3;dy<=3;++dy) for (int dx=-3;dx<=3;++dx) {
            const auto found=tiles.find((start.y+dy)*512+start.x+dx);
            if (found==tiles.end() || found->second.level || found->second.tileIndex!=0)
                return fail("construction yard cannot expand at a start");
        }
        if (resource==1) {
            RuntimeRandomMapResult repeated;
            if (!runtimeGenerateRandomMap(config,options,repeated,error) || repeated.iniText!=result.iniText) return fail("same choices/seed changed the battlefield");
        }
        const std::string title="战场 "+std::to_string(environment)+"-"+std::to_string(climate)+"-"+std::to_string(resource);
        if (!runtimeRenameRandomMap(result,title,error)) return fail(error);
        RuntimeRandomMapOptions fromName;
        if (!runtimeRandomMapOptionsFromFileName(result.fileName,fromName) || fromName.name!=title)
            return fail("named map identity did not retain choices");
        RuntimeRandomMapResult onPeer;
        if (!runtimeGenerateRandomMap(config,fromName,onPeer,error) || onPeer.iniText!=result.iniText)
            return fail("another machine could not recreate a named map");
        std::filesystem::path path;
        if (!runtimeStoreRandomMap(result,output,path,error)) return fail(error);
        RuntimeRandomMapResult loaded;
        if (!runtimeLoadRandomMap(path,loaded,error) || loaded.iniText!=result.iniText
            || loaded.starts.size()!=result.starts.size() || loaded.preview.pixels!=result.preview.pixels) return fail("saved battlefield changed on reload");
        std::cout<<"PASS shape="<<environment<<" climate="<<climate<<" resources="<<resource
            <<" players="<<options.players<<" water="<<result.waterCells<<" raised="<<result.raisedCells
            <<" ore="<<result.oreCells<<" trees="<<result.trees<<" structures="<<result.buildings<<'\n';
        ++checked;
    }
    RuntimeRandomMapOptions invalid;
    if (runtimeRandomMapOptionsFromFileName("../../rmg1_00000001_301112_.map",invalid)) return fail("path accepted as a map identity");
    std::cout<<"Random map: "<<checked<<" playable map combinations, original formats, named saves and reproducible LAN maps passed.\n";
    return 0;
}
