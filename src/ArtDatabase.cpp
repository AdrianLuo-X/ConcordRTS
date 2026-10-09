#include "ArtDatabase.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace {

std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );

    return value;
}

std::string trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::string canonicalArtKey(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(whitespace);
    std::string key;
    key.reserve(last - first + 1);
    for (std::size_t index = first; index <= last; ++index) {
        key.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(value[index]))
        ));
    }
    return key;
}

void readIndexedFireFlhValues(
    const IniFile& ini,
    const std::string& sectionName,
    const std::string& keyPrefix,
    std::vector<std::string>& output
) {
    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) {
        return;
    }

    const std::string normalizedPrefix = toLower(keyPrefix);
    constexpr const char* suffix = "flh";
    constexpr std::size_t suffixLength = 3;
    for (const IniFile::KeyValue& entry : *section) {
        const std::string normalizedKey = toLower(trim(entry.first));
        if (normalizedKey.size()
                <= normalizedPrefix.size() + suffixLength
            || normalizedKey.compare(
                0,
                normalizedPrefix.size(),
                normalizedPrefix
            ) != 0
            || normalizedKey.compare(
                normalizedKey.size() - suffixLength,
                suffixLength,
                suffix
            ) != 0) {
            continue;
        }

        const std::string indexToken = normalizedKey.substr(
            normalizedPrefix.size(),
            normalizedKey.size()
                - normalizedPrefix.size()
                - suffixLength
        );
        if (indexToken.empty()
            || !std::all_of(
                indexToken.begin(),
                indexToken.end(),
                [](unsigned char character) {
                    return std::isdigit(character) != 0;
                }
            )) {
            continue;
        }

        std::size_t oneBasedIndex = 0;
        try {
            oneBasedIndex = static_cast<std::size_t>(std::stoul(indexToken));
        } catch (...) {
            continue;
        }
        if (oneBasedIndex == 0 || oneBasedIndex > 256) {
            continue;
        }
        if (output.size() < oneBasedIndex) {
            output.resize(oneBasedIndex);
        }
        output[oneBasedIndex - 1] = trim(entry.second);
    }
}

std::string chooseArtId(const std::string& objectId, const std::string& image) {
    if (!image.empty()) {
        return image;
    }

    return objectId;
}

int readArtIntLocal(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    int defaultValue
) {
    const std::string value = ini.getValue(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return defaultValue;
    }
}

bool readArtBoolLocal(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    bool defaultValue
) {
    const std::string value = ini.getValue(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }

    const std::string normalized = toLower(value);
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        return false;
    }
    return defaultValue;
}

void addActiveAnimationLayer(
    const IniFile& ini,
    ArtType& art,
    const std::string& id,
    const std::string& suffix
) {
    const std::string key = "ActiveAnim" + suffix;
    const std::string image = ini.getValue(id, key, "");
    if (image.empty()) {
        return;
    }

    ArtType::ActiveAnimation animation;
    animation.key = key;
    animation.image = image;
    animation.damagedImage =
        ini.getValue(id, key + "Damaged", "");
    animation.offsetX =
        readArtIntLocal(ini, id, key + "X", 0);
    animation.offsetY =
        readArtIntLocal(ini, id, key + "Y", 0);
    animation.ySort =
        readArtIntLocal(ini, id, key + "YSort", 0);
    animation.zAdjust =
        readArtIntLocal(ini, id, key + "ZAdjust", 0);
    animation.powered =
        readArtBoolLocal(ini, id, key + "Powered", true);
    animation.poweredLight =
        readArtBoolLocal(ini, id, key + "PoweredLight", false);
    animation.poweredSpecial =
        readArtBoolLocal(ini, id, key + "PoweredSpecial", false);
    animation.poweredEffect =
        readArtBoolLocal(ini, id, key + "PoweredEffect", false);
    art.activeAnimations.push_back(animation);
}

void addNamedAnimationLayer(
    const IniFile& ini,
    ArtType& art,
    const std::string& id,
    const std::string& key
) {
    const std::string image = ini.getValue(id, key, "");
    if (image.empty()) {
        return;
    }

    ArtType::ActiveAnimation animation;
    animation.key = key;
    animation.image = image;
    animation.damagedImage =
        ini.getValue(id, key + "Damaged", "");
    animation.offsetX =
        readArtIntLocal(ini, id, key + "X", 0);
    animation.offsetY =
        readArtIntLocal(ini, id, key + "Y", 0);
    animation.ySort =
        readArtIntLocal(ini, id, key + "YSort", 0);
    animation.zAdjust =
        readArtIntLocal(ini, id, key + "ZAdjust", 0);
    animation.powered =
        readArtBoolLocal(ini, id, key + "Powered", true);
    animation.poweredLight =
        readArtBoolLocal(ini, id, key + "PoweredLight", false);
    animation.poweredSpecial =
        readArtBoolLocal(ini, id, key + "PoweredSpecial", false);
    animation.poweredEffect =
        readArtBoolLocal(ini, id, key + "PoweredEffect", false);
    art.activeAnimations.push_back(animation);
}

void addSpecialAnimationLayer(
    const IniFile& ini,
    ArtType& art,
    const std::string& id,
    const std::string& key
) {
    const std::string image = ini.getValue(id, key, "");
    if (image.empty()) {
        return;
    }

    ArtType::ActiveAnimation animation;
    animation.key = key;
    animation.image = image;
    animation.damagedImage =
        ini.getValue(id, key + "Damaged", "");
    animation.offsetX =
        readArtIntLocal(ini, id, key + "X", 0);
    animation.offsetY =
        readArtIntLocal(ini, id, key + "Y", 0);
    animation.ySort =
        readArtIntLocal(ini, id, key + "YSort", 0);
    animation.zAdjust =
        readArtIntLocal(ini, id, key + "ZAdjust", 0);
    animation.powered =
        readArtBoolLocal(ini, id, key + "Powered", true);
    animation.poweredLight =
        readArtBoolLocal(ini, id, key + "PoweredLight", false);
    animation.poweredSpecial =
        readArtBoolLocal(ini, id, key + "PoweredSpecial", false);
    animation.poweredEffect =
        readArtBoolLocal(ini, id, key + "PoweredEffect", false);
    art.specialAnimations.push_back(animation);
}

