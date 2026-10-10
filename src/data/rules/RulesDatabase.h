#pragma once

#include "core/io/IniFile.h"
#include "data/rules/ModExtensionTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct PrerequisiteAlias {
    std::string name;
    std::string value;
};

struct SideDefinition {
    std::string name;
    std::vector<std::string> members;
};

struct CountryDefinition {
    std::string id;
    std::vector<std::string> veteranAircraft;
    std::vector<std::string> veteranInfantry;
    std::vector<std::string> veteranUnits;
};

struct CrewSurvivorRules {
    std::array<int, 3> survivorDivisors{500, 250, 750};
    std::array<std::string, 3> crewTypes{"E1", "E2", "INIT"};
    std::string technician = "CTECH";
    std::string engineer = "ENGINEER";
    double escapeChance = 0.5;
};

struct ComputerMiningRules {
    // Original difficulty order: hardest, medium, easiest.
    std::array<int, 3> virtualPurifiers{4, 2, 0};
    std::array<int, 3> harvestersPerRefinery{2, 2, 1};
    std::array<int, 3> slaveMinerNumber{4, 3, 2};
};

struct ComputerBaseBuildingRules {
    // Stock rulesmd.ini fallbacks, in Hard, Normal, Easy order. These are
    // authored defaults, not values supplied by the retail constructor.
    std::array<int, 3> alliedDefenseCounts{25, 20, 6};
    std::array<int, 3> sovietDefenseCounts{25, 22, 6};
    std::array<int, 3> thirdDefenseCounts{25, 22, 6};
    std::array<int, 3> extraRefineries{2, 1, 0};
    std::vector<std::string> refineryTypes{"NAREFN", "GAREFN", "YAREFN"};
    std::vector<std::string> harvesterTypes{"HARV", "CMIN"};
};

struct ComputerTacticsRules {
    // Difficulty arrays use the game's Hard, Normal, Easy order.
    std::array<int, 3> teamDelays{2000, 2500, 3500};
    std::array<int, 3> minimumDefensiveTeams{1, 1, 1};
    std::array<int, 3> maximumDefensiveTeams{2, 2, 2};
    std::array<int, 3> totalTeamCap{30, 30, 30};
    std::array<int, 3> fillEarliestTeamProbability{100, 100, 100};
    int dissolveUnfilledTeamDelay = 5000;
    bool useMinimumDefenseRule = true;
    double minorSuperReadyPercent = 0.7;
    int safeDistance = 20;
    double stray = 2.0;
    double relaxedStray = 3.0;
    double successWeightDelta = 20.0;
    double failureWeightDelta = -50.0;
    double trackRecordCoefficient = 1.0;
    std::array<int, 4> captureNormal{75, 5, 5, 15};
    std::array<int, 4> captureWounded{15, 40, 40, 5};
    std::array<int, 4> captureLowPower{15, 5, 75, 5};
    std::array<int, 4> captureLowMoney{15, 75, 5, 5};
    int captureLowMoneyMark = 2000;
    double captureWoundedMark = 0.25;
};

struct ObjectCapability {
    bool movable = false;
    bool infantry = false;
    bool vehicle = false;
    bool aircraft = false;
    bool building = false;
    bool naval = false;
    bool waterBound = false;
    bool turret = false;
    bool crusher = false;
    bool crushable = false;
    bool omniCrusher = false;
    bool omniCrushResistant = false;
    bool repairable = true;
    bool canPassiveAquire = true;
    bool canRetaliate = true;
    int speed = 0;
    std::string speedType;
    std::string movementZone;
    std::string locomotor;
    std::string category;
};

struct WeaponSet {
    std::string primary;
    std::string primarySource;
    std::string secondary;
    std::string secondarySource;
    std::string elitePrimary;
    std::string elitePrimarySource;
    std::string eliteSecondary;
    std::string eliteSecondarySource;
    std::string deploy;
    std::string eliteDeploy;
    std::string deploySource;
    std::string eliteDeploySource;
    std::string occupy;
    std::string occupySource;
    std::string eliteOccupy;
    std::string eliteOccupySource;
    int openTransportWeapon = -1;
    std::string openTransportWeaponSource;
    int indexedWeaponCount = 0;
    std::vector<std::string> indexedWeapons;
    std::vector<std::string> eliteIndexedWeapons;
};

struct DeployProfile {
    bool canDeploy = false;
    bool deployer = false;
    bool deployFire = false;
    bool deployToFire = false;
    bool isSimpleDeployer = false;
    bool noMovingFire = false;
    bool deployToLand = false;
    bool deployedCrushable = false;
    double deployTime = 0.0;
    int undeployDelay = -1;
    int deployFacing = -1;
    std::string deploysInto;
    std::string undeploysInto;
    std::string unloadingClass;
    std::string deployWeapon;
    std::string eliteDeployWeapon;
    std::string deployWeaponSource;
    std::string eliteDeployWeaponSource;
    std::string deployingAnim;
    std::string voiceDeploy;
    std::string deploySound;
    std::string undeploySound;
    std::string deploySequence;
    std::string deployIdleSequence;
    std::string deployFireSequence;
    std::string deployedSequence;
    std::string deployedIdleSequence;
    std::string deployedFireSequence;
    std::string undeploySequence;
};

struct OccupyProfile {
    bool occupier = false;
    bool canBeOccupied = false;
    bool canOccupyFire = false;
    bool distributedFire = false;
    int maxNumberOccupants = 0;
    std::string occupyWeapon;
    std::string eliteOccupyWeapon;
    std::string occupyPip;
};

struct EngineerProfile {
    bool engineer = false;
    bool bridgeRepair = false;
    bool repairable = true;
    bool clickRepairable = true;
    bool unsellable = false;
    bool capturable = true;
    bool needsEngineer = false;
    bool bridgeRepairHut = false;
};

struct TransportProfile {
    bool canTransport = false;
    bool airportBound = false;
    int passengers = 0;
    int size = 0;
    int openTransportWeapon = -1;
    std::string unloadingClass;
    std::string dock;
};

struct BuildingCapability {
    int power = 0;
    bool powered = false;
    bool factory = false;
    bool constructionYard = false;
    bool refinery = false;
    bool barracksFactory = false;
    bool vehicleFactory = false;
    bool aircraftFactory = false;
    bool navalFactory = false;
    bool weaponsFactory = false;
    bool factoryPlant = false;
    double infantryCostBonus = 1.0;
    double unitsCostBonus = 1.0;
    double aircraftCostBonus = 1.0;
    double buildingsCostBonus = 1.0;
    double defensesCostBonus = 1.0;
    bool unitRepair = false;
    bool hospital = false;
    bool armory = false;
    // Hospital/Armory reuse BuildingType Ammo as a finite service-use count.
    // Keep the authored value here so multiplayer tech-tree fingerprints see
    // the same service capacity as the runtime.
    int ammo = -1;
    bool cloning = false;
    int infantryGainSelfHeal = 0;
    int unitsGainSelfHeal = 0;
    bool gapGenerator = false;
    int gapRadiusInCells = 0;
    int superGapRadiusInCells = 0;
    bool secretLab = false;
    std::vector<std::string> secretInfantry;
    std::vector<std::string> secretUnits;
    std::vector<std::string> secretBuildings;
    int produceCashStartup = 0;
    int produceCashAmount = 0;
    int produceCashDelay = 0;
    bool grinding = false;
    bool bunker = false;
    bool infantryAbsorb = false;
    int passengers = 0;
    int sizeLimit = 0;
    int extraPower = 0;
    bool orePurifier = false;
    bool spySat = false;
    bool radar = false;
    std::string buildCat;
    std::string factoryType;
    std::string freeUnit;
    std::string prerequisite;
    std::string prerequisiteOverride;
};

