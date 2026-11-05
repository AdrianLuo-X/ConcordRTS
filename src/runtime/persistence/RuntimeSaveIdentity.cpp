#include "runtime/persistence/RuntimeSaveIdentity.h"
#include "core/io/IniFile.h"
#include "core/io/RuntimeSaveTextFields.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"

using runtime_save_text::trimAsciiLocal;
using runtime_save_text::toLowerAsciiLocal;
using runtime_authority_hash::toUpperAsciiLocal;

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
    const RuntimeSaveActiveIdentity& state,
    const IniFile& ini,
    int version,
    const RuntimeSaveIdentityNormalization& normalization,
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
        state.defaultMapPath
    );
    if (activeMap.empty()) {
        errorMessage = "active map identity is unavailable";
        return false;
    }
    std::string savedMap;
    if (!decodeRuntimeSaveIdentityField(
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
        state.campaignId
    );
    const std::string activeMission = trimAsciiLocal(
        state.campaignMissionId
    );
    const std::string activeSource = normalization.campaignSourceId(
        state.campaignSourceId
    );
    if (activeCampaign.empty() || activeMission.empty()) {
        errorMessage = "active campaign identity is unavailable";
        return false;
    }
    std::string savedCampaign;
    std::string savedMission;
    std::string savedSource;
    if (!decodeRuntimeSaveIdentityField(
            ini,
            "Campaign",
            "CampaignHex",
            "Campaign",
            savedCampaign,
            errorMessage
        )
        || !decodeRuntimeSaveIdentityField(
            ini,
            "Campaign",
            "MissionHex",
            "Mission",
            savedMission,
            errorMessage
        )) {
        return false;
    }
    if (!decodeRuntimeSaveIdentityField(
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
