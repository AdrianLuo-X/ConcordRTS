#pragma once

#include "core/io/IniFile.h"
#include "data/rules/RulesDatabase.h"

#include <array>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

struct SequenceProfile {
    std::string sequence;
    bool hasReady = false;
    bool hasGuard = false;
    bool hasWalk = false;
    bool hasFire = false;
    bool hasSecondaryFire = false;
    bool hasDeploy = false;
    bool hasDeployIdle = false;
    bool hasDeployFire = false;
    bool hasDeployed = false;
    bool hasDeployedIdle = false;
    bool hasDeployedFire = false;
    bool hasUndeploy = false;
};

struct RenderProfile {
    std::string image;
    std::string cameo;
    std::string altCameo;
    std::string foundation;
    std::string palette;
    std::string customPalette;
    std::string altPalette;
    std::string layer;
    bool theater = false;
    bool newTheater = false;
    bool remapableSpecified = false;
    bool remapable = false;
    bool normalized = false;
    bool voxel = false;
    bool turret = false;
    bool shadowSpecified = false;
    bool shadow = false;
    int shadowIndex = -1;
    bool useTurretShadow = false;
    int zAdjust = 0;
    int normalZAdjust = 0;
    std::array<int, 2> zShapePointMove{};
    int ySortAdjust = 0;
    int height = 0;
    bool terrainPalette = false;
    bool animPalette = false;
    bool flat = false;
    bool translucent = false;
    std::string turretOffset;
    std::string primaryFireFLH;
    std::string secondaryFireFLH;
    std::string primaryFirePixelOffset;
    std::string secondaryFirePixelOffset;
    bool primaryFireDualOffset = false;
    bool secondaryFireDualOffset = false;
    std::string elitePrimaryFireFLH;
    std::string eliteSecondaryFireFLH;
    std::vector<std::string> weaponFireFLHs;
    std::vector<std::string> eliteWeaponFireFLHs;
};

struct BuildingAnimProfile {
    std::string foundation;
    std::string buildup;
    std::string bibShape;
    std::string turretAnim;
    bool turretAnimIsVoxel = false;
    int turretAnimX = 0;
    int turretAnimY = 0;
    int turretAnimZAdjust = 0;
    int activeAnimationCount = 0;
    int specialAnimationCount = 0;
    int auxiliaryAnimationCount = 0;
    int damageFireOffsetCount = 0;
    bool hasPoweredAnimation = false;
    bool hasPoweredLightAnimation = false;
    bool hasPoweredSpecialAnimation = false;
    bool hasPoweredEffectAnimation = false;
    bool hasLowPowerAnimation = false;
    bool hasSuperLowPowerAnimation = false;
    bool hasActiveAnimationAnchor = false;
    bool hasSpecialAnimation = false;
    bool hasDamageFireOffsets = false;
    std::vector<std::string> activeAnimationKeys;
    std::vector<std::string> specialAnimationKeys;
    std::vector<std::string> auxiliaryAnimationKeys;
    std::vector<int> damageFireOffsetIndices;
    std::string layer;
    int zAdjust = 0;
    int normalZAdjust = 0;
    std::array<int, 2> zShapePointMove{};
    int ySortAdjust = 0;
    int height = 0;
};