void addAuxiliaryAnimationLayer(
    const IniFile& ini,
    ArtType& art,
    const std::string& id,
    const std::string& key
) {
    const std::string image = ini.getValue(id, key, "");
    if (image.empty()) {
        return;
    }

    ArtType::ActiveAnimation animation;
    animation.key = key;
    animation.image = image;
    animation.damagedImage =
        ini.getValue(id, key + "Damaged", "");
    animation.offsetX =
        readArtIntLocal(ini, id, key + "X", 0);
    animation.offsetY =
        readArtIntLocal(ini, id, key + "Y", 0);
    animation.ySort =
        readArtIntLocal(ini, id, key + "YSort", 0);
    animation.zAdjust =
        readArtIntLocal(ini, id, key + "ZAdjust", 0);
    animation.powered =
        readArtBoolLocal(ini, id, key + "Powered", true);
    animation.poweredLight =
        readArtBoolLocal(ini, id, key + "PoweredLight", false);
    animation.poweredSpecial =
        readArtBoolLocal(ini, id, key + "PoweredSpecial", false);
    animation.poweredEffect =
        readArtBoolLocal(ini, id, key + "PoweredEffect", false);
    art.auxiliaryAnimations.push_back(animation);
}

bool hasIniValue(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    return !ini.getValue(section, key, "").empty();
}

int readFirstArtInt(
    const IniFile& ini,
    const std::string& section,
    const std::string& firstKey,
    const std::string& secondKey,
    int defaultValue
) {
    if (hasIniValue(ini, section, firstKey)) {
        return readArtIntLocal(ini, section, firstKey, defaultValue);
    }

    return readArtIntLocal(ini, section, secondKey, defaultValue);
}

ArtType::SequenceFrame readSequenceFrame(
    const IniFile& ini,
    const std::string& sequence,
    const std::string& key
) {
    ArtType::SequenceFrame frame;
    frame.name = key;
    frame.present = hasIniValue(ini, sequence, key);
    if (!frame.present) {
        return frame;
    }

    const std::vector<int> values = [&ini, &sequence, &key]() {
        std::vector<int> result;
        const std::string value = ini.getValue(sequence, key, "");
        std::stringstream stream(value);
        std::string token;
        while (std::getline(stream, token, ',')) {
            const auto firstNumber = std::find_if(
                token.begin(),
                token.end(),
                [](unsigned char character) {
                    return std::isdigit(character)
                        || character == '-'
                        || character == '+';
                }
            );
            if (firstNumber == token.end()) {
                continue;
            }

            try {
                std::size_t parsed = 0;
                const int number =
                    std::stoi(std::string(firstNumber, token.end()), &parsed);
                if (parsed > 0) {
                    result.push_back(number);
                }
            } catch (const std::exception&) {
                break;
            }
        }
        return result;
    }();

    if (!values.empty()) {
        frame.startFrame = values[0];
    }
    if (values.size() >= 2) {
        frame.frameCount = values[1];
    }
    if (values.size() >= 3) {
        frame.facingMultiplier = values[2];
    }
    if (frame.frameCount <= 0) {
        frame.present = false;
    }

    return frame;
}

void addSequenceFrames(
    const IniFile& ini,
    ArtType& art,
    const std::string& sequence
) {
    const char* keys[] = {
        "Ready",
        "Guard",
        "Prone",
        "Walk",
        "Panic",
        "Shovel",
        "Carry",
        "Fly",
        "Hover",
        "FireFly",
        "Fire",
        "FireUp",
        "SecondaryFire",
        "Down",
        "Crawl",
        "Up",
        "FireProne",
        "SecondaryProne",
        "Idle",
        "Idle1",
        "Idle2",
        "Die1",
        "Die2",
        "Die3",
        "Die4",
        "Die5",
        "WetDie1",
        "WetDie2",
        "Tread",
        "Swim",
        "WetAttack",
        "WetIdle1",
        "WetIdle2",
        "Deploy",
        "Deployed",
        "DeployFire",
        "DeployedFire",
        "DeployIdle",
        "DeployedIdle",
        "Undeploy",
        "Paradrop"
    };

    for (const char* key : keys) {
        ArtType::SequenceFrame frame = readSequenceFrame(ini, sequence, key);
        if (frame.present) {
            art.sequenceFrames.push_back(frame);
        }
    }
}

void addDamageFireOffsets(
    const IniFile& ini,
    ArtType& art,
    const std::string& id
) {
    for (int index = 0; index < 16; ++index) {
        const std::string key = "DamageFireOffset" + std::to_string(index);
        if (!hasIniValue(ini, id, key)) {
            continue;
        }

        const std::vector<int> values = [&ini, &id, &key]() {
            std::vector<int> result;
            const std::string value = ini.getValue(id, key, "");
            std::stringstream stream(value);
            std::string token;
            while (std::getline(stream, token, ',')) {
                const auto firstNumber = std::find_if(
                    token.begin(),
                    token.end(),
                    [](unsigned char character) {
                        return std::isdigit(character)
                            || character == '-'
                            || character == '+';
                    }
                );
                if (firstNumber == token.end()) {
                    continue;
                }

                try {
                    std::size_t parsed = 0;
                    const int number = std::stoi(
                        std::string(firstNumber, token.end()),
                        &parsed
                    );
                    if (parsed > 0) {
                        result.push_back(number);
                    }
                } catch (const std::exception&) {
                    break;
                }
            }
            return result;
        }();

        if (values.size() < 2) {
            continue;
        }

        ArtType::DamageFireOffset offset;
        offset.index = index;
        offset.offsetX = values[0];
        offset.offsetY = values[1];
        art.damageFireOffsets.push_back(offset);
    }
}

