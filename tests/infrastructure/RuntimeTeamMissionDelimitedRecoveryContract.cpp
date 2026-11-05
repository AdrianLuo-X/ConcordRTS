#include "simulation/triggers/RuntimeTriggerPersistence.h"
#include "core/io/IniFile.h"
#include "runtime/persistence/RuntimeSaveDocument.h"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINSOCKAPI_)
#error Mission recovery standard producer included a Windows surface
#endif
namespace original_ini {
char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}
bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}
bool isPackedMapSection(const std::string& sectionName) {
    return equalsAsciiIgnoreCase(sectionName, "IsoMapPack5")
        || equalsAsciiIgnoreCase(sectionName, "OverlayPack")
        || equalsAsciiIgnoreCase(sectionName, "OverlayDataPack")
        || equalsAsciiIgnoreCase(sectionName, "PreviewPack");
}
class FrozenIni {
public:
    using KeyValue = std::pair<std::string, std::string>;
    using Section = std::vector<KeyValue>;

    // Compatibility entry: the narrow OS filename encoding is unchanged.
    // It is independent of the INI payload's encoding.
    bool loadFromFile(const std::string& path);
    // Native filesystem path (UTF-16 on Windows, native bytes on POSIX).
    // UTF-8 callers construct the path explicitly from u8string/u8string_view.
    // Content still uses the original text-mode stream and byte parser.
    bool loadFromPath(const std::filesystem::path& path);
    // A consumer may retain its original diagnostic spelling/encoding. This
    // label is never used to open the file; the one-argument form reports UTF-8.
    bool loadFromPath(const std::filesystem::path& path, std::string_view diagnosticPath);
    bool loadFromString(const std::string& text);
    void appendFrom(const FrozenIni& overlay);

    const Section* getSection(const std::string& sectionName) const;
    std::vector<std::string> getSectionNames() const;

    std::string getValue(
        const std::string& sectionName,
        const std::string& key,
        const std::string& defaultValue = ""
    ) const;

private:
    std::unordered_map<std::string, Section> sections_;

    static std::string trim(const std::string& text);
    static std::string removeComment(const std::string& line, bool stripSlashComments = true);
};
std::string FrozenIni::trim(const std::string& text) {
    const char* whitespace = " \t\r\n";

    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }

    const auto end = text.find_last_not_of(whitespace);
    return text.substr(start, end - start + 1);
}
std::string FrozenIni::removeComment(const std::string& line, bool stripSlashComments) {
    const auto semicolonPos = line.find(';');

    std::size_t commentPos = std::string::npos;

    if (semicolonPos != std::string::npos) {
        commentPos = semicolonPos;
    }

    const auto slashPos = stripSlashComments ? line.find("//") : std::string::npos;
    if (slashPos != std::string::npos) {
        if (commentPos == std::string::npos || slashPos < commentPos) {
            commentPos = slashPos;
        }
    }

    if (commentPos == std::string::npos) {
        return line;
    }

    return line.substr(0, commentPos);
}
bool FrozenIni::loadFromString(const std::string& text) {
    sections_.clear();

    std::istringstream file(text);
    std::string currentSection;
    std::string line;

    while (std::getline(file, line)) {
        const std::string sectionLine = trim(removeComment(line));

        if (sectionLine.empty()) {
            continue;
        }

        if (sectionLine.front() == '[' && sectionLine.back() == ']') {
            const std::string parsedSection =
                trim(sectionLine.substr(1, sectionLine.size() - 2));
            currentSection = parsedSection;
            for (const auto& [existingName, values] : sections_) {
                (void)values;
                if (equalsAsciiIgnoreCase(existingName, parsedSection)) {
                    currentSection = existingName;
                    break;
                }
            }
            sections_[currentSection];
            continue;
        }

        line = trim(removeComment(line, !isPackedMapSection(currentSection)));
        const auto equalsPos = line.find('=');
        if (equalsPos == std::string::npos) {
            continue;
        }

        if (currentSection.empty()) {
            continue;
        }

        std::string key = trim(line.substr(0, equalsPos));
        std::string value = trim(line.substr(equalsPos + 1));

        sections_[currentSection].push_back({key, value});
    }

    return true;
}
const FrozenIni::Section* FrozenIni::getSection(const std::string& sectionName) const {
    const auto it = sections_.find(sectionName);
    if (it != sections_.end()) {
        return &it->second;
    }
    for (const auto& [existingName, section] : sections_) {
        if (equalsAsciiIgnoreCase(existingName, sectionName)) {
            return &section;
        }
    }
    return nullptr;
}
std::vector<std::string> FrozenIni::getSectionNames() const {
    std::vector<std::string> names;
    names.reserve(sections_.size());

    for (const auto& [sectionName, values] : sections_) {
        names.push_back(sectionName);
    }

    return names;
}
std::string FrozenIni::getValue(
    const std::string& sectionName,
    const std::string& key,
    const std::string& defaultValue
) const {
    const Section* section = getSection(sectionName);
    if (section == nullptr) {
        return defaultValue;
    }

    // 从后往前找。这样如果同一个 key 出现多次，后面的覆盖前面的。
    for (auto it = section->rbegin(); it != section->rend(); ++it) {
        if (equalsAsciiIgnoreCase(it->first, key)) {
            return it->second;
        }
    }

    return defaultValue;
}
}
static_assert(sizeof(IniFile)==sizeof(original_ini::FrozenIni));
static_assert(alignof(IniFile)==alignof(original_ini::FrozenIni));