struct ArtType {
    std::string id;
    std::string image;
    std::string cameo;
    std::string altCameo;
    std::string sequence;
    std::string foundation;
    std::string palette;
    std::string customPalette;
    std::string altPalette;
    std::string layer;
    bool theater = false;
    bool newTheater = false;
    bool remapableSpecified = false;
    bool remapable = false;
    bool normalized = false;
    bool voxel = false;
    bool turret = false;
    bool demandLoad = false;
    bool canBeHidden = true;
    int makeInfantry = -1;
    int spawnDelay = 0;
    std::string secondSpawnOffset;
    bool demandLoadBuildup = false;
    bool freeBuildup = false;
    std::string toOverlay;
    int damageLevels = 0;
    int height = 0;
    std::string turretOffset;
    std::string primaryFireFLH;
    std::string secondaryFireFLH;
    std::string primaryFirePixelOffset;
    std::string secondaryFirePixelOffset;
    bool primaryFireDualOffset = false;
    bool secondaryFireDualOffset = false;
    std::string elitePrimaryFireFLH;
    std::string eliteSecondaryFireFLH;
    std::vector<std::string> weaponFireFLHs;
    std::vector<std::string> eliteWeaponFireFLHs;
    std::array<std::string, 5> alternateFireFLHs{};
    int walkFrames = 0;
    int firingFrames = 0;
    int standingFrames = 0;
    int startWalkFrame = 0;
    int startStandFrame = -1;
    int idle1StartFrame = 0;
    int idle1Frames = 0;
    int idle2StartFrame = 0;
    int idle2Frames = 0;
    std::string reportSound;
    std::string startSound;
    int facings = 0;
    std::string trailerAnim;
    std::string expireAnim;
    bool scorch = false;
    bool crater = false;
    bool forceBigCraters = false;
    std::string spawnsParticle;
    int numParticles = 1;
    int damage = 0;
    double maxXYVelocity = 0.0;
    double minZVelocity = 0.0;
    bool bouncer = false;
    int rate = 0;
    int yDrawOffset = 0;
    int translucency = 0;
    int detailLevel = 0;
    int animationStart = 0;
    int animationLoopStart = 0;
    int animationLoopEnd = 0;
    int animationLoopCount = 1;
    int animationEnd = 0;
    bool hasAnimationStart = false;
    bool hasAnimationLoopStart = false;
    bool hasAnimationLoopEnd = false;
    bool hasAnimationEnd = false;
    bool shadowSpecified = false;
    bool shadow = false;
    int shadowIndex = -1;
    bool useTurretShadow = false;
    int zAdjust = 0;
    int normalZAdjust = 0;
    // Signed X/Y displacement of the building depth-template sampling point.
    std::array<int, 2> zShapePointMove{};
    int ySortAdjust = 0;
    bool pingPong = false;
    bool reverse = false;
    std::string buildup;
    std::string bibShape;
    int gateStages = 0;
    std::string specialZOverlay;
    int specialZOverlayZAdjust = 0;
    // Keep explicit false distinct from the stock legacy assets' absent tags.
    bool terrainPaletteSpecified = false;
    bool animPaletteSpecified = false;
    bool terrainPalette = false;
    bool animPalette = false;
    bool flat = false;
    bool translucent = false;
    bool isTiberium = false;
    bool isAnimatedTiberium = false;
    bool hideIfNoOre = false;
    std::string tiberiumSpawnType;
    int tiberiumSpreadRadius = -1;
    bool isAnimDelayedFire = false;
    int delayedFireDelay = 0;
    int trailerSeperation = 0;
    struct ActiveAnimation {
        std::string key;
        std::string image;
        std::string damagedImage;
        int offsetX = 0;
        int offsetY = 0;
        int ySort = 0;
        int zAdjust = 0;
        bool powered = true;
        bool poweredLight = false;
        bool poweredSpecial = false;
        bool poweredEffect = false;
    };
    struct SequenceFrame {
        std::string name;
        bool present = false;
        int startFrame = 0;
        int frameCount = 0;
        int facingMultiplier = 0;
    };
    struct DamageFireOffset {
        int index = 0;
        int offsetX = 0;
        int offsetY = 0;
    };
    struct CellOffset {
        int x = 0;
        int y = 0;
    };
    struct DockingOffset {
        int index = 0;
        int x = 0;
        int y = 0;
        int z = 0;
    };
    // Original BuildingType has ten fixed, zero-based screen-pixel windows.
    std::array<CellOffset, 10> muzzleFlashes{};
    bool hasQueueingCell = false;
    CellOffset queueingCell;
    std::vector<DockingOffset> dockingOffsets;
    std::vector<ActiveAnimation> activeAnimations;
    std::vector<ActiveAnimation> specialAnimations;
    std::vector<ActiveAnimation> auxiliaryAnimations;
    std::vector<SequenceFrame> sequenceFrames;
    std::vector<DamageFireOffset> damageFireOffsets;
    RenderProfile renderProfile;
    SequenceProfile sequenceProfile;
    BuildingAnimProfile buildingAnimProfile;
    bool rotates = false;
    bool useLineTrail = false;
    std::string lineTrailColor = "0,0,0";
    int lineTrailColorDecrement = 16;
};