struct WeaponProfile {
    std::string id;
    int damage = 0;
    int rof = 0;
    double range = 0.0;
    double minimumRange = 0.0;
    std::string projectile;
    int speed = 0;
    std::string warhead;
    int burst = 0;
    std::string report;
    std::string anim;
    bool charges = false;
    bool bright = false;
    bool decloakToFire = true;
    bool cellRangefinding = false;
    bool neverUse = false;
    bool laser = false;
    bool houseColorLaser = false;
    bool diskLaser = false;
    bool drainWeapon = false;
    std::string laserInnerColor;
    std::string laserOuterColor;
    std::string laserOuterSpread;
    int laserDuration = 0;
    int laserThickness = 1;
    bool magBeam = false;
    bool limboLaunch = false;
    bool disguiseFireOnly = false;
    bool fireOnce = false;
    bool areaFire = false;
};

struct WeaponLifecycleProfile {
    std::string id;
    bool omniFire = false;
    bool fireOnce = false;
    bool distributedFire = false;
};

struct ProjectileProfile {
    std::string id;
    std::string image;
    std::string effectiveImage;
    std::string palette;
    std::string color;
    bool drawProjectile = true;
    bool imageIsNone = false;
    bool inviso = false;
    bool paletteIsSet = false;
    bool firersPalette = false;
    bool animPalette = false;
    bool arcing = false;
    bool vertical = false;
    bool ranged = false;
    bool proximity = false;
    bool airburst = false;
    bool aa = false;
    bool ag = true;
    bool an = false;
    bool asw = false;
    bool shadow = true;
    bool subjectToCliffs = false;
    bool subjectToElevation = false;
    bool subjectToWalls = false;
    bool dropping = false;
    bool level = false;
    bool veryHigh = false;
    int courseLockDuration = 0;
    int rot = 0;
    int frames = 0;
    int translucency = 0;
    int cluster = 0;
    int acceleration = 3;
    std::string trailer;
    int trailerSeperation = 0;
    std::string airburstWeapon;
    std::string shrapnelWeapon;
    int shrapnelCount = 0;
};

struct WarheadProfile {
    bool penetratesBunker = false;
    std::string id;
    double cellSpread = 0.0;
    double percentAtMax = 0.0;
    std::string verses;
    std::string animList;
    std::string anim;
    bool versesValid = false;
    int versesCount = 0;
    bool damagesInfantry = false;
    bool damagesVehicles = false;
    bool damagesBuildings = false;
    bool damagesWalls = false;
    bool damagesWood = false;
    std::string explosion;
    std::string particle;
    std::string particleSystem;
    int infDeath = 0;
    std::string proneDamage;
    double proneDamageMultiplier = 1.0;
    double combatLightSize = 0.0;
    bool clDisableRed = false;
    bool clDisableGreen = false;
    bool clDisableBlue = false;
    bool wall = false;
    bool wood = false;
    bool affectsAllies = false;
    bool affectsEnemies = true;
    bool bright = false;
    bool conventional = false;
    bool sparky = true;
    bool fire = false;
    bool tiberium = false;
    bool mindControl = false;
    bool temporal = false;
    bool radiation = false;
    bool electricAssault = false;
    bool psychicDamage = false;
    bool makesDisguise = false;
    bool parasite = false;
    bool culling = false;
    int paralyzes = 0;
    bool psychedelic = false;
    bool ivanBomb = false;
    bool bombDisarm = false;
    bool airstrike = false;
    bool isLocomotor = false;
    std::string locomotor;
    int cellInset = 0;
    bool poison = false;
};

struct WarheadLifecycleProfile {
    std::string id;
    int shakeXlo = 0;
    int shakeXhi = 0;
    int shakeYlo = 0;
    int shakeYhi = 0;
};

struct UiProfile {
    std::string pip;
    std::string pipScale;
    std::string occupyPip;
    bool pipsDrawForAll = false;
    int strength = 0;
    int power = 0;
    bool powered = false;
    bool hasRulesSidebarResource = false;
    bool hasRulesPowerResource = false;
    bool hasRulesHealthResource = false;
    std::string missingResourceNote;
};

enum class VeteranAbility {
    Faster,
    Stronger,
    Firepower,
    Scatter,
    Rof,
    Sight,
    Cloak,
    TiberiumProof,
    VeinProof,
    SelfHeal,
    Explodes,
    RadarInvisible,
    Sensors,
    Fearless,
    C4,
    TiberiumHeal,
    GuardArea,
    Crusher
};

struct VeterancyProfile {
    bool trainable = true;
    std::vector<VeteranAbility> veteranAbilities;
    // EliteAbilities are cumulative in the original engine. This collection
    // therefore contains both VeteranAbilities and the elite-only additions.
    std::vector<VeteranAbility> eliteAbilities;
};

struct VeterancyRules {
    double veteranRatio = 3.0;
    double veteranCombat = 1.0;
    double veteranSpeed = 1.0;
    double veteranSight = 1.0;
    double veteranArmor = 1.0;
    double veteranRof = 1.0;
    double selfHealRateMinutes = 0.016;
    int veteranCap = 2;
    bool initialVeteran = false;
};

struct TerrainMovementRules {
    double trackedUphill = 1.0;
    double trackedDownhill = 1.0;
    double wheeledUphill = 1.0;
    double wheeledDownhill = 1.0;
};

struct ElevationModelRules {
    int increment = 4;
    double incrementBonus = 2.0;
    double bonusCap = 2.0;
};

struct SpecialBuildingRules {
    int selfHealInfantryFrames = 50;
    int selfHealInfantryAmount = 20;
    int selfHealUnitFrames = 75;
    int selfHealUnitAmount = 5;
    std::vector<std::string> secretInfantry;
    std::vector<std::string> secretUnits;
    std::vector<std::string> secretBuildings;
};

struct ProductionRules {
    // Minutes required to produce a 1000-credit object before modifiers.
    double buildSpeed = 0.7;
    // Average buildup minutes; retail constructor default, overridden by rules.
    double buildupTime = 0.05;
    // Cumulative time multiplier contributed by each additional factory.
    double multipleFactory = 0.8;
    double wallBuildSpeedCoefficient = 3.0;
    double lowPowerPenaltyModifier = 1.0;
    double minLowPowerProductionSpeed = 0.5;
    double maxLowPowerProductionSpeed = 0.8;
};

struct HarvesterCadenceRules {
    // Retail StageClass interval: nine stages complete one ordinary pickup.
    int loadRate = 2;
    // Minutes between unloading complete resource types (ore, then gems).
    double dumpRateMinutes = 0.016;
};

struct CratePowerupRule {
    std::string type;
    int weight = 0;
    std::string animation;
    bool availableOverWater = false;
    double data = 0.0;
};

struct CrateRules {
    int maximum = 255;
    int minimum = 1;
    double radius = 3.0;
    double regenerationMinutes = 3.0;
    std::string silverReward = "HealBase";
    int soloMoney = 5000;
    std::string unitType = "none";
    std::string woodReward = "Money";
    std::string waterReward = "Money";
    std::string healSound = "HealCrate";
    std::string woodImage = "CRATE";
    std::string image = "CRATE";
    std::string waterImage = "WCRATE";
    bool freeMcv = true;
    std::vector<CratePowerupRule> powerups;
};