template<class Left,class Right> bool sameSections(const Left& a,const Right& b) {
    const auto names=a.getSectionNames();
    if(names!=b.getSectionNames())return false;
    for(const auto& name:names) {
        if(*a.getSection(name)!=*b.getSection(name))return false;
        for(const auto& entry:*a.getSection(name))
            if(a.getValue(name,entry.first,"missing")!=b.getValue(name,entry.first,"missing"))return false;
    }
    return a.getValue("missing","missing","fallback")==b.getValue("missing","missing","fallback");
}
int genericChecks=0;
void generalContracts() {
    const std::vector<std::string> sections{"General","TeamMissions","teammissions","TEAMMISSIONS","TeamMissionsArchive","IsoMapPack5","OverlayPack","OverlayDataPack","PreviewPack"};
    const std::vector<std::string> keys{"0"," 1 ","01","+0","-1","Count","NextInstanceId",""};
    const std::vector<std::string> values{"value","a;b","a//b"," a;b//c ","",";comment","//comment"};
    for(const auto& section:sections)for(const auto& key:keys)for(const auto& value:values) {
        const std::string text=";header\n["+section+"] ;section comment\n"+key+"="+value+"\n[GENERAL]\nOther=ok ;ordinary\n";
        IniFile current,emptyOptions;original_ini::FrozenIni original;
        if(!current.loadFromString(text) || !emptyOptions.loadFromString(text,{}) || !original.loadFromString(text)
            || !sameSections(current,original) || !sameSections(emptyOptions,original))throw std::runtime_error("General/empty options differ from frozen full original parser");
        ++genericChecks;
    }
    const std::string text="[TeamMissions]\nCount=2;count comment\nNextInstanceId=5//id comment\n0=record;cell//literal\n+0=other;comment\n[Other]\n0=other;comment\n[TEAMMISSIONS]\n1=second//literal;cell\n";
    IniFile document;if(!parseRuntimeSaveDocument(text,document))throw std::runtime_error("typed document parser");
    if(document.getValue("TeamMissions","0","")!="record;cell//literal"
       || document.getValue("TeamMissions","1","")!="second//literal;cell"
       || document.getValue("TeamMissions","Count","")!="2"
       || document.getValue("TeamMissions","NextInstanceId","")!="5"
       || document.getValue("TeamMissions","+0","")!="other"
       || document.getValue("Other","0","")!="other")throw std::runtime_error("literal policy escaped actual numeric section scope");
    ++genericChecks;
}