void addDockingOffsets(
    const IniFile& ini,
    ArtType& art,
    const std::string& id
) {
    for (int index = 0; index < 16; ++index) {
        const std::string key = "DockingOffset" + std::to_string(index);
        if (!hasIniValue(ini, id, key)) {
            continue;
        }

        const std::vector<int> values = [&ini, &id, &key]() {
            std::vector<int> result;
            const std::string value = ini.getValue(id, key, "");
            std::stringstream stream(value);
            std::string token;
            while (std::getline(stream, token, ',')) {
                const auto firstNumber = std::find_if(
                    token.begin(),
                    token.end(),
                    [](unsigned char character) {
                        return std::isdigit(character)
                            || character == '-'
                            || character == '+';
                    }
                );
                if (firstNumber == token.end()) {
                    continue;
                }

                try {
                    std::size_t parsed = 0;
                    const int number = std::stoi(
                        std::string(firstNumber, token.end()),
                        &parsed
                    );
                    if (parsed > 0) {
                        result.push_back(number);
                    }
                } catch (const std::exception&) {
                    break;
                }
            }
            return result;
        }();

        if (values.size() < 3) {
            continue;
        }

        ArtType::DockingOffset offset;
        offset.index = index;
        offset.x = values[0];
        offset.y = values[1];
        offset.z = values[2];
        art.dockingOffsets.push_back(offset);
    }
}

bool artSequenceHasFrame(const ArtType& art, const std::string& name) {
    const std::string normalized = toLower(name);
    return std::any_of(
        art.sequenceFrames.begin(),
        art.sequenceFrames.end(),
        [&normalized](const ArtType::SequenceFrame& frame) {
            return toLower(frame.name) == normalized;
        }
    );
}

bool animationListHasPoweredMetadata(
    const std::vector<ArtType::ActiveAnimation>& animations
) {
    return std::any_of(
        animations.begin(),
        animations.end(),
        [](const ArtType::ActiveAnimation& animation) {
            return animation.powered
                || animation.poweredLight
                || animation.poweredSpecial
                || animation.poweredEffect;
        }
    );
}

std::vector<std::string> animationKeys(
    const std::vector<ArtType::ActiveAnimation>& animations
) {
    std::vector<std::string> keys;
    keys.reserve(animations.size());
    for (const ArtType::ActiveAnimation& animation : animations) {
        keys.push_back(animation.key);
    }
    return keys;
}

bool animationListHasPoweredLight(
    const std::vector<ArtType::ActiveAnimation>& animations
) {
    return std::any_of(
        animations.begin(),
        animations.end(),
        [](const ArtType::ActiveAnimation& animation) {
            return animation.poweredLight;
        }
    );
}

bool animationListHasPoweredSpecial(
    const std::vector<ArtType::ActiveAnimation>& animations
) {
    return std::any_of(
        animations.begin(),
        animations.end(),
        [](const ArtType::ActiveAnimation& animation) {
            return animation.poweredSpecial;
        }
    );
}

bool animationListHasPoweredEffect(
    const std::vector<ArtType::ActiveAnimation>& animations
) {
    return std::any_of(
        animations.begin(),
        animations.end(),
        [](const ArtType::ActiveAnimation& animation) {
            return animation.poweredEffect;
        }
    );
}

bool animationListHasKey(
    const std::vector<ArtType::ActiveAnimation>& animations,
    const std::string& key
) {
    const std::string normalized = toLower(key);
    return std::any_of(
        animations.begin(),
        animations.end(),
        [&normalized](const ArtType::ActiveAnimation& animation) {
            return toLower(animation.key) == normalized;
        }
    );
}

std::vector<int> damageFireOffsetIndices(
    const std::vector<ArtType::DamageFireOffset>& offsets
) {
    std::vector<int> indices;
    indices.reserve(offsets.size());
    for (const ArtType::DamageFireOffset& offset : offsets) {
        indices.push_back(offset.index);
    }
    return indices;
}

RenderProfile renderProfileForArt(const ArtType& art) {
    RenderProfile profile;
    profile.image = art.image;
    profile.cameo = art.cameo;
    profile.altCameo = art.altCameo;
    profile.foundation = art.foundation;
    profile.palette = art.palette;
    profile.customPalette = art.customPalette;
    profile.altPalette = art.altPalette;
    profile.layer = art.layer;
    profile.theater = art.theater;
    profile.newTheater = art.newTheater;
    profile.remapableSpecified = art.remapableSpecified;
    profile.remapable = art.remapable;
    profile.normalized = art.normalized;
    profile.voxel = art.voxel;
    profile.turret = art.turret;
    profile.shadowSpecified = art.shadowSpecified;
    profile.shadow = art.shadow;
    profile.shadowIndex = art.shadowIndex;
    profile.useTurretShadow = art.useTurretShadow;
    profile.zAdjust = art.zAdjust;
    profile.normalZAdjust = art.normalZAdjust;
    profile.zShapePointMove = art.zShapePointMove;
    profile.ySortAdjust = art.ySortAdjust;
    profile.height = art.height;
    profile.terrainPalette = art.terrainPalette;
    profile.animPalette = art.animPalette;
    profile.flat = art.flat;
    profile.translucent = art.translucent;
    profile.turretOffset = art.turretOffset;
    profile.primaryFireFLH = art.primaryFireFLH;
    profile.secondaryFireFLH = art.secondaryFireFLH;
    profile.primaryFirePixelOffset = art.primaryFirePixelOffset;
    profile.secondaryFirePixelOffset = art.secondaryFirePixelOffset;
    profile.primaryFireDualOffset = art.primaryFireDualOffset;
    profile.secondaryFireDualOffset = art.secondaryFireDualOffset;
    profile.elitePrimaryFireFLH = art.elitePrimaryFireFLH;
    profile.eliteSecondaryFireFLH = art.eliteSecondaryFireFLH;
    profile.weaponFireFLHs = art.weaponFireFLHs;
    profile.eliteWeaponFireFLHs = art.eliteWeaponFireFLHs;
    return profile;
}

