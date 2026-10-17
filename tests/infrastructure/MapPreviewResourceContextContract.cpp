#include "assets/preview/MapPreviewResourceContext.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace concord::assets;
namespace fs = std::filesystem;
std::size_t checks = 0;
void require(bool condition, const std::string& reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
// Legal 1x1 raw RA2/YR SHP. This fixture is input data, not a copied decoder.
std::vector<unsigned char> shp(unsigned char pixel) {
    std::vector<unsigned char> bytes(33U, 0U);
    bytes[2]=1U; bytes[4]=1U; bytes[6]=1U;
    bytes[12]=1U; bytes[14]=1U; bytes[28]=32U; bytes[32]=pixel;
    return bytes;
}
void write(const fs::path& path, const std::vector<unsigned char>& bytes) {
    std::ofstream output(path,std::ios::binary);
    require(output.is_open(),"open isolated fixture");
    output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    require(output.good(),"write isolated fixture");
}
MapPreviewResourceContext context(const fs::path& root, std::uint64_t generation = 0) {
    MapPreviewResourceContext result;
    result.packageRoot=root.string(); result.tileRootPath=(root/"tiles").string();
    result.assetRootPath=root.string(); result.mountGeneration=generation;
    return result;
}
void bytes(MapPreviewResourceSession& session, const std::string& path,
    const std::vector<unsigned char>& expected, MapPreviewCacheDomain domain=MapPreviewCacheDomain::OverlayLooseBytes) {
    std::vector<unsigned char> actual; std::string error;
    require(session.readLooseBytes(domain,path,actual,error),"read fixture: "+error);
    require(actual==expected,"source bytes differ: "+path);
}
void ownership() {
    std::array<std::weak_ptr<int>,9> observed;
    MapPreviewResourceSession session(context(fs::current_path(),7));
    for(std::size_t index=0; index<observed.size(); ++index) {
        auto token=std::make_shared<int>(static_cast<int>(index)); observed[index]=token;
        session.cache<std::shared_ptr<int>>(static_cast<MapPreviewCacheDomain>(index)).emplace("kept",std::move(token));
    }
    require(session.cachedDomainCount()==9,"all nine legacy success/error domains allocated");
    for(const auto& observer:observed) require(!observer.expired(),"session owns cached value");
    session.refresh(); require(session.context().mountGeneration==8,"refresh increments explicit generation");
    require(session.cachedDomainCount()==0,"refresh clears all nine domains");
    for(const auto& observer:observed) require(observer.expired(),"refresh destroys cached successes and errors");
    session.refresh(); require(session.context().mountGeneration==9,"repeated refresh increments generation");
    std::weak_ptr<int> destroyed;
    { MapPreviewResourceSession temporary(context(fs::current_path()));
      auto token=std::make_shared<int>(1);destroyed=token;
      temporary.cache<std::shared_ptr<int>>(MapPreviewCacheDomain::DiagnosticResult).emplace("error-or-success",std::move(token)); }
    require(destroyed.expired(),"destroying session releases its last cached owner");
}
void files(const fs::path& runRoot) {
    const auto rootA=runRoot/"package-A", rootB=runRoot/"package-B";
    fs::create_directories(rootA); fs::create_directories(rootB);
    const auto a=shp(17U), b=shp(29U), updated=shp(63U);
    write(rootA/"same.shp",a); write(rootB/"same.shp",b);
    MapPreviewResourceSession sessionA(context(rootA)), sessionB(context(rootB));
    bytes(sessionA,"same.shp",a);bytes(sessionB,"same.shp",b);bytes(sessionA,"same.shp",a);
    const auto oldTime=fs::last_write_time(rootA/"same.shp");
    write(rootA/"same.shp",updated);fs::last_write_time(rootA/"same.shp",oldTime);
    require(fs::file_size(rootA/"same.shp")==a.size(),"same-length replacement fixture");
    require(fs::last_write_time(rootA/"same.shp")==oldTime,"same-time replacement fixture");
    bytes(sessionA,"same.shp",a);bytes(sessionB,"same.shp",b);
    sessionA.refresh();bytes(sessionA,"same.shp",updated);bytes(sessionB,"same.shp",b);
    bytes(sessionA,"same.shp",updated); // repeated preview resource read, same generation
    bytes(sessionA,"same.shp",updated,MapPreviewCacheDomain::DiagnosticLooseBytes);
    const std::string missing="late.shp";std::vector<unsigned char> actual;std::string error;
    require(!sessionA.readLooseBytes(MapPreviewCacheDomain::OverlayLooseBytes,missing,actual,error),"missing file fails");
    require(error=="failed to open file: "+missing,"original read error spelling");
    require(sessionA.cache<std::vector<unsigned char>>(MapPreviewCacheDomain::OverlayLooseBytes).count(missing)==0,"missing read not negative-cached");
    write(rootA/missing,b);bytes(sessionA,missing,b); // must recover without refresh
    {
        MapPreviewResourceSession temporary(context(rootA));bytes(temporary,"same.shp",updated);
        temporary.cache<std::string>(MapPreviewCacheDomain::OverlayTmpError).emplace("same-key","old decode error");
        temporary.cache<std::string>(MapPreviewCacheDomain::OverlayShpError).emplace("same-key","old decode error");
        temporary.cache<std::pair<std::string,bool>>(MapPreviewCacheDomain::DiagnosticResult).emplace("same-key",std::make_pair("old decode error",false));
        require(temporary.cache<std::string>(MapPreviewCacheDomain::OverlayTmpError).at("same-key")=="old decode error","same-generation TMP error retained");
        temporary.refresh();
        require(temporary.cache<std::string>(MapPreviewCacheDomain::OverlayTmpError).empty(),"TMP error invalidated");
        require(temporary.cache<std::string>(MapPreviewCacheDomain::OverlayShpError).empty(),"SHP error invalidated");
        require(temporary.cache<std::pair<std::string,bool>>(MapPreviewCacheDomain::DiagnosticResult).empty(),"diagnostic error invalidated");
    }
    write(rootA/"same.shp",a);
    { MapPreviewResourceSession fresh(context(rootA));bytes(fresh,"same.shp",a); }
    require(sessionA.context().editionModPriority==MapPreviewEditionModPriority::LegacyConfiguredRootOrder,"explicit legacy edition/mod priority retained");
    std::cout<<"Preview session fixtures retained: "<<runRoot.string()<<'\n';
}
} // namespace
int main(int argc,char** argv) {
    try {
        require(argc==2,"pass a new-build-directory fixture parent");
        const auto parent=fs::absolute(argv[1]).lexically_normal();
        const auto runRoot=parent/("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        require(!fs::exists(runRoot),"fresh fixture path required");
        fs::create_directories(runRoot);
        ownership();files(runRoot);
        std::cout<<"Map preview resource context contract passed: checks="<<checks
            <<" roots=A/B/A equalSizeSameTimeRefresh=ok missingThenCreated=ok repeated=ok nineDomainRelease=ok destruction=ok\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"Map preview resource context contract failed after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}