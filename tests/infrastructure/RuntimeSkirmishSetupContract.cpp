#include "ui/skirmish/RuntimeSkirmishSetup.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <tuple>

namespace {
int checks=0, failures=0;
void check(bool value, const char* name) { ++checks; if(!value) { ++failures; std::cerr<<name<<'\n'; } }
auto authority(const RuntimeConfig& c) {
    return std::tuple(c.fixedTickHz,c.multiplayerActive,c.multiplayerSetupHash,
        c.multiplayerCanonicalMapId,c.multiplayerMapContentFingerprint,
        c.multiplayerCooperativeScenarioFingerprint,c.multiplayerTimelineEpoch,
        c.simulationRandomSeed,c.runtimeReplayEventOutputPath);
}
auto preferences(const RuntimeConfig& c) {
    return std::tuple(c.configPath,c.resourceRootPath,c.localDataRootPath,c.modRootPath,
        c.windowWidth,c.windowHeight,c.targetFps,c.scrollRate,c.visualDetail,
        c.tooltipsEnabled,c.targetLinesEnabled,c.scrollCoastingEnabled,c.seeHiddenObjects,
        c.musicEnabled,c.soundEffectsEnabled,c.musicVolume,c.soundVolume,c.voiceVolume,
        c.keyBindings,c.keyboardReferenceLayout,c.techLevel,c.showMainMenu,c.showSkirmishMenu,
        c.campaignId,c.campaignMissionId,c.campaignDifficulty,c.campaignPlayerHouse,
        c.campaignNextMissionId,c.campaignIntroVideoPath,c.campaignPostScoreVideoPath,
        c.campaignBriefingKey,c.campaignSavePath);
}
}
int main() {
    for(bool online : {false,true}) for(int speed : {-3,0,4,6,9}) for(int size : {0,1,8}) {
        RuntimeConfig c; c.multiplayerActive=online;c.fixedTickHz=47;
        c.multiplayerSetupHash=13;c.multiplayerTimelineEpoch=11;c.simulationRandomSeed=42;
        c.multiplayerCanonicalMapId="room/map";c.multiplayerMapContentFingerprint=33;
        c.multiplayerCooperativeScenarioFingerprint=77;c.runtimeReplayEventOutputPath="keep/replay";
        c.configPath="private/settings";c.modRootPath="mod";c.keyBindings={{"a",9}};
        c.campaignId="untouched-id";c.campaignSavePath="untouched-path";
        c.skirmishLoadSavePath="old-skirmish";c.campaignLoadSavePath="old-campaign";c.campaignSourceId="yr";
        c.playerSlots.resize(8);c.playerSlots[0].peerId="old";c.playerSlots[0].local=true;
        const auto oldAuthority=authority(c);const auto oldPreferences=preferences(c);
        RuntimeSkirmishSelection s;s.accepted=true;s.mapPath="map/中文 spaced.map";
        s.mapStartingPointLimit=8;s.gameMode="FreeForAll";s.gameModeRulesOverride="MPFreeForAllMD.ini";
        s.playerCountry="YuriCountry";s.playerColor="Purple";s.playerCountryDisplayName=L"尤里";
        s.playerCountryBrief=L"brief\nsecond line";s.loadingScreenAssetId="load";
        s.startingCredits=30000;s.unitCount=7;s.gameSpeed=speed;s.shortGame=false;
        s.mcvRepacks=true;s.crates=true;s.superweapons=false;s.buildOffAlly=true;
        s.computerAiEnabled=true;s.noCooldown=true;
        for(int i=0;i<size;++i) { RuntimeSkirmishPlayerSlot p;
            p.enabled=i!=7;p.human=i==0;p.name="slot"+std::to_string(i);
            p.country=i==0?"Observer":"Random";p.color=std::to_string(i);p.team=std::to_string(i%4);
            p.difficulty="Hard";p.startPosition=i==0?-4:i;s.slots.push_back(p);
        }
        applyRuntimeSkirmishSelection(c,s);
        check(authority(c)==oldAuthority,"fixed authority changed");
        check(preferences(c)==oldPreferences,"unrelated config changed");
        check(c.skirmishLoadSavePath.empty()&&c.campaignLoadSavePath.empty()&&c.campaignSourceId.empty(),"old save authority retained");
        check(c.defaultMapPath==s.mapPath&&c.mapStartingPointLimit==8&&c.gameMode==s.gameMode&&c.gameModeRulesOverride==s.gameModeRulesOverride,"map/mode lost");
        check(c.playerCountry==s.playerCountry&&c.playerColor==s.playerColor&&c.loadingCountryDisplayName==s.playerCountryDisplayName&&c.loadingCountryBrief==s.playerCountryBrief&&c.loadingScreenAssetId=="load","loading identity lost");
        check(c.startingCredits==30000&&c.startingUnits==7&&c.gameSpeed==std::clamp(speed,0,6),"presentation options lost");
        check(!c.shortGame&&c.mcvRepacks&&c.crates&&!c.superweapons&&c.buildOffAlly&&c.computerAiEnabled,"game options lost");
        check(c.noCooldown==!online,"offline noCooldown guard lost");
        check(c.playerSlots.size()==s.slots.size(),"slot count changed");
        for(std::size_t i=0;i<s.slots.size();++i) {const auto& a=c.playerSlots[i];const auto& b=s.slots[i];
            check(a.enabled==b.enabled&&a.human==b.human&&a.name==b.name&&a.country==b.country&&a.color==b.color&&a.team==b.team&&a.difficulty==b.difficulty,"seat projection changed");
            check(a.startPosition==std::max(0,b.startPosition)&&a.peerId.empty()&&!a.local,"seat authority leakage");
        }
    }
    std::cout<<"Skirmish setup: checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