SequenceProfile sequenceProfileForArt(const ArtType& art) {
    SequenceProfile profile;
    profile.sequence = art.sequence;
    profile.hasReady = artSequenceHasFrame(art, "Ready");
    profile.hasGuard = artSequenceHasFrame(art, "Guard");
    profile.hasWalk = artSequenceHasFrame(art, "Walk");
    profile.hasFire = artSequenceHasFrame(art, "Fire");
    profile.hasSecondaryFire = artSequenceHasFrame(art, "SecondaryFire");
    profile.hasDeploy = artSequenceHasFrame(art, "Deploy");
    profile.hasDeployIdle = artSequenceHasFrame(art, "DeployIdle");
    profile.hasDeployFire = artSequenceHasFrame(art, "DeployFire");
    profile.hasDeployed = artSequenceHasFrame(art, "Deployed");
    profile.hasDeployedIdle = artSequenceHasFrame(art, "DeployedIdle");
    profile.hasDeployedFire = artSequenceHasFrame(art, "DeployedFire");
    profile.hasUndeploy = artSequenceHasFrame(art, "Undeploy");
    return profile;
}

BuildingAnimProfile buildingAnimProfileForArt(const ArtType& art) {
    BuildingAnimProfile profile;
    profile.foundation = art.foundation;
    profile.buildup = art.buildup;
    profile.bibShape = art.bibShape;
    profile.activeAnimationCount =
        static_cast<int>(art.activeAnimations.size());
    profile.specialAnimationCount =
        static_cast<int>(art.specialAnimations.size());
    profile.auxiliaryAnimationCount =
        static_cast<int>(art.auxiliaryAnimations.size());
    profile.damageFireOffsetCount =
        static_cast<int>(art.damageFireOffsets.size());
    profile.hasPoweredAnimation =
        animationListHasPoweredMetadata(art.activeAnimations)
        || animationListHasPoweredMetadata(art.specialAnimations)
        || animationListHasPoweredMetadata(art.auxiliaryAnimations);
    profile.hasPoweredLightAnimation =
        animationListHasPoweredLight(art.activeAnimations)
        || animationListHasPoweredLight(art.specialAnimations)
        || animationListHasPoweredLight(art.auxiliaryAnimations);
    profile.hasPoweredSpecialAnimation =
        animationListHasPoweredSpecial(art.activeAnimations)
        || animationListHasPoweredSpecial(art.specialAnimations)
        || animationListHasPoweredSpecial(art.auxiliaryAnimations);
    profile.hasPoweredEffectAnimation =
        animationListHasPoweredEffect(art.activeAnimations)
        || animationListHasPoweredEffect(art.specialAnimations)
        || animationListHasPoweredEffect(art.auxiliaryAnimations);
    profile.hasLowPowerAnimation =
        animationListHasKey(art.auxiliaryAnimations, "LowPower");
    profile.hasSuperLowPowerAnimation =
        animationListHasKey(art.auxiliaryAnimations, "SuperLowPower");
    profile.hasActiveAnimationAnchor = std::any_of(
        art.activeAnimations.begin(),
        art.activeAnimations.end(),
        [](const ArtType::ActiveAnimation& animation) {
            return animation.offsetX != 0
                || animation.offsetY != 0
                || animation.ySort != 0
                || animation.zAdjust != 0;
        }
    );
    profile.hasSpecialAnimation = !art.specialAnimations.empty();
    profile.hasDamageFireOffsets = !art.damageFireOffsets.empty();
    profile.activeAnimationKeys = animationKeys(art.activeAnimations);
    profile.specialAnimationKeys = animationKeys(art.specialAnimations);
    profile.auxiliaryAnimationKeys = animationKeys(art.auxiliaryAnimations);
    profile.damageFireOffsetIndices =
        damageFireOffsetIndices(art.damageFireOffsets);
    profile.layer = art.layer;
    profile.zAdjust = art.zAdjust;
    profile.normalZAdjust = art.normalZAdjust;
    profile.zShapePointMove = art.zShapePointMove;
    profile.ySortAdjust = art.ySortAdjust;
    profile.height = art.height;
    return profile;
}

ArtObjectLink makeMissingLink(
    const std::string& objectKind,
    const std::string& objectId,
    const std::string& artId
) {
    ArtObjectLink link;
    link.objectKind = objectKind;
    link.objectId = objectId;
    link.artId = artId;
    link.hasArt = false;

    return link;
}