struct UnitType {
    std::string ambientSound;
    std::string turretRotateSound;
    std::string auxSound1;
    std::string auxSound2;
    int pixelSelectionBracketDelta = 0;
    int minDebris = 0;
    int maxDebris = 0;
    std::string debrisTypes;
    std::string debrisMaximums;
    std::string debrisAnims;
    bool alternateArcticArt = false;
    bool bunkerable = false;
    std::string id;
    std::string uiName;
    std::string name;
    std::string image;
    std::string owner;
    std::string requiredHouses;
    std::string forbiddenHouses;
    int techLevel = 0;
    std::optional<int> buildLimit;
    double buildTimeMultiplier = 1.0;
    VeterancyProfile veterancy;
    std::string prerequisite;
    std::string prerequisiteOverride;
    bool dontScore = false;
    bool insignificant = false;
    int strength = 0;
    std::string armor;
    std::vector<std::string> damageParticleSystems;
    std::array<int, 3> damageSmokeOffset{0, 0, 0};
    bool explodes = false;
    std::string deathWeapon;
    double deathWeaponDamageModifier = 1.0;
    std::string primaryWeapon;
    std::string secondaryWeapon;
    std::string elitePrimaryWeapon;
    std::string eliteSecondaryWeapon;
    int turretCount = 0;
    int weaponCount = 0;
    std::vector<std::string> indexedWeapons;
    std::vector<std::string> eliteIndexedWeapons;
    bool chargeTurret = false;
    bool gattling = false;
    int weaponStages = 0;
    std::vector<int> weaponStageThresholds;
    std::vector<int> eliteWeaponStageThresholds;
    int weaponRateUp = 0;
    int weaponRateDown = 0;
    int speed = 0;
    bool accelerates = true;
    double accelerationFactor = 0.03;
    double hoverAcceleration = 0.02;
    int threatPosed = 0;
    double threatAvoidanceCoefficient = 0.0;
    int walkRate = 1;
    int idleRate = 0;
    double weight = 1.0;
    std::string speedType;
    std::string movementZone;
    std::string locomotor;
    // Retail ground slope alignment defaults to yes; authored exceptions
    // (e.g. DRON and CAOS) retain their flat body orientation.
    bool isTilter = true;
    bool jumpJet = false;
    double jumpjetSpeed = 14.0;
    double jumpjetClimb = 5.0;
    double jumpjetAccel = 2.0;
    int jumpjetTurnRate = 4;
    double jumpjetWobbles = 0.15;
    int jumpjetDeviation = 40;
    bool jumpjetNoWobbles = false;
    bool crashable = false;
    bool tiltCrashJumpjet = false;
    double jumpjetCrash = 5.0;
    double crashGravity = 6.0;
    double shipSinkingWeight = 3.0;
    std::string crashingSound;
    std::string sinkingSound;
    std::string impactLandSound;
    std::string impactWaterSound;
    bool consideredAircraft = false;
    bool balloonHover = false;
    int jumpJetHeight = 0;
    int sight = 0;
    std::string category;
    int navalTargeting = 0;
    int landTargeting = 0;
    int guardRange = 0;
    double airRangeBonus = 0.0;
    std::string spawns;
    int spawnsNumber = 0;
    int spawnRegenRate = 0;
    int spawnReloadRate = 0;
    bool spawned = false;
    bool missileSpawn = false;
    bool noSpawnAlt = false;
    bool noShadow = false;
    bool underwater = false;
    bool organic = false;
    bool fearless = false;
    bool fraidycat = false;
    bool notHuman = false;
    bool cloakable = false;
    int cloakingSpeed = 7;
    bool sensors = false;
    bool sensorArray = false;
    int sensorsSight = 0;
    bool crusher = false;
    bool crushable = false;
    bool omniCrusher = false;
    bool omniCrushResistant = false;
    bool repairable = true;
    // RA2/YR TechnoType SelfHealing recovers to full health at RepairRate.
    bool selfHealing = false;
    bool tiberiumProof = false;
    bool tiberiumHeal = false;
    bool crateGoodie = false;
    bool engineer = false;
    bool bridgeRepair = false;
    bool turret = false;
    bool turretSpins = false;
    int passengers = 0;
    int size = 0;
    int sizeLimit = 0;
    std::string deploysInto;
    std::string undeploysInto;
    std::string enslaves;
    std::string deployWeapon;
    std::string eliteDeployWeapon;
    std::string deployWeaponSource;
    std::string eliteDeployWeaponSource;
    bool deployer = false;
    bool deployFire = false;
    bool deployToFire = false;
    bool isSimpleDeployer = false;
    std::string unloadingClass;
    double deployTime = 0.0;
    int undeployDelay = -1;
    bool deployedCrushable = false;
    bool deployToLand = false;
    int deployFacing = -1;
    std::string deployingAnim;
    bool noMovingFire = false;
    bool occupier = false;
    std::string occupyWeapon;
    std::string eliteOccupyWeapon;
    int openTransportWeapon = -1;
    int ifvMode = -1;
    std::array<int, 17> ifvTurretIndices = [] {
        std::array<int, 17> values{};
        values.fill(-1);
        return values;
    }();
    std::array<int, 17> ifvWeaponIndices = [] {
        std::array<int, 17> values{};
        values.fill(-1);
        return values;
    }();
    bool gunner = false;
    bool openTopped = false;
    bool opportunityFire = false;
    bool preventAttackMove = false;
    bool distributedFire = false;
    int radialFireSegments = 0;
    std::string pip;
    std::string pipScale;
    std::string occupyPip;
    bool canPassiveAquire = true;
    bool canRetaliate = true;
    bool airportBound = false;
    bool poweredUnit = false;
    std::string dock;
    bool waterBound = false;
    bool naval = false;
    bool harvester = false;
    bool teleporter = false;
    bool moveToShroud = true;
    bool agent = false;
    bool infiltrate = false;
    bool requiresStolenAlliedTech = false;
    bool requiresStolenSovietTech = false;
    bool requiresStolenThirdTech = false;
    bool c4 = false;
    bool ivan = false;
    bool bombable = true;
    int bombSight = 0;
    bool parasiteable = true;
    int suppressionThreshold = 0;
    bool reselectIfLimboed = false;
    bool rejoinTeamIfLimboed = false;
    bool berserkFriendly = false;
    bool disguiseWhenStill = false;
    bool canDisguise = false;
    bool permaDisguise = false;
    bool detectDisguise = false;
    int detectDisguiseRange = 0;
    int airstrikeTeam = 0;
    int eliteAirstrikeTeam = 0;
    std::string airstrikeTeamType;
    std::string eliteAirstrikeTeamType;
    int airstrikeRechargeTime = 0;
    int eliteAirstrikeRechargeTime = 0;
    bool immuneToPsionics = false;
    int mindControlRingOffset = 140;
    bool immuneToRadiation = false;
    bool immuneToTemporal = false;
    bool immuneToElectric = false;
    bool resourceGatherer = false;
    bool resourceDestination = false;
    bool slaved = false;
    bool allowedToStartInMultiplayer = true;
    int storage = 0;
    int slavesNumber = 0;
    int slaveRegenRate = 0;
    int slaveReloadRate = 0;
    int harvestRate = 0;
    // Negative Ammo means the type has no finite ammunition magazine.
    int ammo = -1;
    // Negative InitialAmmo means a finite magazine starts full.
    int initialAmmo = -1;
    // Stock aircraft reload while docked unless their type opts out. Other
    // techno categories retain the original false default.
    bool manualReload = false;
    int rot = 0;
    // Fixed recycler/sale refund. Zero falls back to the global refund ratio.
    int soylent = 0;
    int cost = 0;
    bool crewed = false;
    bool carriesCrate = false;
    bool isTrain = false;
    bool isSelectableCombatant = false;
    std::string voiceSelect;
    std::string voiceMove;
    std::string voiceAttack;
    std::string voiceFeedback;
    std::string voiceSpecialAttack;
    std::string voiceSecondaryWeaponAttack;
    std::string voiceCrashing;
    std::string voiceSelectDeactivated;
    std::string voiceSelectEnslaved;
    std::string voiceEnter;
    std::string voiceCapture;
    std::string voiceHarvest;
    std::string moveSound;
    std::string createSound;
    std::string dieSound;
    std::string crushSound;
    std::string enterTransportSound;
    std::string leaveTransportSound;
    std::string enterWaterSound;
    std::string leaveWaterSound;
    std::string chronoInSound;
    std::string chronoOutSound;
    std::string activateSound;
    std::string deactivateSound;
    std::string voiceDeploy;
    std::string deploySound;
    std::string undeploySound;
    std::string explosion;
    ObjectCapability capability;
    WeaponSet weaponSet;
    DeployProfile deployProfile;
    OccupyProfile occupyProfile;
    EngineerProfile engineerProfile;
    TransportProfile transportProfile;
    UiProfile uiProfile;
    bool immune = false;
    bool typeImmune = false;
    bool immuneToPoison = false;
};

struct RefinerySmokeOffset {
    std::string name;
    int x = 0;
    int y = 0;
    int z = 0;
};