struct ArtObjectLink {
    std::string objectKind;
    std::string objectId;
    std::string artId;
    bool hasArt = false;
    std::string image;
    std::string cameo;
    std::string altCameo;
    std::string sequence;
    std::string foundation;
    std::string palette;
    std::string customPalette;
    std::string altPalette;
    std::string layer;
    bool theater = false;
    bool newTheater = false;
    bool remapableSpecified = false;
    bool remapable = false;
    bool normalized = false;
    bool voxel = false;
    bool turret = false;
    bool demandLoad = false;
    bool demandLoadBuildup = false;
    int detailLevel = 0;
    bool freeBuildup = false;
    std::string toOverlay;
    int damageLevels = 0;
    int height = 0;
    bool shadowSpecified = false;
    bool shadow = false;
    int shadowIndex = -1;
    bool useTurretShadow = false;
    int zAdjust = 0;
    int normalZAdjust = 0;
    std::array<int, 2> zShapePointMove{};
    int ySortAdjust = 0;
    std::string buildup;
    std::string bibShape;
    int gateStages = 0;
    std::string specialZOverlay;
    int specialZOverlayZAdjust = 0;
    bool terrainPalette = false;
    bool animPalette = false;
    bool flat = false;
    bool translucent = false;
    bool isTiberium = false;
    bool isAnimatedTiberium = false;
    bool hideIfNoOre = false;
    std::string tiberiumSpawnType;
    int tiberiumSpreadRadius = -1;
    bool isAnimDelayedFire = false;
    int delayedFireDelay = 0;
    std::string trailerAnim;
    int trailerSeperation = 0;
    std::string turretOffset;
    std::string primaryFireFLH;
    std::string secondaryFireFLH;
    std::string primaryFirePixelOffset;
    std::string secondaryFirePixelOffset;
    bool primaryFireDualOffset = false;
    bool secondaryFireDualOffset = false;
    std::string elitePrimaryFireFLH;
    std::string eliteSecondaryFireFLH;
    std::vector<std::string> weaponFireFLHs;
    std::vector<std::string> eliteWeaponFireFLHs;
    std::string turretAnim;
    bool turretAnimIsVoxel = false;
    int turretAnimX = 0;
    int turretAnimY = 0;
    int turretAnimZAdjust = 0;
    std::vector<ArtType::ActiveAnimation> activeAnimations;
    std::vector<ArtType::ActiveAnimation> specialAnimations;
    std::vector<ArtType::ActiveAnimation> auxiliaryAnimations;
    std::vector<ArtType::SequenceFrame> sequenceFrames;
    bool hasQueueingCell = false;
    ArtType::CellOffset queueingCell;
    std::vector<ArtType::DockingOffset> dockingOffsets;
    std::vector<ArtType::DamageFireOffset> damageFireOffsets;
    RenderProfile renderProfile;
    SequenceProfile sequenceProfile;
    BuildingAnimProfile buildingAnimProfile;
};

struct ArtCapabilityCacheStats {
    std::size_t artProfileCount = 0;
    std::size_t renderProfileCount = 0;
    std::size_t sequenceProfileCount = 0;
    std::size_t buildingAnimProfileCount = 0;
    std::size_t indexedArtCount = 0;
};

class ArtDatabase {
public:
    bool loadFromIni(const IniFile& ini);

    const std::vector<ArtType>& getArtTypes() const;
    const std::vector<std::string>& getWarnings() const;

    const ArtType* findArtType(const std::string& id) const;
    ArtCapabilityCacheStats getCapabilityCacheStats() const;
    std::vector<ArtObjectLink> buildLinks(const RulesDatabase& rules) const;

private:
    std::vector<ArtType> artTypes_;
    std::vector<std::string> warnings_;
    std::unordered_map<std::string, std::size_t> artIndexById_;

    ArtType loadArtType(const IniFile& ini, const std::string& id);

    int readInt(
        const IniFile& ini,
        const std::string& section,
        const std::string& key,
        int defaultValue
    );

    double readDouble(
        const IniFile& ini,
        const std::string& section,
        const std::string& key,
        double defaultValue
    );

    bool readBool(
        const IniFile& ini,
        const std::string& section,
        const std::string& key,
        bool defaultValue
    );

    std::vector<int> readIntList(
        const IniFile& ini,
        const std::string& section,
        const std::string& key
    );

    void addWarning(const std::string& message);
};