ArtObjectLink makeLink(
    const std::string& objectKind,
    const std::string& objectId,
    const std::string& artId,
    const ArtType& art
) {
    ArtObjectLink link;
    link.objectKind = objectKind;
    link.objectId = objectId;
    link.artId = artId;
    link.hasArt = true;
    link.image = art.image.empty() ? artId : art.image;
    link.cameo = art.cameo;
    link.altCameo = art.altCameo;
    link.sequence = art.sequence;
    link.foundation = art.foundation;
    link.palette = art.palette;
    link.customPalette = art.customPalette;
    link.altPalette = art.altPalette;
    link.layer = art.layer;
    link.theater = art.theater;
    link.newTheater = art.newTheater;
    link.remapableSpecified = art.remapableSpecified;
    link.remapable = art.remapable;
    link.normalized = art.normalized;
    link.voxel = art.voxel;
    link.turret = art.turret;
    link.demandLoad = art.demandLoad;
    link.demandLoadBuildup = art.demandLoadBuildup;
    link.detailLevel = art.detailLevel;
    link.freeBuildup = art.freeBuildup;
    link.toOverlay = art.toOverlay;
    link.damageLevels = art.damageLevels;
    link.height = art.height;
    link.shadowSpecified = art.shadowSpecified;
    link.shadow = art.shadow;
    link.shadowIndex = art.shadowIndex;
    link.useTurretShadow = art.useTurretShadow;
    link.zAdjust = art.zAdjust;
    link.normalZAdjust = art.normalZAdjust;
    link.zShapePointMove = art.zShapePointMove;
    link.ySortAdjust = art.ySortAdjust;
    link.buildup = art.buildup;
    link.bibShape = art.bibShape;
    link.gateStages = art.gateStages;
    link.specialZOverlay = art.specialZOverlay;
    link.specialZOverlayZAdjust = art.specialZOverlayZAdjust;
    link.terrainPalette = art.terrainPalette;
    link.animPalette = art.animPalette;
    link.flat = art.flat;
    link.translucent = art.translucent;
    link.isTiberium = art.isTiberium;
    link.isAnimatedTiberium = art.isAnimatedTiberium;
    link.hideIfNoOre = art.hideIfNoOre;
    link.tiberiumSpawnType = art.tiberiumSpawnType;
    link.tiberiumSpreadRadius = art.tiberiumSpreadRadius;
    link.isAnimDelayedFire = art.isAnimDelayedFire;
    link.delayedFireDelay = art.delayedFireDelay;
    link.trailerAnim = art.trailerAnim;
    link.trailerSeperation = art.trailerSeperation;
    link.turretOffset = art.turretOffset;
    link.primaryFireFLH = art.primaryFireFLH;
    link.secondaryFireFLH = art.secondaryFireFLH;
    link.primaryFirePixelOffset = art.primaryFirePixelOffset;
    link.secondaryFirePixelOffset = art.secondaryFirePixelOffset;
    link.primaryFireDualOffset = art.primaryFireDualOffset;
    link.secondaryFireDualOffset = art.secondaryFireDualOffset;
    link.elitePrimaryFireFLH = art.elitePrimaryFireFLH;
    link.eliteSecondaryFireFLH = art.eliteSecondaryFireFLH;
    link.weaponFireFLHs = art.weaponFireFLHs;
    link.eliteWeaponFireFLHs = art.eliteWeaponFireFLHs;
    link.activeAnimations = art.activeAnimations;
    link.specialAnimations = art.specialAnimations;
    link.auxiliaryAnimations = art.auxiliaryAnimations;
    link.sequenceFrames = art.sequenceFrames;
    link.hasQueueingCell = art.hasQueueingCell;
    link.queueingCell = art.queueingCell;
    link.dockingOffsets = art.dockingOffsets;
    link.damageFireOffsets = art.damageFireOffsets;
    link.renderProfile = art.renderProfile;
    link.sequenceProfile = art.sequenceProfile;
    link.buildingAnimProfile = art.buildingAnimProfile;

    return link;
}

void addUnitLinks(
    const ArtDatabase& artDatabase,
    std::vector<ArtObjectLink>& links,
    const std::vector<UnitType>& units,
    const std::string& objectKind
) {
    for (const UnitType& unit : units) {
        const std::string artId = chooseArtId(unit.id, unit.image);
        const ArtType* art = artDatabase.findArtType(artId);

        if (art == nullptr) {
            links.push_back(makeMissingLink(objectKind, unit.id, artId));
            continue;
        }

        links.push_back(makeLink(objectKind, unit.id, artId, *art));
    }
}

void addBuildingLinks(
    const ArtDatabase& artDatabase,
    std::vector<ArtObjectLink>& links,
    const std::vector<BuildingType>& buildings
) {
    for (const BuildingType& building : buildings) {
        const std::string artId = chooseArtId(building.id, building.image);
        const ArtType* art = artDatabase.findArtType(artId);

        if (art == nullptr) {
            links.push_back(makeMissingLink("building", building.id, artId));
            continue;
        }

        ArtObjectLink link = makeLink("building", building.id, artId, *art);
        link.turretAnim = building.turretAnim;
        link.turretAnimIsVoxel = building.turretAnimIsVoxel;
        link.turretAnimX = building.turretAnimX;
        link.turretAnimY = building.turretAnimY;
        link.turretAnimZAdjust = building.turretAnimZAdjust;
        link.buildingAnimProfile.turretAnim = building.turretAnim;
        link.buildingAnimProfile.turretAnimIsVoxel =
            building.turretAnimIsVoxel;
        link.buildingAnimProfile.turretAnimX = building.turretAnimX;
        link.buildingAnimProfile.turretAnimY = building.turretAnimY;
        link.buildingAnimProfile.turretAnimZAdjust =
            building.turretAnimZAdjust;
        links.push_back(std::move(link));
    }
}

void addOverlayLinks(
    const ArtDatabase& artDatabase,
    std::vector<ArtObjectLink>& links,
    const std::vector<OverlayType>& overlays
) {
    for (const OverlayType& overlay : overlays) {
        const std::string artId = chooseArtId(overlay.id, overlay.image);
        const ArtType* art = artDatabase.findArtType(artId);

        if (art == nullptr) {
            links.push_back(makeMissingLink("overlay", overlay.id, artId));
            continue;
        }

        links.push_back(makeLink("overlay", overlay.id, artId, *art));
    }
}

} // namespace

bool ArtDatabase::loadFromIni(const IniFile& ini) {
    artTypes_.clear();
    warnings_.clear();
    artIndexById_.clear();

    const std::vector<std::string> sectionNames = ini.getSectionNames();
    artTypes_.reserve(sectionNames.size());

    for (const std::string& sectionName : sectionNames) {
        ArtType art = loadArtType(ini, sectionName);

        const std::string artKey = canonicalArtKey(art.id);
        if (artIndexById_.find(artKey) != artIndexById_.end()) {
            addWarning("Duplicate art section ignored: [" + art.id + "]");
            continue;
        }

        artIndexById_[artKey] = artTypes_.size();
        artTypes_.push_back(art);
    }

    return true;
}

const std::vector<ArtType>& ArtDatabase::getArtTypes() const {
    return artTypes_;
}

const std::vector<std::string>& ArtDatabase::getWarnings() const {
    return warnings_;
}

const ArtType* ArtDatabase::findArtType(const std::string& id) const {
    const std::string key = canonicalArtKey(id);
    if (key.empty()) {
        return nullptr;
    }

    const auto it = artIndexById_.find(key);
    if (it == artIndexById_.end()) {
        return nullptr;
    }
    if (it->second >= artTypes_.size()) {
        return nullptr;
    }

    return &artTypes_[it->second];
}