struct RefinerySmokeProfile {
    std::vector<RefinerySmokeOffset> offsets;
    int frames = 0;
    std::string particleSystem;
    std::string particle;
    std::string image;
};

struct BuildingType {
    std::string ambientSound;
    std::string turretRotateSound;
    bool alternateArcticArt = false;
    std::string id;
    std::string uiName;
    std::string name;
    std::string image;
    std::string owner;
    std::string requiredHouses;
    std::string forbiddenHouses;
    int techLevel = 0;
    std::optional<int> buildLimit;
    double buildTimeMultiplier = 1.0;
    VeterancyProfile veterancy{false, {}, {}};
    std::string prerequisite;
    std::string prerequisiteOverride;
    bool dontScore = false;
    bool insignificant = false;
    int strength = 0;
    bool noShadow = false;
    int threatPosed = 0;
    bool immune = false;
    bool typeImmune = false;
    bool immuneToPsionics = true;
    bool immuneToRadiation = false;
    bool immuneToTemporal = false;
    bool immuneToElectric = false;
    std::string armor;
    bool explodes = false;
    std::string deathWeapon;
    double deathWeaponDamageModifier = 1.0;
    std::string primaryWeapon;
    std::string secondaryWeapon;
    std::string elitePrimaryWeapon;
    std::string eliteSecondaryWeapon;
    int turretCount = 0;
    int weaponCount = 0;
    std::vector<std::string> indexedWeapons;
    std::vector<std::string> eliteIndexedWeapons;
    bool chargeTurret = false;
    bool gattling = false;
    int weaponStages = 0;
    std::vector<int> weaponStageThresholds;
    std::vector<int> eliteWeaponStageThresholds;
    int weaponRateUp = 0;
    int weaponRateDown = 0;
    int rot = 0;
    int power = 0;
    bool powered = false;
    bool poweredSpecial = false;
    bool drainable = false;
    bool overpowerable = false;
    int navalTargeting = 0;
    int landTargeting = 0;
    int guardRange = 0;
    double airRangeBonus = 0.0;
    int sight = 0;
    bool organic = false;
    bool cloakable = false;
    int cloakingSpeed = 7;
    bool sensors = false;
    bool sensorArray = false;
    int sensorsSight = 0;
    std::string buildCat;
    int adjacent = 1;
    bool baseNormal = true;
    bool eligibleForAllyBuilding = false;
    int psychicDetectionRadius = 0;
    bool isBaseDefense = false;
    // Westwood uses 0/1/2 for Allied/Soviet/Yuri base-planning content.
    // Stock buildings often list every country in Owner, so this is also
    // required to keep another side's cameos out of a player's sidebar.
    int aiBasePlanningSide = -1;
    std::string powersUnit;
    std::string factory;
    std::string freeUnit;
    std::string deploysInto;
    std::string undeploysInto;
    std::string enslaves;
    std::string deployWeapon;
    std::string eliteDeployWeapon;
    std::string deployWeaponSource;
    std::string eliteDeployWeaponSource;
    bool deployer = false;
    bool deployFire = false;
    bool deployToFire = false;
    bool isSimpleDeployer = false;
    std::string unloadingClass;
    double deployTime = 0.0;
    int deployFacing = -1;
    bool noMovingFire = false;
    bool refinery = false;
    bool dockUnload = false;
    int numberOfDocks = 0;
    bool resourceGatherer = false;
    bool resourceDestination = false;
    bool wall = false;
    bool gate = false;
    double gateCloseDelay = 0.0;
    bool crusher = false;
    bool crushable = false;
    bool omniCrusher = false;
    bool omniCrushResistant = false;
    bool repairable = true;
    // TechnoType SelfHealing also applies to mod-authored structures.
    bool selfHealing = false;
    bool clickRepairable = true;
    bool unsellable = false;
    bool capturable = true;
    bool spyable = false;
    bool canC4 = true;
    bool bombable = true;
    int bombSight = 0;
    bool detectDisguise = false;
    int detectDisguiseRange = 0;
    bool waterBound = false;
    bool naval = false;
    bool needsEngineer = false;
    std::string captureEvaEvent;
    bool bridgeRepairHut = false;
    bool canBeOccupied = false;
    bool canOccupyFire = false;
    bool distributedFire = false;
    int maxNumberOccupants = 0;
    std::string pip;
    std::string pipScale;
    bool pipsDrawForAll = false;
    std::string explosion;
    int minDebris = 0;
    int maxDebris = 0;
    std::string debrisTypes;
    std::string debrisMaximums;
    std::string debrisAnims;
    bool constructionYard = false;
    bool weaponsFactory = false;
    bool factoryPlant = false;
    double infantryCostBonus = 1.0;
    double unitsCostBonus = 1.0;
    double aircraftCostBonus = 1.0;
    double buildingsCostBonus = 1.0;
    double defensesCostBonus = 1.0;
    bool unitRepair = false;
    bool unitReload = false;
    bool unitSell = false;
    int numberImpassableRows = 0;
    bool hospital = false;
    bool armory = false;
    // Unlike ordinary RA2/YR armed structures, Hospital/Armory interpret
    // Ammo as a non-reloading number of infantry services.
    int ammo = -1;
    bool cloning = false;
    int infantryGainSelfHeal = 0;
    int unitsGainSelfHeal = 0;
    bool gapGenerator = false;
    int gapRadiusInCells = 0;
    int superGapRadiusInCells = 0;
    bool secretLab = false;
    std::vector<std::string> secretInfantry;
    std::vector<std::string> secretUnits;
    std::vector<std::string> secretBuildings;
    int produceCashStartup = 0;
    int produceCashAmount = 0;
    int produceCashDelay = 0;
    bool grinding = false;
    bool bunker = false;
    bool infantryAbsorb = false;
    int passengers = 0;
    int sizeLimit = 0;
    int extraPower = 0;
    bool orePurifier = false;
    bool spySat = false;
    bool radar = false;
    bool turret = false;
    std::string turretAnim;
    bool turretAnimIsVoxel = false;
    int turretAnimX = 0;
    int turretAnimY = 0;
    int turretAnimZAdjust = 0;
    bool turretRecoil = false;
    int turretTravel = 0;
    int barrelTravel = 0;
    int barrelCompressFrames = 0;
    int barrelHoldFrames = 0;
    int barrelRecoverFrames = 0;
    std::string superWeapon;
    std::string superWeapon2;
    std::vector<std::string> extraSuperWeapons;
    // Minutes remaining when the authored SuperAnimTwo transition starts.
    double chargedAnimTime = 0.0;
    bool canPassiveAquire = true;
    bool canRetaliate = true;
    int storage = 0;
    int slavesNumber = 0;
    int slaveRegenRate = 0;
    int slaveReloadRate = 0;
    int harvestRate = 0;
    // Fixed recycler/sale refund. Zero falls back to the global refund ratio.
    int soylent = 0;
    int cost = 0;
    bool crewed = false;
    std::string destroyAnim;
    bool leaveRubble = false;
    bool invisibleInGame = false;
    int lightVisibility = 5000;
    // Original light levels are signed thousandths, including negative tints.
    int lightIntensity = 0;
    int lightRedTint = 1000;
    int lightGreenTint = 1000;
    int lightBlueTint = 1000;
    bool crateBeneath = false;
    bool crateBeneathIsMoney = false;
    bool revealToAll = false;
    std::string createUnitSound;
    std::string damageSound;
    std::string dieSound;
    std::string crushSound;
    std::string workingSound;
    std::string notWorkingSound;
    RefinerySmokeProfile refinerySmoke;
    ObjectCapability capability;
    WeaponSet weaponSet;
    DeployProfile deployProfile;
    OccupyProfile occupyProfile;
    EngineerProfile engineerProfile;
    TransportProfile transportProfile;
    BuildingCapability buildingCapability;
    UiProfile uiProfile;
    bool immuneToPoison = false;
};