// This is one actual standard production domain, not a complete World/tick.
struct Operations final : RuntimeTriggerPersistenceOperations {
    bool objectExists(int id) const override { return id>=1 && id<=8; }
    const RuntimeTeamType* findTeam(const std::string&) const override { return nullptr; }
    std::string resolveHouse(const std::string& name) const override { return name; }
    void applyTechLevel(const std::string&,int) override {}
    bool unitTypeExists(const std::string&) const override { return false; }
};
bool cellsEqual(const std::map<int,RuntimeCellPoint>& a,const std::map<int,RuntimeCellPoint>& b) {
    if(a.size()!=b.size())return false;
    auto left=a.begin(),right=b.begin();
    for(;left!=a.end();++left,++right)
        if(left->first!=right->first || left->second.x!=right->second.x || left->second.y!=right->second.y)return false;
    return true;
}
auto scalarFields(const RuntimeTeamMissionState& m) {
    return std::tie(m.instanceId,m.teamTypeId,m.scriptId,m.objectIds,m.stepIndex,m.stepStartedTick,m.stepIssued,
        m.targetObjectId,m.targetCell.x,m.targetCell.y,m.completed,m.sourceAiTriggerId,m.sourceAiBuildId,
        m.aiOutcomeReported,m.achievedGreatSuccess,m.targetInteractionCompleted,m.regrouping,m.regroupCell.x,
        m.regroupCell.y,m.regroupStartedTick,m.status);
}
int main(int argc,char** argv) try {
    if(argc>2)return 2;
    std::filesystem::path output;
    if(argc==2) { output=std::filesystem::path(argv[1]);std::filesystem::create_directories(output); }
    generalContracts();
    int cases=0,mismatches=0,matching=0;
    Operations operations;
    for(int returns=0;returns<3;++returns)for(int regroup=0;regroup<3;++regroup)for(int status=0;status<2;++status) {
        RuntimeTriggerState source,restored;
        RuntimeScriptType script;script.id="SCRIPT";script.steps={{1,2}};source.scripts["SCRIPT"]=script;restored.scripts=source.scripts;
        RuntimeTeamMissionState mission;mission.instanceId=4;mission.teamTypeId="TEAM";mission.scriptId="SCRIPT";
        mission.objectIds={1,2};mission.stepIndex=2;mission.stepStartedTick=7;mission.stepIssued=true;
        mission.targetObjectId=5;mission.targetCell={11,12};mission.completed=false;
        mission.sourceAiTriggerId="TRIGGER";mission.sourceAiBuildId=6;mission.aiOutcomeReported=true;
        mission.achievedGreatSuccess=true;mission.targetInteractionCompleted=true;mission.regrouping=true;
        mission.regroupCell={15,16};mission.regroupStartedTick=9;
        mission.status=status==0?"moving normally":"moving; // status delimiter";
        for(int n=0;n<returns;++n)mission.transportReturnCells[n+1]={20+n,30+n};
        for(int n=0;n<regroup;++n)mission.regroupDestinations[n+1]={40+n,50+n};
        source.teamMissions[4]=mission;source.nextTeamMissionInstanceId=5;
        std::ostringstream writer;writer<<"[RuntimeSave]\nVersion=14\n";
        writeRuntimeTeamMissionState(source,writer);
        const std::string payload=writer.str();IniFile ini;
        const bool parsed=parseRuntimeSaveDocument(payload,ini);
        if(!parsed) { std::cerr<<"document parse failed\n";return 2; }
        bool campaignMode=false;restoreRuntimeTeamMissionState(restored,ini,14,campaignMode,operations);
        std::vector<std::string> differences;
        const auto found=restored.teamMissions.find(4);
        if(restored.teamMissions.size()!=1 || found==restored.teamMissions.end())differences.push_back("mission missing");
        else {
            if(scalarFields(mission)!=scalarFields(found->second))differences.push_back("all scalar/vector fields");
            if(!cellsEqual(mission.transportReturnCells,found->second.transportReturnCells))differences.push_back("transportReturnCells");
            if(!cellsEqual(mission.regroupDestinations,found->second.regroupDestinations))differences.push_back("regroupDestinations");
        }
        if(restored.nextTeamMissionInstanceId!=source.nextTeamMissionInstanceId)differences.push_back("next id");
        std::ostringstream after;after<<"[RuntimeSave]\nVersion=14\n";writeRuntimeTeamMissionState(restored,after);
        if(after.str()!=payload)differences.push_back("rewritten payload bytes");
        std::ostringstream report;report<<"case="<<cases<<" returns="<<returns<<" regroup="<<regroup<<" statusDelimiter="<<status
            <<" missionCount="<<restored.teamMissions.size()<<" mismatches="<<differences.size();
        for(const auto& field:differences)report<<" ["<<field<<"]";
        report<<'\n';
        std::cout<<report.str();
        if(!output.empty()) {
            std::ofstream beforeFile(output/("case-"+std::to_string(cases)+"-original.ini"),std::ios::binary);beforeFile<<payload;beforeFile.close();
            std::ofstream afterFile(output/("case-"+std::to_string(cases)+"-restored.ini"),std::ios::binary);afterFile<<after.str();afterFile.close();
            std::ofstream reportFile(output/("case-"+std::to_string(cases)+"-result.txt"),std::ios::binary);reportFile<<report.str();reportFile.close();
            if(!beforeFile || !afterFile || !reportFile)return 2;
            IniFile fileDocument;
            if(!loadRuntimeSaveDocument((output/("case-"+std::to_string(cases)+"-original.ini")).string(),fileDocument)
                || !sameSections(fileDocument,ini))throw std::runtime_error("same typed file and memory document");
            RuntimeTriggerState fileRestored;fileRestored.scripts=source.scripts;
            restoreRuntimeTeamMissionState(fileRestored,fileDocument,14,campaignMode,operations);
            const auto fromFile=fileRestored.teamMissions.find(4);
            if(fromFile==fileRestored.teamMissions.end() || scalarFields(mission)!=scalarFields(fromFile->second)
                || !cellsEqual(mission.transportReturnCells,fromFile->second.transportReturnCells)
                || !cellsEqual(mission.regroupDestinations,fromFile->second.regroupDestinations)
                || fileRestored.nextTeamMissionInstanceId!=source.nextTeamMissionInstanceId)
                throw std::runtime_error("all mission fields restored from real file");
            IniFile ordinaryFile;original_ini::FrozenIni ordinaryOriginal;
            if(!ordinaryFile.loadFromFile((output/("case-"+std::to_string(cases)+"-original.ini")).string())
                || !ordinaryOriginal.loadFromString(payload) || !sameSections(ordinaryFile,ordinaryOriginal))
                throw std::runtime_error("default file API differs from original general parsing");
            ++genericChecks;

        }
        ++cases;if(differences.empty())++matching;else ++mismatches;
    }
    std::cout<<"Mission recovery cases="<<cases<<" matching="<<matching<<" mismatching="<<mismatches<<" skipped=0\n";
    std::cout<<"General parser checks="<<genericChecks<<" original ABI size="<<sizeof(IniFile)<<" skipped=0\n";
    return mismatches==0?0:1;
}
catch(const std::exception& error) { std::cerr<<"Mission recovery failed: "<<error.what()<<'\n';return 1; }