ArtCapabilityCacheStats ArtDatabase::getCapabilityCacheStats() const {
    ArtCapabilityCacheStats stats;
    stats.artProfileCount = artTypes_.size();
    stats.renderProfileCount = artTypes_.size();
    stats.sequenceProfileCount = artTypes_.size();
    stats.buildingAnimProfileCount = artTypes_.size();
    stats.indexedArtCount = artIndexById_.size();
    return stats;
}

std::vector<ArtObjectLink> ArtDatabase::buildLinks(const RulesDatabase& rules) const {
    std::vector<ArtObjectLink> links;

    links.reserve(
        rules.getInfantryTypes().size()
        + rules.getVehicleTypes().size()
        + rules.getAircraftTypes().size()
        + rules.getBuildingTypes().size()
        + rules.getOverlayTypes().size()
    );

    addUnitLinks(*this, links, rules.getInfantryTypes(), "infantry");
    addUnitLinks(*this, links, rules.getVehicleTypes(), "vehicle");
    addUnitLinks(*this, links, rules.getAircraftTypes(), "aircraft");
    addBuildingLinks(*this, links, rules.getBuildingTypes());
    addOverlayLinks(*this, links, rules.getOverlayTypes());

    return links;
}

ArtType ArtDatabase::loadArtType(const IniFile& ini, const std::string& id) {
    ArtType art;

    art.id = id;
    art.image = ini.getValue(id, "Image", "");
    art.cameo = ini.getValue(id, "Cameo", "");
    art.altCameo = ini.getValue(id, "AltCameo", "");
    art.sequence = ini.getValue(id, "Sequence", "");
    art.foundation = ini.getValue(id, "Foundation", "");
    art.palette = ini.getValue(id, "Palette", "");
    art.customPalette = ini.getValue(id, "CustomPalette", "");
    art.altPalette = ini.getValue(id, "AltPalette", "");
    art.layer = ini.getValue(id, "Layer", "");
    art.theater = readBool(ini, id, "Theater", false);
    art.newTheater = readBool(ini, id, "NewTheater", false)
        || art.theater;
    art.remapableSpecified = hasIniValue(ini, id, "Remapable");
    art.remapable = readBool(ini, id, "Remapable", false);
    art.normalized = readBool(ini, id, "Normalized", false);
    art.voxel = readBool(ini, id, "Voxel", false);
    art.spawnDelay = std::max(0, readInt(ini, id, "SpawnDelay", 0));
    art.rotates = readBool(ini, id, "Rotates", false);
    art.useLineTrail = readBool(ini, id, "UseLineTrail", false);
    art.lineTrailColor = ini.getValue(id, "LineTrailColor", "0,0,0");
    art.lineTrailColorDecrement = std::clamp(readInt(ini, id, "LineTrailColorDecrement", 16), 0, 255);
    art.secondSpawnOffset = ini.getValue(id, "SecondSpawnOffset", "");
    for (std::size_t i = 0; i < art.alternateFireFLHs.size(); ++i)
        art.alternateFireFLHs[i] = ini.getValue(id, "AlternateFLH" + std::to_string(i), "");
    art.turret = readBool(ini, id, "Turret", false);
    art.demandLoad = readBool(ini, id, "DemandLoad", false);
    art.demandLoadBuildup = readBool(ini, id, "DemandLoadBuildup", false);
    art.freeBuildup = readBool(ini, id, "FreeBuildup", false);
    art.toOverlay = ini.getValue(id, "ToOverlay", "");
    art.damageLevels = readInt(ini, id, "DamageLevels", 0);
    art.height = readInt(ini, id, "Height", 0);
    art.turretOffset = ini.getValue(id, "TurretOffset", "");
    readIndexedFireFlhValues(
        ini,
        id,
        "Weapon",
        art.weaponFireFLHs
    );
    readIndexedFireFlhValues(
        ini,
        id,
        "EliteWeapon",
        art.eliteWeaponFireFLHs
    );
    art.primaryFirePixelOffset = ini.getValue(id, "PrimaryFirePixelOffset", "");
    art.secondaryFirePixelOffset = ini.getValue(id, "SecondaryFirePixelOffset", "");
    art.primaryFireDualOffset = readBool(ini, id, "PrimaryFireDualOffset", false);
    art.secondaryFireDualOffset = readBool(ini, id, "SecondaryFireDualOffset", false);
    art.primaryFireFLH = ini.getValue(id, "PrimaryFireFLH", "");
    if (art.primaryFireFLH.empty()
        && !art.weaponFireFLHs.empty()) {
        art.primaryFireFLH = art.weaponFireFLHs[0];
    }
    art.secondaryFireFLH = ini.getValue(id, "SecondaryFireFLH", "");
    if (art.secondaryFireFLH.empty()
        && art.weaponFireFLHs.size() >= 2U) {
        art.secondaryFireFLH = art.weaponFireFLHs[1];
    }
    art.elitePrimaryFireFLH = ini.getValue(
        id,
        "ElitePrimaryFireFLH",
        ""
    );
    if (art.elitePrimaryFireFLH.empty()
        && !art.eliteWeaponFireFLHs.empty()) {
        art.elitePrimaryFireFLH = art.eliteWeaponFireFLHs[0];
    }
    art.eliteSecondaryFireFLH = ini.getValue(
        id,
        "EliteSecondaryFireFLH",
        ""
    );
    if (art.eliteSecondaryFireFLH.empty()
        && art.eliteWeaponFireFLHs.size() >= 2U) {
        art.eliteSecondaryFireFLH = art.eliteWeaponFireFLHs[1];
    }
    art.walkFrames = readInt(ini, id, "WalkFrames", 0);
    art.firingFrames = std::max(0, readInt(ini, id, "FiringFrames", 0));
    art.standingFrames = readInt(ini, id, "StandingFrames", 0);
    art.startWalkFrame = readInt(ini, id, "StartWalkFrame", 0);
    art.startStandFrame = readInt(ini, id, "StartStandFrame", -1);
    art.facings = readInt(ini, id, "Facings", 0);
    art.trailerAnim = ini.getValue(id, "TrailerAnim", "");
    art.expireAnim = ini.getValue(id, "ExpireAnim", "");
    art.scorch = readBool(ini, id, "Scorch", false);
    art.crater = readBool(ini, id, "Crater", false);
    art.forceBigCraters = readBool(ini, id, "ForceBigCraters", false);
    art.spawnsParticle = trim(ini.getValue(id, "SpawnsParticle", ""));
    art.numParticles = std::max(0, readInt(ini, id, "NumParticles", 1));
    art.damage = std::max(
        0,
        static_cast<int>(std::lround(readDouble(ini, id, "Damage", 0.0)))
    );
    art.maxXYVelocity =
        std::max(0.0, readDouble(ini, id, "MaxXYVel", 0.0));
    art.minZVelocity =
        std::max(0.0, readDouble(ini, id, "MinZVel", 0.0));
    art.bouncer = readBool(ini, id, "Bouncer", false);
    art.reportSound = trim(ini.getValue(id, "Report", ""));
    art.startSound = trim(ini.getValue(id, "StartSound", ""));
    art.rate = readInt(ini, id, "Rate", 0);
    art.detailLevel = readInt(ini, id, "DetailLevel", 0);
    art.animationStart = readInt(ini, id, "Start", 0);
    art.animationLoopStart = readInt(ini, id, "LoopStart", 0);
    art.animationLoopEnd = readInt(ini, id, "LoopEnd", 0);
    art.animationLoopCount = readInt(ini, id, "LoopCount", 1);
    art.animationEnd = readInt(ini, id, "End", 0);
    art.hasAnimationStart = hasIniValue(ini, id, "Start");
    art.hasAnimationLoopStart = hasIniValue(ini, id, "LoopStart");
    art.hasAnimationLoopEnd = hasIniValue(ini, id, "LoopEnd");
    art.hasAnimationEnd = hasIniValue(ini, id, "End");
    const std::vector<int> queueingCell =
        readIntList(ini, id, "QueueingCell");
    if (queueingCell.size() >= 2) {
        art.hasQueueingCell = true;
        art.queueingCell.x = queueingCell[0];
        art.queueingCell.y = queueingCell[1];
    }
    art.shadowSpecified = hasIniValue(ini, id, "Shadow");
    art.shadow = readBool(ini, id, "Shadow", false);
    art.shadowIndex = readInt(ini, id, "ShadowIndex", -1);
    art.useTurretShadow = readBool(ini, id, "UseTurretShadow", false);
    art.zAdjust = readInt(ini, id, "ZAdjust", 0);
    art.yDrawOffset = readInt(ini, id, "YDrawOffset", 0);
    art.translucency = std::clamp(readInt(ini, id, "Translucency", 0), 0, 100);
    art.normalZAdjust = readInt(ini, id, "NormalZAdjust", 0);
    const std::vector<int> zShapePointMove =
        readIntList(ini, id, "ZShapePointMove");
    if (zShapePointMove.size() >= 2) {
        art.zShapePointMove = {zShapePointMove[0], zShapePointMove[1]};
    }
    art.ySortAdjust = readFirstArtInt(
        ini,
        id,
        "YSortAdjust",
        "YSort",
        0
    );
    art.pingPong = readBool(ini, id, "PingPong", false);
    art.reverse = readBool(ini, id, "Reverse", false);
    art.buildup = ini.getValue(id, "Buildup", "");
    art.bibShape = ini.getValue(id, "BibShape", "");
    art.specialZOverlay = trim(
        ini.getValue(id, "SpecialZOverlay", "")
    );
    const std::string rawSpecialZOverlayZAdjust = trim(
        ini.getValue(id, "SpecialZOverlayZAdjust", "")
    );
    if (!rawSpecialZOverlayZAdjust.empty()) {
        try {
            std::size_t parsedLength = 0;
            art.specialZOverlayZAdjust = std::stoi(
                rawSpecialZOverlayZAdjust,
                &parsedLength
            );
            if (parsedLength != rawSpecialZOverlayZAdjust.size()) {
                throw std::invalid_argument("SpecialZOverlayZAdjust");
            }
        } catch (const std::exception&) {
            addWarning(
                "Invalid integer: [" + id
                + "] SpecialZOverlayZAdjust="
                + rawSpecialZOverlayZAdjust
            );
            art.specialZOverlayZAdjust = 0;
        }
    }
    const std::string rawGateStages = trim(
        ini.getValue(id, "GateStages", "")
    );
    if (!rawGateStages.empty()) {
        try {
            std::size_t parsedLength = 0;
            const int parsedGateStages = std::stoi(
                rawGateStages,
                &parsedLength
            );
            constexpr int maxGateStages = 4095;
            if (parsedLength != rawGateStages.size()
                || parsedGateStages < 0
                || parsedGateStages > maxGateStages) {
                throw std::invalid_argument("GateStages");
            }
            art.gateStages = parsedGateStages;
        } catch (const std::exception&) {
            addWarning(
                "Invalid non-negative integer: [" + id
                + "] GateStages=" + rawGateStages
            );
            art.gateStages = 0;
        }
    }
    art.terrainPaletteSpecified = hasIniValue(ini, id, "TerrainPalette");
    art.animPaletteSpecified = hasIniValue(ini, id, "AnimPalette");
    art.terrainPalette = readBool(ini, id, "TerrainPalette", false);
    art.animPalette = readBool(ini, id, "AnimPalette", false);
    art.canBeHidden = readBool(ini, id, "CanBeHidden", true);
    art.makeInfantry = std::max(-1, readInt(ini, id, "MakeInfantry", -1));
    art.flat = readBool(ini, id, "Flat", false);
    art.translucent = readBool(ini, id, "Translucent", false);
    art.isTiberium = readBool(ini, id, "IsTiberium", false);
    art.isAnimatedTiberium =
        readBool(ini, id, "IsAnimatedTiberium", false);
    art.hideIfNoOre = readBool(ini, id, "HideIfNoOre", false);
    art.tiberiumSpawnType = trim(ini.getValue(id, "TiberiumSpawnType", ""));
    art.tiberiumSpreadRadius =
        std::max(-1, readInt(ini, id, "TiberiumRadius", -1));
    art.tiberiumSpreadRadius =
        std::max(
            -1,
            readInt(
                ini,
                id,
                "TiberiumSpreadRadius",
                art.tiberiumSpreadRadius
            )
        );
    art.isAnimDelayedFire =
        readBool(ini, id, "IsAnimDelayedFire", false);
    art.delayedFireDelay = std::max(
        0,
        readInt(ini, id, "DelayedFireDelay", 0)
    );
    art.trailerSeperation = readInt(ini, id, "TrailerSeperation", 0);
    addActiveAnimationLayer(ini, art, id, "");
    addActiveAnimationLayer(ini, art, id, "Two");
    addActiveAnimationLayer(ini, art, id, "Three");
    addActiveAnimationLayer(ini, art, id, "Four");
    addNamedAnimationLayer(ini, art, id, "IdleAnim");
    addNamedAnimationLayer(ini, art, id, "IdleAnimTwo");
    addSpecialAnimationLayer(ini, art, id, "SpecialAnim");
    addSpecialAnimationLayer(ini, art, id, "SpecialAnimTwo");
    addSpecialAnimationLayer(ini, art, id, "SpecialAnimThree");
    addSpecialAnimationLayer(ini, art, id, "SpecialAnimFour");
    addAuxiliaryAnimationLayer(ini, art, id, "DeployingAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "UnderDoorAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "RoofDeployingAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "UnderRoofDoorAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "DoorAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "ProductionAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "PreProductionAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "SuperAnim");
    addAuxiliaryAnimationLayer(ini, art, id, "SuperAnimTwo");
    addAuxiliaryAnimationLayer(ini, art, id, "SuperAnimThree");
    addAuxiliaryAnimationLayer(ini, art, id, "SuperAnimFour");
    addAuxiliaryAnimationLayer(ini, art, id, "LowPower");
    addAuxiliaryAnimationLayer(ini, art, id, "SuperLowPower");
    addDamageFireOffsets(ini, art, id);
    for (std::size_t index = 0; index < art.muzzleFlashes.size(); ++index) {
        std::istringstream input(ini.getValue(id, "MuzzleFlash" + std::to_string(index), ""));
        int x = 0, y = 0; char separator = 0;
        if (input >> x >> separator >> y; input && separator == ','
            && (input >> std::ws, input.eof()) && !(x == 65535 && y == 65535))
            art.muzzleFlashes[index] = {x, y};
    }
    addDockingOffsets(ini, art, id);

    if (!art.sequence.empty()) {
        addSequenceFrames(ini, art, art.sequence);

        const std::vector<int> walk = readIntList(ini, art.sequence, "Walk");
        if (walk.size() >= 2) {
            if (art.startWalkFrame <= 0) {
                art.startWalkFrame = walk[0];
            }
            if (art.walkFrames <= 0) {
                art.walkFrames = std::max(1, walk[1]);
            }
            if (art.facings <= 0 && walk.size() >= 3 && walk[2] > 1) {
                art.facings = 8;
            }
        }

        std::vector<int> ready = readIntList(ini, art.sequence, "Ready");
        if (ready.empty()) {
            ready = readIntList(ini, art.sequence, "Guard");
        }
        if (art.standingFrames <= 0 && ready.size() >= 2) {
            art.standingFrames = std::max(1, ready[1]);
        }
        if (art.facings <= 0 && ready.size() >= 3 && ready[2] > 1) {
            art.facings = 8;
        }

        const std::vector<int> idle1 = readIntList(ini, art.sequence, "Idle1");
        if (idle1.size() >= 2) {
            art.idle1StartFrame = idle1[0];
            art.idle1Frames = std::max(1, idle1[1]);
        }

        const std::vector<int> idle2 = readIntList(ini, art.sequence, "Idle2");
        if (idle2.size() >= 2) {
            art.idle2StartFrame = idle2[0];
            art.idle2Frames = std::max(1, idle2[1]);
        }
    }

    art.renderProfile = renderProfileForArt(art);
    art.sequenceProfile = sequenceProfileForArt(art);
    art.buildingAnimProfile = buildingAnimProfileForArt(art);

    return art;
}

int ArtDatabase::readInt(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    int defaultValue
) {
    const std::string value = ini.getValue(section, key, "");

    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        addWarning("Invalid integer: [" + section + "] " + key + "=" + value);
        return defaultValue;
    }
}