struct WeaponType {
    std::string id;
    int damage = 0;
    int rof = 0;
    double range = 0.0;
    double minimumRange = 0.0;
    std::string projectile;
    int speed = 0;
    std::string warhead;
    int burst = 0;
    int burstDelay = 3;
    bool burstDelaySpecified = false;
    std::vector<int> burstDelays;
    bool turboBoost = false;
    std::string report;
    std::string anim;
    std::string occupantAnim;
    std::string openToppedAnim;
    bool charges = false;
    bool bright = false;
    bool decloakToFire = true;
    bool revealOnFire = true;
    bool cellRangefinding = false;
    bool migAttackCursor = false;
    bool sabotageCursor = false;
    bool neverUse = false;
    bool suicide = false;
    bool fireInTransport = true;
    bool laser = false;
    bool houseColorLaser = false;
    bool diskLaser = false;
    bool drainWeapon = false;
    std::string laserInnerColor;
    std::string laserOuterColor;
    std::string laserOuterSpread;
    int laserDuration = 0;
    int laserThickness = 1;
    bool sonic = false;
    int ambientDamage = 0;
    bool electricBolt = false;
    bool alternateElectricColor = false;
    bool radBeam = false;
    bool magBeam = false;
    bool isRailgun = false;
    bool useSparkParticles = false;
    bool useFireParticles = false;
    std::string attachedParticleSystem;
    bool spawner = false;
    bool limboLaunch = false;
    bool infiniteMindControl = false;
    bool fireOnce = false;
    bool permanentMindControl = false;
    int disguiseFakeBlinkTime = 0;
    bool disguiseFireOnly = false;
    bool areaFire = false;
    bool isRadEruption = false;
    int radLevel = 0;
    WeaponProfile profile;
    WeaponLifecycleProfile lifecycle;
};

struct ProjectileType {
    std::string id;
    std::string image;
    std::string palette;
    std::string color;
    bool paletteIsSet = false;
    bool arcing = false;
    bool floater = false;
    int rot = 0;
    bool aa = false;
    bool ag = true;
    bool agExplicit = false;
    bool an = false;
    bool asw = false;
    bool inviso = false;
    bool imageIsNone = false;
    bool animates = false;
    bool inaccurate = false;
    bool flakScatter = false;
    bool subjectToCliffs = false;
    bool subjectToElevation = false;
    bool subjectToWalls = false;
    bool shadow = true;
    bool high = false;
    bool veryHigh = false;
    bool vertical = false;
    bool airburst = false;
    bool proximity = false;
    bool ranged = false;
    bool dropping = false;
    bool level = false;
    int courseLockDuration = 0;
    bool firersPalette = false;
    bool animPalette = false;
    int frames = 0;
    int translucency = 0;
    int cluster = 0;
    int acceleration = 3;
    std::string trailer;
    int trailerSeperation = 0;
    std::string airburstWeapon;
    std::string shrapnelWeapon;
    int shrapnelCount = 0;
    ProjectileProfile profile;
};

struct OverlayType {
    std::string id;
    int listIndex = -1;
    std::string name;
    std::string image;
    bool theater = false;
    bool newTheater = false;
    std::string palette;
    std::string altPalette;
    std::string toOverlay;
    std::string foundation;
    bool tiberium = false;
    bool gem = false;
    bool ore = false;
    bool crate = false;
    bool crateTrigger = false;
    bool legalTarget = false;
    bool radarInvisible = false;
    std::string radarColor;
    bool noUseTileLandType = false;
    bool drawFlat = false;
    bool wall = false;
    bool isARock = false;
    int zAdjust = 0;
    int shadowIndex = -1;
};

struct SmudgeType {
    std::string id;
    int listIndex = -1;
    bool crater = false;
    bool burn = false;
    int width = 1;
    int height = 1;
};

struct TiberiumType {
    std::string id;
    int listIndex = -1;
    std::string name;
    int image = 0;
    int value = 0;
    int growth = 0;
    double growthPercentage = 0.0;
    int spread = 0;
    double spreadPercentage = 0.0;
    int power = 0;
    std::string color;
};

struct TerrainType {
    std::string ambientSound;
    std::string id;
    int listIndex = -1;
    std::string name;
    bool spawnsTiberium = false;
    bool isAnimated = false;
    int animationRate = 0;
    double animationProbability = 0.0;
    int strength = 0;
    bool immune = false;
    std::string radarColor;
};

struct LandType {
    std::string id;
    double foot = 100.0;
    double track = 100.0;
    double wheel = 100.0;
    double floatSpeed = 100.0;
    double floatBeach = 100.0;
    double hover = 100.0;
    double amphibious = 100.0;
    bool buildable = true;
};

enum class ParticleBehavior {
    None,
    Gas,
    Smoke,
    Fire,
    Spark,
    Railgun,
    Web,
    WeakGas,
    Unknown
};

struct ParticleVector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct ParticleColor {
    int red = 0;
    int green = 0;
    int blue = 0;
};

struct ParticleSystemType {
    std::string id;
    int listIndex = -1;
    std::string holdsWhat;
    std::string behaviorName;
    ParticleBehavior behavior = ParticleBehavior::None;
    std::string image;
    bool spawns = false;
    int spawnFrames = 1;
    double slowdown = 0.0;
    int particleCap = 500;
    int spawnRadius = 0;
    double spawnCutoff = 0.0;
    double spawnTranslucencyCutoff = 0.0;
    int lifetime = -1;
    ParticleVector3 spawnDirection;
    double particlesPerCoord = 0.1;
    double spiralDeltaPerCoord = 0.025;
    double spiralRadius = 2.890625;
    double positionPerturbationCoefficient = 0.0;
    double movementPerturbationCoefficient = 0.0;
    double velocityPerturbationCoefficient = 0.0;
    double spawnSparkPercentage = 0.0;
    int sparkSpawnFrames = 0;
    int lightSize = 0;
    ParticleColor laserColor;
    bool hasLaserColor = false;
    bool laser = false;
    bool oneFrameLight = false;
};

struct ParticleType {
    std::string id;
    int listIndex = -1;
    std::string image;
    std::string behaviorName;
    ParticleBehavior behavior = ParticleBehavior::None;
    ParticleVector3 nextParticleOffset;
    int xVelocity = 0;
    int yVelocity = 0;
    int minZVelocity = 0;
    int zVelocityRange = 0;
    double colorSpeed = 0.0;
    std::vector<ParticleColor> colorList;
    ParticleColor startColor1;
    ParticleColor startColor2;
    bool hasStartColor1 = false;
    bool hasStartColor2 = false;
    int maxDamageDelay = 0;
    int maxLifetime = 0;
    std::string warhead;
    int damage = 0;
    int startFrame = 0;
    int numLoopFrames = 1;
    bool numLoopFramesSpecified = false;
    int translucency = 0;
    int windEffect = 0;
    double velocity = 0.0;
    double deacceleration = 0.0;
    int radius = 0;
    bool deleteOnStateLimit = false;
    int endStateAI = 0;
    int startStateAI = 0;
    int stateAIAdvance = 0;
    int finalDamageState = 0;
    int translucent25State = 0;
    int translucent50State = 0;
    bool normalized = false;
    std::string nextParticle;
    std::string report;
};

struct WarheadType {
    bool rocker = false;
    bool penetratesBunker = false;
    std::string id;
    double cellSpread = 0.0;
    double percentAtMax = 0.0;
    std::string verses;
    std::string animList;
    std::string explosion;
    std::string particle;
    std::string particleSystem;
    int infDeath = 0;
    std::string proneDamage;
    double proneDamageMultiplier = 1.0;
    double combatLightSize = 0.0;
    bool clDisableRed = false;
    bool clDisableGreen = false;
    bool clDisableBlue = false;
    bool wall = false;
    bool wood = false;
    bool affectsAllies = false;
    bool affectsEnemies = true;
    bool bright = false;
    bool conventional = false;
    bool sparky = true;
    bool fire = false;
    bool tiberium = false;
    bool mindControl = false;
    bool temporal = false;
    bool radiation = false;
    bool electricAssault = false;
    bool psychicDamage = false;
    bool makesDisguise = false;
    bool parasite = false;
    bool culling = false;
    int paralyzes = 0;
    bool psychedelic = false;
    bool ivanBomb = false;
    bool bombDisarm = false;
    bool airstrike = false;
    bool isLocomotor = false;
    std::string locomotor;
    int cellInset = 0;
    WarheadProfile profile;
    AttachEffectRule attachEffect;
    bool shieldPenetrate = false;
    WarheadLifecycleProfile lifecycle;
    bool poison = false;
};

