#include "runtime/persistence/RuntimeSaveIdentity.h"
#include "runtime/config/RuntimeConfig.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINSOCKAPI_)
#error H039 identity contract included a native Windows surface
#endif
using runtime_save_text::trimAsciiLocal;
using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;

// Frozen original policy functions qualify the boundary, not a second
// production normalizer. Root still binds its actual original helpers.
namespace original_policy {
std::string pathString(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, errorCode);
    return (errorCode ? path.lexically_normal() : canonical).generic_string();
}
std::string normalizedRuntimeSaveMapIdentity(const std::string& rawPath) {
    const std::string trimmed = trimAsciiLocal(rawPath);
    if (trimmed.empty()) {
        return {};
    }
    return toLowerAsciiLocal(pathString(std::filesystem::path(trimmed)));
}
std::string trimAscii(std::string value) {
    const auto notSpace = [](unsigned char character) {
        return std::isspace(character) == 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace)
    );
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end()
    );
    return value;
}
std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}
std::string normalizeRuntimeCampaignSourceId(const std::string& sourceId) {
    const std::string normalized = lowerAscii(trimAscii(sourceId));
    if (normalized == "ra2" || normalized == "yr") {
        return normalized;
    }
    return normalized;
}
}
struct RecordingNormalization final : RuntimeSaveIdentityNormalization {
    mutable std::vector<std::string> trace;
    int failAt=0;
    void mark(const std::string& event) const {
        trace.push_back(event);
        if (failAt>0 && trace.size()==static_cast<std::size_t>(failAt))
            throw std::runtime_error("original normalization exception");
    }
    std::string mapIdentity(const std::string& path) const override {
        mark("map:"+path);return original_policy::normalizedRuntimeSaveMapIdentity(path);
    }
    std::string campaignSourceId(const std::string& source) const override {
        mark("source:"+source);return original_policy::normalizeRuntimeCampaignSourceId(source);
    }
};
struct IdentityFixture { RuntimeConfig config; bool campaignMode=false; };
namespace frozen_original {
bool decodeRuntimeSaveIdentityField(
    const IniFile& ini,
    const std::string& section,
    const std::string& encodedKey,
    const std::string& legacyKey,
    std::string& value,
    std::string& errorMessage
) {
    const std::string encoded = trimAsciiLocal(
        ini.getValue(section, encodedKey, "")
    );
    if (!encoded.empty()) {
        if (!iniHexDecode(encoded, value, 32U * 1024U)) {
            errorMessage = "invalid encoded save identity";
            return false;
        }
        value = trimAsciiLocal(value);
        return true;
    }
    value = trimAsciiLocal(ini.getValue(section, legacyKey, ""));
    return true;
}
bool validateRuntimeSaveOuterIdentity(
    const IdentityFixture& state,
    const IniFile& ini,
    int version,
    const RecordingNormalization& normalization,
    std::string& errorMessage
) {
    if (version < 13) {
        return true;
    }
    // Portable multiplayer v15 binds map/content/setup identity in its
    // descriptor and [MultiplayerSnapshot] envelope. It deliberately never
    // carries a process-local filesystem path.
    if (version == 15) {
        if (state.campaignMode
            || ini.getSection("MultiplayerSnapshot") == nullptr) {
            errorMessage = "invalid multiplayer snapshot identity envelope";
            return false;
        }
        return true;
    }

    const std::string activeMap = trimAsciiLocal(
        state.config.defaultMapPath
    );
    if (activeMap.empty()) {
        errorMessage = "active map identity is unavailable";
        return false;
    }
    std::string savedMap;
    if (!frozen_original::decodeRuntimeSaveIdentityField(
            ini,
            "RuntimeSave",
            "MapHex",
            "Map",
            savedMap,
            errorMessage
        )) {
        return false;
    }
    if (normalization.mapIdentity(savedMap)
        != normalization.mapIdentity(activeMap)) {
        errorMessage = "save belongs to a different map";
        return false;
    }

    const IniFile::Section* campaignSection = ini.getSection("Campaign");
    if (!state.campaignMode) {
        if (campaignSection != nullptr) {
            errorMessage = "save is a campaign mission";
            return false;
        }
        return true;
    }

    const std::string savedMode = toUpperAsciiLocal(trimAsciiLocal(
        ini.getValue("Campaign", "Mode", "")
    ));
    if (campaignSection == nullptr || savedMode != "CAMPAIGNMISSION") {
        errorMessage = "save is not a campaign mission";
        return false;
    }

    const std::string activeCampaign = trimAsciiLocal(
        state.config.campaignId
    );
    const std::string activeMission = trimAsciiLocal(
        state.config.campaignMissionId
    );
    const std::string activeSource = normalization.campaignSourceId(
        state.config.campaignSourceId
    );
    if (activeCampaign.empty() || activeMission.empty()) {
        errorMessage = "active campaign identity is unavailable";
        return false;
    }
    std::string savedCampaign;
    std::string savedMission;
    std::string savedSource;
    if (!frozen_original::decodeRuntimeSaveIdentityField(
            ini,
            "Campaign",
            "CampaignHex",
            "Campaign",
            savedCampaign,
            errorMessage
        )
        || !frozen_original::decodeRuntimeSaveIdentityField(
            ini,
            "Campaign",
            "MissionHex",
            "Mission",
            savedMission,
            errorMessage
        )) {
        return false;
    }
    if (!frozen_original::decodeRuntimeSaveIdentityField(
            ini,
            "Campaign",
            "SourceIdHex",
            "SourceId",
            savedSource,
            errorMessage
        )) {
        return false;
    }
    if (!savedSource.empty()
        && normalization.campaignSourceId(savedSource) != activeSource) {
        errorMessage = "save belongs to a different campaign data source";
        return false;
    }
    if (toLowerAsciiLocal(savedCampaign)
            != toLowerAsciiLocal(activeCampaign)
        || toLowerAsciiLocal(savedMission)
            != toLowerAsciiLocal(activeMission)) {
        errorMessage = "save belongs to a different campaign mission";
        return false;
    }
    return true;
}
}
namespace {
int checks=0, observations=0, scenarios=0;
void require(bool condition,const char* why) {
    ++checks;if (!condition) throw std::runtime_error(why);
}
IniFile ini(const std::string& text) {
    IniFile value;require(value.loadFromString(text),"fixture INI admission");return value;
}
std::string fields(const std::string& map="fixture-map.map") {
    return "[RuntimeSave]\nMap="+map+"\n";
}
std::string campaign(const std::string& id="CampaignA",const std::string& mission="Mission01",
                     const std::string& source="YR") {
    return "[Campaign]\nMode=CampaignMission\nCampaign="+id+"\nMission="+mission+"\nSourceId="+source+"\n";
}
RuntimeSaveActiveIdentity borrow(const IdentityFixture& s) {
    return {s.campaignMode,s.config.defaultMapPath,s.config.campaignId,
        s.config.campaignMissionId,s.config.campaignSourceId};
}
struct Observation { bool result=false,threw=false;std::string error="unchanged-error"; };
Observation observe(const IdentityFixture& state,const IniFile& document,int version,
                    RecordingNormalization& normalization,bool original) {
    Observation value;
    try {
        value.result=original ? frozen_original::validateRuntimeSaveOuterIdentity(
            state,document,version,normalization,value.error)
            : ::validateRuntimeSaveOuterIdentity(borrow(state),document,version,normalization,value.error);
    } catch (const std::runtime_error& ex) {
        require(std::string(ex.what())=="original normalization exception","exception identity");
        value.threw=true;
    }
    return value;
}
void compare(const IdentityFixture& state,const std::string& text,int version,
             const std::string& expected="",int expectedResult=-1) {
    const IniFile document=ini(text);++scenarios;
    for (int failAt=0;failAt!=6;++failAt) {
        RecordingNormalization oldService,newService;
        oldService.failAt=failAt;newService.failAt=failAt;
        const Observation old=observe(state,document,version,oldService,true);
        const Observation current=observe(state,document,version,newService,false);
        require(std::tie(old.result,old.threw,old.error)==std::tie(current.result,current.threw,current.error),"original identity outcome/error/exception");
        require(oldService.trace==newService.trace,"original normalizer order/short circuit");
        if (failAt==0 && expectedResult>=0) {
            require(!current.threw && current.result==(expectedResult!=0),"independent expected admission");
            require(current.error==expected,"independent expected error priority/stale success");
        }
        ++observations;
    }
}
void decoderContracts() {
    const std::vector<std::string> encodings{"", " ", "20", "204120", "00", "0", "GG", "4142zz", "00ff41",
        iniHexEncode(std::string(32768,'x')),iniHexEncode(std::string(32769,'x'))};
    const std::vector<std::string> legacy{"", "  legacy value  ", "YR", "a/b/../c"};
    for (const auto& encoded:encodings) for (const auto& old:legacy) {
        const IniFile doc=ini("[S]\nHex="+encoded+"\nLegacy="+old+"\n");
        std::string a="prior-output",b=a,ea="stale-error",eb=ea;
        const bool ra=frozen_original::decodeRuntimeSaveIdentityField(doc,"S","Hex","Legacy",a,ea);
        const bool rb=::decodeRuntimeSaveIdentityField(doc,"S","Hex","Legacy",b,eb);
        require(std::tie(ra,a,ea)==std::tie(rb,b,eb),"decoder limit/partial mutation/priority/error");
        if (trimAsciiLocal(encoded).empty()) require(rb && b==trimAsciiLocal(old) && eb=="stale-error","empty hex legacy and stale error");
        if (encoded=="204120") require(rb && b=="A","encoded wins over conflicting legacy");
        if (encoded=="4142zz") require(!rb && b=="prior-output" && eb=="invalid encoded save identity","original invalid decoder leaves output unchanged");
        ++observations;
    }
}
void identityContracts() {
    IdentityFixture s;s.config.defaultMapPath=" fixture-map.map ";
    s.config.campaignId=" CampaignA ";s.config.campaignMissionId=" Mission01 ";s.config.campaignSourceId=" YR ";
    for (int version:{-1,0,1,12}) {
        auto missing=s;missing.campaignMode=true;missing.config.defaultMapPath.clear();
        compare(missing,"",version,"unchanged-error",1);
    }
    compare(s,"",15,"invalid multiplayer snapshot identity envelope",0);
    compare(s,"[MultiplayerSnapshot]\nMarker=1\n",15,"unchanged-error",1);
    auto c=s;c.campaignMode=true;
    compare(c,"[MultiplayerSnapshot]\nMarker=1\n",15,"invalid multiplayer snapshot identity envelope",0);
    const std::vector<int> versions{13,14,16}; // Caller, not this original helper, rejects >15.
    for (int v:versions) {
        auto absent=s;absent.config.defaultMapPath=" ";
        compare(absent,fields()+"MapHex=GG\n",v,"active map identity is unavailable",0);
        compare(s,fields()+"MapHex=GG\n",v,"invalid encoded save identity",0);
        compare(s,fields("other.map"),v,"save belongs to a different map",0);
        compare(s,fields(),v,"unchanged-error",1);
        compare(s,fields("unused.map")+"MapHex="+iniHexEncode("fixture-map.map")+"\n",v,"unchanged-error",1);
        compare(s,fields("./unused/../fixture-map.map"),v,"unchanged-error",1);
        compare(s,fields()+campaign(),v,"save is a campaign mission",0);
        compare(c,fields(),v,"save is not a campaign mission",0);
        compare(c,fields()+"[Campaign]\nMode=wrong\n",v,"save is not a campaign mission",0);
        compare(c,fields()+campaign("campaigna","MISSION01"," yr "),v,"unchanged-error",1);
        compare(c,fields()+campaign("wrong","wrong","RA2"),v,"save belongs to a different campaign data source",0);
        compare(c,fields()+campaign("wrong","wrong",""),v,"save belongs to a different campaign mission",0);
        compare(c,fields()+campaign("CampaignA","Mission01",""),v,"unchanged-error",1);
        auto noId=c;noId.config.campaignId=" ";
        compare(noId,fields()+campaign()+"CampaignHex=GG\n",v,"active campaign identity is unavailable",0);
        compare(c,fields()+campaign()+"CampaignHex=GG\nMissionHex=0\n",v,"invalid encoded save identity",0);
        compare(c,fields()+campaign()+"MissionHex=GG\nSourceIdHex=0\n",v,"invalid encoded save identity",0);
        compare(c,fields()+campaign()+"SourceIdHex=GG\n",v,"invalid encoded save identity",0);
    }
    for (const auto& savedMap:std::vector<std::string>{"", " fixture-map.map ", "./fixture-map.map", "other.map"})
      for (const auto& source:std::vector<std::string>{"", "YR", " ra2 ", "custom"})
       for (const auto& savedId:std::vector<std::string>{"CampaignA", " campaigna ", "wrong"})
        for (int v:{12,13,14,15})
            compare(c,fields(savedMap)+campaign(savedId,"Mission01",source),v);
    RecordingNormalization normal;
    const auto view=borrow(s);s.config.defaultMapPath="changed.map";
    require(&view.defaultMapPath==&s.config.defaultMapPath && view.defaultMapPath=="changed.map","actual live string borrowing");
    require(&view.campaignMode==&s.campaignMode,"actual live mode borrowing");
    require(normal.campaignSourceId(" Ra2 ")=="ra2" && normal.campaignSourceId(" CustomID ")=="customid","original ASCII source policy");
    const auto traceSize=normal.trace.size();
    require(traceSize==2,"synchronous independent normalizer qualification");
}
}
int main() {
    try {
        decoderContracts();identityContracts();
        std::cout<<"H039 identity contract passed checks="<<checks<<" observations="<<observations<<" scenarios="<<scenarios<<" skipped=0\n";
        return 0;
    } catch (const std::exception& ex) { std::cerr<<ex.what()<<" checks="<<checks<<"\n";return 1; }
}