double ArtDatabase::readDouble(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    double defaultValue
) {
    const std::string value = ini.getValue(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }

    try {
        return std::stod(value);
    } catch (const std::exception&) {
        addWarning(
            "Invalid floating point: [" + section + "] "
            + key + "=" + value
        );
        return defaultValue;
    }
}

bool ArtDatabase::readBool(
    const IniFile& ini,
    const std::string& section,
    const std::string& key,
    bool defaultValue
) {
    const std::string value = ini.getValue(section, key, "");

    if (value.empty()) {
        return defaultValue;
    }

    const std::string normalized = toLower(value);
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        return true;
    }

    if (normalized == "no" || normalized == "false" || normalized == "0") {
        return false;
    }

    addWarning("Invalid boolean: [" + section + "] " + key + "=" + value);
    return defaultValue;
}

std::vector<int> ArtDatabase::readIntList(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    std::vector<int> values;
    const std::string value = ini.getValue(section, key, "");
    if (value.empty()) {
        return values;
    }

    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const auto firstNumber = std::find_if(
            token.begin(),
            token.end(),
            [](unsigned char character) {
                return std::isdigit(character)
                    || character == '-'
                    || character == '+';
            }
        );
        if (firstNumber == token.end()) {
            continue;
        }

        try {
            std::size_t parsed = 0;
            const int number = std::stoi(std::string(firstNumber, token.end()), &parsed);
            if (parsed == 0) {
                continue;
            }
            values.push_back(number);
        } catch (const std::exception&) {
            addWarning("Invalid integer list item: [" + section + "] "
                + key + "=" + value);
            break;
        }
    }

    return values;
}

void ArtDatabase::addWarning(const std::string& message) {
    warnings_.push_back(message);
}