struct WeaponWarheadValidationReport {
    int totalWeapons = 0;
    int weaponsWithValidWarhead = 0;
    int weaponsWithoutWarhead = 0;
    int weaponsWithMissingWarheadSection = 0;

    std::vector<std::string> weaponsWithoutWarheadList;
    std::vector<std::string> missingWarheadList;
};

struct RulesCapabilityCacheStats {
    std::size_t objectProfileCount = 0;
    std::size_t objectCapabilityCount = 0;
    std::size_t weaponSetCount = 0;
    std::size_t deployProfileCount = 0;
    std::size_t uiProfileCount = 0;
    std::size_t weaponProfileCount = 0;
    std::size_t projectileProfileCount = 0;
    std::size_t warheadProfileCount = 0;
    std::size_t indexedObjectCount = 0;
    std::size_t indexedWeaponCount = 0;
    std::size_t indexedProjectileCount = 0;
    std::size_t indexedWarheadCount = 0;
};

struct SpawnedMissileProfile {
    std::string typeId;
    // Some classic spawned missiles finish by firing a virtual weapon that
    // is not referenced by an ordinary object slot (V3Airburst in stock YR).
    std::string terminalWeapon;
    int pauseFrames = 0;
    int tiltFrames = 0;
    double pitchInitial = 0.0;
    double pitchFinal = 0.5;
    double turnRate = 0.08;
    double raiseRate = 0.0;
    double acceleration = 0.8;
    int bodyLength = 0;
    std::string launchSound;
    int altitudeLeptons = 0;
    int damage = 0;
    int eliteDamage = 0;
    std::string warhead;
    std::string eliteWarhead;
    bool lazyCurve = false;
};

struct AiSuperDefenseRules {
    // Stock rules author this tuple in hard, medium, easy order.
    std::array<int, 3> probabilityPercent{{90, 50, 10}};
    int alertFrames = 50;
    int distanceCells = 12;
};

struct PrismSupportRules {
    // Empty disables the stock hard-coded Prism Tower network. Retail YR
    // supplies ATESLA explicitly through [General] PrismType.
    std::string prismType;
    double supportModifier = 1.0;
    int supportMax = 8;
    int supportDelayFrames = 100;
    int supportDurationFrames = 15;
    int supportHeightLeptons = 300;
};

struct VoxelAnimType {
    std::string id;
    std::string image;
    int duration = 30;
    double elasticity = 0.800000011920929;
    double minAngularVelocity = 0.0;
    double maxAngularVelocity = 0.17453292519943295;
    double minZVelocity = 3.5;
    double maxZVelocity = 5.0;
    double maxXYVelocity = 15.0;
    std::string bounceAnim;
    std::string expireAnim;
    std::string trailerAnim;
    int damage = 0;
    int damageRadius = 0;
    std::string warhead;
};

struct CombatDetailRules {
    std::string metallicDebris;
    double ballisticScatter = 1.0;
    int projectileGravity = 6;
    double turboBoost = 1.5;
    int openToppedRangeBonus = 2;
    double openToppedDamageMultiplier = 1.2;
    double openToppedWarpDistance = 7.0;
};

struct SpecialMechanismRules {
    double directRockingCoefficient = 1.5;
    double bunkerDamageMultiplier = 1.3;
    double bunkerRofMultiplier = 1.3;
    double bunkerWeaponRangeBonus = 2.0;
    AiSuperDefenseRules aiSuperDefense;
    PrismSupportRules prismSupport;
    double conditionRed = 0.25;
    double conditionYellow = 0.5;
    double ambientChangeRate = 0.0;
    double ambientChangeStep = 0.0;
    double occupyDamageMultiplier = 1.2;
    double occupyRofMultiplier = 1.2;
    int occupyWeaponRange = 5;
    int chronoDelay = 60;
    int chronoReinforcementDelay = 180;
    int chronoDistanceFactor = 48;
    bool chronoTrigger = true;
    int chronoMinimumDelay = 16;
    int chronoRangeMinimum = 0;
    std::string chronoPlacementAnim;
    std::string chronoBeamAnim;
    std::string chronoBlastAnim;
    std::string chronoBlastDestinationAnim;
    std::string warpInAnim;
    std::string warpOutAnim;
    std::string warpAwayAnim;
    std::string chronoSparkleAnim;
    std::string chronoInSound;
    std::string chronoOutSound;
    std::string mindControlSound;
    std::string masterMindOverloadDeathSound;
    std::vector<int> overloadCount{3, 6, 10, 50};
    std::vector<int> overloadDamage{0, 50, 100, 500};
    std::vector<int> overloadFrames{30, 60, 60, 60};
    std::string diskLaserChargeUp;
    int drainMoneyFrameDelay = 30;
    int drainMoneyAmount = 30;
    std::string drainAnimationType = "DISKRAY";
    std::string teslaChargeSound;
    std::string teslaZapSound;
    std::string alliedDisguise;
    std::string sovietDisguise;
    std::string thirdDisguise;
    std::vector<std::string> defaultMirageDisguises;
    int spyPowerBlackoutFrames = 1000;
    double spyMoneyStealPercent = 0.5;
    bool attackCursorOnDisguise = true;
    int mindControlAttackLineFrames = 20;
    std::string controlledAnimationType = "MINDANIM";
    std::string permaControlledAnimationType = "MINDANIMR";
    int infantryBlinkDisguiseTime = 20;
    int disguiseDelay = 1;
    int disguiseRearmDelay = 1;
    int cloakingStages = 9;
    double cloakDelayMinutes = 0.02;
    double reloadRateMinutes = 0.3;
    double repairPercent = 0.15;
    double buildingRepairRateMinutes = 0.016;
    int repairStep = 8;
    double unitRepairRateMinutes = 0.016;
    double infantryRepairRateMinutes = 0.001;
    int infantryRepairStep = 20;
    std::vector<std::string> repairBayTypes;
    double c4DelayMinutes = 0.03;
    std::string c4Warhead = "Super";
    std::string flameDamageWarhead = "Fire";
    std::string ivanWarhead = "IvanWH";
    int ivanDamage = 450;
    int ivanTimedDelayFrames = 450;
    bool canDetonateTimeBomb = false;
    bool canDetonateDeathBomb = false;
    int ivanIconFlickerRateFrames = 8;
    std::string defaultDeathWeapon = "DefaultDeathWeapon";
    double fallingDamageMultiplier = 1.0;
    bool currentStrengthDamage = true;
    int radiationDurationMultiple = 1;
    int radiationApplicationDelay = 16;
    int radiationLevelMax = 500;
    int radiationLevelDelay = 90;
    int radiationLightDelay = 90;
    double radiationLevelFactor = 0.2;
    double radiationLightFactor = 0.1;
    double radiationTintFactor = 1.0;
    std::string radiationColor = "0,255,0";
    std::string radiationSiteWarhead = "RadSite";
};

// A SuperWeaponTypes entry is intentionally kept separate from the runtime
// state machine.  The fields mirror the rulesmd.ini contract (including the
// cross-section values from General, SpecialWeapons, and AudioVisual) so a
// mod can change a weapon without changing engine code.
struct SuperWeaponRule {
    std::string id;
    std::string uiName;
    std::string name;
    std::string type;
    std::string action;
    std::string sidebarImage;
    std::string preDependent;
    std::string rechargeVoice;
    std::string chargingVoice;
    std::string impatientVoice;
    std::string suspendVoice;
    std::string specialSound;
    std::string startSound;
    double rechargeTimeMinutes = 5.0;
    double range = 0.0;
    int lineMultiplier = 0;
    // -1 means the stock default (flash while ready), 0 disables flashing.
    int flashSidebarTabFrames = -1;
    bool isPowered = true;
    bool preClick = false;
    bool postClick = false;
    bool showTimer = false;
    bool disableableFromShell = false;
    bool aiDefendAgainst = false;

