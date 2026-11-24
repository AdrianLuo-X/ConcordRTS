#include "assets/sprites/RuntimeAuxiliaryShpBytes.h"
#include "data/art/ArtDatabase.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct Environment;
struct FakeArtDatabase { Environment* env; const ArtType* findArtType(const std::string&) const; };
struct FakeContext {
    FakeArtDatabase* art = nullptr;
    std::string assetRootPath = "A";
    std::string structureRootPath = "S";
    Environment* env = nullptr;
};
struct Environment {
    int scenario;
    int fault = -1;
    int marks = 0;
    ArtType art;
    ArtType fallback;
    FakeArtDatabase db{this};
    FakeContext context;
    std::string mixRoot = "B";
    std::vector<std::string> trace;
    RuntimeAuxiliaryShpByteCache cache;
    explicit Environment(int s) : scenario(s) {
        art.theater = (s & 2) != 0; art.rate = 17;
        fallback.theater = (s & 4) != 0;
        context.art = (s & 1) != 0 ? &db : nullptr;
        context.env = this;
        if (s % 7 == 0) mixRoot = "a";
        if (s % 11 == 0) mixRoot.clear();
    }
    void mark(const std::string& label) {
        trace.push_back(label); ++marks;
        if (marks == fault) throw std::runtime_error("injected");
    }
    std::vector<std::string> candidates(const std::string& id, const ArtType* a) {
        mark("candidates:"+id+":"+(a ? "fallback" : "none"));
        switch (scenario % 5) {
        case 0: return {};
        case 1: return {"SKIP.pcx","CLOUD"};
        case 2: return {"SIDE1","fallback"};
        case 3: return {"cloud.tem","CLOUD","Cloud"};
        default: return {"fx.sha","alternate"};
        }
    }
    const ArtType* findArt(const std::string& id) {
        mark("art:"+id);
        return scenario % 3 == 0 ? nullptr : &art;
    }
    const std::string& exports() {
        mark("exports");
        if (scenario % 13 == 0) art.theater = !art.theater;
        return mixRoot;
    }
    std::vector<unsigned char> payload() const {
        std::vector<unsigned char> b(scenario % 9 == 0 ? 3U : 12U, 0);
        if (b.size() >= 8) {b[6] = static_cast<unsigned char>(scenario % 17);b[7] = 0;}
        if (b.size() > 8) b[9] = static_cast<unsigned char>(scenario);
        return b;
    }
    bool mix(const std::string& root, const std::string& file,
        std::vector<unsigned char>& bytes, std::string& source, std::string& error) {
        mark("mix-before:"+root+"|"+file);
        if (scenario % 17 == 0) {context.structureRootPath += "x";mixRoot += "z";}
        const int route = scenario % 6;
        const bool ok = route == 0 || (route == 1 && root != context.assetRootPath);
        if (ok || route == 4) bytes = payload();
        else if (scenario % 4 != 0) bytes.clear();
        source = "mix:"+root+"/"+file;
        if (!ok) error = "missing:"+source;
        mark("mix-after:"+source);
        return ok;
    }
    bool loose(const std::vector<std::string>& roots, const std::string& asset,
        const std::string& file, std::vector<unsigned char>& bytes,
        std::string& source, std::string& error) {
        mark("loose-before:"+roots.at(0)+"|"+asset+"|"+file);
        const bool ok = scenario % 6 == 2;
        if (ok || scenario % 6 == 5) bytes = payload();
        else if (scenario % 4 != 0) bytes.clear();
        source = "loose:"+roots.at(0)+"/"+file;
        if (!ok) error = "missing:"+source;
        mark("loose-after:"+source);
        return ok;
    }
    bool timing() {mark("timing");return false;}
};
const ArtType* FakeArtDatabase::findArtType(const std::string& id) const {return env->findArt(id);}
static thread_local Environment* activeEnvironment = nullptr;
static std::string normalize(std::string s) {
    if (s == "throw-normalization") throw std::runtime_error("normalize");
    for (char& c:s) {if(c=='\\') c='/';c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));}
    return s;
}
class ModelSource final : public RuntimeAuxiliaryShpByteSource {
    Environment& e;
public:
    explicit ModelSource(Environment& value):e(value){}
    std::vector<std::string> imageCandidates(const std::string& id,const ArtType* a) override {return e.candidates(id,a);}
    const ArtType* findArtType(const std::string& id) override {return e.context.art ? e.context.art->findArtType(id) : nullptr;}
    const std::string& cachedMixExportsRoot() override {return e.exports();}
    bool sameNormalizedRoot(const std::string& a,const std::string& b) override {return normalize(a)==normalize(b);}
    bool readMix(const std::string& a,const std::string& b,std::vector<unsigned char>& c,std::string& d,std::string& f) override {return e.mix(a,b,c,d,f);}
    bool readByName(const std::vector<std::string>& a,const std::string& b,const std::string& c,
        std::vector<unsigned char>& d,std::string& f,std::string& g) override {return e.loose(a,b,c,d,f,g);}
    bool detailTimingEnabled() override {return e.timing();}
};
namespace original {
double runtimeElapsedMs(const std::chrono::steady_clock::time_point&,
    const std::chrono::steady_clock::time_point&);
#include "ResourceHelpersOriginal.inl"
using RuntimeMapRenderContext = FakeContext;
using RuntimeMapRenderCache = RuntimeAuxiliaryShpByteCache;
std::vector<std::string> resolveAuxiliarySpriteImageIdCandidates(const std::string& id,
    const FakeContext& c,const ArtType* a) {return c.env->candidates(id,a);}
const std::string& cachedRuntimeMixExportsRootForContext(const FakeContext& c,RuntimeAuxiliaryShpByteCache&) {return c.env->exports();}
std::string normalizedLowerPath(const std::string& value) {return normalize(value);}
bool runtimeRenderDetailDiagnosticsEnabled(const FakeContext& c) {return c.env->timing();}
bool loadRuntimeResourceBytesFromMixArchives(const std::string& a,const std::string& b,
    RuntimeAuxiliaryShpByteCache&,std::vector<unsigned char>& c,std::string& d,std::string& f) {return activeEnvironment->mix(a,b,c,d,f);}
bool loadRuntimeResourceBytesByName(const std::vector<std::string>& a,const std::string& b,
    const std::string& c,RuntimeAuxiliaryShpByteCache&,std::vector<unsigned char>& d,
    std::string& f,std::string& g) {return activeEnvironment->loose(a,b,c,d,f,g);}
RuntimeAuxiliaryShpBytesCacheEntry* run(const std::string& imageId,
    const RuntimeMapRenderContext& context,RuntimeMapRenderCache& cache,const ArtType* fallbackArtType,
    std::string& outputCandidateKey) {
#include "AuxiliaryByteAcquisitionOriginal.inl"
    outputCandidateKey = candidateKey;
    return &bytesEntry;
}
}
static std::string snapshot(const Environment& e) {
    std::ostringstream out;
    out<<e.context.assetRootPath<<'|'<<e.context.structureRootPath<<'|'<<e.mixRoot<<'|'
        <<e.art.theater<<'|'<<e.art.rate<<'|'<<e.cache.auxiliaryShpBytesCacheHits<<'|'
        <<e.cache.auxiliaryShpBytesCacheMisses<<'|'<<e.cache.auxiliaryShpBytesCacheMissMs<<'\n';
    std::map<std::string,const RuntimeAuxiliaryShpBytesCacheEntry*> rows;
    for(const auto& [key,value]:e.cache.auxiliaryShpBytesByImage) rows.emplace(key,&value);
    for(const auto& [key,p]:rows) {
        const auto& v=*p;out<<std::quoted(key)<<' '<<v.loadAttempted<<' '<<v.loaded<<' '
            <<v.sourceFingerprint<<' '<<std::quoted(v.resolvedImageId)<<' '<<std::quoted(v.sourcePath)<<' '
            <<v.frameCount<<' '<<std::quoted(v.errorMessage)<<' '<<v.timingFrame0Attempted<<' '
            <<v.timingFrame0Valid<<' '<<std::quoted(v.timingFrame0Error)<<' ';
        for(auto b:v.bytes)out<<int(b)<<',';out<<'\n';
    }
    for(const auto& t:e.trace)out<<std::quoted(t)<<'\n';
    return out.str();
}
static int checks=0,states=0,faults=0,failures=0;
static void check(bool value,const std::string& label) {++checks;if(!value){if(failures<6)std::cerr<<label<<'\n';++failures;}}
static void pair(int scenario,int fault) {
    Environment before(scenario),after(scenario);before.fault=after.fault=fault;
    ModelSource source(after);RuntimeAuxiliaryShpByteContext context{after.context.assetRootPath,after.context.structureRootPath};
    const std::string id=scenario%31==0 ? "" : "Image"+std::to_string(scenario%3);
    const ArtType* fallbackBefore=(scenario&8)!=0 ? &before.fallback : nullptr;
    const ArtType* fallbackAfter=(scenario&8)!=0 ? &after.fallback : nullptr;
    for(int round=0;round<3;++round) {
        RuntimeAuxiliaryShpBytesCacheEntry* oldResult=nullptr;RuntimeAuxiliaryShpByteLookup newResult;
        std::string oldKey;bool a=false,b=false;
        activeEnvironment=&before;try {oldResult=original::run(id,before.context,before.cache,fallbackBefore,oldKey);}catch(const std::runtime_error&){a=true;}
        activeEnvironment=&after;try {newResult=loadRuntimeAuxiliaryShpBytes(id,context,after.cache,source,fallbackAfter);}catch(const std::runtime_error&){b=true;}
        const auto label="byte scenario="+std::to_string(scenario)+" fault="+std::to_string(fault)+" round="+std::to_string(round);
        check(a==b,label+" exception");check((oldResult==nullptr)==(newResult.entry==nullptr),label+" result");
        check(snapshot(before)==snapshot(after),label+" full-cache-input-trace");
        if(oldResult&&newResult.entry)check(oldKey==newResult.candidateKey,label+" candidateKey");
        before.fault=after.fault=-1;
    }
    ++states;if(fault>0)++faults;
}
int main() {
    for(int s=0;s<256;++s){pair(s,-1);Environment e(s);activeEnvironment=&e;
        std::string key;const auto* f=(s&8)!=0?&e.fallback:nullptr;
        original::run(s%31==0?"":"Image"+std::to_string(s%3),e.context,e.cache,f,key);
        const int count=e.marks;for(int fault=1;fault<=count;++fault)pair(s,fault);
    }
    activeEnvironment=nullptr;
    std::cout<<"auxiliary-byte-cache checks="<<checks<<" states="<<states<<" injected-faults="<<faults
        <<" complete-owned-cache-inputs-trace=exact continuation=3 source-order-negative-and-partial-bytes=exact failures="<<failures<<'\n';
    return failures?1:0;
}