    // Optional explicit overrides.  Empty values use the conventional
    // Type/Action mapping in SuperWeaponSystem.
    std::string targetMode;
    std::string weaponType;
    std::string projectile;
    std::string warhead;
    std::string payloadWeapon;
    std::string payloadProjectile;
    std::string payloadWarhead;
    std::string preImpactAnim;
    std::string placementAnim;
    int nuclearAltitudeLeptons = 0;
    int nuclearUpAcceleration = 1;
    int nuclearDownAcceleration = 1;
    int nuclearDownSpeed = 100;
    std::string launchAnim;
    std::string impactAnim;
    std::string activeAnim;
    std::string invokeAnim;
    std::string firstAnim;
    std::string secondAnim;
    std::string boltAnim;
    std::string boltExplosionAnim;
    std::string cloudAnim;
    std::string planeType;
    std::string parachuteAnim;
    std::string mutationType;
    std::string infantryTypes;
    std::string infantryCounts;
    std::array<std::string, 3> sideInfantryTypes;
    std::array<std::string, 3> sideInfantryCounts;
    int supportFlightLevelLeptons = 1500;
    int paradropRadiusLeptons = 1024;
    int parachuteFallRate = 3;
    int spyCameraFrames = 16;

    int damage = 0;
    int radius = 0;
    int durationFrames = 0;
    int defermentFrames = 0;
    int hitDelayFrames = 0;
    int fireAtPercentage = 20;
    int scatterDelayFrames = 0;
    int cellSpread = 0;
    int separation = 0;
    int captureRange = 0;
    int blackoutDurationFrames = 0;
    int fadeSoundFrames = 0;
    bool mutateExplosion = false;
    int powerPenalty = 0;
    bool targetInfantry = true;
    bool targetUnits = true;
    bool targetBuildings = true;
    bool targetEnemy = true;
    bool targetFriendly = false;
    bool targetLand = true;
    bool targetWater = false;
    bool exploredOnly = false;
};

class RulesDatabase {
public:
    bool loadFromIni(const IniFile& ini);
    const TechnoModExtensionRule* findTechnoModExtension(const std::string& id) const;
    const ShieldTypeRule* findShieldType(const std::string& id) const;
    std::uint64_t getModExtensionRulesHash() const { return modExtensionRulesHash_; }

    const std::vector<UnitType>& getInfantryTypes() const;
    const std::vector<UnitType>& getVehicleTypes() const;
    const std::vector<UnitType>& getAircraftTypes() const;
    const std::vector<std::string>& getPadAircraft() const;
    bool isPadAircraft(const std::string& id) const;
    const std::vector<BuildingType>& getBuildingTypes() const;
    const std::vector<std::string>& getAuthoredBuildingTypeIds() const { return authoredBuildingTypeIds_; }
    const std::vector<std::string>& getAuthoredVehicleTypeIds() const { return authoredVehicleTypeIds_; }
    const std::vector<std::string>& getAuthoredAircraftTypeIds() const { return authoredAircraftTypeIds_; }
    const std::vector<WeaponType>& getWeaponTypes() const;
    const std::vector<ProjectileType>& getProjectileTypes() const;
    const std::vector<OverlayType>& getOverlayTypes() const;
    const std::vector<SmudgeType>& getSmudgeTypes() const;
    const std::vector<TiberiumType>& getTiberiumTypes() const;
    const std::vector<TerrainType>& getTerrainTypes() const;
    const std::vector<LandType>& getLandTypes() const;
    const std::vector<WarheadType>& getWarheadTypes() const;
    const std::vector<ParticleSystemType>& getParticleSystemTypes() const;
    const std::vector<ParticleType>& getParticleTypes() const;
    const std::vector<std::string>& getCountries() const;
    const std::vector<CountryDefinition>& getCountryDefinitions() const;
    const CountryDefinition* findCountryDefinition(
        const std::string& id
    ) const;
    const std::vector<SideDefinition>& getSides() const;
    const std::vector<PrerequisiteAlias>& getPrerequisiteAliases() const;
    const std::vector<std::string>& getWarnings() const;
    std::string getAudioVisualValue(const std::string& key) const;
    const std::string& getVoiceIFVRepair() const;
    const std::string& getInfantryVirusAnim() const;
    const std::string& getInfantryDeathAnim(int infDeath) const;
    const VeterancyRules& getVeterancyRules() const;
    const ElevationModelRules& getElevationModelRules() const;
    const TerrainMovementRules& getTerrainMovementRules() const;
    const ProductionRules& getProductionRules() const;
    const HarvesterCadenceRules& getHarvesterCadenceRules() const;
    const CrateRules& getCrateRules() const;
    const SpecialBuildingRules& getSpecialBuildingRules() const;
    const SpecialMechanismRules& getSpecialMechanismRules() const;
    const CombatDetailRules& getCombatDetailRules() const;
    std::uint64_t getDetailRulesHash() const { return detailRulesHash_; }
    const std::vector<VoxelAnimType>& getVoxelAnimTypes() const { return voxelAnimTypes_; }
    const VoxelAnimType* findVoxelAnimType(const std::string& id) const;
    const std::vector<SuperWeaponRule>& getSuperWeaponTypes() const;
    const SuperWeaponRule* findSuperWeaponType(const std::string& id) const;

    const UnitType* findUnitType(
        const std::string& kind,
        const std::string& id
    ) const;
    const BuildingType* findBuildingType(const std::string& id) const;
    const WeaponType* findWeaponType(const std::string& id) const;
    const ProjectileType* findProjectileType(const std::string& id) const;
    const TiberiumType* findTiberiumType(const std::string& id) const;
    const TerrainType* findTerrainType(const std::string& id) const;
    const LandType* findLandType(const std::string& id) const;
    const WarheadType* findWarheadType(const std::string& id) const;
    const ParticleSystemType* findParticleSystemType(
        const std::string& id
    ) const;
    const ParticleType* findParticleType(const std::string& id) const;
    const WeaponLifecycleProfile* findWeaponLifecycleProfile(
        const std::string& id
    ) const;
    const WarheadLifecycleProfile* findWarheadLifecycleProfile(
        const std::string& id
    ) const;
    double getPurifierBonus() const;
    const ComputerMiningRules& getComputerMiningRules() const;
    const ComputerBaseBuildingRules& getComputerBaseBuildingRules() const;
    const ComputerTacticsRules& getComputerTacticsRules() const;
    double getHarvesterTooFarDistance() const;
    double getChronoHarvTooFarDistance() const;
    int getTiberiumNearScan() const;
    int getTiberiumFarScan() const;
    int getSlaveMinerShortScan() const;
    int getSlaveMinerSlaveScan() const;
    int getSlaveMinerLongScan() const;
    int getSlaveMinerScanCorrection() const;
    int getSlaveMinerKickFrameDelay() const;
    int getPoseDir() const;
    int getWindDirection() const;
    const std::vector<std::string>& getDamageFireTypes() const;
    const std::string& getOreTwinkle() const;
    const std::string& getWake() const;
    int getOreTwinkleChance() const;
    int getMaximumQueuedObjects() const;
    double getRefundPercent() const;
    const CrewSurvivorRules& getCrewSurvivorRules() const;
    double getTiberiumHealMinutes() const;
    bool getTiberiumGrows() const;
    bool getTiberiumSpreads() const;
    bool getDestroyableBridges() const;
    int getBridgeStrength() const;
    const SpawnedMissileProfile* findSpawnedMissileProfile(
        const std::string& typeId
    ) const;
    RulesCapabilityCacheStats getCapabilityCacheStats() const;

    WeaponWarheadValidationReport validateWeaponWarheads() const;

private:
    void loadModExtensions(const IniFile& ini);
    AttachEffectRule loadAttachEffect(const IniFile& ini, const std::string& id, bool warhead);
    std::unordered_map<std::string, TechnoModExtensionRule> technoModExtensions_;
    std::unordered_map<std::string, ShieldTypeRule> shieldTypes_;
    std::uint64_t modExtensionRulesHash_ = 0;
    std::vector<UnitType> infantryTypes_;
    std::vector<UnitType> vehicleTypes_;
    std::vector<UnitType> aircraftTypes_;
    std::vector<BuildingType> buildingTypes_;
    std::vector<WeaponType> weaponTypes_;
    std::vector<ProjectileType> projectileTypes_;
    std::vector<OverlayType> overlayTypes_;
    std::vector<SmudgeType> smudgeTypes_;
    std::vector<TiberiumType> tiberiumTypes_;
    std::vector<TerrainType> terrainTypes_;
    std::vector<LandType> landTypes_;
    std::vector<WarheadType> warheadTypes_;
    std::vector<ParticleSystemType> particleSystemTypes_;
    std::vector<ParticleType> particleTypes_;
    std::vector<std::string> infantryTypeIds_;
    std::vector<std::string> vehicleTypeIds_;
    std::vector<std::string> aircraftTypeIds_;
    std::vector<std::string> padAircraft_;
    std::vector<std::string> buildingTypeIds_;
    std::vector<std::string> authoredBuildingTypeIds_;
    std::vector<std::string> authoredVehicleTypeIds_;
    std::vector<std::string> authoredAircraftTypeIds_;
    std::vector<std::string> overlayTypeIds_;
    std::vector<std::string> tiberiumTypeIds_;
    std::vector<std::string> terrainTypeIds_;
    std::vector<std::string> countries_;
    std::vector<CountryDefinition> countryDefinitions_;
    std::unordered_map<std::string, std::size_t> countryIndexById_;
    std::vector<SideDefinition> sides_;
    std::vector<PrerequisiteAlias> prerequisiteAliases_;
    std::vector<std::string> warnings_;
    std::unordered_map<std::string, std::string> audioVisualValues_;
    std::string voiceIFVRepair_;
    std::string infantryVirusAnim_;
    std::array<std::string, 11> infantryDeathAnims_;
    VeterancyRules veterancyRules_;
    ElevationModelRules elevationModelRules_;
    TerrainMovementRules terrainMovementRules_;
    ProductionRules productionRules_;
    HarvesterCadenceRules harvesterCadenceRules_;
    CrateRules crateRules_;
    SpecialBuildingRules specialBuildingRules_;
    SpecialMechanismRules specialMechanismRules_;
    CombatDetailRules combatDetailRules_;
    std::uint64_t detailRulesHash_ = 0;
    std::vector<VoxelAnimType> voxelAnimTypes_;
    std::vector<SuperWeaponRule> superWeaponTypes_;
    double purifierBonus_ = 0.0;
    ComputerMiningRules computerMiningRules_;
    ComputerBaseBuildingRules computerBaseBuildingRules_;
    ComputerTacticsRules computerTacticsRules_;
    double harvesterTooFarDistance_ = 5.0;
    double chronoHarvTooFarDistance_ = 50.0;
    int tiberiumNearScan_ = 5;
    int tiberiumFarScan_ = 50;
    int slaveMinerShortScan_ = 8;
    int slaveMinerSlaveScan_ = 14;
    int slaveMinerLongScan_ = 48;
    int slaveMinerScanCorrection_ = 3;
    int slaveMinerKickFrameDelay_ = 150;
    int poseDir_ = 0;
    int windDirection_ = 7;
    std::vector<std::string> damageFireTypes_{
        "FIRE01", "FIRE02", "FIRE03"
    };
    std::string oreTwinkle_ = "TWNK1";
    std::string wake_;
    int oreTwinkleChance_ = 30;
    int maximumQueuedObjects_ = 29;
    double refundPercent_ = 0.5;
    CrewSurvivorRules crewSurvivorRules_;
    double tiberiumHealMinutes_ = 0.01;
    bool tiberiumGrows_ = true;
    bool tiberiumSpreads_ = true;
    bool destroyableBridges_ = true;
    int bridgeStrength_ = 1500;
    std::vector<SpawnedMissileProfile> spawnedMissileProfiles_;

    std::unordered_set<std::string> projectileIds_;
    std::unordered_set<std::string> warheadIds_;
    std::unordered_map<std::string, std::size_t> infantryIndexById_;
    std::unordered_map<std::string, std::size_t> vehicleIndexById_;
    std::unordered_map<std::string, std::size_t> aircraftIndexById_;
    std::unordered_map<std::string, std::size_t> buildingIndexById_;
    std::unordered_map<std::string, std::size_t> weaponIndexById_;
    std::unordered_map<std::string, std::size_t> projectileIndexById_;
    std::unordered_map<std::string, std::size_t> tiberiumIndexById_;
    std::unordered_map<std::string, std::size_t> terrainIndexById_;
    std::unordered_map<std::string, std::size_t> landTypeIndexById_;
    std::unordered_map<std::string, std::size_t> warheadIndexById_;
    std::unordered_map<std::string, std::size_t> particleSystemIndexById_;
    std::unordered_map<std::string, std::size_t> particleIndexById_;

    UnitType loadUnitType(
        const IniFile& ini,
        const std::string& id,
        const std::string& objectKind,
        bool defaultCrushable
    );
    BuildingType loadBuildingType(const IniFile& ini, const std::string& id);
    WeaponType loadWeaponType(const IniFile& ini, const std::string& id);
    ProjectileType loadProjectileType(const IniFile& ini, const std::string& id);
    OverlayType loadOverlayType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );
    SmudgeType loadSmudgeType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );
    TiberiumType loadTiberiumType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );
    TerrainType loadTerrainType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );
    LandType loadLandType(const IniFile& ini, const std::string& id);
    WarheadType loadWarheadType(const IniFile& ini, const std::string& id);
    ParticleSystemType loadParticleSystemType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );
    ParticleType loadParticleType(
        const IniFile& ini,
        const std::string& id,
        int listIndex
    );

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

    double readProductionDouble(
        const IniFile& ini,
        const std::string& section,
        const std::string& key,
        double defaultValue,
        double maximumValue
    );

    double readPercent(
        const IniFile& ini,
        const std::string& section,
        const std::string& key,
        double defaultValue
    );

    double readRatio(
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

    std::vector<VeteranAbility> readVeteranAbilities(
        const IniFile& ini,
        const std::string& section,
        const std::string& key
    );

    void loadUnitList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<UnitType>& output
    );

    void loadBuildingList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<BuildingType>& output
    );

    void loadWarheadList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<WarheadType>& output
    );

    void loadParticleSystemList(const IniFile& ini);
    void loadParticleList(const IniFile& ini);

    void loadOverlayList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<OverlayType>& output
    );

    void loadSmudgeList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<SmudgeType>& output
    );

    void loadTiberiumList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<TiberiumType>& output
    );

    void loadTerrainList(
        const IniFile& ini,
        const std::string& typeListSection,
        std::vector<TerrainType>& output
    );

    void loadLandTypes(const IniFile& ini);

    void loadCountries(const IniFile& ini);
    void loadSides(const IniFile& ini);
    void loadPrerequisiteAliases(const IniFile& ini);
    void loadSuperWeaponTypes(const IniFile& ini);

    void discoverWeaponsFromObjects(const IniFile& ini);
    void discoverWeaponsFromProjectiles(const IniFile& ini);
    void discoverProjectilesFromWeapons(const IniFile& ini);
    void discoverWarheadsFromWeapons(const IniFile& ini);
    void rebuildLookupIndexes();

    void addWeaponCandidate(
        const IniFile& ini,
        const std::string& weaponId,
        const std::string& source,
        std::unordered_set<std::string>& loadedWeaponIds
    );

    void addWarheadCandidate(
        const IniFile& ini,
        const std::string& warheadId,
        const std::string& source
    );

    void addProjectileCandidate(
        const IniFile& ini,
        const std::string& projectileId,
        const std::string& source
    );

    static bool isEmptyWeaponId(const std::string& weaponId);
    static bool isEmptyProjectileId(const std::string& projectileId);
    static bool isEmptyWarheadId(const std::string& warheadId);

    void addWarning(const std::string& message);
};
