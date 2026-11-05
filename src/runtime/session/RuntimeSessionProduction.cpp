#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

std::vector<ResourceMountRequest> buildResourceMountRequests(
    const RuntimeConfig& config) {
    std::vector<ResourceMountRequest> requests;
    requests.push_back({
        ResourceMountKind::OriginalGame,
        "original",
        config.resourceRootPath,
        true
    });
    requests.push_back({
        ResourceMountKind::LocalData,
        "local_data",
        config.localDataRootPath,
        true
    });

    if (!config.modRootPath.empty()) {
        requests.push_back({
            ResourceMountKind::Mod,
            "mod",
            config.modRootPath,
            true
        });
    }

    return requests;
}

void addRuntimeLightweightResourceFile(
    ResourceMountLayerReport& layer,
    const std::filesystem::path& filePath) {
    std::error_code errorCode;
    if (!std::filesystem::exists(filePath, errorCode)
        || !std::filesystem::is_regular_file(filePath, errorCode)) {
        return;
    }

    const std::string extension = toLowerAsciiLocal(filePath.extension().string());
    const std::string filename = toLowerAsciiLocal(filePath.filename().string());
    ++layer.fileCount;
    const std::uintmax_t fileSize = std::filesystem::file_size(filePath, errorCode);
    if (!errorCode) {
        layer.totalBytes += fileSize;
    }

    if (extension == ".mix") {
        ++layer.mixCount;
        layer.hasMix = true;
    } else if (extension == ".ini") {
        ++layer.iniCount;
    } else if (extension == ".map"
        || extension == ".mpr"
        || extension == ".yrm"
        || extension == ".mmx"
        || extension == ".yro") {
        ++layer.mapCount;
        layer.hasMaps = true;
    } else if (extension == ".pal") {
        ++layer.palCount;
        layer.hasPalette = true;
    } else if (extension == ".shp") {
        ++layer.shpCount;
    } else if (extension == ".tmp" || extension == ".tem" || extension == ".urb") {
        ++layer.tmpCount;
    } else if (extension == ".vxl") {
        ++layer.vxlCount;
    } else if (extension == ".hva") {
        ++layer.hvaCount;
    }

    if (filename == "rules.ini" || filename == "rulesmd.ini") {
        layer.hasRules = true;
    } else if (filename == "art.ini" || filename == "artmd.ini") {
        layer.hasArt = true;
    } else if (filename == "temperat.ini"
        || filename == "temperatmd.ini"
        || filename == "snow.ini"
        || filename == "snowmd.ini"
        || filename == "urban.ini"
        || filename == "urbanmd.ini"
        || filename == "desert.ini"
        || filename == "desertmd.ini"
        || filename == "lunar.ini"
        || filename == "lunarmd.ini"
        || filename == "newurban.ini"
        || filename == "urbann.ini"
        || filename == "urbannmd.ini") {
        layer.hasTheaterIni = true;
    }

    if (layer.sampleFiles.size() < 12) {
        layer.sampleFiles.push_back(filePath.filename().generic_string());
    }
}

void accumulateRuntimeLightweightResourceLayer(
    ResourceMountReport& report,
    const ResourceMountLayerReport& layer) {
    ++report.mountedLayerCount;
    if (layer.exists) {
        ++report.existingLayerCount;
    }
    report.totalFiles += layer.fileCount;
    report.totalDirectories += layer.directoryCount;
    report.totalBytes += layer.totalBytes;
    report.totalMixFiles += layer.mixCount;
    report.totalIniFiles += layer.iniCount;
    report.totalMapFiles += layer.mapCount;
    report.totalPalFiles += layer.palCount;
    report.totalShpFiles += layer.shpCount;
    report.totalTmpFiles += layer.tmpCount;
    report.totalVxlFiles += layer.vxlCount;
    report.totalHvaFiles += layer.hvaCount;
    report.hasRules = report.hasRules || layer.hasRules;
    report.hasArt = report.hasArt || layer.hasArt;
    report.hasMaps = report.hasMaps || layer.hasMaps;
    report.hasTheaterIni = report.hasTheaterIni || layer.hasTheaterIni;
    report.hasPalette = report.hasPalette || layer.hasPalette;
    report.hasMix = report.hasMix || layer.hasMix;
    for (const std::string& warning : layer.warnings) {
        report.warnings.push_back(layer.name + ": " + warning);
    }
}

ResourceMountReport buildRuntimeLightweightResourceMountReport(
    const RuntimeConfig& config,
    const std::string& rulesPath,
    const std::string& artPath) {
    ResourceMountReport report;
    const std::filesystem::path resourceRoot(config.resourceRootPath);
    const std::filesystem::path localDataRoot(config.localDataRootPath);

    for (const ResourceMountRequest& request : buildResourceMountRequests(config)) {
        ResourceMountLayerReport layer;
        layer.kind = request.kind;
        layer.name = request.name;
        layer.rootPath = request.rootPath;
        layer.exists = isExistingDirectory(request.rootPath);
        layer.indexed = false;
        if (!layer.exists) {
            layer.warnings.push_back("mount root does not exist: " + request.rootPath);
            accumulateRuntimeLightweightResourceLayer(report, layer);
            continue;
        }

        if (request.kind == ResourceMountKind::OriginalGame) {
            addRuntimeLightweightResourceFile(layer, resourceRoot / "ra2.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "ra2md.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "language.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "langmd.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "Game" / "ra2.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "Game" / "language.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "GameMD" / "ra2md.mix");
            addRuntimeLightweightResourceFile(layer, resourceRoot / "GameMD" / "langmd.mix");
        } else if (request.kind == ResourceMountKind::LocalData) {
            addRuntimeLightweightResourceFile(layer, std::filesystem::path(rulesPath));
            addRuntimeLightweightResourceFile(layer, std::filesystem::path(artPath));
            addRuntimeLightweightResourceFile(
                layer,
                std::filesystem::path(config.defaultMapPath)
            );
            addRuntimeLightweightResourceFile(
                layer,
                localDataRoot / "resources" / "mix_exports" / "ra2md" / "localmd" / "isoubn.pal"
            );
            addRuntimeLightweightResourceFile(
                layer,
                localDataRoot / "resources" / "mix_exports" / "palettes" / "isoubn.pal"
            );
        }

        accumulateRuntimeLightweightResourceLayer(report, layer);
    }

    report.success =
        report.existingLayerCount > 0
        && (report.hasRules || report.hasArt || report.hasMaps || report.hasMix);
    return report;
}

void depositRuntimeCredits(
    RuntimeGameSessionState& state,
    long long amount) {
    state.economy.credits = runtimeCreditBalanceAfterDeposit(
        state.economy.credits,
        amount
    );
}

RuntimeEconomyState* runtimeHumanHouseEconomy(
    RuntimeProductionHostState& state,
    const std::string& owner) {
    RuntimeHumanHouseAuthority* authority =
        findRuntimeHumanHouseAuthority(state, owner);
    if (authority == nullptr) {
        return !state.config.multiplayerActive && runtimeUsesGameplayIntentRouter(state)
            && runtimeCanonicalHumanHouseOwner(owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)
            ? &state.economy : nullptr;
    }
    if (authority->localFacade) {
        return runtimeCanonicalHumanHouseOwner(state.economy.owner)
                == authority->canonicalOwner
            ? &state.economy
            : nullptr;
    }
    if (state.remoteHumanHouseScopeActive
        && runtimeCanonicalHumanHouseOwner(state.economy.owner)
            == authority->canonicalOwner) {
        return &state.economy;
    }
    return authority->remoteEconomy.has_value()
        ? &*authority->remoteEconomy
        : nullptr;
}

const RuntimeEconomyState* runtimeHumanHouseEconomy(
    const RuntimeProductionHostState& state,
    const std::string& owner) {
    const RuntimeHumanHouseAuthority* authority =
        findRuntimeHumanHouseAuthority(state, owner);
    if (authority == nullptr) {
        return !state.config.multiplayerActive && runtimeUsesGameplayIntentRouter(state)
            && runtimeCanonicalHumanHouseOwner(owner)==runtimeCanonicalHumanHouseOwner(state.economy.owner)
            ? &state.economy : nullptr;
    }
    if (authority->localFacade) {
        return runtimeCanonicalHumanHouseOwner(state.economy.owner)
                == authority->canonicalOwner
            ? &state.economy
            : nullptr;
    }
    if (state.remoteHumanHouseScopeActive
        && runtimeCanonicalHumanHouseOwner(state.economy.owner)
            == authority->canonicalOwner) {
        return &state.economy;
    }
    return authority->remoteEconomy.has_value()
        ? &*authority->remoteEconomy
        : nullptr;
}

WorldObjectKind runtimeKindForBuildItem(RuntimeBuildKind kind) {
    switch (kind) {
    case RuntimeBuildKind::Building:
    case RuntimeBuildKind::Defense:
        return WorldObjectKind::Structure;
    case RuntimeBuildKind::Infantry:
        return WorldObjectKind::Infantry;
    case RuntimeBuildKind::Unit:
        return WorldObjectKind::Unit;
    }
    return WorldObjectKind::Unit;
}

RuntimeProductionTimingResult runtimeProductionTimingForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    int poweredCompatibleFactoryCount,
    double powerProduced,
    double powerUsed) {
    RuntimeProductionTimingInput input;
    input.rules = state.rules.getProductionRules();
    const int rawCost = runtimeRawBuildCostForItem(state, item);
    input.cost = rawCost == 0 && item.cost > 0 ? item.cost : rawCost;
    input.buildTimeMultiplier = runtimeBuildTimeMultiplier(state, item);
    input.wall = item.wall;
    input.poweredCompatibleFactoryCount = poweredCompatibleFactoryCount;
    input.powerProduced = powerProduced;
    input.powerUsed = powerUsed;
    input.fixedTickHz = state.config.fixedTickHz;
    return runtimeCalculateProductionTiming(input);
}

bool runtimeBuildingCountsAsRefinery(const BuildingType& building) {
    return building.refinery
        || (building.resourceDestination && building.storage > 0)
        || (!building.undeploysInto.empty()
            && building.buildCat == "Resource"
            && building.storage > 0);
}

int runtimeBuildingHarvesterDockCount(const BuildingType& building) {
    if (!building.dockUnload) {
        return 0;
    }
    if (building.numberOfDocks > 0) {
        return building.numberOfDocks;
    }
    return runtimeBuildingCountsAsRefinery(building) ? 1 : 0;
}

bool runtimeBuildingAcceptsHarvesterUnload(const BuildingType& building) {
    return runtimeBuildingHarvesterDockCount(building) > 0
        && runtimeBuildingCountsAsRefinery(building);
}

bool runtimeHarvesterCanUnloadAtRefinery(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery,
    const std::string* owner) {
    if (!harvester.alive
        || !refinery.alive
        || harvester.owner != (owner ? *owner : state.economy.owner)
        || refinery.owner != (owner ? *owner : state.economy.owner)
        || refinery.kind != WorldObjectKind::Structure) {
        return false;
    }

    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, refinery.type);
    if (unit == nullptr
        || building == nullptr
        || !runtimeBuildingAcceptsHarvesterUnload(*building)) {
        return false;
    }

    const std::vector<std::string> dockTypes =
        splitRuntimeTokenList(unit->dock);
    if (dockTypes.empty()) {
        return true;
    }

    const std::string refineryType = toUpperAsciiLocal(refinery.type);
    for (const std::string& dockType : dockTypes) {
        if (toUpperAsciiLocal(dockType) == refineryType) {
            return true;
        }
    }
    return false;
}

bool runtimeBuildingCanGatherResources(const BuildingType& building) {
    return building.resourceGatherer
        || (!building.undeploysInto.empty()
            && building.buildCat == "Resource"
            && building.storage > 0);
}

bool runtimeBuildingStoresResources(const BuildingType& building) {
    return building.storage > 0
        && (
            toLowerAsciiLocal(building.pipScale) == "tiberium"
            || building.refinery
            || building.resourceDestination
            || runtimeBuildingCanGatherResources(building)
            || toLowerAsciiLocal(building.buildCat) == "resource"
        );
}

int runtimeResourceStorageCapacityForOwnerObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner) {
    if (!object.alive
        || object.owner != owner
        || object.kind != WorldObjectKind::Structure) {
        return 0;
    }

    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building == nullptr || !runtimeBuildingStoresResources(*building)) {
        return 0;
    }

    return std::max(0, building->storage);
}

int runtimeOwnedResourceStorageCapacityForOwner(const RuntimeGameSessionState& state, const std::string& owner) {
    int capacity = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        capacity += runtimeResourceStorageCapacityForOwnerObject(state, object, owner);
    }
    return capacity;
}

bool spendRuntimeCredits(RuntimeGameSessionState& state, int requestedAmount) {
    auto house = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    return spendRuntimeHouseCredits(house, requestedAmount);
}

HouseEconomyContext makeRuntimeHouseEconomyContext(RuntimeEconomyState& economy, bool computer) {
    return {economy.owner, economy.credits, economy.storedResourceUnits,
        economy.storedResourceValue, economy.storedResourceGemUnits, economy.resourceTrips,
        economy.harvesterTeleports, economy.resourceStorageCapacity,
        economy.harvesters, economy.refineries, computer};
}

void refreshRuntimeResourceStorageCapacity(const RuntimeGameSessionState& state, RuntimeEconomyState& economy) {
    economy.resourceStorageCapacity = runtimeOwnedResourceStorageCapacityForOwner(state, economy.owner);
    auto context = makeRuntimeHouseEconomyContext(economy, false);
    normalizeHouseResourceStorage(context);
}

void refreshRuntimeResourceStorageCapacity(RuntimeGameSessionState& state) {
    refreshRuntimeResourceStorageCapacity(state, state.economy);
}

void recordRuntimeHouseResourceDelivery(RuntimeGameSessionState& state, HouseEconomyContext& context,
    RuntimeEconomyState& feedback, int units, int gems, int value) {
    context.resourceStorageCapacity = runtimeOwnedResourceStorageCapacityForOwner(state, context.owner);
    const auto result = recordHouseResourceDelivery(context, units, gems, value);
    feedback.lastResourceStorageCapacity = result.capacity;
    feedback.lastResourceStoredUnits = result.storedUnits;
    feedback.lastResourceStoredValue = result.storedValue;
    feedback.lastResourceStoredGemUnits = result.storedGemUnits;
    feedback.lastResourceStorageOverflowUnits = result.overflowUnits;
}

bool runtimeHasOwnedFactory(
    const RuntimeGameSessionState& state,
    const std::string& factory) {
    if (factory.empty()) {
        return true;
    }

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
            || object.kind != WorldObjectKind::Structure) {
            continue;
        }

        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr && building->factory == factory) {
            return true;
        }
    }

    return false;
}

bool runtimeHasOwnedRefinery(const RuntimeGameSessionState& state) {
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
            || object.kind != WorldObjectKind::Structure) {
            continue;
        }

        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building != nullptr && runtimeBuildingCountsAsRefinery(*building)) {
            return true;
        }
    }

    return false;
}

int runtimeBuildItemPlanningSide(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    if (const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, item.typeId)) {
        return building->aiBasePlanningSide >= 0
            ? building->aiBasePlanningSide
            : runtimeOwnerListPlanningSide(building->owner);
    }
    if (const UnitType* unit =
            findRuntimeUnitTypeById(state.rules, item.typeId)) {
        // Mode rules may give every country the MCV. Its deployed yard
        // remains the authority for which faction factory produces it.
        if (const auto* yard = findRuntimeBuildingTypeById(state.rules, unit->deploysInto);
            yard != nullptr && yard->constructionYard && yard->aiBasePlanningSide >= 0)
            return yard->aiBasePlanningSide;
        return runtimeOwnerListPlanningSide(unit->owner);
    }
    return runtimeOwnerListPlanningSide(item.owner);
}

bool runtimeFactoryOwnerCompatibleWithBuildItem(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory,
    const RuntimeBuildCatalogItem& item) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, factory.type);
    if (building == nullptr) {
        return true;
    }
    if (runtimeIsUnholyAllianceMode(state)) {
        const int factorySide = building->aiBasePlanningSide >= 0
            ? building->aiBasePlanningSide
            : runtimeOwnerListPlanningSide(building->owner);
        const int itemSide = runtimeBuildItemPlanningSide(state, item);
        if (factorySide >= 0 && itemSide >= 0) {
            return factorySide == itemSide;
        }
    }
    if (trimAsciiLocal(item.owner).empty()) {
        return true;
    }
    const std::string factoryOwner = runtimeModeEffectiveOwnerList(
        state,
        building->id,
        building->owner
    );
    return runtimeOwnerListsOverlap(state, factoryOwner, item.owner);
}

bool runtimeOwnerHasCompatibleFactory(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    if (trimAsciiLocal(item.requiredFactory).empty()) {
        return true;
    }
    const std::string wantedOwner = runtimeRepairDepotCanonicalOwnerId(
        state,
        owner
    );
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.kind != WorldObjectKind::Structure
            || runtimeRepairDepotCanonicalOwnerId(state, object.owner)
                != wantedOwner) {
            continue;
        }
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            object.type
        );
        if (building != nullptr
            && toUpperAsciiLocal(trimAsciiLocal(building->factory))
                == toUpperAsciiLocal(trimAsciiLocal(item.requiredFactory))
            && runtimeFactoryOwnerCompatibleWithBuildItem(
                state,
                object,
                item
            )) {
            return true;
        }
    }
    return false;
}

bool runtimeOwnerHasAlternateRefineryPrerequisite(
    const RuntimeGameSessionState& state, const std::string& owner) {
    const std::string canonicalOwner = runtimeRepairDepotCanonicalOwnerId(state, owner);
    for (const auto& alias : state.rules.getPrerequisiteAliases()) {
        if (toUpperAsciiLocal(alias.name) != "PROCALTERNATE") continue;
        for (const auto& type : splitRuntimeTokenList(alias.value)) {
            // PrerequisiteProcAlternate names UnitTypes (stock: SMIN), not a
            // permanent technology unlock or an alias for a refinery building.
            if (!state.rules.findUnitType("Vehicle", type)) continue;
            const std::string wanted = toUpperAsciiLocal(type);
            for (const auto& object : state.world.getObjects()) {
                if (object.alive && object.health > 0 && object.kind == WorldObjectKind::Unit
                    && toUpperAsciiLocal(object.type) == wanted
                    && runtimeRepairDepotCanonicalOwnerId(state, object.owner) == canonicalOwner) {
                    return true;
                }
            }
        }
    }
    return false;
}

int runtimePowerNet(const RuntimeGameSessionState& state) {
    return state.economy.powerProduced - state.economy.powerUsed;
}

int runtimePowerNetAfterBuildItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    int produced = state.economy.powerProduced;
    int used = state.economy.powerUsed;
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        if (item.powerDelta >= 0) {
            produced += item.powerDelta;
        } else {
            used += -item.powerDelta;
        }
    }
    return produced - used;
}

std::string runtimeBuildLimitKey(const std::string& value) {
    return toUpperAsciiLocal(trimAsciiLocal(value));
}

int runtimeBuildLimitMagnitude(int configuredLimit) {
    const long long magnitude = configuredLimit < 0
        ? -static_cast<long long>(configuredLimit)
        : static_cast<long long>(configuredLimit);
    return static_cast<int>(std::min<long long>(
        magnitude,
        std::numeric_limits<int>::max()
    ));
}

bool runtimeProductionItemKeysMatch(
    std::string_view left,
    std::string_view right) {
    return runtimeBuildLimitKey(std::string(left))
        == runtimeBuildLimitKey(std::string(right));
}

int runtimeBuildLimitUsedCount(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    bool includePending) {
    if (!item.buildLimit.has_value()) {
        return 0;
    }
    const int configuredLimit = *item.buildLimit;
    const int builtCount = configuredLimit < 0
        ? runtimeLimitedBuildHistoryCount(state, owner, item.typeId)
        : runtimeLiveOwnedTypeCount(state, owner, item.typeId);
    const int pendingCount = (
        includePending
            ? runtimePendingBuildTypeCount(state, owner, item.typeId)
            : 0
    );
    return static_cast<int>(std::min<long long>(
        static_cast<long long>(std::max(0, builtCount))
            + static_cast<long long>(std::max(0, pendingCount)),
        std::numeric_limits<int>::max()
    ));
}

std::string runtimeBuildLimitBlockReason(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    bool computerPlayer,
    bool includePending) {
    if (!item.buildLimit.has_value()) {
        return "";
    }
    const int configuredLimit = *item.buildLimit;
    // Vanilla BuildLimit=0 is an AI-only object contract.  Human sidebars
    // keep the cameo disabled while computer production may still select it.
    if (configuredLimit == 0) {
        return computerPlayer
            ? std::string()
            : "disabled by BuildLimit=0";
    }
    const int maximum = runtimeBuildLimitMagnitude(configuredLimit);
    if (runtimeBuildLimitUsedCount(state, owner, item, includePending)
        < maximum) {
        return "";
    }
    return "build limit " + std::to_string(maximum)
        + " reached for " + item.typeId;
}

std::optional<RuntimeFactoryPlantCostCategory>
runtimeFactoryPlantCostCategoryForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            item.typeId
        );
        if (building == nullptr) {
            return std::nullopt;
        }
        const std::string buildCategory = toUpperAsciiLocal(
            trimAsciiLocal(building->buildCat)
        );
        return buildCategory == "COMBAT"
            ? RuntimeFactoryPlantCostCategory::Defenses
            : RuntimeFactoryPlantCostCategory::Buildings;
    }
    if (state.rules.findUnitType("InfantryType", item.typeId) != nullptr) {
        return RuntimeFactoryPlantCostCategory::Infantry;
    }
    if (state.rules.findUnitType("UnitType", item.typeId) != nullptr) {
        return RuntimeFactoryPlantCostCategory::Units;
    }
    if (state.rules.findUnitType("AircraftType", item.typeId) != nullptr) {
        return RuntimeFactoryPlantCostCategory::Aircraft;
    }
    return std::nullopt;
}

int runtimeRawBuildCostForItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            item.typeId
        );
        return building == nullptr ? 0 : std::max(0, building->cost);
    }
    const UnitType* unit = item.kind == RuntimeBuildKind::Infantry
        ? state.rules.findUnitType("InfantryType", item.typeId)
        : state.rules.findUnitType("UnitType", item.typeId);
    if (unit == nullptr && item.kind == RuntimeBuildKind::Unit) {
        unit = state.rules.findUnitType("AircraftType", item.typeId);
    }
    return unit == nullptr ? 0 : std::max(0, unit->cost);
}

int runtimeAdjustedFactoryPlantCost(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item,
    int baseCost,
    const RuntimeFactoryPlantCostSnapshot* costSnapshot) {
    const std::optional<RuntimeFactoryPlantCostCategory> category =
        runtimeFactoryPlantCostCategoryForItem(state, item);
    if (!category.has_value()) {
        return std::max(0, baseCost);
    }
    const RuntimeSpecialBuildingContext context =
        makeRuntimeSpecialBuildingContext(state);
    const auto adjusted = [&](RuntimeFactoryPlantCostCategory costCategory,
                              int cost) {
        if (costSnapshot != nullptr) {
            return costSnapshot->adjustedProductionCost(
                costCategory, std::max(0, cost));
        }
        return RuntimeSpecialBuildingSystem::adjustedProductionCost(
            context,
            owner,
            costCategory,
            std::max(0, cost)
        );
    };

    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        const BuildingType* building = findRuntimeBuildingTypeById(
            state.rules,
            item.typeId
        );
        const UnitType* freeUnit = building == nullptr
            || trimAsciiLocal(building->freeUnit).empty()
            ? nullptr
            : findRuntimeUnitTypeById(state.rules, building->freeUnit);
        if (freeUnit != nullptr && freeUnit->cost > 0
            && baseCost >= freeUnit->cost) {
            RuntimeFactoryPlantCostCategory freeUnitCategory =
                RuntimeFactoryPlantCostCategory::Units;
            if (state.rules.findUnitType(
                    "InfantryType",
                    freeUnit->id
                ) != nullptr) {
                freeUnitCategory = RuntimeFactoryPlantCostCategory::Infantry;
            } else if (state.rules.findUnitType(
                           "AircraftType",
                           freeUnit->id
                       ) != nullptr) {
                freeUnitCategory = RuntimeFactoryPlantCostCategory::Aircraft;
            }
            const int structureCost = adjusted(
                *category,
                baseCost - freeUnit->cost
            );
            const int payloadCost = adjusted(
                freeUnitCategory,
                freeUnit->cost
            );
            return structureCost > std::numeric_limits<int>::max()
                    - payloadCost
                ? std::numeric_limits<int>::max()
                : structureCost + payloadCost;
        }
    }
    return adjusted(*category, baseCost);
}

bool refreshRuntimeFactoryPlantCatalog(RuntimeGameSessionState& state) {
    RuntimePerfTraceScope trace("refreshRuntimeFactoryPlantCatalog", state.tick);
    bool changed = false;
    const RuntimeFactoryPlantCostSnapshot costSnapshot = state.economy.catalog.empty()
        ? RuntimeFactoryPlantCostSnapshot{}
        : RuntimeSpecialBuildingSystem::factoryPlantCostSnapshot(
            makeRuntimeSpecialBuildingContext(state), state.economy.owner);
    for (RuntimeBuildCatalogItem& item : state.economy.catalog) {
        const int adjustedCost = runtimeAdjustedFactoryPlantCost(
            state,
            state.economy.owner,
            item,
            runtimeRawBuildCostForItem(state, item),
            &costSnapshot
        );
        if (item.cost == adjustedCost) {
            continue;
        }
        item.cost = adjustedCost;
        changed = true;
    }
    // The original engine prices an order when it actually enters the active
    // factory. Orders that are still waiting follow the current house
    // multiplier; active and placement-ready orders retain their paid price.
    for (RuntimeProductionLaneState& laneState :
            state.economy.localProductionLanes.lanes) {
        for (RuntimeProductionLaneOrder& order : laneState.queued) {
            const auto catalog = std::find_if(
                state.economy.catalog.begin(),
                state.economy.catalog.end(),
                [&order](const RuntimeBuildCatalogItem& item) {
                    return toUpperAsciiLocal(item.typeId)
                        == toUpperAsciiLocal(order.itemKey);
                }
            );
            if (catalog != state.economy.catalog.end()
                && order.cost != catalog->cost) {
                order.cost = std::max(0, catalog->cost);
                changed = true;
            }
        }
    }
    return changed;
}

bool runtimeBuildItemHasSecretLabUnlock(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    std::optional<RuntimePerfTraceScope> secretLabQueryTrace;
    if (RuntimeSidebarOwnedQueryDiagnostics::Phase::observes(&state))
        secretLabQueryTrace.emplace("sidebar.secretLab.unlockQuery", state.tick);
    if (auto* phase = RuntimePaintSecretLabPhaseScope::active) {
        if (const auto* context = phase->contextFor(state)) {
            return RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
                *context, owner, runtimeSpecialUnlockKind(item), item.typeId
            );
        }
    }
    return RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
        makeRuntimeSpecialBuildingContext(state),
        owner,
        runtimeSpecialUnlockKind(item),
        item.typeId
    );
}

bool runtimeBuildItemMeetsStolenTechRequirements(
    const RuntimeGameSessionState& state, const std::string& owner,
    const RuntimeBuildCatalogItem& item) {
    if (item.kind == RuntimeBuildKind::Building || item.kind == RuntimeBuildKind::Defense) {
        return true;
    }
    const UnitType* unit = findRuntimeUnitTypeById(state.rules, item.typeId);
    return unit == nullptr || state.spies.meetsStolenTechRequirements(owner, *unit);
}

bool runtimeFactoryLandVehicleEligible(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const auto* type = findRuntimeUnitType(state.rules, object);
    return type && !type->naval && !type->waterBound
        && runtimeObjectUsesVehicleArrival(state, object);
}

bool runtimeFactoryLandDeliveryOwnsEgress(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    if (!runtimeFactoryLandVehicleEligible(state, object)) return false;
    return std::any_of(state.factoryDeliveries.leases().begin(),
        state.factoryDeliveries.leases().end(), [&](const auto& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Land
                && lease.lane == RuntimeProductionLane::Vehicles;
        });
}

bool runtimeFactoryClearanceBehaviorIsValid(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    if (behavior.factoryExitClearanceHostId == -1) return true;
    return behavior.factoryExitClearanceHostId >= 0
        && runtimeFactoryLandVehicleEligible(state, object)
        && behavior.kind == RuntimeBehaviorKind::Move
        && behavior.targetObjectId == -1 && !behavior.forceFire
        && !behavior.attackGround && !behavior.attackBridge;
}

bool runtimeFactoryFastClearanceSource(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, int hostId) {
    if (hostId < 0 || !runtimeFactoryLandVehicleEligible(state, object)
        || !runtimeNoCooldownForOwner(state, object.owner)) return false;
    const auto* host = findObjectById(state, hostId);
    if (!host || !host->alive || host->health <= 0
        || host->kind != WorldObjectKind::Structure
        || runtimeCanonicalHumanHouseOwner(host->owner)
            != runtimeCanonicalHumanHouseOwner(object.owner)) return false;
    const auto* type = findRuntimeBuildingTypeById(state.rules, host->type);
    return type && type->weaponsFactory && !type->naval && !type->waterBound
        && toUpperAsciiLocal(type->factory) == "UNITTYPE";
}

bool runtimeFactoryFastClearanceMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    return behavior.factoryExitClearanceHostId >= 0
        && runtimeFactoryClearanceBehaviorIsValid(state, object, behavior)
        && runtimeFactoryFastClearanceSource(state, object,
            behavior.factoryExitClearanceHostId);
}

bool runtimeFactoryEgressBypassesArrival(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    // Retail ForceTrack 66 bypasses the ordinary destination slowdown. A
    // released car's fresh yield remains an ordinary Move unless fast build
    // explicitly grants it the short factory clearance clock below.
    return runtimeFactoryLandDeliveryOwnsEgress(state, object)
        || runtimeFactoryFastClearanceMove(state, object, behavior);
}

bool runtimeFactoryEgressUsesFastClock(const RuntimeGameSessionState& state,
    const WorldObjectInstance& object, const RuntimeObjectBehavior& behavior) {
    if (runtimeFactoryFastClearanceMove(state, object, behavior)) return true;
    if (!runtimeFactoryLandDeliveryOwnsEgress(state, object)) return false;
    return std::any_of(state.factoryDeliveries.leases().begin(),
        state.factoryDeliveries.leases().end(), [&](const auto& lease) {
            return lease.producedObjectId == object.instanceId
                && lease.domain == RuntimeFactoryDeliveryDomain::Land
                && lease.lane == RuntimeProductionLane::Vehicles
                && runtimeFactoryFastClearanceSource(state, object, lease.hostId);
        });
}

bool runtimeBuildItemLooksNaval(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    if (item.kind != RuntimeBuildKind::Unit) {
        return false;
    }

    if (item.naval || item.waterBound) {
        return true;
    }

    const UnitType* unit = findRuntimeUnitTypeById(state.rules, item.typeId);
    return unit != nullptr && runtimeUnitTypeLooksNaval(*unit);
}

std::string chooseRuntimeEconomyOwner(const GameWorld& world) {
    const char* preferredOwners[] = {"Americans", "British", "French", "Germans", "Alliance"};
    for (const char* owner : preferredOwners) {
        for (const WorldObjectInstance& object : world.getObjects()) {
            if (object.alive && object.owner == owner) {
                return owner;
            }
        }
    }

    for (const WorldObjectInstance& object : world.getObjects()) {
        if (object.alive && !object.owner.empty() && object.owner != "Neutral") {
            return object.owner;
        }
    }

    return "Americans";
}

bool runtimeBuildItemVisibleForCountry(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& playerCountry) {
    if (runtimeIsUnholyAllianceMode(state)) {
        if ((!trimAsciiLocal(item.requiredHouses).empty()
                && !runtimeOwnerListAllowsHouse(
                    item.requiredHouses,
                    playerCountry
                ))
            || (!trimAsciiLocal(item.forbiddenHouses).empty()
                && runtimeOwnerListAllowsHouse(
                    item.forbiddenHouses,
                    playerCountry
                ))) {
            return false;
        }
    } else if (!evaluateTechTreeCountryFilter(
        state.rules,
        playerCountry,
        item.owner,
        item.requiredHouses,
        item.forbiddenHouses
    ).visible) {
        return false;
    }

    if (item.kind != RuntimeBuildKind::Building
        && item.kind != RuntimeBuildKind::Defense) {
        return true;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, item.typeId);
    const int playerSide = runtimeBasePlanningSideForCountry(playerCountry);
    return runtimeIsUnholyAllianceMode(state)
        || building == nullptr
        || building->aiBasePlanningSide < 0
        || playerSide < 0
        || building->aiBasePlanningSide == playerSide;
}

bool runtimeBuildItemVisibleForPlayerCountry(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    return runtimeBuildItemVisibleForCountry(
        state,
        item,
        runtimeCountryForOwner(state, state.economy.owner)
    );
}

void applyRuntimeModeRulesToBuildItem(
    const RuntimeGameSessionState& state,
    RuntimeBuildCatalogItem& item) {
    if (!state.skirmishModeRulesLoaded) {
        return;
    }
    const IniFile::Section* section = runtimeIniSectionNoCase(
        state.skirmishModeRules,
        item.typeId
    );
    if (section == nullptr) {
        return;
    }
    if (const std::optional<std::string> techLevel =
            runtimeIniSectionOptionalValueNoCase(section, "TechLevel")) {
        int parsedTechLevel = item.techLevel;
        if (parseRuntimeInt(*techLevel, parsedTechLevel)) {
            item.techLevel = parsedTechLevel;
        }
    }
    if (const std::optional<std::string> owner =
            runtimeIniSectionOptionalValueNoCase(section, "Owner")) {
        item.owner = *owner;
    }
    if (const std::optional<std::string> prerequisite =
            runtimeIniSectionOptionalValueNoCase(section, "Prerequisite")) {
        item.prerequisite = *prerequisite;
    }
    std::optional<std::string> prerequisiteOverride =
        runtimeIniSectionOptionalValueNoCase(section, "PrerequisiteOverride");
    if (!prerequisiteOverride.has_value()) {
        prerequisiteOverride = runtimeIniSectionOptionalValueNoCase(
            section,
            "Prerequisite.Override"
        );
    }
    if (prerequisiteOverride.has_value()) {
        item.prerequisiteOverride = *prerequisiteOverride;
    }
    if (const std::optional<std::string> buildLimit =
            runtimeIniSectionOptionalValueNoCase(section, "BuildLimit")) {
        int parsedBuildLimit = item.buildLimit.value_or(0);
        if (parseRuntimeInt(*buildLimit, parsedBuildLimit)) {
            item.buildLimit = parsedBuildLimit;
        }
    }
}

void refreshRuntimeEconomyMetricsForOwner(
    RuntimeGameSessionState& state, RuntimeEconomyState& economy) {
    const std::string ownerKey =
        runtimeCanonicalHumanHouseOwner(economy.owner);
    int produced = 0;
    int used = 0;
    int harvesters = 0;
    int refineries = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || runtimeCanonicalHumanHouseOwner(object.owner) != ownerKey) {
            continue;
        }
        if (object.kind == WorldObjectKind::Structure) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building == nullptr) {
                continue;
            }
            const int power = runtimeBuildingPowerOutput(*building, object);
            if (power >= 0) {
                produced += power;
            } else {
                used += -power;
            }
            if (runtimeBuildingCountsAsRefinery(*building)) {
                ++refineries;
            }
        } else {
            const UnitType* unit =
                findRuntimeUnitTypeById(state.rules, object.type);
            if (unit != nullptr && unit->harvester) {
                ++harvesters;
            }
        }
    }
    if (runtimeDiskDrainOwnerBlackout(state, economy.owner)) produced = 0;
    economy.powerProduced = produced;
    economy.powerUsed = used;
    economy.harvesters = harvesters;
    economy.refineries = refineries;
    refreshRuntimeResourceStorageCapacity(state, economy);
}

void refreshRuntimeEconomyMetricsForCurrentOwner(RuntimeGameSessionState& state) {
    refreshRuntimeEconomyMetricsForOwner(state, state.economy);
}

void rebuildRuntimeEconomyPower(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("rebuildRuntimeEconomyPower", state.tick);
    if (state.remoteHumanHouseScopeActive) {
        refreshRuntimeEconomyMetricsForCurrentOwner(state);
        (void)refreshRuntimeFactoryPlantCatalog(state);
        return;
    }
    struct OwnerPowerGrid {
        int produced = 0;
        int used = 0;
    };

    const std::string localOwner =
        toUpperAsciiLocal(trimAsciiLocal(state.economy.owner));
    std::unordered_map<std::string, OwnerPowerGrid> gridsByOwner;
    int harvesters = 0;
    int refineries = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive) {
            continue;
        }

        const std::string owner =
            toUpperAsciiLocal(trimAsciiLocal(object.owner));
        if (object.kind == WorldObjectKind::Structure) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building == nullptr) {
                continue;
            }
            OwnerPowerGrid& grid = gridsByOwner[owner];
            const int power = runtimeBuildingPowerOutput(*building, object);
            if (power >= 0) {
                grid.produced += power;
            } else {
                grid.used += -power;
            }
            if (owner == localOwner
                && runtimeBuildingCountsAsRefinery(*building)) {
                ++refineries;
            }
        } else if (owner == localOwner) {
            const UnitType* unit =
                findRuntimeUnitTypeById(state.rules, object.type);
            if (unit != nullptr && unit->harvester) {
                ++harvesters;
            }
        }
    }

    for (auto& [owner, grid] : gridsByOwner)
        if (runtimeDiskDrainOwnerBlackout(state, owner)) grid.produced = 0;
    const auto localGridIt = gridsByOwner.find(localOwner);
    const int produced = localGridIt == gridsByOwner.end()
        ? 0
        : localGridIt->second.produced;
    const int used = localGridIt == gridsByOwner.end()
        ? 0
        : localGridIt->second.used;

    retargetRuntimeSidebarPowerAnimation(state, produced, used);
    state.economy.powerProduced = produced;
    state.economy.powerUsed = used;
    state.economy.harvesters = harvesters;
    state.economy.refineries = refineries;
    refreshRuntimeResourceStorageCapacity(state);

    const bool lowPower = produced - used < 0
        || runtimeDiskDrainOwnerBlackout(state, state.economy.owner)
        || state.spies.blackoutUntilTick(
            runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner)
        ) > state.tick;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive || object.kind != WorldObjectKind::Structure) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr) {
            continue;
        }
        const std::string owner =
            toUpperAsciiLocal(trimAsciiLocal(object.owner));
        const bool neutral = owner.empty()
            || owner == "NEUTRAL"
            || owner == "CIVILIAN"
            || owner == "SPECIAL";
        object.workAnimationActive = !(
            neutral
            && building->needsEngineer
            && !trimAsciiLocal(building->workingSound).empty()
        );
        const auto gridIt = gridsByOwner.find(owner);
        const bool ownerLowPower = !neutral
            && gridIt != gridsByOwner.end()
            && gridIt->second.produced - gridIt->second.used < 0;
        const bool ownerSpyBlackout = !neutral
            && state.spies.blackoutUntilTick(
                runtimeRepairDepotCanonicalOwnerId(state, object.owner)
            ) > state.tick;
        // Every house owns an independent power grid. Render layers such as a
        // power plant rotor pause on a deficient grid; gameplay gating still
        // separately honors the building's Rules=Powered capability. Layers
        // authored with Powered=no remain animated in the renderer.
        const bool poweredOperational = !ownerLowPower && !ownerSpyBlackout
            && !runtimeDiskDrainOwnerBlackout(state, object.owner)
            && !runtimeDiskDrainDisablesObject(state, object.instanceId);
        const std::string notWorkingSound =
            runtimeBuildingPowerTransitionSound(
                *building,
                state.runtimePowerStateObserved
                    && !state.runtimeSuppressPlayerAudioFeedback,
                owner == localOwner,
                object.poweredOperational,
                poweredOperational
            );
        if (!poweredOperational) {
            if (object.powerAnimationPausedAtTick < 0) {
                object.powerAnimationPausedAtTick = state.tick;
            }
        } else if (object.powerAnimationPausedAtTick >= 0) {
            const long long pausedDuration = std::max(
                0LL,
                static_cast<long long>(state.tick)
                    - static_cast<long long>(
                        object.powerAnimationPausedAtTick
                    )
            );
            const long long totalPausedTicks =
                static_cast<long long>(object.powerAnimationPausedTicks)
                + pausedDuration;
            object.powerAnimationPausedTicks = static_cast<int>(
                std::min(
                    static_cast<long long>(
                        std::numeric_limits<int>::max()
                    ),
                    totalPausedTicks
                )
            );
            object.powerAnimationPausedAtTick = -1;
        }
        object.poweredOperational = poweredOperational;
        if (!notWorkingSound.empty()) {
            emitRuntimeSound(state, "sound:" + notWorkingSound, runtimeSpatialAudioPositionForObject(object));
        }
    }
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive || object.kind == WorldObjectKind::Structure) {
            continue;
        }
        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit == nullptr || !unit->poweredUnit) {
            continue;
        }
        const bool poweredOperational =
            runtimePoweredUnitIsOperational(state, object);
        const std::string transitionSound =
            runtimePoweredUnitTransitionSound(
                *unit,
                state.runtimePowerStateObserved
                    && !state.runtimeSuppressPlayerAudioFeedback,
                toUpperAsciiLocal(trimAsciiLocal(object.owner)) == localOwner,
                object.poweredOperational,
                poweredOperational
            );
        object.poweredOperational = poweredOperational;
        if (!transitionSound.empty()) {
            emitRuntimeSound(state, "sound:" + transitionSound, runtimeSpatialAudioPositionForObject(object));
        }
    }
    refreshRuntimeAircraftDockAssignments(state);
    if (state.runtimePowerStateObserved
        && state.runtimeLastLowPowerState != lowPower
        && lowPower) {
        emitRuntimeLowPowerWarning(state, true);
    } else if (state.runtimePowerStateObserved
        && state.runtimeLastLowPowerState != lowPower
        && !lowPower) {
        emitRuntimeSound(state, runtimeSoundIntentPowerOn);
        state.runtimeLastLowPowerSoundTick = state.tick;
    }
    state.runtimePowerStateObserved = true;
    state.runtimeLastLowPowerState = lowPower;
    (void)refreshRuntimeFactoryPlantCatalog(state);
    updateRuntimeRadarUiState(state, runtimeRadarAvailable(state));
}

void initializeRuntimeLocalProductionLanes(RuntimeEconomyState& economy) {
    economy.localProductionLanes = RuntimeProductionLanesState{};
    economy.localProductionLanesInitialized = true;
    economy.selectedPlacementLane.reset();
    economy.selectedPlacementSerial = 0;
    economy.placementCell = RuntimeCellPoint{};
    economy.placementValid = false;
}

void initializeRuntimeEconomy(RuntimeProductionHostState& state) {
    state.deathWeapons.reset();
    state.combatDetails = RuntimeCombatDetailsState{};
    state.spies.reset();
    state.spiesInitialized = false;
    state.factoryDeliveries.reset();
    state.pendingCloneDeliveries.clear();
    state.playerCommands = {};
    state.playerLocalCommands = {};
    state.playerCommunication = {};
    resetRuntimePlayerDefeatNotices(state);
    state.playerLastRadarCell.reset();
    state.pendingObjectExits.clear();
    state.pendingObjectExitIds.clear();
    state.pendingBridgeCollapses.clear();
    state.deferredBridgeMutationCells.clear();
    state.drainingObjectExits = false;
    state.campaignMode =
        toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
        == "CAMPAIGNMISSION";
    // Re-entering another mode or a campaign must not consult the previous
    // session's mode file while its new technology catalog is being created.
    state.skirmishModeRules = IniFile{};
    state.skirmishModeRulesLoaded = false;
    state.skirmishModeRulesPath.clear();
    state.skirmishModeAlliesAllowed = true;
    state.skirmishModeAllyChangeAllowed = true;
    state.skirmishModeMustAlly = false;
    initializeRuntimeCampaignScore(state);
    state.sidebarPowerAnimation = RuntimeSidebarPowerAnimation{};
    state.lastSidebarPowerDisplayedProduced = 0;
    state.lastSidebarPowerDisplayedUsed = 0;
    state.lastSidebarPowerAnimationActive = false;
    state.lastSidebarLowPowerIndicators = 0;
    state.multiplayerGameplayCommandsApplied = 0;
    state.multiplayerGameplayCommandsNoOp = 0;
    state.multiplayerGameplayAuthorityLog.clear();
    state.multiplayerDefeatedOwners.clear();
    state.multiplayerWinningOwners.clear();
    state.multiplayerWinningTeams.clear();
    state.economy = RuntimeEconomyState{};
    initializeRuntimeLocalProductionLanes(state.economy);
    state.economy.owner = state.campaignMode
        ? trimAsciiLocal(state.config.campaignPlayerHouse)
        : trimAsciiLocal(state.config.playerCountry);
    if (state.campaignMode
        && toUpperAsciiLocal(trimAsciiLocal(state.economy.owner))
            == "PLAYER HOUSE") {
        // Official campaign maps use both the display house name "Player
        // House" and the runtime object owner "Player". Prefer the concrete
        // owner that is actually present in this map so trigger conditions,
        // power grids and victory actions refer to the same identity.
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (toUpperAsciiLocal(trimAsciiLocal(object.owner)) == "PLAYER") {
                state.economy.owner = object.owner;
                break;
            }
        }
    }
    if (state.economy.owner.empty()
        || (!state.campaignMode
            && toUpperAsciiLocal(state.economy.owner) == "RANDOM")) {
        if (state.campaignMode) {
            state.economy.owner = trimAsciiLocal(state.config.playerCountry);
        }
    }
    if (state.economy.owner.empty()) {
        state.economy.owner = chooseRuntimeEconomyOwner(state.world);
    }
    const std::string fallbackCountry = state.economy.owner;
    if (state.campaignMode) {
        // "Player House" is an authored map house, not a skirmish slot. Keep
        // the country lookup deterministic for tech and sprite ownership
        // checks without creating opponents, starts or crates.
        state.skirmishCountryByOwner[
            toLowerAsciiLocal(state.economy.owner)
        ] = trimAsciiLocal(state.config.playerCountry).empty()
            ? fallbackCountry
            : trimAsciiLocal(state.config.playerCountry);
    } else {
        initializeRuntimeSkirmishPlayerIdentities(state, fallbackCountry);
    }
    state.economy.techLevel = std::clamp(state.config.techLevel, 1, 10);
    state.localRadarColor = loadRuntimeLocalRadarColor(state.config);
    state.economy.credits = std::max(0, state.config.startingCredits);
    const RuntimeOwnerRemapColorTable configuredColors =
        loadRuntimeRemapColorTable(state.config);
    const auto applyOwnerColor = [&state, &configuredColors](
        const std::string& owner,
        const std::string& color
    ) {
        const std::string ownerKey =
            toLowerAsciiLocal(trimAsciiLocal(owner));
        const std::string colorKey =
            toLowerAsciiLocal(trimAsciiLocal(color));
        const auto found = configuredColors.find(colorKey);
        if (!ownerKey.empty() && found != configuredColors.end()) {
            state.ownerRemapColors[ownerKey] = found->second;
        }
    };
    for (std::size_t slotIndex = 0;
        slotIndex < state.config.playerSlots.size();
        ++slotIndex) {
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        if (slot.enabled) {
            applyOwnerColor(
                runtimeSkirmishOwnerForSlot(slotIndex),
                slot.color
            );
        }
    }
    applyOwnerColor(state.economy.owner, state.config.playerColor);
    addRuntimeTechTreeBuildCatalog(state);
    (void)addRuntimeSecretLabBuildCatalog(state);
    if (!state.campaignMode) {
        loadAndApplyRuntimeSkirmishModeRules(state);
    }
    applyRuntimeSuperweaponCatalogOption(state);
    initializeRuntimeLimitedBuildHistory(state);
    initializeRuntimeHumanHouseAuthorities(state);
    rebuildRuntimeEconomyPower(state);
    snapRuntimeSidebarPowerAnimation(state);
    state.economy.status =
        "economy ready for " + state.economy.owner
        + " (" + runtimeCountryForOwner(state, state.economy.owner) + ")"
        + " tech nodes "
        + std::to_string(state.techTree.buildableNodes)
        + "/"
        + std::to_string(state.techTree.totalNodes)
        + " modeRules="
        + (state.campaignMode
            ? std::string("campaign-authored")
            : state.skirmishModeRulesLoaded
            ? std::filesystem::path(state.skirmishModeRulesPath).filename().string()
            : std::string("base"));
    // Map-authored objects exist before the first fixed simulation tick.
    // Initialize permanent Spy disguises here so the first paint cannot
    // briefly expose their true identity.
    ensureRuntimeSpiesInitialized(state);
}

RuntimeStructureFootprint runtimeStructureFootprintForBuildItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    const auto found = state.structureFootprints.find(item.typeId);
    if (found != state.structureFootprints.end() && found->second.valid) {
        return found->second;
    }
    return RuntimeStructureFootprint{1, 1, true};
}

bool runtimeResourceOverlayBlocksPlacement(
    const RuntimeGameSessionState& state,
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return false;
    }

    const LandType* tiberiumLandType = state.rules.findLandType("Tiberium");
    return tiberiumLandType == nullptr || !tiberiumLandType->buildable;
}

RuntimeBuildupTiming runtimeStructureBuildupTiming(
    RuntimeProductionHostState& state, const WorldObjectInstance& object,
    std::string* resolvedSource) {
    int frames = 0;
    const ArtType* art = runtimeArtTypeForObject(state, object);
    const BuildingType* building = findRuntimeBuildingType(state.rules, object);
    if (art != nullptr && !trimAsciiLocal(art->buildup).empty()) {
        RuntimeMapRenderContext context;
        context.theater = state.theaterLoaded ? &state.theater : nullptr;
        context.art = state.artLoaded ? &state.art : nullptr;
        context.rules = state.rulesLoaded ? &state.rules : nullptr;
        context.mapFacts = state.mapFactsLoaded ? &state.mapFacts : nullptr;
        context.assetRootPath = runtimeAssetRootPath(state.config);
        context.structureRootPath = runtimeStructureRootPath(state.config, state.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(state.config, state.report.map);
        context.palettePath = runtimePalettePath(state.config, state.report.map);
        RuntimeShpFrameImage first;
        std::string resolvedId, source, error;
        int rawFrames = 0;
        if (loadRuntimeAuxiliaryShpFrameImage(context, state.renderCache, art->buildup,
                0, first, resolvedId, source, error, false, &rawFrames, art)) {
            frames = runtimeBuildupFrameCount(rawFrames,
                building != nullptr && building->gate, art->gateStages);
            if (resolvedSource != nullptr) *resolvedSource = source;
        }
    }
    return runtimeBuildupTiming(state.rules.getProductionRules().buildupTime, frames,
        state.config.fixedTickHz);
}

void beginRuntimeStructureSaleAnimation(RuntimeProductionHostState& state, WorldObjectInstance& object) {
    const BuildingType* building = findRuntimeBuildingType(state.rules, object);
    const auto timing = runtimeStructureBuildupTiming(state, object);
    int total = timing.fixedTicks;
    int remaining = total;
    if (object.buildAnimTicksTotal > 0 && object.buildAnimTicksRemaining > 0) {
        // Cancel unfinished construction from the stage already on screen.
        // Keep its original cadence; do not flash the fully built last frame.
        total = object.buildAnimTicksTotal;
        const int stage = runtimeBuildupFrameAtProgress(timing.frames, total,
            object.buildAnimTicksRemaining, state.config.fixedTickHz, false);
        remaining = timing.frames > 1
            ? static_cast<int>((static_cast<std::int64_t>(stage) + 1) * total / timing.frames) : 0;
    }
    if (timing.frames <= 1 || (building != nullptr && building->wall)) total = remaining = 0;
    object.buildAnimTicksTotal = remaining > 0 ? total : 0;
    object.buildAnimTicksRemaining = remaining;
    object.rubble = false;
}

int runtimeFactoryAirExitCloseTicks(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory) {
    return runtimeFactoryAirExitTransitionTicks(runtimeArtTypeForObject(state, factory),
        &state.art, state.config.fixedTickHz);
}

bool runtimeFactoryUsesAirExit(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory, const WorldObjectInstance& produced) {
    const auto* type = findRuntimeBuildingTypeById(state.rules, factory.type);
    return factory.kind == WorldObjectKind::Structure && type && type->weaponsFactory
        && toUpperAsciiLocal(type->factory) == "UNITTYPE"
        && runtimeObjectMovementMode(state, produced) == PathMovementMode::Air;
}

std::optional<RuntimeCellPoint> runtimeFactoryAirExitSpawnCell(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory) {
    const auto footprint = runtimeStructureFootprintForObject(state, factory);
    const RuntimeCellPoint cell{factory.x + (std::max(1, footprint.width) - 1) / 2,
        factory.y + (std::max(1, footprint.height) - 1) / 2};
    if (state.terrainByCell.find(runtimeCellKey(cell.x, cell.y)) == state.terrainByCell.end())
        return std::nullopt;
    for (const auto& object : state.world.getObjects())
        if (object.alive && object.factoryExitHostId == factory.instanceId) return std::nullopt;
    return cell;
}

const WorldObjectInstance* runtimeFactoryAirExitContainingHost(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced) {
    if (produced.factoryExitHostId < 0) return nullptr;
    const auto* host = findObjectById(state, produced.factoryExitHostId);
    if (!host || !host->alive || !host->factoryExitAirborne) return nullptr;
    const auto footprint = runtimeStructureFootprintForObject(state, *host);
    const double x = runtimeObjectRenderX(produced), y = runtimeObjectRenderY(produced);
    return x >= host->x && x < host->x + std::max(1, footprint.width)
        && y >= host->y && y < host->y + std::max(1, footprint.height) ? host : nullptr;
}

bool runtimeFactoryAirExitWaitingForRoof(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced) {
    const auto* host = runtimeFactoryAirExitContainingHost(state, produced);
    return host && state.tick - host->factoryExitAnimationStartTick
        < runtimeFactoryAirExitCloseTicks(state, *host);
}

bool runtimeFactoryAirExitReadyToMove(const RuntimeGameSessionState& state,
    const WorldObjectInstance& produced) {
    const auto* host = runtimeFactoryAirExitContainingHost(state, produced);
    if (!host) return true;
    if (runtimeFactoryAirExitWaitingForRoof(state, produced)) return false;
    const auto* unit = findRuntimeUnitType(state.rules, produced);
    if (!unit || !runtimeUnitUsesJumpjetMovement(*unit)) return true;
    const auto flight = state.jumpjetFlights.find(produced.instanceId);
    const auto height = flight == state.jumpjetFlights.end()
        ? produced.flightLevel * runtimeJumpjetCellQ16 : flight->second.height;
    return height >= terrainLevelAtRuntimeCell(state, produced.x, produced.y) * runtimeJumpjetCellQ16
        + unit->jumpJetHeight * runtimeJumpjetQ16;
}

bool advanceRuntimeFactoryExitAnimations(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeFactoryExitAnimations", state.tick);
    bool changed = false;
    std::unordered_set<int> occupiedRoofs;
    std::unordered_set<int> releasedRoofs;
    for (auto& produced : state.world.accessObjects()) {
        if (produced.factoryExitHostId < 0) continue;
        const auto* host = findObjectById(state, produced.factoryExitHostId);
        bool cleared = !produced.alive || host == nullptr || !host->alive
            || host->kind != WorldObjectKind::Structure;
        if (!cleared) {
            requestRuntimeFactoryAirExitClearance(state, produced, *host);
            const auto footprint = runtimeStructureFootprintForObject(state, *host);
            const double x = runtimeObjectRenderX(produced), y = runtimeObjectRenderY(produced);
            // Keep the roof open through the flight from its center and the
            // entire one-cell outward movement, including render interpolation.
            cleared = x >= host->x + std::max(1, footprint.width) + 1.0 - 1e-6
                || x <= host->x - 2.0 + 1e-6
                || y >= host->y + std::max(1, footprint.height) + 1.0 - 1e-6
                || y <= host->y - 2.0 + 1e-6;
        }
        if (cleared) {
            releasedRoofs.insert(produced.factoryExitHostId);
            produced.factoryExitHostId = -1;
            changed = true;
        } else {
            occupiedRoofs.insert(produced.factoryExitHostId);
        }
    }
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (object.factoryExitAnimationStartTick < 0) {
            continue;
        }
        if (object.alive && object.factoryExitAirborne) {
            const int closeTicks = runtimeFactoryAirExitCloseTicks(state, object);
            const int previousEnd = object.factoryExitAnimationEndTick;
            if (occupiedRoofs.contains(object.instanceId)) {
                object.factoryExitAnimationEndTick = std::max(previousEnd, state.tick + closeTicks + 1);
            } else if (releasedRoofs.contains(object.instanceId)) {
                object.factoryExitAnimationEndTick = std::max(
                    object.factoryExitAnimationStartTick + closeTicks * 2 + 1,
                    state.tick + closeTicks);
            }
            changed = changed || previousEnd != object.factoryExitAnimationEndTick;
        }
        if (!object.alive
            || state.tick >= object.factoryExitAnimationEndTick) {
            object.factoryExitAnimationStartTick = -1;
            object.factoryExitAnimationEndTick = -1;
            object.factoryExitAirborne = false;
            changed = true;
        }
    }
    return changed;
}

const UnitType* findRuntimeProductionListedUnitType(
    const std::vector<UnitType>& types,
    const std::string& typeId) {
    const std::string wanted = toUpperAsciiLocal(trimAsciiLocal(typeId));
    const auto found = std::find_if(
        types.begin(),
        types.end(),
        [&wanted](const UnitType& type) {
            return toUpperAsciiLocal(trimAsciiLocal(type.id)) == wanted;
        }
    );
    return found == types.end() ? nullptr : &*found;
}

std::optional<RuntimeProductionLane> runtimeProductionLaneForCatalogItem(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, item.typeId);
        if (building == nullptr) {
            return std::nullopt;
        }
        return runtimeProductionLaneForBuilding(*building);
    }

    if (item.kind == RuntimeBuildKind::Infantry) {
        const UnitType* infantry = findRuntimeProductionListedUnitType(
            state.rules.getInfantryTypes(),
            item.typeId
        );
        if (infantry == nullptr) {
            return std::nullopt;
        }
        return runtimeProductionLaneForUnit(
            *infantry,
            RuntimeProductionUnitClass::Infantry
        );
    }

    // Registration list is authoritative.  ConsideredAircraft changes world
    // behavior, not the production lane: VehicleTypes such as DISK/ZEP stay
    // on Vehicles, while only AircraftTypes use the Aircraft lane.
    if (const UnitType* vehicle = findRuntimeProductionListedUnitType(
            state.rules.getVehicleTypes(),
            item.typeId
        )) {
        return runtimeProductionLaneForUnit(
            *vehicle,
            RuntimeProductionUnitClass::Vehicle
        );
    }
    if (const UnitType* aircraft = findRuntimeProductionListedUnitType(
            state.rules.getAircraftTypes(),
            item.typeId
        )) {
        return runtimeProductionLaneForUnit(
            *aircraft,
            RuntimeProductionUnitClass::Aircraft
        );
    }
    return std::nullopt;
}

std::optional<RuntimeProductionCatalogAdapter>
runtimeProductionCatalogLookup(
    const RuntimeGameSessionState& state,
    std::string_view itemKey) {
    const std::string wanted = toUpperAsciiLocal(
        trimAsciiLocal(std::string(itemKey))
    );
    for (const RuntimeBuildCatalogItem& item : state.economy.catalog) {
        if (toUpperAsciiLocal(trimAsciiLocal(item.typeId)) != wanted) {
            continue;
        }
        const std::optional<RuntimeProductionLane> lane =
            runtimeProductionLaneForCatalogItem(state, item);
        if (lane.has_value()) {
            return RuntimeProductionCatalogAdapter{&item, *lane};
        }
    }
    return std::nullopt;
}

bool runtimeProductionOwnersMatch(
    const RuntimeGameSessionState& state,
    const std::string& left,
    const std::string& right) {
    const std::string canonicalLeft =
        runtimeRepairDepotCanonicalOwnerId(state, left);
    const std::string canonicalRight =
        runtimeRepairDepotCanonicalOwnerId(state, right);
    return !canonicalLeft.empty()
        && canonicalLeft == canonicalRight;
}

bool runtimeStructureProvidesFactoryForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    const std::string& requiredFactory) {
    if (!object.alive
        || !runtimeProductionOwnersMatch(state, object.owner, owner)
        || object.kind != WorldObjectKind::Structure
        || trimAsciiLocal(requiredFactory).empty()) {
        return false;
    }

    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    return building != nullptr
        && toUpperAsciiLocal(trimAsciiLocal(building->factory))
            == toUpperAsciiLocal(trimAsciiLocal(requiredFactory))
        && (!building->powered || object.poweredOperational);
}

bool runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    RuntimeFactoryDeliveryDomain domain) {
    if (!object.alive
        || object.health == 0
        || object.rubble
        || object.kind != WorldObjectKind::Structure
        || !runtimeProductionOwnersMatch(state, object.owner, owner)) {
        return false;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building == nullptr) {
        return false;
    }
    const std::string factory =
        toUpperAsciiLocal(trimAsciiLocal(building->factory));
    const bool navalFactory = building->naval
        || building->waterBound
        || factory == "NAVALUNITTYPE"
        || factory == "VESSELTYPE";
    if (domain == RuntimeFactoryDeliveryDomain::Naval) {
        return navalFactory
            && (factory == "UNITTYPE"
                || factory == "VEHICLETYPE"
                || factory == "NAVALUNITTYPE"
                || factory == "VESSELTYPE");
    }
    return !navalFactory
        && building->weaponsFactory
        && (factory == "UNITTYPE" || factory == "VEHICLETYPE");
}

std::optional<RuntimeFactoryDeliveryDomain>
runtimeFactoryDeliveryHostDomain(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building == nullptr) {
        return std::nullopt;
    }
    const std::string factory =
        toUpperAsciiLocal(trimAsciiLocal(building->factory));
    const bool navalFactory = building->naval
        || building->waterBound
        || factory == "NAVALUNITTYPE"
        || factory == "VESSELTYPE";
    if (navalFactory) {
        return RuntimeFactoryDeliveryDomain::Naval;
    }
    if (building->weaponsFactory
        && (factory == "UNITTYPE" || factory == "VEHICLETYPE")) {
        return RuntimeFactoryDeliveryDomain::Land;
    }
    return std::nullopt;
}

bool runtimeProductionFactoryEligibleForOwner(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& owner,
    RuntimeProductionLane lane) {
    if (!object.alive
        || !runtimeProductionOwnersMatch(state, object.owner, owner)
        || object.kind != WorldObjectKind::Structure) {
        return false;
    }
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building == nullptr
        || (building->powered && !object.poweredOperational)) {
        return false;
    }

    if (lane == RuntimeProductionLane::Ships) {
        return runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
            state,
            object,
            owner,
            RuntimeFactoryDeliveryDomain::Naval
        );
    }
    if (lane == RuntimeProductionLane::Vehicles) {
        return runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
            state,
            object,
            owner,
            RuntimeFactoryDeliveryDomain::Land
        );
    }
    return runtimeProductionFactorySupportsLane(*building, lane);
}

void addRuntimeFactoryAnchorCandidate(
    std::vector<const WorldObjectInstance*>& anchors,
    const WorldObjectInstance& object) {
    const auto duplicateIt = std::find_if(
        anchors.begin(),
        anchors.end(),
        [&object](const WorldObjectInstance* anchor) {
            return anchor != nullptr
                && anchor->instanceId == object.instanceId;
        }
    );
    if (duplicateIt == anchors.end()) {
        anchors.push_back(&object);
    }
}

void sortRuntimeFactoryAnchorCandidates(
    std::vector<const WorldObjectInstance*>& anchors) {
    std::sort(
        anchors.begin(),
        anchors.end(),
        [](const WorldObjectInstance* left,
           const WorldObjectInstance* right) {
            if (left == nullptr || right == nullptr) {
                return right != nullptr;
            }
            return left->instanceId < right->instanceId;
        }
    );
}

std::vector<const WorldObjectInstance*>
findRuntimeFactoryAnchorCandidatesForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback) {
    std::vector<const WorldObjectInstance*> anchors;
    if (!allowLegacyFallback) {
        const std::optional<RuntimeProductionLane> lane =
            runtimeProductionLaneForCatalogItem(state, item);
        if (!lane.has_value()) {
            return anchors;
        }
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (runtimeProductionFactoryEligibleForOwner(
                    state,
                    object,
                    owner,
                    *lane
                )
                && runtimeFactoryOwnerCompatibleWithBuildItem(
                    state,
                    object,
                    item
                )) {
                addRuntimeFactoryAnchorCandidate(anchors, object);
            }
        }
        sortRuntimeFactoryAnchorCandidates(anchors);
        prioritizeRuntimePlayerFactory(state, owner, anchors);
        return anchors;
    }

    const bool navalItem = runtimeBuildItemLooksNaval(state, item);
    const auto factoryMatchesProductionDomain = [
        &state,
        &item,
        navalItem
    ](const WorldObjectInstance& object) {
        if (item.kind != RuntimeBuildKind::Unit
            || toUpperAsciiLocal(item.requiredFactory) != "UNITTYPE") {
            return true;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr
            || toUpperAsciiLocal(building->factory) != "UNITTYPE") {
            return false;
        }
        const bool navalFactory = building->naval || building->waterBound;
        return navalItem
            ? navalFactory
            : building->weaponsFactory && !navalFactory;
    };
    if (navalItem) {
        for (const WorldObjectInstance& object : state.world.getObjects()) {
            if (!runtimeStructureProvidesFactoryForOwner(
                    state,
                    object,
                    owner,
                    "UnitType"
                )
                || !runtimeFactoryOwnerCompatibleWithBuildItem(
                    state,
                    object,
                    item
                )) {
                continue;
            }

            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            const std::string typeToken = toLowerAsciiLocal(object.type);
            if (building != nullptr
                && (building->naval
                    || building->waterBound
                    || typeToken.find("yard") != std::string::npos)) {
                addRuntimeFactoryAnchorCandidate(anchors, object);
            }
        }
    }

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (runtimeStructureProvidesFactoryForOwner(
                state,
                object,
                owner,
                item.requiredFactory
            )
            && runtimeFactoryOwnerCompatibleWithBuildItem(
                state,
                object,
                item
            )
            && factoryMatchesProductionDomain(object)) {
            addRuntimeFactoryAnchorCandidate(anchors, object);
        }
    }

    if (!anchors.empty()) {
        sortRuntimeFactoryAnchorCandidates(anchors);
        prioritizeRuntimePlayerFactory(state, owner, anchors);
        return anchors;
    }

    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && runtimeProductionOwnersMatch(state, object.owner, owner)
            && object.kind == WorldObjectKind::Structure
            && runtimeFactoryOwnerCompatibleWithBuildItem(
                state,
                object,
                item
            )
            && factoryMatchesProductionDomain(object)) {
            addRuntimeFactoryAnchorCandidate(anchors, object);
        }
    }

    sortRuntimeFactoryAnchorCandidates(anchors);
        prioritizeRuntimePlayerFactory(state, owner, anchors);
    return anchors;
}

const WorldObjectInstance* findRuntimeFactoryAnchorForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback) {
    const std::vector<const WorldObjectInstance*> anchors =
        findRuntimeFactoryAnchorCandidatesForOwner(
            state,
            item,
            owner,
            allowLegacyFallback
        );
    return anchors.empty() ? nullptr : anchors.front();
}

const TerrainCell* runtimeProductionTerrainCellAt(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const auto cached = state.terrainByCell.find(
        runtimeCellKey(cell.x, cell.y)
    );
    if (cached != state.terrainByCell.end()) {
        return cached->second;
    }
    const auto terrain = std::find_if(
        state.world.getTerrainCells().begin(),
        state.world.getTerrainCells().end(),
        [&cell](const TerrainCell& candidate) {
            return candidate.x == cell.x && candidate.y == cell.y;
        }
    );
    return terrain == state.world.getTerrainCells().end()
        ? nullptr
        : &*terrain;
}

bool runtimeProductionCellIsOpenSurfaceWater(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const TerrainCell* terrain = runtimeProductionTerrainCellAt(state, cell);
    if (terrain == nullptr) {
        return false;
    }
    const RuntimeTerrainInfo info =
        runtimeCachedTerrainInfoForCell(state, *terrain);
    return info.passable && info.water && !info.bridge
        && !runtimeCellIsBridge(state, cell.x, cell.y);
}

bool runtimeBuildItemUsesPadAircraft(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item) {
    return item.kind == RuntimeBuildKind::Unit
        && findRuntimePadAircraftUnitType(state.rules, item.typeId) != nullptr;
}

RuntimeAircraftProductionMatchInputBuild
buildRuntimeAircraftProductionMatchInputForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::optional<RuntimeAircraftProductionOrder> prospectiveOrder,
    const RuntimeRepairDepotSystemState* repairAuthority,
    bool includeExistingOrders) {
    RuntimeAircraftProductionMatchInputBuild build;
    RuntimeAircraftProductionMatchInput& input = build.input;
    input.canonicalOwner = runtimeRepairDepotCanonicalOwnerId(state, owner);
    std::map<std::pair<int, int>, std::size_t> slotIndexByKey;
    std::set<std::pair<int, int>> nonPadOccupiedSlots;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && object.dockedToObjectId >= 0
            && object.dockIndex >= 0
            && findRuntimePadAircraftUnitType(
                state.rules,
                object
            ) == nullptr) {
            nonPadOccupiedSlots.emplace(
                object.dockedToObjectId,
                object.dockIndex
            );
        }
    }

    for (const WorldObjectInstance& host : state.world.getObjects()) {
        if (!runtimeAircraftPadHostStructurallyEligibleForOwner(
                state,
                host,
                owner
            )) {
            continue;
        }
        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, host.type);
        if (building == nullptr || building->numberOfDocks <= 0) {
            continue;
        }
        std::vector<std::string> compatibleTypes;
        for (const std::string& typeId : state.rules.getPadAircraft()) {
            RuntimeBuildCatalogItem item;
            item.kind = RuntimeBuildKind::Unit;
            item.typeId = typeId;
            if (runtimeAircraftCanDockAtStructureForOwner(
                    state,
                    item,
                    host,
                    owner
                )) {
                compatibleTypes.push_back(trimAsciiLocal(typeId));
            }
        }
        std::sort(
            compatibleTypes.begin(),
            compatibleTypes.end(),
            [](const std::string& left, const std::string& right) {
                return toUpperAsciiLocal(left) < toUpperAsciiLocal(right);
            }
        );
        compatibleTypes.erase(
            std::unique(
                compatibleTypes.begin(),
                compatibleTypes.end(),
                [](const std::string& left, const std::string& right) {
                    return toUpperAsciiLocal(left)
                        == toUpperAsciiLocal(right);
                }
            ),
            compatibleTypes.end()
        );
        if (compatibleTypes.empty()) {
            continue;
        }
        for (int dockIndex = 0;
            dockIndex < building->numberOfDocks;
            ++dockIndex) {
            RuntimeAircraftProductionSlot slot;
            slot.canonicalOwner = input.canonicalOwner;
            slot.hostId = host.instanceId;
            slot.dockIndex = dockIndex;
            slot.compatibleAircraftTypeIds = compatibleTypes;
            const RuntimeRepairDepotSystemState* effectiveRepairAuthority =
                repairAuthority != nullptr
                    ? repairAuthority
                    : (state.repairDepotsInitialized
                        ? &state.repairDepots
                        : nullptr);
            slot.available = nonPadOccupiedSlots.find({
                    host.instanceId,
                    dockIndex
                }) == nonPadOccupiedSlots.end()
                && (effectiveRepairAuthority == nullptr
                    || !effectiveRepairAuthority->claimsDock(
                        host.instanceId,
                        dockIndex
                    ));
            slot.alive = true;
            slot.poweredOperational = !building->powered
                || host.poweredOperational;
            const std::string primaryCategory = toUpperAsciiLocal(building->factory)
                + ((building->naval || building->waterBound) ? ":NAVAL" : ":LAND");
            const auto primary = state.playerCommands.primaryFactories.find({
                runtimeCanonicalHumanHouseOwner(owner), primaryCategory});
            slot.preferredPrimary = slot.poweredOperational
                && primary != state.playerCommands.primaryFactories.end()
                && primary->second == host.instanceId;
            slotIndexByKey.emplace(
                std::pair{host.instanceId, dockIndex},
                input.slots.size()
            );
            input.slots.push_back(std::move(slot));
        }
    }

    for (const WorldObjectInstance& aircraft : state.world.getObjects()) {
        const auto slot = slotIndexByKey.find({
            aircraft.dockedToObjectId,
            aircraft.dockIndex
        });
        if (!aircraft.alive || slot == slotIndexByKey.end()) {
            continue;
        }
        const std::string aircraftOwner = runtimeRepairDepotCanonicalOwnerId(
            state,
            aircraft.owner
        );
        if (aircraftOwner != input.canonicalOwner
            || findRuntimePadAircraftUnitType(
                    state.rules,
                    aircraft
                ) == nullptr) {
            input.slots[slot->second].available = false;
            continue;
        }
        RuntimeAircraftProductionLiveOccupancy occupancy;
        occupancy.canonicalOwner = aircraftOwner;
        occupancy.aircraftId = aircraft.instanceId;
        occupancy.hostId = aircraft.dockedToObjectId;
        occupancy.dockIndex = aircraft.dockIndex;
        occupancy.airborne = aircraft.airborne;
        input.liveOccupancies.push_back(std::move(occupancy));
    }

    const auto appendOrder = [&state, &input](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order,
        RuntimeAircraftProductionOrderPhase phase
    ) -> bool {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, order.itemKey);
        if (!catalog.has_value()
            || catalog->item == nullptr
            || catalog->lane != lane
            || catalog->item->kind != RuntimeBuildKind::Unit
            || !state.rules.isPadAircraft(catalog->item->typeId)) {
            return false;
        }
        RuntimeAircraftProductionOrder matchedOrder;
        matchedOrder.serial = order.serial;
        matchedOrder.typeId = catalog->item->typeId;
        matchedOrder.phase = phase;
        input.orders.push_back(std::move(matchedOrder));
        return true;
    };
    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        if (!includeExistingOrders) {
            break;
        }
        const RuntimeProductionLaneState& laneState =
            state.economy.localProductionLanes.lanes[
                runtimeProductionLaneIndex(lane)
            ];
        for (const RuntimeProductionLaneOrder& queued : laneState.queued) {
            if (state.rules.isPadAircraft(queued.itemKey)) {
                if (!appendOrder(
                    lane,
                    queued,
                    RuntimeAircraftProductionOrderPhase::Queued
                )) {
                    RuntimeAircraftProductionMatchResult invalid;
                    invalid.veto =
                        RuntimeAircraftProductionMatchVeto::InvalidOrder;
                    invalid.vetoOrderSerial = queued.serial;
                    invalid.vetoTypeId = queued.itemKey;
                    invalid.reason =
                        "pad aircraft order does not match its catalog lane";
                    build.error = std::move(invalid);
                    return build;
                }
            }
        }
        if (laneState.active.has_value()
            && state.rules.isPadAircraft(laneState.active->itemKey)) {
            if (!appendOrder(
                lane,
                *laneState.active,
                RuntimeAircraftProductionOrderPhase::Active
            )) {
                RuntimeAircraftProductionMatchResult invalid;
                invalid.veto =
                    RuntimeAircraftProductionMatchVeto::InvalidOrder;
                invalid.vetoOrderSerial = laneState.active->serial;
                invalid.vetoTypeId = laneState.active->itemKey;
                invalid.reason =
                    "active pad aircraft order does not match its catalog lane";
                build.error = std::move(invalid);
                return build;
            }
        }
        if (laneState.ready.has_value()
            && state.rules.isPadAircraft(laneState.ready->itemKey)) {
            if (!appendOrder(
                lane,
                *laneState.ready,
                RuntimeAircraftProductionOrderPhase::Ready
            )) {
                RuntimeAircraftProductionMatchResult invalid;
                invalid.veto =
                    RuntimeAircraftProductionMatchVeto::InvalidOrder;
                invalid.vetoOrderSerial = laneState.ready->serial;
                invalid.vetoTypeId = laneState.ready->itemKey;
                invalid.reason =
                    "ready pad aircraft order does not match its catalog lane";
                build.error = std::move(invalid);
                return build;
            }
        }
    }
    input.prospectiveOrder = std::move(prospectiveOrder);
    return build;
}

RuntimeAircraftProductionMatchInputBuild
buildRuntimeAircraftProductionRecoveryInputForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    bool includeExistingOrders) {
    RuntimeAircraftProductionMatchInputBuild build =
        buildRuntimeAircraftProductionMatchInputForOwner(
            state,
            owner,
            std::nullopt,
            nullptr,
            includeExistingOrders
        );
    if (build.error.has_value()) {
        return build;
    }

    // Recovery is the one transition where an unbound live aircraft is a
    // movable demand instead of a hard occupancy. A foreign aircraft bound
    // to a same-owner pad is still a hard blocker, but pads owned by other
    // houses are outside this owner's input and must not poison it.
    const std::string canonicalOwner =
        runtimeRepairDepotCanonicalOwnerId(state, owner);
    std::set<std::pair<int, int>> slotKeys;
    for (const RuntimeAircraftProductionSlot& slot : build.input.slots) {
        slotKeys.emplace(slot.hostId, slot.dockIndex);
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.dockedToObjectId < 0
            || object.dockIndex < 0
            || findRuntimePadAircraftUnitType(state.rules, object) == nullptr
            || slotKeys.find({
                object.dockedToObjectId,
                object.dockIndex
            }) != slotKeys.end()) {
            continue;
        }
        const WorldObjectInstance* host = findObjectById(
            state,
            object.dockedToObjectId
        );
        if (host != nullptr
            && runtimeRepairDepotCanonicalOwnerId(state, host->owner)
                == canonicalOwner) {
            RuntimeAircraftProductionMatchResult invalid;
            invalid.veto =
                RuntimeAircraftProductionMatchVeto::OccupancySlotMissing;
            invalid.reason =
                "PadAircraft recovery found an invalid local dock mirror";
            build.error = std::move(invalid);
            return build;
        }
    }

    // Hard live occupancies and unavailable slots are contractions of the
    // recovery graph: neither can be used or moved by this transaction. Do
    // the strict cross-check before removing them so large foreign airbases
    // with thousands of already-bound aircraft do not hit the movable graph
    // limit, while duplicate or stale mirrors still fail closed.
    std::map<std::pair<int, int>, std::size_t> slotIndexByKey;
    for (std::size_t slotIndex = 0;
        slotIndex < build.input.slots.size();
        ++slotIndex) {
        const RuntimeAircraftProductionSlot& slot =
            build.input.slots[slotIndex];
        if (!slotIndexByKey.emplace(
                std::pair{slot.hostId, slot.dockIndex},
                slotIndex
            ).second) {
            RuntimeAircraftProductionMatchResult invalid;
            invalid.veto =
                RuntimeAircraftProductionMatchVeto::DuplicateSlot;
            invalid.reason =
                "PadAircraft recovery repeats a physical dock slot";
            build.error = std::move(invalid);
            return build;
        }
    }
    std::set<std::pair<int, int>> hardSlotKeys;
    std::set<int> hardAircraftIds;
    for (const RuntimeAircraftProductionLiveOccupancy& occupancy :
            build.input.liveOccupancies) {
        const std::pair<int, int> slotKey{
            occupancy.hostId,
            occupancy.dockIndex
        };
        const auto hardSlot = slotIndexByKey.find(slotKey);
        if (occupancy.canonicalOwner != build.input.canonicalOwner
            || hardSlot == slotIndexByKey.end()
            || !build.input.slots[hardSlot->second].alive
            || !build.input.slots[hardSlot->second].available
            || !hardSlotKeys.insert(slotKey).second
            || !hardAircraftIds.insert(occupancy.aircraftId).second) {
            RuntimeAircraftProductionMatchResult invalid;
            invalid.veto =
                RuntimeAircraftProductionMatchVeto::InvalidOccupancy;
            invalid.reason =
                "PadAircraft recovery has an invalid hard dock mirror";
            build.error = std::move(invalid);
            return build;
        }
    }
    std::erase_if(
        build.input.slots,
        [&hardSlotKeys](const RuntimeAircraftProductionSlot& slot) {
            return !slot.alive
                || !slot.available
                || hardSlotKeys.contains({slot.hostId, slot.dockIndex});
        }
    );
    build.input.liveOccupancies.clear();
    return build;
}

RuntimeAircraftProductionMatchResult runtimeAircraftProductionMatchForOwner(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::optional<RuntimeAircraftProductionOrder> prospectiveOrder,
    const RuntimeRepairDepotSystemState* repairAuthority) {
    RuntimeAircraftProductionMatchInputBuild build =
        buildRuntimeAircraftProductionMatchInputForOwner(
            state,
            owner,
            std::move(prospectiveOrder),
            repairAuthority,
            true
        );
    if (build.error.has_value()) {
        return std::move(*build.error);
    }
    return runtimeMatchAircraftProductionDocks(build.input);
}

std::optional<RuntimeAircraftProductionAssignment>
runtimeAircraftProductionAssignmentForSerial(
    const RuntimeGameSessionState& state,
    const std::string& owner,
    std::uint64_t serial) {
    if (serial == 0) {
        return std::nullopt;
    }
    const RuntimeAircraftProductionMatchResult matched =
        runtimeAircraftProductionMatchForOwner(state, owner);
    if (!matched.inputValid || !matched.feasible) {
        return std::nullopt;
    }
    const auto assignment = std::find_if(
        matched.assignments.begin(),
        matched.assignments.end(),
        [serial](const RuntimeAircraftProductionAssignment& candidate) {
            return candidate.orderSerial == serial;
        }
    );
    return assignment == matched.assignments.end()
        ? std::nullopt
        : std::optional<RuntimeAircraftProductionAssignment>(*assignment);
}

bool validateRuntimePadAircraftProductionProjection(
    const RuntimeGameSessionState& state,
    std::string& errorMessage, const std::string* miningOwner) {
    if (!validateRuntimeAircraftPadSlotCapacity(state, errorMessage)) {
        return false;
    }

    std::map<std::pair<int, int>, int> liveDockUsers;
    std::set<std::pair<int, int>> occupiedSlots;
    if (state.repairDepotsInitialized) {
        for (const RuntimeRepairDepotRelation& relation :
                state.repairDepots.relations()) {
            if (state.repairDepots.claimsDock(
                    relation.hostId,
                    relation.dockIndex
                )) {
                occupiedSlots.emplace(
                    relation.hostId,
                    relation.dockIndex
                );
            }
        }
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        const bool padAircraft = findRuntimePadAircraftUnitType(
            state.rules,
            object
        ) != nullptr;
        const bool unbound = object.dockedToObjectId == -1
            && object.dockIndex == -1;
        const bool bound = object.dockedToObjectId >= 0
            && object.dockIndex >= 0;
        if (padAircraft
            && runtimeObjectSuspendedByMagBeam(state, object.instanceId)
            && (!unbound
                || toLowerAsciiLocal(object.mission)
                    != "magbeamsuspended")) {
            errorMessage =
                "MagBeam PadAircraft has conflicting dock authority";
            return false;
        }
        if (padAircraft && !unbound && !bound) {
            errorMessage =
                "pad aircraft has a malformed dock authority pair";
            return false;
        }
        if (!object.alive || !bound) {
            continue;
        }

        const std::pair<int, int> slotKey{
            object.dockedToObjectId,
            object.dockIndex
        };
        const auto existing = liveDockUsers.find(slotKey);
        if (existing != liveDockUsers.end()) {
            const WorldObjectInstance* existingObject =
                findObjectById(state, existing->second);
            if (padAircraft
                || (existingObject != nullptr
                    && findRuntimePadAircraftUnitType(
                        state.rules,
                        *existingObject
                    ) != nullptr)) {
                errorMessage =
                    "aircraft pad is occupied by more than one live object";
                return false;
            }
        } else {
            liveDockUsers.emplace(slotKey, object.instanceId);
            occupiedSlots.insert(slotKey);
        }
        if (!padAircraft) {
            continue;
        }

        const WorldObjectInstance* host = findObjectById(
            state,
            object.dockedToObjectId
        );
        const BuildingType* building = host == nullptr
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        if (host == nullptr
            || building == nullptr
            || object.dockIndex >= building->numberOfDocks
            || !runtimeProductionOwnersMatch(
                state,
                object.owner,
                host->owner
            )
            || !runtimeAircraftDockAssignmentCompatible(
                state,
                object,
                *host
            )) {
            errorMessage =
                "pad aircraft references an invalid or incompatible host";
            return false;
        }
        if (state.repairDepotsInitialized
            && state.repairDepots.claimsDock(
                host->instanceId,
                object.dockIndex
            )) {
            errorMessage =
                "pad aircraft conflicts with repair dock authority";
            return false;
        }
    }

    std::vector<const WorldObjectInstance*> unboundPadAircraft;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive
            && object.dockedToObjectId == -1
            && object.dockIndex == -1
            && !runtimeObjectSuspendedByMagBeam(
                state,
                object.instanceId
            )
            && findRuntimePadAircraftUnitType(
                state.rules,
                object
            ) != nullptr) {
            unboundPadAircraft.push_back(&object);
        }
    }
    std::sort(
        unboundPadAircraft.begin(),
        unboundPadAircraft.end(),
        [](const WorldObjectInstance* left,
           const WorldObjectInstance* right) {
            return left != nullptr && right != nullptr
                ? left->instanceId < right->instanceId
                : right != nullptr;
        }
    );
    for (const WorldObjectInstance* aircraft : unboundPadAircraft) {
        if (aircraft == nullptr) {
            continue;
        }
        bool wouldBeAssigned = false;
        for (const WorldObjectInstance& host : state.world.getObjects()) {
            if (!runtimeAircraftDockAssignmentCompatible(
                    state,
                    *aircraft,
                    host
                )) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, host.type);
            const int dockCount = building == nullptr
                ? 0
                : std::max(0, building->numberOfDocks);
            for (int dockIndex = 0; dockIndex < dockCount; ++dockIndex) {
                if (occupiedSlots.insert({
                        host.instanceId,
                        dockIndex
                    }).second) {
                    wouldBeAssigned = true;
                    break;
                }
            }
            if (wouldBeAssigned) {
                break;
            }
        }
        if (wouldBeAssigned) {
            errorMessage =
                "unbound pad aircraft would be reassigned after load";
            return false;
        }
    }

    const RuntimeAircraftProductionMatchResult matched = [&] {
        if (!miningOwner) return runtimeAircraftProductionMatchForOwner(state, state.economy.owner);
        // The old mining facade has no production catalog or pending orders.
        auto input = buildRuntimeAircraftProductionMatchInputForOwner(
            state, *miningOwner, std::nullopt, nullptr, false);
        return input.error ? std::move(*input.error) : runtimeMatchAircraftProductionDocks(input.input);
    }();
    if (!matched.inputValid || !matched.feasible) {
        errorMessage = matched.reason.empty()
            ? "saved pad aircraft production is not feasible"
            : "pad aircraft production is not feasible: " + matched.reason;
        return false;
    }
    return true;
}

bool runtimeBuildItemNeedsExitClearance(
    const RuntimeGameSessionState& state, const RuntimeBuildCatalogItem& item) {
    WorldObjectInstance probe;
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.type = item.typeId;
    return !runtimeBuildItemUsesPadAircraft(state, item)
        && runtimeObjectMovementMode(state, probe) != PathMovementMode::Air;
}

std::optional<RuntimeCellPoint> runtimeProductionOutwardCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    const RuntimeCellPoint& cell) {
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, anchor);
    const int dx = cell.x >= anchor.x + std::max(1, footprint.width)
        ? 1 : cell.x < anchor.x ? -1 : 0;
    const int dy = cell.y >= anchor.y + std::max(1, footprint.height)
        ? 1 : cell.y < anchor.y ? -1 : 0;
    if (dx == 0 && dy == 0) {
        return std::nullopt;
    }
    return RuntimeCellPoint{cell.x + dx, cell.y + dy};
}

bool requestRuntimeFactoryExitClearance(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const std::vector<RuntimeCellPoint>& corridor,
    int producedObjectId,
    bool airOnly,
    int clearanceHostId) {
    const auto inCorridor = [&](const RuntimeCellPoint& cell) {
        // A one-cell slot is too small for an emerging Kirov. Keep a two-cell
        // apron around the air exit; ground factory traffic retains its lane.
        const int apron = airOnly ? 2 : 0;
        return std::any_of(corridor.begin(), corridor.end(), [&](const auto& point) {
            return std::abs(cell.x - point.x) <= apron && std::abs(cell.y - point.y) <= apron;
        });
    };
    const auto canYield = [&](const WorldObjectInstance& object) {
        if (object.instanceId == producedObjectId
            || !object.alive || object.health <= 0
            || !runtimeObjectCanMove(state, object)
            || !runtimeOwnersAreAllied(state, owner, object.owner)
            || (airOnly && runtimeObjectMovementMode(state, object) != PathMovementMode::Air)
            || object.factoryExitHostId >= 0
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || object.dockedToObjectId >= 0
            || object.garrisonHostObjectId >= 0) {
            return false;
        }
        if (const auto* type = findRuntimeUnitType(state.rules, object); type && type->missileSpawn)
            return false;
        for (const auto& relation : state.repairDepots.relations()) {
            if (relation.unitId == object.instanceId) {
                return false;
            }
        }
        for (const auto& lease : state.factoryDeliveries.leases()) {
            if (lease.producedObjectId == object.instanceId) {
                return false;
            }
        }
        return true;
    };
    std::vector<int> blockers;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.instanceId == producedObjectId) {
            continue;
        }
        const auto behavior = object.renderMoveActive
            ? state.behaviors.find(object.instanceId) : state.behaviors.end();
        const bool leaving = object.renderMoveActive
            && behavior != state.behaviors.end()
            && inCorridor(behavior->second.renderFrom);
        if ((inCorridor(runtimeCellPointForObject(object)) || leaving)
            && canYield(object)) {
            blockers.push_back(object.instanceId);
        }
    }
    if (blockers.empty()) {
        return false;
    }
    std::sort(blockers.begin(), blockers.end());

    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const auto* terrain = &state.terrainByCell;
    if (terrain->empty()) {
        generatedTerrain = buildRuntimeTerrainLookup(state.world);
        terrain = &generatedTerrain;
    }
    const auto occupied = buildRuntimeOccupancyLookup(state);
    // Air movement deliberately ignores ground occupancy. Give yielding
    // flyers their own occupied cells, including the visible move origin.
    std::unordered_map<int, int> airOccupied;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive || runtimeObjectMovementMode(state, object) != PathMovementMode::Air) continue;
        airOccupied.emplace(runtimeCellKey(object.x, object.y), object.instanceId);
        const auto moving = state.behaviors.find(object.instanceId);
        if (object.renderMoveActive && moving != state.behaviors.end())
            airOccupied.emplace(runtimeCellKey(moving->second.renderFrom.x, moving->second.renderFrom.y), object.instanceId);
    }
    std::unordered_set<int> reserved;
    for (const auto& cell : corridor) {
        reserved.insert(runtimeCellKey(cell.x, cell.y));
    }
    for (const auto& [id, behavior] : state.behaviors) {
        if (behavior.kind == RuntimeBehaviorKind::Move
            && behavior.state != RuntimeBehaviorState::Complete) {
            reserved.insert(runtimeCellKey(
                behavior.moveTarget.x, behavior.moveTarget.y
            ));
        }
    }

    // A full infantry cell has three occupants, so treating its representative
    // as a passable destination cannot propagate a yield request through it.
    // Follow the connected, stationary infantry crowd instead. Only the outer
    // soldier with a genuinely free next step receives an ordinary Move.
    // The finite object set bounds this search without imposing a crowd radius.
    if (!airOnly) {
        std::map<int, std::vector<int>> waiting;
        for (const auto& object : state.world.getObjects()) {
            if (object.kind != WorldObjectKind::Infantry || !canYield(object)
                || runtimeObjectMovementMode(state, object) != PathMovementMode::Ground
                || object.renderMoveActive) continue;
            const auto move = state.behaviors.find(object.instanceId);
            if (move != state.behaviors.end() && move->second.kind == RuntimeBehaviorKind::Move
                && move->second.state != RuntimeBehaviorState::Complete
                && (move->second.pathRequestPending || !move->second.path.empty())) continue;
            waiting[runtimeCellKey(object.x, object.y)].push_back(object.instanceId);
        }
        for (auto& [key, ids] : waiting) std::sort(ids.begin(), ids.end());
        std::vector<int> frontier;
        std::unordered_set<int> seen;
        for (int id : blockers) {
            const auto* object = findObjectById(state, id);
            if (!object) continue;
            const auto cell = waiting.find(runtimeCellKey(object->x, object->y));
            if (cell != waiting.end()) for (int candidate : cell->second)
                if (seen.insert(candidate).second) frontier.push_back(candidate);
        }
        for (std::size_t next = 0; next < frontier.size(); ++next) {
            auto* object = findMutableObjectById(state, frontier[next]);
            const auto current = runtimeCellPointForObject(*object);
            auto neighbors = runtimeNeighborsOf(current);
            std::sort(neighbors.begin(), neighbors.end(), [&](const auto& a, const auto& b) {
                const int da = std::abs(a.x-current.x)+std::abs(a.y-current.y);
                const int db = std::abs(b.x-current.x)+std::abs(b.y-current.y);
                return da != db ? da < db : runtimeCellKey(a.x,a.y) < runtimeCellKey(b.x,b.y);
            });
            for (const auto& candidate : neighbors) {
                const int key = runtimeCellKey(candidate.x,candidate.y);
                if (reserved.contains(key) || occupied.contains(key)
                    || !runtimeCellTransitionIsPassable(state,*terrain,occupied,current,
                        candidate,*object,-1,nullptr,false)) continue;
                eraseRuntimeBehavior(state,object->instanceId);
                RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;
                move.state=RuntimeBehaviorState::Queued;move.moveTarget=candidate;
                move.path.push_back(candidate);state.behaviors[object->instanceId]=std::move(move);
                setRuntimeMissionIntent(state,*object,RuntimeMissionIntent::Moving,"factoryExitClearance");
                return true;
            }
            for (const auto& candidate : neighbors) {
                // Pressure crosses touching walkable cells, never a wall,
                // cliff, water boundary or diagonally sealed corner.
                if (std::abs(candidate.x-current.x)+std::abs(candidate.y-current.y)!=1) continue;
                const int key=runtimeCellKey(candidate.x,candidate.y);
                const auto adjacent=waiting.find(key);
                if (adjacent==waiting.end() || runtimeCellHasHardWholeCellBlocker(
                        state,candidate,object->instanceId,-1,{})) continue;
                const auto from=terrain->find(runtimeCellKey(current.x,current.y)),to=terrain->find(key);
                if (from==terrain->end() || to==terrain->end() || !from->second || !to->second) continue;
                const auto a=runtimeCachedTerrainInfoForCell(state,*from->second);
                const auto b=runtimeCachedTerrainInfoForCell(state,*to->second);
                const int height=std::abs(runtimeEffectiveTerrainLevel(*from->second)-runtimeEffectiveTerrainLevel(*to->second));
                if (!b.passable || !runtimeAuthoredLandTypeAllowsMover(state,b,*object)
                    || !runtimeMapQueryPassabilityAllows(state,candidate,*object,PathMovementMode::Ground)
                    || (a.water!=b.water && !(a.shore||b.shore||a.bridge||b.bridge))
                    || height>1 || (height>0 && !(a.ramp||b.ramp||a.bridge||b.bridge))) continue;
                for (int id:adjacent->second) if(seen.insert(id).second)frontier.push_back(id);
            }
        }
    }

    // A bounded chain lets the outside of a vehicle crowd move first. Stable object
    // IDs and cell ordering keep simultaneous factory requests deterministic.
    std::unordered_set<int> visited;
    const auto yield = [&](auto&& self, int objectId, int depth) -> bool {
        if (depth > 4 || visited.size() >= 32
            || !visited.insert(objectId).second) {
            return false;
        }
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr || !canYield(*object)) {
            return false;
        }
        const bool fastFactoryYield = !airOnly
            && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId);
        const auto previous = state.behaviors.find(objectId);
        if (object->renderMoveActive
            || (previous != state.behaviors.end()
                && previous->second.kind == RuntimeBehaviorKind::Move
                && !inCorridor(previous->second.moveTarget)
                && (previous->second.pathRequestPending
                    || !previous->second.path.empty()
                    // Long rally commands are accepted before the movement
                    // stage requests their path. A temporary empty route is
                    // still a live order and must survive factory pressure.
                    || (fastFactoryYield
                        && previous->second.state == RuntimeBehaviorState::Queued)))) {
            return true;
        }
        const RuntimeCellPoint current = runtimeCellPointForObject(*object);
        if (!inCorridor(current) && depth == 0) {
            return false;
        }
        if (runtimeObjectMovementMode(state, *object) == PathMovementMode::Air) {
            std::vector<RuntimeCellPoint> frontier{current};
            std::map<int, RuntimeCellPoint> parent;
            parent.emplace(runtimeCellKey(current.x,current.y),current);
            const auto hasParkingSpace = [&](const RuntimeCellPoint& candidate) {
                for (const auto& other : state.world.getObjects()) {
                    if (other.instanceId == objectId || !runtimeObjectOccupiesAirNavigation(other)) continue;
                    const auto close = [&](const RuntimeCellPoint& point) {
                        return std::abs(candidate.x - point.x) < 3 && std::abs(candidate.y - point.y) < 3;
                    };
                    if (close(runtimeCellPointForObject(other))) return false;
                    const auto moving = state.behaviors.find(other.instanceId);
                    if (moving != state.behaviors.end() && moving->second.kind == RuntimeBehaviorKind::Move
                        && moving->second.state != RuntimeBehaviorState::Complete
                        && close(moving->second.moveTarget)) return false;
                }
                return true;
            };
            for (std::size_t next=0; next<frontier.size() && next<256; ++next) {
                const auto from=frontier[next];
                for (const auto& candidate:runtimeNeighborsOf(from)) {
                    const int key=runtimeCellKey(candidate.x,candidate.y);
                    const auto occupant=airOccupied.find(key);
                    if (parent.contains(key) || (occupant!=airOccupied.end() && occupant->second!=objectId)
                        || !runtimeCellTransitionIsPassable(state,*terrain,occupied,from,candidate,*object,-1,nullptr,false)) continue;
                    parent.emplace(key,from);
                    if (inCorridor(candidate)) { frontier.push_back(candidate); continue; }
                    if (reserved.contains(key)) continue;
                    if (airOnly && !hasParkingSpace(candidate)) { frontier.push_back(candidate); continue; }
                    const auto ground=occupied.find(key);
                    const auto* below=ground==occupied.end()?nullptr:findObjectById(state,ground->second);
                    if (below && below->kind==WorldObjectKind::Structure) { frontier.push_back(candidate); continue; }
                    std::deque<RuntimeCellPoint> path;
                    for (auto step=candidate;step!=current;step=parent.at(runtimeCellKey(step.x,step.y))) path.push_front(step);
                    eraseRuntimeBehavior(state,objectId);
                    RuntimeObjectBehavior move;move.kind=RuntimeBehaviorKind::Move;move.state=RuntimeBehaviorState::Queued;
                    move.moveTarget=candidate;move.path=std::move(path);state.behaviors[objectId]=std::move(move);
                    setRuntimeMissionIntent(state,*object,RuntimeMissionIntent::Moving,"factoryAirExitClearance");
                    reserved.insert(key);return true;
                }
            }
            for (const auto& candidate:runtimeNeighborsOf(current)) {
                const auto occupant=airOccupied.find(runtimeCellKey(candidate.x,candidate.y));
                if (occupant!=airOccupied.end() && occupant->second!=objectId
                    && self(self,occupant->second,depth+1)) return true;
            }
            return false;
        }
        const auto preserveInterruptedMove = [&]() {
            const auto active = state.behaviors.find(objectId);
            if (!fastFactoryYield || active == state.behaviors.end()
                || active->second.kind != RuntimeBehaviorKind::Move
                || active->second.state == RuntimeBehaviorState::Complete
                || active->second.state == RuntimeBehaviorState::Idle
                || active->second.factoryExitClearanceHostId >= 0
                || inCorridor(active->second.moveTarget)
                || active->second.moveTarget == current) return true;
            const auto& interrupted = active->second;
            // Preserve every live ordinary destination, including a failed
            // route's turn cooldown. Contextual routes keep their own executor.
            if (interrupted.targetObjectId != -1 || interrupted.forceFire
                || interrupted.attackGround || interrupted.attackBridge
                || interrupted.pendingPathTowardRange
                || interrupted.pendingAllowedOccupiedObjectId >= 0
                || interrupted.pendingHasSharedGoal || interrupted.moveCooldownTicks != 0
                || state.playerCommands.following.contains(objectId)
                || state.playerCommands.travelling.contains(objectId)
                || state.playerCommands.waypoints.routes.contains(objectId)
                || state.pendingEngineerCaptures.contains(objectId)
                || state.pendingEngineerActions.contains(objectId)
                || state.pendingOccupyTargets.contains(objectId)
                || state.pendingTransportTargets.contains(objectId)
                || state.pendingTimedBombIntents.contains(objectId)
                || std::any_of(interrupted.pendingIgnoredObjectIds.begin(),
                    interrupted.pendingIgnoredObjectIds.end(),
                    [&](int id) { return id != objectId; })) return false;
            const std::string resumeOwner = runtimeCanonicalHumanHouseOwner(object->owner);
            const RuntimeCellPoint destination = interrupted.moveTarget;
            if (resumeOwner.empty() || resumeOwner.size() > 256U
                || destination.x < 0 || destination.y < 0
                || destination.x > runtimeMultiplayerGameplayMaximumCellCoordinate
                || destination.y > runtimeMultiplayerGameplayMaximumCellCoordinate) return false;
            RuntimeMultiplayerGameplayTargetOrderCommand resume;
            resume.order = RuntimeMultiplayerGameplayTargetOrderKind::Move;
            resume.objectIds = {objectId};
            resume.target = RuntimeMultiplayerGameplayCellTarget{{destination.x, destination.y}};
            const auto queued = state.playerCommands.queues.find(objectId);
            if (queued != state.playerCommands.queues.end()) {
                if (queued->second.owner != resumeOwner) return false;
                // Only the head can already represent the interrupted step.
                // A later repeat must remain behind the earlier queued orders.
                if (!queued->second.orders.empty() && queued->second.orders.front() == resume) return true;
                if (queued->second.orders.size() >= RuntimePlayerCommandState::maximumOrdersPerObject)
                    return false;
                queued->second.orders.push_front(std::move(resume));
                if (queued->second.factoryRallyOrderIndex >= 0) ++queued->second.factoryRallyOrderIndex;
            } else {
                RuntimePlayerQueuedOrders queue;
                queue.owner = resumeOwner;
                queue.orders.push_front(std::move(resume));
                state.playerCommands.queues.emplace(objectId, std::move(queue));
            }
            return true;
        };
        auto neighbors = runtimeNeighborsOf(current);
        const auto turnDistance = [&](const RuntimeCellPoint& candidate) {
            const int facing = runtimeFacingFromDelta(candidate.x - current.x, candidate.y - current.y);
            const int delta = std::abs(facing - object->facing) % 256;
            return std::min(delta, 256 - delta);
        };
        // Fast-build traffic keeps its native ROT. Prefer a short step in its
        // current direction, including propagated crowd pressure, rather than
        // making parked predecessors reverse before the next car can leave.
        std::sort(neighbors.begin(), neighbors.end(), [&](const auto& a, const auto& b) {
            const int da = std::abs(a.x - current.x) + std::abs(a.y - current.y);
            const int db = std::abs(b.x - current.x) + std::abs(b.y - current.y);
            if (da != db) return da < db;
            if (fastFactoryYield && object->facing >= 0) {
                const int ta = turnDistance(a), tb = turnDistance(b);
                if (ta != tb) return ta < tb;
            }
            return runtimeCellKey(a.x, a.y) < runtimeCellKey(b.x, b.y);
        });
        for (const RuntimeCellPoint& candidate : neighbors) {
            const int key = runtimeCellKey(candidate.x, candidate.y);
            if (reserved.contains(key) || occupied.contains(key)
                || !runtimeCellTransitionIsPassable(
                    state, *terrain, occupied, current, candidate,
                    *object, -1, nullptr, false
                )) {
                continue;
            }
            if (!preserveInterruptedMove()) return false;
            eraseRuntimeBehavior(state, objectId);
            RuntimeObjectBehavior move;
            move.kind = RuntimeBehaviorKind::Move;
            move.state = RuntimeBehaviorState::Queued;
            move.moveTarget = candidate;
            move.path.push_back(candidate);
            if (!airOnly && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId))
                move.factoryExitClearanceHostId = clearanceHostId;
            state.behaviors[objectId] = std::move(move);
            setRuntimeMissionIntent(
                state, *object, RuntimeMissionIntent::Moving, "factoryExitClearance"
            );
            reserved.insert(key);
            return true;
        }
        for (const RuntimeCellPoint& candidate : neighbors) {
            const int key = runtimeCellKey(candidate.x, candidate.y);
            const auto occupant = occupied.find(key);
            if (reserved.contains(key) || occupant == occupied.end()
                || !runtimeCellTransitionIsPassable(
                    state, *terrain, occupied, current, candidate,
                    *object, occupant->second, nullptr, false
                )) {
                continue;
            }
            if (self(self, occupant->second, depth + 1)) {
                return true;
            }
        }
        // A unit at the outer exit may have to reverse through the door
        // before it can turn aside. The corridor is forbidden as a parking
        // destination, but it is still valid ground to travel through.
        std::vector<RuntimeCellPoint> frontier{current};
        std::map<int, RuntimeCellPoint> parent;
        parent.emplace(runtimeCellKey(current.x, current.y), current);
        for (std::size_t next = 0; next < frontier.size() && next < 32; ++next) {
            const auto from = frontier[next];
            for (const auto& candidate : runtimeNeighborsOf(from)) {
                const int key = runtimeCellKey(candidate.x, candidate.y);
                if (parent.contains(key) || occupied.contains(key)
                    || !runtimeCellTransitionIsPassable(state, *terrain, occupied, from, candidate,
                        *object, -1, nullptr, false)) continue;
                parent.emplace(key, from);
                if (inCorridor(candidate)) { frontier.push_back(candidate); continue; }
                if (reserved.contains(key)) continue;
                std::deque<RuntimeCellPoint> path;
                for (auto step = candidate; step != current; step = parent.at(runtimeCellKey(step.x, step.y)))
                    path.push_front(step);
                if (!preserveInterruptedMove()) return false;
                eraseRuntimeBehavior(state, objectId);
                RuntimeObjectBehavior move; move.kind = RuntimeBehaviorKind::Move;
                move.state = RuntimeBehaviorState::Queued; move.moveTarget = candidate;
                move.path = std::move(path);
                if (!airOnly && runtimeFactoryFastClearanceSource(state, *object, clearanceHostId))
                    move.factoryExitClearanceHostId = clearanceHostId;
                state.behaviors[objectId] = std::move(move);
                setRuntimeMissionIntent(state, *object, RuntimeMissionIntent::Moving, "factoryExitClearanceReverse");
                reserved.insert(key);
                return true;
            }
        }
        return false;
    };
    for (int objectId : blockers) {
        (void)yield(yield, objectId, 0);
    }
    // Keep the completed order until both the physical and visual exit clear,
    // even if the crowd needs several ticks to make space.
    return true;
}

std::vector<RuntimeCellPoint> runtimeFactoryAirExitCorridor(const RuntimeGameSessionState& state,
    const WorldObjectInstance& factory) {
    const auto footprint=runtimeStructureFootprintForObject(state,factory);
    const int width=std::max(1,footprint.width),height=std::max(1,footprint.height);
    std::vector<RuntimeCellPoint> corridor;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)
        corridor.push_back({factory.x+x,factory.y+y});
    corridor.push_back({factory.x+width,factory.y+(height-1)/2});
    corridor.push_back({factory.x+width+1,factory.y+(height-1)/2});
    return corridor;
}

void requestRuntimeFactoryAirExitClearance(RuntimeProductionHostState& state,
    const WorldObjectInstance& produced,const WorldObjectInstance& factory) {
    if(!runtimeFactoryUsesAirExit(state,factory,produced))return;
    auto corridor=runtimeFactoryAirExitCorridor(state,factory);
    const auto move=state.behaviors.find(produced.instanceId);
    if(move!=state.behaviors.end() && move->second.kind==RuntimeBehaviorKind::Move)
        corridor.push_back(move->second.moveTarget);
    (void)requestRuntimeFactoryExitClearance(state,produced.owner,corridor,produced.instanceId,true);
}

bool requestRuntimeProductionDoorClearance(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const WorldObjectInstance& anchor,
    const std::string& owner) {
    WorldObjectInstance probe;
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.type = item.typeId;
    probe.owner = owner;
    probe.alive = true;
    const auto mode = runtimeObjectMovementMode(state, probe);
    if (runtimeBuildItemUsesPadAircraft(state, item)) {
        return false;
    }
    if (mode == PathMovementMode::Air) {
        return runtimeFactoryUsesAirExit(state,anchor,probe)
            && requestRuntimeFactoryExitClearance(state,owner,runtimeFactoryAirExitCorridor(state,anchor),-1,true);
    }
    const auto lane = runtimeProductionLaneForCatalogItem(state, item);
    if (lane && (*lane == RuntimeProductionLane::Vehicles
            || *lane == RuntimeProductionLane::Ships)
        && (state.factoryDeliveries.claimsDock(
                anchor.instanceId, *lane == RuntimeProductionLane::Ships ? 0 : -1
            )
            || state.repairDepots.claimsDock(anchor.instanceId, 0))) {
        return false;
    }
    const auto footprint = runtimeStructureFootprintForObject(state, anchor);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const auto* factory = findRuntimeBuildingTypeById(state.rules, anchor.type);
    const bool vehicleDoor = !runtimeBuildItemLooksNaval(state, item)
        && factory != nullptr && factory->weaponsFactory
        && toUpperAsciiLocal(factory->factory) == "UNITTYPE";
    const RuntimeCellPoint door = vehicleDoor
        ? RuntimeCellPoint{anchor.x + width, anchor.y + height / 2}
        : RuntimeCellPoint{anchor.x + width / 2, anchor.y + height};
    const auto outward = runtimeProductionOutwardCell(state, anchor, door);
    const auto* terrain = runtimeProductionTerrainCellAt(state, door);
    if (terrain == nullptr || !outward.has_value()) {
        return false;
    }
    const auto info = runtimeCachedTerrainInfoForCell(state, *terrain);
    if (!info.passable || !runtimeAuthoredLandTypeAllowsMover(state, info, probe)
        || (runtimeBuildItemLooksNaval(state, item)
            && !runtimeProductionCellIsOpenSurfaceWater(state, door))
        || (mode == PathMovementMode::Ground
            && info.water && !info.bridge && !info.shore)) {
        return false;
    }
    return requestRuntimeFactoryExitClearance(state, owner, {door, *outward},
        -1, false, vehicleDoor ? anchor.instanceId : -1);
}

bool requestRuntimeProductionDoorClearanceForOwner(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback,
    bool tryAllFactories) {
    const auto lane = runtimeProductionLaneForCatalogItem(state, item);
    for (const auto* anchor : findRuntimeFactoryAnchorCandidatesForOwner(
            state, item, owner, allowLegacyFallback)) {
        if (anchor == nullptr) continue;
        if (lane && (*lane == RuntimeProductionLane::Vehicles
                || *lane == RuntimeProductionLane::Ships)
            && (state.factoryDeliveries.claimsDock(
                    anchor->instanceId, *lane == RuntimeProductionLane::Ships ? 0 : -1
                ) || state.repairDepots.claimsDock(anchor->instanceId, 0))) {
            continue;
        }
        if (requestRuntimeProductionDoorClearance(state, item, *anchor, owner)) {
            return true;
        }
        if (!tryAllFactories) return false;
    }
    return false;
}

std::optional<RuntimeProductionSpawn> findRuntimeProductionSpawnForOwner(
    const RuntimeGameSessionState& state,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback) {
    if (runtimeBuildItemUsesPadAircraft(state, item)) {
        std::vector<const WorldObjectInstance*> padHosts;
        for (const WorldObjectInstance& candidate : state.world.getObjects()) {
            if (runtimeAircraftPadHostStructurallyEligibleForOwner(
                    state,
                    candidate,
                    owner
                )) {
                padHosts.push_back(&candidate);
            }
        }
        sortRuntimeFactoryAnchorCandidates(padHosts);
        for (const WorldObjectInstance* anchor : padHosts) {
            if (anchor == nullptr
                || !runtimeAircraftCanDockAtStructureForOwner(
                    state,
                    item,
                    *anchor,
                    owner
                )) {
                continue;
            }
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, anchor->type);
            const int dockCount = building == nullptr
                ? 0
                : std::max(0, building->numberOfDocks);
            const int dockIndex =
                runtimeFreeAircraftDockIndex(state, *anchor, dockCount);
            if (dockIndex >= 0) {
                return runtimeAircraftDockSpawn(state, *anchor, dockIndex);
            }
        }
        return std::nullopt;
    }

    const std::vector<const WorldObjectInstance*> anchors =
        findRuntimeFactoryAnchorCandidatesForOwner(
            state,
            item,
            owner,
            allowLegacyFallback
        );
    if (anchors.empty()) {
        return std::nullopt;
    }

    WorldObjectInstance probe;
    probe.instanceId = -1;
    probe.kind = runtimeKindForBuildItem(item.kind);
    probe.owner = owner;
    probe.type = item.typeId;
    probe.alive = true;
    const std::unordered_map<int, int> occupiedByCell =
        buildRuntimeOccupancyLookup(state, -1);
    const std::optional<RuntimeProductionLane> deliveryLane =
        runtimeProductionLaneForCatalogItem(state, item);

    const int searchRadii[] = {0, 3};
    for (int searchRadius : searchRadii) {
        for (const WorldObjectInstance* anchor : anchors) {
            if (anchor == nullptr) {
                continue;
            }
            if (deliveryLane.has_value()
                && (*deliveryLane == RuntimeProductionLane::Vehicles
                    || *deliveryLane == RuntimeProductionLane::Ships)) {
                const int claimIndex = *deliveryLane
                        == RuntimeProductionLane::Ships
                    ? 0
                    : -1;
                if (state.factoryDeliveries.claimsDock(
                        anchor->instanceId,
                        claimIndex
                    )
                    || (state.repairDepotsInitialized
                        && state.repairDepots.claimsDock(
                            anchor->instanceId,
                            0
                        ))) {
                    continue;
                }
            }

            const RuntimeCellPoint anchorCell = runtimeCellPointForObject(*anchor);
            const RuntimeStructureFootprint footprint =
                runtimeStructureFootprintForObject(state, *anchor);
            const int width = std::max(1, footprint.width);
            const int height = std::max(1, footprint.height);
            const int centerX = anchorCell.x + width / 2;
            const int centerY = anchorCell.y + height / 2;

            const BuildingType* factory =
                findRuntimeBuildingTypeById(state.rules, anchor->type);
            const bool useVehicleFactoryDoor =
                !runtimeBuildItemLooksNaval(state, item)
                && factory != nullptr
                && factory->weaponsFactory
                && toUpperAsciiLocal(factory->factory) == "UNITTYPE";
            if (runtimeFactoryUsesAirExit(state, *anchor, probe)) {
                const auto roofCell = runtimeFactoryAirExitSpawnCell(state, *anchor);
                if (!roofCell) continue;
                RuntimeProductionSpawn spawn;
                spawn.anchor = anchor;
                spawn.cell = *roofCell;
                return spawn;
            }
            std::vector<RuntimeCellPoint> candidates;
            if (useVehicleFactoryDoor) {
                // Stock GAWEAP/NAWEAP/YAWEAP use a 5x3 foundation and an
                // internal ExitCoord on its centerline.  The visible doorway
                // is the immediately adjacent cell on the right edge.
                candidates = {
                    {anchorCell.x + width, centerY},
                    {anchorCell.x + width + 1, centerY},
                    {anchorCell.x + width, centerY - 1},
                    {anchorCell.x + width, centerY + 1},
                    {centerX, anchorCell.y + height},
                    {centerX - 1, anchorCell.y + height},
                    {centerX + 1, anchorCell.y + height},
                    {anchorCell.x - 1, centerY},
                    {centerX, anchorCell.y - 1}
                };
            } else {
                candidates = {
                    {centerX, anchorCell.y + height},
                    {centerX - 1, anchorCell.y + height},
                    {centerX + 1, anchorCell.y + height},
                    {anchorCell.x + width, centerY},
                    {anchorCell.x - 1, centerY},
                    {centerX, anchorCell.y - 1},
                    {anchorCell.x + width, anchorCell.y + height},
                    {anchorCell.x - 1, anchorCell.y + height}
                };
            }

            for (const RuntimeCellPoint& candidate : candidates) {
                const bool navalDelivery = deliveryLane.has_value()
                    && *deliveryLane == RuntimeProductionLane::Ships;
                const std::optional<RuntimeCellPoint> passable =
                    navalDelivery
                    ? findRuntimeOpenSurfaceWaterCellNear(
                        state,
                        probe,
                        candidate,
                        searchRadius,
                        &occupiedByCell
                    )
                    : findRuntimePassableCellNear(
                        state,
                        probe,
                        candidate,
                        searchRadius,
                        -1,
                        nullptr,
                        nullptr,
                        &occupiedByCell
                    );
                if (passable.has_value()) {
                    RuntimeProductionSpawn spawn;
                    spawn.anchor = anchor;
                    spawn.cell = *passable;
                    return spawn;
                }
            }
        }
    }

    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeProductionExitMoveTarget(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& anchor,
    const WorldObjectInstance& produced,
    bool forceOpenSurfaceWater) {
    const RuntimeCellPoint anchorCell = runtimeCellPointForObject(anchor);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, anchor);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const RuntimeCellPoint producedCell = runtimeCellPointForObject(produced);
    const int rightEdgeX = anchorCell.x + width;
    const int bottomEdgeY = anchorCell.y + height;
    if (runtimeFactoryUsesAirExit(state, anchor, produced)
        && producedCell.x >= anchorCell.x && producedCell.x < rightEdgeX
        && producedCell.y >= anchorCell.y && producedCell.y < bottomEdgeY) {
        return findRuntimePassableCellNear(state, produced,
            {rightEdgeX + 1, anchorCell.y + (height - 1) / 2}, 3);
    }
    const int stepX = producedCell.x >= rightEdgeX
        ? 1
        : producedCell.x < anchorCell.x ? -1 : 0;
    const int stepY = producedCell.y >= bottomEdgeY
        ? 1
        : producedCell.y < anchorCell.y ? -1 : 0;
    if (stepX == 0 && stepY == 0) {
        return std::nullopt;
    }

    const RuntimeCellPoint desired{
        producedCell.x + stepX,
        producedCell.y + stepY
    };
    const std::unordered_map<int, int> occupiedByCell =
        buildRuntimeOccupancyLookup(state, produced.instanceId);
    const std::optional<RuntimeCellPoint> target =
        (forceOpenSurfaceWater
            || runtimeObjectMovementMode(state, produced)
                == PathMovementMode::Naval)
        ? findRuntimeOpenSurfaceWaterCellNear(
            state,
            produced,
            desired,
            6,
            &occupiedByCell,
            &producedCell
        )
        : findRuntimePassableCellNear(
            state,
            produced,
            desired,
            0,
            -1,
            nullptr,
            nullptr,
            &occupiedByCell
        );
    if (!target.has_value() || *target == producedCell) {
        return std::nullopt;
    }
    return target;
}

void queueRuntimeProductionExitMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const WorldObjectInstance& anchor) {
    const std::optional<RuntimeCellPoint> target =
        findRuntimeProductionExitMoveTarget(state, anchor, produced);
    if (!target.has_value()) {
        return;
    }

    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = *target;
    produced.mission = "Move";
    state.behaviors[produced.instanceId] = std::move(behavior);
    RuntimeObjectBehavior& queuedBehavior =
        state.behaviors[produced.instanceId];
    if (runtimeObjectMovementMode(state, produced) == PathMovementMode::Air) {
        // The air mover computes its traffic-only path on its next tick.
        // A ground-queue fallback can move an AI flyer's goal back over
        // its factory, leaving the roof occupied indefinitely.
        return;
    }
    submitRuntimeMovePathRequest(
        state,
        produced,
        queuedBehavior,
        runtimeCellPointForObject(produced),
        queuedBehavior.moveTarget,
        -1,
        nullptr,
        !runtimeCloneDeliveryOwnsDoorway(state, produced.instanceId)
    );
}

void queueRuntimeProductionExitMoveForOwner(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const RuntimeBuildCatalogItem& item,
    const std::string& owner,
    bool allowLegacyFallback) {
    const WorldObjectInstance* anchor = findRuntimeFactoryAnchorForOwner(
        state,
        item,
        owner,
        allowLegacyFallback
    );
    if (anchor == nullptr) {
        return;
    }
    queueRuntimeProductionExitMove(state, produced, *anchor);
}

bool runtimeCloneDeliveryOwnsDoorway(
    const RuntimeGameSessionState& state, int objectId) {
    const auto* clone = findObjectById(state, objectId);
    if (!clone || !clone->alive || clone->health <= 0
        || clone->kind != WorldObjectKind::Infantry) return false;
    const auto owner = runtimeRepairDepotCanonicalOwnerId(state, clone->owner);
    for (const auto& host : state.world.getObjects()) {
        if (host.specialCloneDeliveryObjectId != objectId
            || !host.alive || host.health <= 0 || host.rubble
            || host.kind != WorldObjectKind::Structure) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, host.type);
        if (building && building->cloning
            && runtimeRepairDepotCanonicalOwnerId(state, host.owner) == owner)
            return true;
    }
    return false;
}

bool runtimePendingCloneDeliveryEligible(
    const RuntimeGameSessionState& state, const RuntimeSpecialBuildingClone& request) {
    const auto* host = findObjectById(state, request.sourceObjectId);
    const auto* building = host ? findRuntimeBuildingTypeById(state.rules, host->type) : nullptr;
    const auto context = makeRuntimeSpecialBuildingContext(state);
    return host && building && building->cloning
        && host->kind == WorldObjectKind::Structure && host->alive
        && host->health > 0 && !host->rubble
        && state.rules.findUnitType("InfantryType", request.infantryTypeId)
        && !trimAsciiLocal(request.owner).empty()
        && (!context.ownerIsNeutral || !context.ownerIsNeutral(request.owner))
        && runtimeRepairDepotCanonicalOwnerId(state, host->owner)
            == runtimeRepairDepotCanonicalOwnerId(state, request.owner);
}

std::uint64_t runtimeClonePendingDeliveriesHash(const RuntimeGameSessionState& state) {
    std::uint64_t hash = 1469598103934665603ULL;
    runtimeStableHashMixString(hash, "ClonePendingDeliveries/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.pendingCloneDeliveries.size()));
    for (const auto& request : state.pendingCloneDeliveries) {
        runtimeStableHashMix(hash, request.sourceObjectId);
        runtimeStableHashMixString(hash, runtimeRepairDepotCanonicalOwnerId(state, request.owner));
        runtimeStableHashMixString(hash, toUpperAsciiLocal(trimAsciiLocal(request.infantryTypeId)));
    }
    return hash;
}

std::vector<int> advanceRuntimePendingCloneDeliveries(
    RuntimeProductionHostState& state, bool leasesAlreadyReconciled) {
    if (state.pendingCloneDeliveries.empty()) return {};
    if (!leasesAlreadyReconciled) (void)reconcileRuntimeCloneDeliveryLeases(state);
    auto requests = std::move(state.pendingCloneDeliveries);
    state.pendingCloneDeliveries.clear();
    std::vector<int> cloneIds;
    std::set<int> blockedHosts;
    for (auto& request : requests) {
        // Sale, destruction, or a change of owner cancels that vat's debt.
        if (!runtimePendingCloneDeliveryEligible(state, request)) continue;
        const auto* host = findObjectById(state, request.sourceObjectId);
        if (blockedHosts.contains(request.sourceObjectId)
            || host->specialCloneDeliveryObjectId >= 0
            || host->factoryExitAnimationEndTick > state.tick) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        const auto spawn = runtimeSpecialBuildingInfantryExitCell(
            state, *host, request.owner, request.infantryTypeId);
        if (!spawn) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        auto* clone = addRuntimeObjectInstanceForOwner(state, request.owner,
            RuntimeBuildKind::Infantry, request.infantryTypeId, *spawn,
            RuntimeProductionLane::Infantry);
        if (!clone) {
            blockedHosts.insert(request.sourceObjectId);
            state.pendingCloneDeliveries.push_back(std::move(request));
            continue;
        }
        applyRuntimeProducedObjectVeterancy(state, *clone);
        recordRuntimeMatchBuild(state, clone->owner);
        const int cloneId = clone->instanceId;
        cloneIds.push_back(cloneId);
        auto* liveClone = findMutableObjectById(state, cloneId);
        auto* liveHost = findMutableObjectById(state, request.sourceObjectId);
        if (liveClone && liveHost) {
            liveHost->specialCloneDeliveryObjectId = cloneId;
            beginRuntimeFactoryExitAnimation(state, *liveHost, *liveClone);
            if (liveHost->factoryExitAnimationEndTick <= state.tick) {
                liveHost->factoryExitAnimationStartTick = state.tick;
                liveHost->factoryExitAnimationEndTick = state.tick
                    + std::max(1, state.config.fixedTickHz * 2);
                liveHost->factoryExitAirborne = false;
            }
            queueRuntimeProductionExitMove(state, *liveClone, *liveHost);
            const auto* building = findRuntimeBuildingTypeById(state.rules, liveHost->type);
            if (building && !trimAsciiLocal(building->createUnitSound).empty())
                emitRuntimeSound(state, "sound:" + trimAsciiLocal(building->createUnitSound));
        }
        blockedHosts.insert(request.sourceObjectId);
    }
    return cloneIds;
}

std::vector<int> cloneRuntimeProducedInfantry(
    RuntimeProductionHostState& state, const std::string& owner,
    const std::string& infantryTypeId, int producingFactoryObjectId) {
    const auto requests = RuntimeSpecialBuildingSystem::clonesForProducedInfantry(
        makeRuntimeSpecialBuildingContext(state), owner, infantryTypeId,
        producingFactoryObjectId, state.tick);
    state.pendingCloneDeliveries.insert(state.pendingCloneDeliveries.end(),
        requests.begin(), requests.end());
    return advanceRuntimePendingCloneDeliveries(state);
}

bool beginRuntimeFactoryExitAnimation(
    RuntimeGameSessionState& state,
    WorldObjectInstance& factory,
    const WorldObjectInstance& produced) {
    if (!factory.alive || factory.kind != WorldObjectKind::Structure) {
        return false;
    }
    const bool airborne =
        runtimeObjectMovementMode(state, produced) == PathMovementMode::Air;
    const ArtType* art = runtimeArtTypeForObject(state, factory);
    if (art == nullptr) {
        return false;
    }
    if (!airborne && runtimeFactoryDeliveryHostDomain(state, factory)
            == RuntimeFactoryDeliveryDomain::Naval) {
        const int duration = runtimeShipyardProductionDuration(*art, state.art, state.config.fixedTickHz);
        if (duration <= 0) return false;
        // Trigger only after a ship was actually created at this host. Queue
        // progress, holds and failed deliveries never enter this branch.
        factory.factoryExitAnimationStartTick = state.tick;
        factory.factoryExitAnimationEndTick = state.tick + duration;
        factory.factoryExitAirborne = false;
        return true;
    }
    const auto matchesLayer = [airborne](
        const ArtType::ActiveAnimation& animation
    ) {
        const std::string key = toLowerAsciiLocal(animation.key);
        return airborne
            ? key == "roofdeployinganim"
                || key == "underroofdooranim"
            : key == "deployinganim"
                || key == "underdooranim"
                || key == "dooranim";
    };
    if (!std::any_of(
            art->auxiliaryAnimations.begin(),
            art->auxiliaryAnimations.end(),
            matchesLayer
        )) {
        return false;
    }
    const int durationTicks = airborne
        ? std::max(100, state.config.fixedTickHz * 4)
        : std::max(24, state.config.fixedTickHz * 2);
    const bool alreadyOpen = airborne && factory.factoryExitAirborne
        && factory.factoryExitAnimationStartTick >= 0
        && factory.factoryExitAnimationEndTick > state.tick;
    if (!alreadyOpen) factory.factoryExitAnimationStartTick = state.tick;
    factory.factoryExitAnimationEndTick = alreadyOpen
        ? std::max(factory.factoryExitAnimationEndTick, state.tick + durationTicks)
        : state.tick + durationTicks;
    factory.factoryExitAirborne = airborne;
    if (airborne) {
        if (auto* liveProduced = findMutableObjectById(state, produced.instanceId)) {
            // Stock war factories face along their long foundation axis.
            // Set this only on first delivery; repeated roof notifications and
            // restored flights must not reset a heading already in motion.
            if (liveProduced->factoryExitHostId != factory.instanceId) {
                liveProduced->facing = runtimeFacingFromDelta(1, 0);
                if (liveProduced->turretFacing >= 0) liveProduced->turretFacing = liveProduced->facing;
            }
            liveProduced->factoryExitHostId = factory.instanceId;
        }
    }
    return true;
}

void queueRuntimeFactoryDeliveryExitMove(
    RuntimeProductionHostState& state,
    WorldObjectInstance& produced,
    const RuntimeFactoryDeliveryLease& lease) {
    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = {lease.rallyCell.x, lease.rallyCell.y};
    if (lease.domain == RuntimeFactoryDeliveryDomain::Land
        && lease.lane == RuntimeProductionLane::Vehicles
        && runtimeFactoryLandVehicleEligible(state, produced)) {
        // A fresh car enters the dedicated exit track facing out of its door.
        produced.facing = runtimeFacingFromDelta(
            lease.rallyCell.x - produced.x, lease.rallyCell.y - produced.y);
        produced.bodyFacingClock = {};
        if (produced.turretFacing >= 0) produced.turretFacing = produced.facing;
        produced.turretFacingClock = {};
    }
    produced.mission = "Move";
    state.behaviors[produced.instanceId] = std::move(behavior);
    RuntimeObjectBehavior& queuedBehavior =
        state.behaviors[produced.instanceId];
    if (runtimeObjectMovementMode(state, produced) == PathMovementMode::Air) {
        return; // The air mover owns the path from inside the factory roof.
    }
    submitRuntimeMovePathRequest(
        state,
        produced,
        queuedBehavior,
        runtimeCellPointForObject(produced),
        queuedBehavior.moveTarget,
        -1,
        nullptr,
        false
    );
}

bool completeRuntimeProductionOrder(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    bool allowLegacyFactoryFallback,
    std::optional<RuntimeProductionLane> completedLane,
    std::uint64_t completedOrderSerial) {
    if (item.kind == RuntimeBuildKind::Building
        || item.kind == RuntimeBuildKind::Defense) {
        return false;
    }

    const bool padAircraft = runtimeBuildItemUsesPadAircraft(state, item);
    if (!padAircraft && requestRuntimeProductionDoorClearanceForOwner(
            state, item, state.economy.owner, allowLegacyFactoryFallback)) {
        state.economy.status = "clearing production exit for " + item.typeId;
        return false;
    }
    if (padAircraft && (!completedLane.has_value()
            || completedOrderSerial == 0)) {
        refreshRuntimeAircraftDockAssignments(state);
    }
    std::optional<RuntimeProductionSpawn> spawn;
    if (padAircraft && completedLane.has_value()
        && completedOrderSerial != 0) {
        const std::optional<RuntimeAircraftProductionAssignment> assignment =
            runtimeAircraftProductionAssignmentForSerial(
                state,
                state.economy.owner,
                completedOrderSerial
            );
        const WorldObjectInstance* host = assignment.has_value()
            ? findObjectById(state, assignment->hostId)
            : nullptr;
        const BuildingType* building = host == nullptr
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        if (assignment.has_value()
            && assignment->phase
                == RuntimeAircraftProductionOrderPhase::Active
            && runtimeProductionItemKeysMatch(
                assignment->typeId,
                item.typeId
            )
            && host != nullptr
            && building != nullptr
            && (!building->powered || host->poweredOperational)
            && assignment->dockIndex >= 0
            && assignment->dockIndex < building->numberOfDocks
            && runtimeAircraftCanDockAtStructureForOwner(
                state,
                item,
                *host,
                state.economy.owner
            )
            && (!state.repairDepotsInitialized
                || !state.repairDepots.claimsDock(
                    host->instanceId,
                    assignment->dockIndex
                ))) {
            spawn = runtimeAircraftDockSpawn(
                state,
                *host,
                assignment->dockIndex
            );
        }
    } else if (padAircraft) {
        // Legacy direct-production probes have no serial to bind. Keep their
        // previous first-free behavior; authoritative lane completion never
        // takes this branch.
        spawn = findRuntimeProductionSpawnForOwner(
            state,
            item,
            state.economy.owner,
            allowLegacyFactoryFallback
        );
    } else {
        spawn = findRuntimeProductionSpawnForOwner(
            state,
            item,
            state.economy.owner,
            allowLegacyFactoryFallback
        );
    }
    if (!spawn.has_value()) {
        const bool clearing = !padAircraft && requestRuntimeProductionDoorClearanceForOwner(
            state, item, state.economy.owner, allowLegacyFactoryFallback, true
        );
        state.economy.status = (clearing ? "clearing production exit for " : "no production cell for ")
            + item.typeId;
        return false;
    }
    const int spawnAnchorId =
        spawn->anchor != nullptr ? spawn->anchor->instanceId : -1;

    if (spawn->anchor != nullptr && !padAircraft && !runtimeBuildItemNeedsExitClearance(state, item)
        && requestRuntimeProductionDoorClearance(state,item,*spawn->anchor,state.economy.owner)) {
        state.economy.status = "clearing production air exit for " + item.typeId;
        return false;
    }
    if (spawn->anchor != nullptr && runtimeBuildItemNeedsExitClearance(state, item)) {
        const auto outward = runtimeProductionOutwardCell(
            state, *spawn->anchor, spawn->cell
        );
        if (outward && requestRuntimeFactoryExitClearance(
                state, state.economy.owner, {spawn->cell, *outward},
                -1, false, spawn->anchor->instanceId)) {
            state.economy.status = "clearing production exit for " + item.typeId;
            return false;
        }
    }

    std::optional<RuntimeFactoryDeliveryLease> deliveryLease;
    if (completedLane.has_value()
        && completedOrderSerial != 0
        && !padAircraft
        && (*completedLane == RuntimeProductionLane::Vehicles
            || *completedLane == RuntimeProductionLane::Ships)) {
        if (spawn->anchor == nullptr) {
            state.economy.status =
                "production host missing for " + item.typeId;
            return false;
        }
        WorldObjectInstance producedProbe;
        producedProbe.instanceId = nextRuntimeObjectInstanceId(state);
        producedProbe.kind = runtimeKindForBuildItem(item.kind);
        producedProbe.owner = state.economy.owner;
        producedProbe.type = item.typeId;
        producedProbe.x = spawn->cell.x;
        producedProbe.y = spawn->cell.y;
        producedProbe.cell = runtimeCellKey(
            producedProbe.x,
            producedProbe.y
        );
        producedProbe.level = terrainLevelAtRuntimeCell(
            state,
            producedProbe.x,
            producedProbe.y
        );
        producedProbe.onBridge = runtimeCellIsBridge(
            state,
            producedProbe.x,
            producedProbe.y
        );
        producedProbe.alive = true;
        const std::optional<RuntimeCellPoint> rally =
            findRuntimeProductionExitMoveTarget(
                state,
                *spawn->anchor,
                producedProbe,
                *completedLane == RuntimeProductionLane::Ships
            );
        if (!rally.has_value()) {
            state.economy.status =
                "production egress blocked for " + item.typeId;
            return false;
        }

        RuntimeFactoryDeliveryAcquireRequest request;
        request.lease.canonicalOwner =
            runtimeRepairDepotCanonicalOwnerId(
                state,
                state.economy.owner
            );
        request.lease.hostId = spawnAnchorId;
        request.lease.dockIndex = *completedLane
                == RuntimeProductionLane::Ships
            ? 0
            : -1;
        request.lease.lane = *completedLane;
        request.lease.orderSerial = completedOrderSerial;
        request.lease.producedObjectId = producedProbe.instanceId;
        request.lease.acquiredTick = std::max(0, state.tick);
        request.lease.rallyCell = {
            rally->x,
            rally->y,
            runtimeCellIsBridge(state, rally->x, rally->y)
        };
        request.lease.domain = *completedLane
                == RuntimeProductionLane::Ships
            ? RuntimeFactoryDeliveryDomain::Naval
            : RuntimeFactoryDeliveryDomain::Land;
        request.repairDockClaimed = state.repairDepotsInitialized
            && state.repairDepots.claimsDock(
                request.lease.hostId,
                0
            );
        const RuntimeFactoryDeliveryAcquireResult acquired =
            state.factoryDeliveries.acquire(request);
        if (!acquired.acquired) {
            state.economy.status =
                "production delivery blocked: " + acquired.reason;
            return false;
        }
        deliveryLease = acquired.lease;
    }

    WorldObjectInstance* unit =
        addRuntimeObjectInstanceForOwner(
            state,
            state.economy.owner,
            item.kind,
            item.typeId,
            spawn->cell,
            completedLane
        );
    if (unit == nullptr) {
        if (deliveryLease.has_value()) {
            (void)state.factoryDeliveries.releaseByOrder(
                deliveryLease->canonicalOwner,
                deliveryLease->lane,
                deliveryLease->orderSerial
            );
        }
        state.economy.status = "production failed";
        return false;
    }
    if (deliveryLease.has_value()
        && unit->instanceId != deliveryLease->producedObjectId) {
        const int unexpectedObjectId = unit->instanceId;
        (void)state.factoryDeliveries.releaseByOrder(
            deliveryLease->canonicalOwner,
            deliveryLease->lane,
            deliveryLease->orderSerial
        );
        std::vector<WorldObjectInstance>& objects =
            state.world.accessObjects();
        if (!objects.empty()
            && objects.back().instanceId == unexpectedObjectId) {
            objects.pop_back();
            state.cloakObservedHealthByObject.erase(unexpectedObjectId);
            invalidateRuntimeObjectProjectionCache(state);
        }
        state.economy.status = "production delivery id changed";
        return false;
    }
    applyRuntimeProducedObjectVeterancy(state, *unit);
    recordRuntimeCompletedBuild(state, unit->owner, item);
    // Airport-bound aircraft are born on their assigned pad.  They must not
    if (spawnAnchorId >= 0) {
        if (WorldObjectInstance* factory =
                findMutableObjectById(state, spawnAnchorId)) {
            beginRuntimeFactoryExitAnimation(state, *factory, *unit);
        }
    }
    // inherit the ordinary factory-exit Move behavior: changing only the
    // visible mission to Dock would leave that queued behavior alive and the
    // next simulation tick would fly the aircraft away from the airfield.
    if (!spawn->aircraftDock) {
        if (deliveryLease.has_value()) {
            queueRuntimeFactoryDeliveryExitMove(
                state,
                *unit,
                *deliveryLease
            );
        } else if (spawnAnchorId >= 0) {
            const WorldObjectInstance* anchor =
                findObjectById(state, spawnAnchorId);
            if (anchor != nullptr) {
                queueRuntimeProductionExitMove(state, *unit, *anchor);
            }
        } else {
            queueRuntimeProductionExitMoveForOwner(
                state,
                *unit,
                item,
                state.economy.owner,
                allowLegacyFactoryFallback
            );
        }
    }

    if (spawnAnchorId >= 0) queueRuntimePlayerFactoryRally(state, spawnAnchorId, unit->instanceId);

    if (item.kind == RuntimeBuildKind::Infantry) {
        ++state.economy.completedInfantry;
    } else {
        ++state.economy.completedUnits;
    }
    state.economy.status =
        "produced " + unit->type + " #"
        + std::to_string(unit->instanceId);
    std::string createSound;
    if (spawn->aircraftDock && spawnAnchorId >= 0) {
        state.behaviors.erase(unit->instanceId);
        const WorldObjectInstance* anchor =
            findObjectById(state, spawnAnchorId);
        if (anchor != nullptr) {
            applyRuntimeAircraftDockPose(
                state,
                *unit,
                *anchor,
                spawn->dockIndex
            );
        }
    } else if (spawnAnchorId >= 0) {
        if (const WorldObjectInstance* anchor =
                findObjectById(state, spawnAnchorId)) {
            if (const BuildingType* factory =
                    findRuntimeBuildingTypeById(state.rules, anchor->type)) {
                createSound = factory->createUnitSound;
            }
        }
    }
    const UnitType* producedType =
        findRuntimeUnitTypeById(state.rules, unit->type);
    if (createSound.empty() && producedType != nullptr) {
        createSound = producedType->createSound;
    }
    if (!trimAsciiLocal(createSound).empty()) {
        emitRuntimeSound(state, "sound:" + trimAsciiLocal(createSound), runtimeSpatialAudioPositionForObject(*unit));
    }
    emitRuntimeSound(state, runtimeSoundIntentUnitReady);
    // Producing an ordinary mobile object cannot change any structure power,
    // refinery, storage, radar, or dock aggregate. Rebuilding those aggregates
    // here scanned the whole world several times at the exact completion tick.
    // Harvesters are the sole mobile economy aggregate and can be maintained
    // incrementally because this path always creates one live local object.
    if (producedType != nullptr && producedType->harvester) {
        ++state.economy.harvesters;
    }
    if (item.kind == RuntimeBuildKind::Infantry) {
        const std::vector<int> cloneIds = cloneRuntimeProducedInfantry(
            state,
            state.economy.owner,
            item.typeId,
            spawnAnchorId
        );
        if (!cloneIds.empty()) {
            state.economy.status +=
                " cloned x" + std::to_string(cloneIds.size());
        }
    }
    return true;
}

RuntimeFactoryDeliveryReconcileInput runtimeFactoryDeliveryReconcileInput(
    const RuntimeGameSessionState& state) {
    RuntimeFactoryDeliveryReconcileInput input;
    std::set<int> hostIds;
    std::set<int> objectIds;
    for (const RuntimeFactoryDeliveryLease& lease :
            state.factoryDeliveries.leases()) {
        if (hostIds.insert(lease.hostId).second) {
            const auto host = std::find_if(
                state.world.getObjects().begin(),
                state.world.getObjects().end(),
                [&lease](const WorldObjectInstance& object) {
                    return object.instanceId == lease.hostId;
                }
            );
            if (host != state.world.getObjects().end() && host->alive) {
                const BuildingType* building =
                    findRuntimeBuildingTypeById(state.rules, host->type);
                const bool validFactory = building != nullptr
                    && runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
                        state,
                        *host,
                        lease.canonicalOwner,
                        lease.domain
                    );
                if (validFactory) {
                    const std::optional<RuntimeFactoryDeliveryDomain>
                        hostDomain = runtimeFactoryDeliveryHostDomain(
                            state,
                            *host
                        );
                    if (!hostDomain.has_value()) {
                        continue;
                    }
                    input.hosts.push_back({
                        runtimeRepairDepotCanonicalOwnerId(
                            state,
                            host->owner
                        ),
                        host->instanceId,
                        *hostDomain,
                        true,
                        false
                    });
                }
            }
        }
        if (objectIds.insert(lease.producedObjectId).second) {
            const auto object = std::find_if(
                state.world.getObjects().begin(),
                state.world.getObjects().end(),
                [&lease](const WorldObjectInstance& candidate) {
                    return candidate.instanceId
                        == lease.producedObjectId;
                }
            );
            const UnitType* unit = object == state.world.getObjects().end()
                ? nullptr
                : findRuntimeUnitTypeById(state.rules, object->type);
            const bool navalUnit = unit != nullptr
                && (unit->naval || unit->waterBound);
            const bool validUnit = object != state.world.getObjects().end()
                && object->alive
                && object->kind == WorldObjectKind::Unit
                && (lease.lane == RuntimeProductionLane::Ships
                    ? lease.domain == RuntimeFactoryDeliveryDomain::Naval
                        && navalUnit
                    : lease.lane == RuntimeProductionLane::Vehicles
                        && lease.domain == RuntimeFactoryDeliveryDomain::Land
                        && !navalUnit);
            if (validUnit) {
                input.objects.push_back({
                    runtimeRepairDepotCanonicalOwnerId(
                        state,
                        object->owner
                    ),
                    object->instanceId,
                    true
                });
            }
        }
    }
    return input;
}

bool advanceRuntimeFreeUnitDeliveries(RuntimeProductionHostState& state) {
    if (state.factoryDeliveries.pendingFreeUnits().empty()) return false;
    // Attempts are bounded and stable across peers. The entitlement survives
    // a blocked exit and save/load, and is consumed only after creation.
    if (state.tick % std::max(1, state.config.fixedTickHz / 4) != 0) return false;
    bool changed = false;
    const auto grants = state.factoryDeliveries.pendingFreeUnits();
    for (const auto& grant : grants) {
        const auto* host = findObjectById(state, grant.hostId);
        const auto* unitType = findRuntimeUnitTypeById(state.rules, grant.unitType);
        if (host == nullptr || !host->alive || host->kind != WorldObjectKind::Structure
            || host->owner != grant.owner || host->type != grant.buildingType
            || unitType == nullptr) {
            state.factoryDeliveries.completeFreeUnit(grant.hostId);
            changed = true;
            continue;
        }
        const WorldObjectInstance anchor = *host;
        RuntimeBuildCatalogItem item;
        item.kind = runtimeBuildKindForUnitTypeId(state.rules, grant.unitType);
        item.typeId = grant.unitType;
        item.cost = 0;
        item.buildTicks = 1;
        item.buildLimit = unitType->buildLimit;
        const auto cell = findRuntimeSpawnCellNearObjectForOwner(state, item, anchor, grant.owner);
        if (!cell) continue;
        auto* unit = addRuntimeObjectInstanceForOwner(state, grant.owner, item.kind, item.typeId, *cell);
        if (unit == nullptr) continue;
        unit->mission = "Guard";
        applyRuntimeProducedObjectVeterancy(state, *unit);
        recordRuntimeCompletedBuild(state, grant.owner, item);
        for (auto& [ownerKey, ai] : state.computerAiByOwner) {
            if (ai.owner == grant.owner) {
                scaleRuntimeComputerAiObjectHealth(state, ai, *unit);
                ++ai.freeUnitsReceived;
                break;
            }
        }
        queueRuntimeProductionExitMove(state, *unit, anchor);
        state.factoryDeliveries.completeFreeUnit(grant.hostId);
        changed = true;
    }
    return changed;
}

bool advanceRuntimeFactoryDeliveries(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeFactoryDeliveries", state.tick);
    const std::uint64_t before = state.factoryDeliveries.stateHash();
    (void)advanceRuntimeFreeUnitDeliveries(state);
    const RuntimeFactoryDeliveryReconcileReport report =
        state.factoryDeliveries.reconcile(
            runtimeFactoryDeliveryReconcileInput(state)
        );
    if (!report.accepted) {
        state.simulationStatus =
            "factory delivery reconcile rejected: " + report.error;
        return false;
    }

    std::vector<RuntimeFactoryDeliveryLease> arrivedLeases;
    for (const RuntimeFactoryDeliveryLease& lease :
            state.factoryDeliveries.leases()) {
        const auto object = std::find_if(
            state.world.getObjects().begin(),
            state.world.getObjects().end(),
            [&lease](const WorldObjectInstance& candidate) {
                return candidate.instanceId == lease.producedObjectId;
            }
        );
        if (object != state.world.getObjects().end() && object->alive) {
            (void)requestRuntimeFactoryExitClearance(
                state, object->owner,
                {{lease.rallyCell.x, lease.rallyCell.y}},
                lease.producedObjectId,
                runtimeObjectMovementMode(state, *object) == PathMovementMode::Air,
                lease.domain == RuntimeFactoryDeliveryDomain::Land
                    ? lease.hostId : -1
            );
        }
        if (object != state.world.getObjects().end()
            && object->alive
            && object->x == lease.rallyCell.x
            && object->y == lease.rallyCell.y
            && object->onBridge == lease.rallyCell.onBridge
            && !object->renderMoveActive) {
            arrivedLeases.push_back(lease);
        }
    }
    for (const auto& lease : arrivedLeases) {
        (void)state.factoryDeliveries.releaseByObject(lease.producedObjectId);
        const auto* object = findObjectById(state, lease.producedObjectId);
        const auto queued = state.playerCommands.queues.find(lease.producedObjectId);
        if (object && lease.domain == RuntimeFactoryDeliveryDomain::Land
            && lease.lane == RuntimeProductionLane::Vehicles
            && runtimeFactoryFastClearanceSource(state, *object, lease.hostId)
            && queued != state.playerCommands.queues.end()
            && queued->second.owner == runtimeCanonicalHumanHouseOwner(object->owner)
            && !queued->second.orders.empty()) {
            const auto& first = queued->second.orders.front();
            if (first.order == RuntimeMultiplayerGameplayTargetOrderKind::Move
                && first.objectIds == std::vector<std::int32_t>{lease.producedObjectId}
                && std::holds_alternative<RuntimeMultiplayerGameplayCellTarget>(first.target)) {
                // The physical lease ends at the outside doorway. Clear that
                // slot with a short ordinary yield before dispatching the
                // queued rally, whose full route retains the original Speed.
                (void)requestRuntimeFactoryExitClearance(state, object->owner,
                    {{lease.rallyCell.x, lease.rallyCell.y}}, -1, false, lease.hostId);
            }
        }
    }
    return before != state.factoryDeliveries.stateHash();
}

bool runtimeFactoryDeliveryOwnsEgress(
    const RuntimeGameSessionState& state,
    int objectId) {
    return std::any_of(
        state.factoryDeliveries.leases().begin(),
        state.factoryDeliveries.leases().end(),
        [objectId](const RuntimeFactoryDeliveryLease& lease) {
            return lease.producedObjectId == objectId;
        }
    );
}

std::string runtimeHarvesterPhaseName(RuntimeHarvesterPhase phase) {
    switch (phase) {
    case RuntimeHarvesterPhase::SeekingOre:
        return "seeking_ore";
    case RuntimeHarvesterPhase::MovingToOre:
        return "moving_to_ore";
    case RuntimeHarvesterPhase::Loading:
        return "loading";
    case RuntimeHarvesterPhase::ReturningToRefinery:
        return "returning_to_refinery";
    case RuntimeHarvesterPhase::WaitingForRefineryDock:
        return "waiting_for_refinery_dock";
    case RuntimeHarvesterPhase::Docking:
        return "docking";
    case RuntimeHarvesterPhase::PreparingToUnload:
        return "preparing_to_unload";
    case RuntimeHarvesterPhase::Unloading:
        return "unloading";
    case RuntimeHarvesterPhase::Idle:
        return "idle";
    }
    return "idle";
}

RuntimeHarvesterPhase runtimeHarvesterPhaseFromName(const std::string& name) {
    const std::string lower = toLowerAsciiLocal(trimAsciiLocal(name));
    if (lower == "seeking_ore") {
        return RuntimeHarvesterPhase::SeekingOre;
    }
    if (lower == "moving_to_ore") {
        return RuntimeHarvesterPhase::MovingToOre;
    }
    if (lower == "loading") {
        return RuntimeHarvesterPhase::Loading;
    }
    if (lower == "returning_to_refinery") {
        return RuntimeHarvesterPhase::ReturningToRefinery;
    }
    if (lower == "waiting_for_refinery_dock"
        || lower == "waiting_refinery_dock") {
        return RuntimeHarvesterPhase::WaitingForRefineryDock;
    }
    if (lower == "docking") {
        return RuntimeHarvesterPhase::Docking;
    }
    if (lower == "preparing_to_unload"
        || lower == "preparing_unload") {
        return RuntimeHarvesterPhase::PreparingToUnload;
    }
    if (lower == "unloading") {
        return RuntimeHarvesterPhase::Unloading;
    }
    return RuntimeHarvesterPhase::Idle;
}

std::string serializeRuntimeHarvesterTask(const RuntimeHarvesterTask& task) {
    return runtimeHarvesterPhaseName(task.phase)
        + "|"
        + std::to_string(task.targetOre.x)
        + "|"
        + std::to_string(task.targetOre.y)
        + "|"
        + std::to_string(task.dockCell.x)
        + "|"
        + std::to_string(task.dockCell.y)
        + "|"
        + std::to_string(task.queueCell.x)
        + "|"
        + std::to_string(task.queueCell.y)
        + "|"
        + std::to_string(task.targetRefineryId)
        + "|"
        + std::to_string(task.cargoValue)
        + "|"
        + std::to_string(task.cargoUnits)
        + "|"
        + std::to_string(task.cargoGemUnits)
        + "|"
        + std::to_string(task.cargoCapacity)
        + "|"
        + std::to_string(task.workTicksRemaining)
        + "|"
        + runtimeBoolText(task.cargoHasGems)
        + "|"
        + runtimeSaveField(task.status)
        + "|" + std::to_string(task.nextThinkTick);
}

bool validateRuntimeStoredSlaveMinerState(
    const RuntimeGameSessionState& state, std::string* error) {
    const auto fail = [&](int id, const std::string& reason) {
        if (error != nullptr) *error = "invalid stored slave miner worker #"
            + std::to_string(id) + ": " + reason;
        return false;
    };
    const auto inside = [](const WorldObjectInstance& object) {
        return toLowerAsciiLocal(trimAsciiLocal(object.mission)) == "insideslaveminer";
    };
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        const auto* worker = findRuntimeObjectByIdIncludingInactive(state, workerId);
        const bool stored = task.phase == RuntimeSlaveMinerPhase::Stored;
        const bool hidden = worker != nullptr && !worker->alive
            && worker->health > 0 && inside(*worker);
        if (stored != hidden) return fail(workerId, "phase and containment disagree");
        // Other existing limbo/transport states retain their own validation.
        if (!stored) continue;
        const auto* parent = findRuntimeObjectByIdIncludingInactive(state, task.refineryId);
        const auto* workerRules = worker->kind == WorldObjectKind::Infantry
            ? findRuntimeUnitType(state.rules, *worker) : nullptr;
        const UnitType* mobile = parent != nullptr && parent->kind == WorldObjectKind::Unit
            ? findRuntimeUnitType(state.rules, *parent) : nullptr;
        const BuildingType* building = parent != nullptr && parent->kind == WorldObjectKind::Structure
            ? findRuntimeBuildingTypeById(state.rules, parent->type)
            : mobile != nullptr && mobile->resourceGatherer && mobile->resourceDestination
                && mobile->storage > 0 && mobile->slavesNumber > 0 && !mobile->deploysInto.empty()
                ? findRuntimeBuildingTypeById(state.rules, mobile->deploysInto) : nullptr;
        if (parent == nullptr || parent == worker || !parent->alive || parent->health <= 0
            || parent->owner != worker->owner || building == nullptr || !runtimeBuildingCanGatherResources(*building)
            || building->storage <= 0 || building->slavesNumber <= 0 || building->enslaves.empty()
            || workerRules == nullptr || !workerRules->slaved
            || toUpperAsciiLocal(trimAsciiLocal(building->enslaves))
                != toUpperAsciiLocal(trimAsciiLocal(worker->type))
            || (mobile != nullptr && !mobile->enslaves.empty()
                && toUpperAsciiLocal(trimAsciiLocal(mobile->enslaves))
                    != toUpperAsciiLocal(trimAsciiLocal(worker->type))))
            return fail(workerId, "invalid worker or parent authority");
        if (worker->x != parent->x || worker->y != parent->y || worker->cell != parent->cell
            || worker->level != parent->level || worker->onBridge != parent->onBridge
            || worker->subCell != -1 || worker->airborne
            || worker->hasRenderPosition || worker->renderMoveActive
            || worker->garrisonHostObjectId >= 0 || worker->tankBunkerHostId >= 0
            || worker->dockedToObjectId >= 0 || worker->dockIndex >= 0
            || !worker->transportPassengerIds.empty() || !worker->garrisonOccupantIds.empty()
            || task.cargoValue != 0 || task.cargoUnits != 0 || task.cargoGemUnits != 0
            || task.workTicksRemaining != 0 || state.behaviors.contains(workerId)
            || state.deathWeapons.hasTriggered(workerId) || state.deathWeapons.hasTriggered(parent->instanceId)
            || state.pendingObjectExitIds.contains(workerId) || state.pendingObjectExitIds.contains(parent->instanceId))
            return fail(workerId, "invalid stored pose, cargo, or active exit");
    }
    for (const auto& object : state.world.getObjects()) {
        if (!inside(object)) continue;
        const auto task = state.slaveMinerTasks.find(object.instanceId);
        if (task == state.slaveMinerTasks.end() || task->second.phase != RuntimeSlaveMinerPhase::Stored)
            return fail(object.instanceId, "containment has no stored task");
    }
    return true;
}

bool runtimeSlaveMinerSavedMoveObjectIsEligible(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    if (!state.rulesLoaded || !object.alive || object.health <= 0
        || object.airborne || runtimeGateObjectIsLimbo(object)) return false;
    if (runtimeObjectIsMobileSlaveMiner(state, object)) return true;
    if (object.kind != WorldObjectKind::Infantry) return false;
    const auto* worker = findRuntimeUnitType(state.rules, object);
    const auto task = state.slaveMinerTasks.find(object.instanceId);
    if (!worker || !worker->slaved || task == state.slaveMinerTasks.end()
        || task->second.phase == RuntimeSlaveMinerPhase::Stored) return false;
    const auto* parent = findObjectById(state, task->second.refineryId);
    if (!parent || parent == &object || !parent->alive || parent->health <= 0
        || runtimeGateObjectIsLimbo(*parent)
        || runtimeCanonicalHumanHouseOwner(parent->owner)
            != runtimeCanonicalHumanHouseOwner(object.owner)) return false;
    const auto* building = runtimeSlaveMinerParentBuilding(state, *parent);
    if (!building || building->slavesNumber <= 0
        || toUpperAsciiLocal(trimAsciiLocal(building->enslaves))
            != toUpperAsciiLocal(trimAsciiLocal(object.type))) return false;
    const auto* mobile = parent->kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, *parent) : nullptr;
    return !mobile || mobile->enslaves.empty()
        || toUpperAsciiLocal(trimAsciiLocal(mobile->enslaves))
            == toUpperAsciiLocal(trimAsciiLocal(object.type));
}

bool runtimeSlaveMinerSavedMoveCellIsValid(
    const RuntimeGameSessionState& state, const RuntimeCellPoint& cell) {
    // runtimeCellKey is a different indexing contract; bound before lookup.
    return cell.x >= 0 && cell.x < 512 && cell.y >= 0 && cell.y < 512
        && findRuntimeTerrainCellAt(state, cell) != nullptr;
}

bool runtimeSlaveMinerSavedMoveRouteIsValid(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object,
    const RuntimeObjectBehavior& behavior) {
    if (behavior.kind != RuntimeBehaviorKind::Move
        || behavior.pathRequestPending || behavior.path.empty()
        || behavior.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes
        || behavior.moveCooldownTicks < 0 || behavior.weaponCooldownTicks < 0
        || behavior.moveIntervalRemainderQ16 < -0x8000
        || behavior.moveIntervalRemainderQ16 > 0x7fff
        || behavior.blockedTicks < 0 || behavior.pathReplans < 0
        || behavior.nextRepathTick < 0
        || !runtimeSlaveMinerSavedMoveCellIsValid(state, behavior.moveTarget)
        || behavior.path.back() != behavior.moveTarget) return false;
    RuntimeCellPoint previous = runtimeCellPointForObject(object);
    if (!runtimeSlaveMinerSavedMoveCellIsValid(state, previous)) return false;
    if (behavior.moveSegmentFrom.x >= 0 || behavior.moveSegmentFrom.y >= 0) {
        const auto& from = behavior.moveSegmentFrom;
        if (behavior.moveCooldownTicks <= 0
            || !runtimeSlaveMinerSavedMoveCellIsValid(state, from)
            || from == previous || std::abs(from.x - previous.x) > 1
            || std::abs(from.y - previous.y) > 1) return false;
    } else if (behavior.moveSegmentFrom.x != -1 || behavior.moveSegmentFrom.y != -1) {
        return false;
    }
    if (behavior.renderMoveTicksRemaining > 0
        && (behavior.renderMoveTicksRemaining != behavior.moveCooldownTicks
            || behavior.renderMoveTicksTotal < behavior.renderMoveTicksRemaining
            || behavior.renderTo != previous
            || (behavior.moveSegmentFrom.x >= 0
                && behavior.renderFrom != behavior.moveSegmentFrom))) return false;
    for (const auto& cell : behavior.path) {
        if (!runtimeSlaveMinerSavedMoveCellIsValid(state, cell) || cell == previous
            || std::abs(cell.x - previous.x) > 1
            || std::abs(cell.y - previous.y) > 1) return false;
        previous = cell;
    }
    // Occupants can move after this route was planned. Normal movement retains
    // responsibility for its future occupancy, blocking and replanning.
    return true;
}

bool runtimeSlaveMinerSavedMoveMatchesOrdinary(
    const RuntimeObjectBehavior& restored, const RuntimeObjectBehavior& ordinary) {
    return restored.kind == ordinary.kind
        && restored.moveTarget == ordinary.moveTarget
        && restored.targetObjectId == ordinary.targetObjectId
        && restored.moveCooldownTicks == ordinary.moveCooldownTicks
        && restored.weaponCooldownTicks == ordinary.weaponCooldownTicks
        && restored.moveIntervalRemainderQ16 == ordinary.moveIntervalRemainderQ16
        && restored.pendingAllowedOccupiedObjectId == ordinary.pendingAllowedOccupiedObjectId
        && restored.attackBridge == ordinary.attackBridge
        && restored.attackBridgeCell == ordinary.attackBridgeCell
        && restored.forceFire == ordinary.forceFire
        && restored.attackGround == ordinary.attackGround
        && restored.moveSegmentFrom == ordinary.moveSegmentFrom
        && restored.factoryExitClearanceHostId == ordinary.factoryExitClearanceHostId;
}

void writeRuntimeSlaveMinerMovePaths(
    const RuntimeGameSessionState& state, std::ostream& output) {
    std::vector<int> ids;
    std::size_t nodes = 0;
    for (const auto& [id, behavior] : state.behaviors) {
        const auto* object = findObjectById(state, id);
        if (!object || !runtimeSlaveMinerSavedMoveObjectIsEligible(state, *object)
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.pathRequestPending || behavior.path.empty()
            || behavior.path.back() != behavior.moveTarget) continue;
        // An OmniCrusher commits its logical destination after the approach.
        // Its existing VehicleMovePresentation section owns that saved segment.
        if (runtimeOmniCrushPendingSegment(state, *object, behavior)) continue;
        if (!runtimeSlaveMinerSavedMoveRouteIsValid(state, *object, behavior)
            || behavior.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes - nodes
            || ids.size() >= 100000U) {
            output.setstate(std::ios::failbit);
            return;
        }
        nodes += behavior.path.size();
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    output << "\n[SlaveMinerMovePaths]\nSchema=1\nCount=" << ids.size() << '\n';
    for (std::size_t index = 0; index < ids.size(); ++index)
        output << index << '=' << runtimeMultiplayerSnapshotBehaviorRecord(
            ids[index], state.behaviors.at(ids[index])) << '\n';
}

bool restoreRuntimeSlaveMinerMovePaths(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    const auto* section = ini.getSection("SlaveMinerMovePaths");
    if (!section) return true;
    const auto fail = [&](int id, const char* reason) {
        error = "invalid slave miner move path"
            + (id > 0 ? " #" + std::to_string(id) : std::string{}) + ": " + reason;
        return false;
    };
    int schema = 0, count = 0;
    if (!parseRuntimeInt(ini.getValue("SlaveMinerMovePaths", "Schema", ""), schema)
        || schema != 1
        || !parseRuntimeInt(ini.getValue("SlaveMinerMovePaths", "Count", ""), count)
        || count < 0 || count > 100000
        || static_cast<std::size_t>(count) > state.behaviors.size()) return fail(-1, "header");
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *section) {
        (void)value;
        const auto normalized = toLowerAsciiLocal(trimAsciiLocal(key));
        if (!keys.insert(normalized).second) return fail(-1, "duplicate key");
        if (normalized == "schema" || normalized == "count") continue;
        int index = -1;
        if (!parseRuntimeInt(normalized, index) || index < 0 || index >= count
            || normalized != std::to_string(index)) return fail(-1, "record index");
    }
    if (keys.size() != static_cast<std::size_t>(count) + 2U) return fail(-1, "record count");
    std::unordered_set<int> seen;
    std::vector<std::pair<int, RuntimeObjectBehavior>> staged;
    staged.reserve(static_cast<std::size_t>(count));
    std::size_t nodes = 0;
    for (int index = 0; index < count; ++index) {
        const auto record = ini.getValue("SlaveMinerMovePaths", std::to_string(index), "");
        const auto fields = splitRuntimeSaveFields(record);
        int expectedId = -1, objectId = -1;
        if (fields.empty() || !parseRuntimeInt(fields[0], expectedId)
            || expectedId <= 0 || !seen.insert(expectedId).second) return fail(expectedId, "object ID");
        const auto ordinary = state.behaviors.find(expectedId);
        const auto* object = findObjectById(state, expectedId);
        if (!object || ordinary == state.behaviors.end()
            || ordinary->second.kind != RuntimeBehaviorKind::Move
            || !runtimeSlaveMinerSavedMoveObjectIsEligible(state, *object))
            return fail(expectedId, "worker/home/ordinary move authority");
        // The parser changes native authority only. Seed it with the already
        // validated walk/arrival presentation rather than clearing that state.
        RuntimeObjectBehavior restored = ordinary->second;
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(record, objectId, restored)
            || objectId != expectedId) return fail(expectedId, "native behavior record");
        if (!runtimeSlaveMinerSavedMoveMatchesOrdinary(restored, ordinary->second))
            return fail(expectedId, "ordinary authority differs");
        if (!runtimeSlaveMinerSavedMoveRouteIsValid(state, *object, restored))
            return fail(expectedId, "route/segment/goal geometry");
        if (restored.path.size() > runtimeSlaveMinerSavedMoveMaximumNodes - nodes)
            return fail(expectedId, "total node budget");
        nodes += restored.path.size();
        staged.emplace_back(expectedId, std::move(restored));
    }
    // Validate the complete optional section before altering any staged state.
    for (auto& [id, behavior] : staged) state.behaviors.at(id) = std::move(behavior);
    return true;
}

bool runtimeClonePendingDeliveryTextValid(const std::string& value) {
    return !value.empty() && value.size() <= 256U
        && std::none_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 0x20U || c == 0x7fU;
        });
}

bool runtimeClonePendingDeliveriesCandidateValid(
    const RuntimeGameSessionState& state,
    const std::vector<RuntimeSpecialBuildingClone>& candidate,
    std::string& error) {
    const auto fail = [&](const char* reason) {
        error = std::string("invalid ClonePendingDeliveries: ") + reason;
        return false;
    };
    if (candidate.size() > runtimeClonePendingDeliveriesMaximumCount)
        return fail("queue exceeds count limit");
    if (!candidate.empty() && !state.rulesLoaded)
        return fail("rules unavailable");
    std::unordered_map<int, std::string> hostOwners;
    for (const auto& host : state.world.getObjects()) {
        if (host.kind != WorldObjectKind::Structure || !host.alive
            || host.health <= 0 || host.rubble) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, host.type);
        if (!building || !building->cloning) continue;
        hostOwners.emplace(host.instanceId,
            runtimeRepairDepotCanonicalOwnerId(state, host.owner));
    }
    for (const auto& request : candidate) {
        if (request.sourceObjectId < 0
            || !runtimeClonePendingDeliveryTextValid(request.owner)
            || !runtimeClonePendingDeliveryTextValid(request.infantryTypeId)
            || !state.rules.findUnitType("InfantryType", request.infantryTypeId))
            return fail("host ID, owner or infantry type");
        const auto host = hostOwners.find(request.sourceObjectId);
        const auto owner = runtimeRepairDepotCanonicalOwnerId(state, request.owner);
        if (host == hostOwners.end() || owner.empty() || host->second != owner
            || runtimeOwnerIsNeutralForAutoTarget(request.owner))
            return fail("missing/dead/non-cloning host or owner mismatch");
    }
    return true;
}

std::string runtimeClonePendingDeliveriesRecord(
    const RuntimeSpecialBuildingClone& request) {
    return std::to_string(request.sourceObjectId) + '|'
        + iniHexEncode(request.owner) + '|'
        + iniHexEncode(request.infantryTypeId);
}

bool validateRuntimeClonePendingDeliveries(
    const RuntimeGameSessionState& state, std::string& error) {
    return runtimeClonePendingDeliveriesCandidateValid(
        state, state.pendingCloneDeliveries, error);
}

void writeRuntimeClonePendingDeliveries(
    const RuntimeGameSessionState& state, std::ostream& output) {
    std::string error;
    if (!validateRuntimeClonePendingDeliveries(state, error)) {
        output.setstate(std::ios::failbit);
        return;
    }
    output << "\n[ClonePendingDeliveries]\nSchema=1\nCount="
        << state.pendingCloneDeliveries.size() << '\n';
    for (std::size_t index = 0; index < state.pendingCloneDeliveries.size(); ++index)
        output << index << '='
            << runtimeClonePendingDeliveriesRecord(state.pendingCloneDeliveries[index])
            << '\n';
}

bool restoreRuntimeClonePendingDeliveries(
    RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    const auto fail = [&](const char* reason) {
        error = std::string("invalid ClonePendingDeliveries: ") + reason;
        return false;
    };
    const auto* section = ini.getSection("ClonePendingDeliveries");
    if (!section) {
        state.pendingCloneDeliveries.clear(); // Legacy ordinary saves.
        return true;
    }
    std::size_t matchingSections = 0;
    for (const auto& name : ini.getSectionNames())
        if (toLowerAsciiLocal(name) == "clonependingdeliveries") ++matchingSections;
    if (matchingSections != 1U) return fail("duplicate section names");
    int count = -1;
    if (ini.getValue("ClonePendingDeliveries", "Schema", "") != "1"
        || !parseRuntimeInt(ini.getValue("ClonePendingDeliveries", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) > runtimeClonePendingDeliveriesMaximumCount)
        return fail("schema or count");
    std::unordered_set<std::string> keys;
    for (const auto& [key, value] : *section) {
        (void)value;
        const auto normalized = toLowerAsciiLocal(trimAsciiLocal(key));
        if (!keys.insert(normalized).second) return fail("duplicate key");
        if (normalized == "schema" || normalized == "count") continue;
        int index = -1;
        if (!parseRuntimeInt(normalized, index) || index < 0 || index >= count
            || normalized != std::to_string(index)) return fail("record index");
    }
    if (keys.size() != static_cast<std::size_t>(count) + 2U)
        return fail("missing or extra records");
    std::vector<RuntimeSpecialBuildingClone> candidate;
    candidate.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue(
            "ClonePendingDeliveries", std::to_string(index), ""));
        RuntimeSpecialBuildingClone request;
        if (fields.size() != 3U
            || !parseRuntimeInt(fields[0], request.sourceObjectId)
            || request.sourceObjectId < 0
            || fields[0] != std::to_string(request.sourceObjectId)
            || !iniHexDecode(fields[1], request.owner, 256U)
            || !iniHexDecode(fields[2], request.infantryTypeId, 256U))
            return fail("host ID, token count or encoded text");
        candidate.push_back(std::move(request));
    }
    if (!runtimeClonePendingDeliveriesCandidateValid(state, candidate, error))
        return false;
    // No saved record can alter live authority until all records are valid.
    state.pendingCloneDeliveries = std::move(candidate);
    return true;
}

bool runtimeProductionOrderMatchesCatalogSnapshot(
    const RuntimeGameSessionState& state,
    RuntimeProductionLane lane,
    const RuntimeProductionLaneOrder& order,
    const RuntimeProductionCatalogAdapter& catalog) {
    if (catalog.item == nullptr
        || catalog.lane != lane
        || order.cost < 0
        || order.totalTicks <= 0) {
        return false;
    }
    RuntimeBuildCatalogItem pricedItem = *catalog.item;
    pricedItem.cost = order.cost;
    return std::max(1, runtimeBaseBuildTimeTicks(state, pricedItem))
        == order.totalTicks;
}

bool validateRuntimeProductionCatalogState(
    const RuntimeGameSessionState& state,
    const RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage) {
    const int maximumQueuedObjects = state.rules.getMaximumQueuedObjects();
    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        const RuntimeProductionLaneState& laneState =
            candidate.lanes.lanes[runtimeProductionLaneIndex(lane)];
        if (lane != RuntimeProductionLane::Aircraft
            && runtimeProductionLaneOccupancy(candidate.lanes, lane)
                > runtimeProductionLaneCapacity(
                    lane,
                    maximumQueuedObjects,
                    0
                )) {
            errorMessage = "saved production lane exceeds capacity";
            return false;
        }
        const auto validateOrder = [&](
            const RuntimeProductionLaneOrder& order
        ) {
            const std::optional<RuntimeProductionCatalogAdapter> catalog =
                runtimeProductionCatalogLookup(state, order.itemKey);
            return catalog.has_value()
                && runtimeProductionOrderMatchesCatalogSnapshot(
                    state,
                    lane,
                    order,
                    *catalog
                );
        };
        if (!std::all_of(
                laneState.queued.begin(),
                laneState.queued.end(),
                validateOrder
            )
            || (laneState.active.has_value()
                && !validateOrder(*laneState.active))
            || (laneState.ready.has_value()
                && !validateOrder(*laneState.ready))) {
            errorMessage = "saved production order does not match catalog";
            return false;
        }
    }

    if (candidate.selectedPlacementLane.has_value()) {
        const RuntimeProductionLane lane =
            *candidate.selectedPlacementLane;
        if ((lane != RuntimeProductionLane::Structures
                && lane != RuntimeProductionLane::Armory)
            || candidate.selectedPlacementSerial == 0) {
            errorMessage = "invalid selected production placement";
            return false;
        }
        const RuntimeProductionLaneState& laneState =
            candidate.lanes.lanes[runtimeProductionLaneIndex(lane)];
        if (!laneState.ready.has_value()
            || laneState.ready->serial
                != candidate.selectedPlacementSerial) {
            errorMessage = "selected production placement is not ready";
            return false;
        }
    } else if (candidate.selectedPlacementSerial != 0) {
        errorMessage = "selected placement serial has no lane";
        return false;
    }
    return true;
}

bool migrateRuntimeSchema1ProductionTiming(
    const RuntimeGameSessionState& state,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage) {
    const auto migrateOrder = [&state, &errorMessage](
        RuntimeProductionLane lane,
        RuntimeProductionLaneOrder& order,
        char role
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, order.itemKey);
        if (!catalog.has_value()
            || catalog->item == nullptr
            || catalog->lane != lane
            || runtimeRawBuildCostForItem(state, *catalog->item) != order.cost
            || order.totalTicks != runtimeLegacyBuildTimeTicks(
                order.cost,
                catalog->item->wall
            )) {
            errorMessage =
                "schema 1 production order does not match legacy catalog";
            return false;
        }

        RuntimeBuildCatalogItem pricedItem = *catalog->item;
        pricedItem.cost = order.cost;
        const int newTotalTicks = std::max(
            1,
            runtimeBaseBuildTimeTicks(state, pricedItem)
        );
        order.totalTicks = newTotalTicks;
        order.creditRemainderMicros = 0;
        order.rateDivisionRemainder = 0;
        order.rateDivisionDenominator = 0;
        if (role == 'Q') {
            order.remainingTicks = newTotalTicks;
            return true;
        }
        if (role == 'R') {
            order.remainingTicks = 0;
            return true;
        }

        if (order.cost > 0) {
            const std::uint64_t remainingCost = static_cast<std::uint64_t>(
                order.cost - order.creditsSpent
            );
            order.remainingTicks = static_cast<int>(
                (static_cast<std::uint64_t>(newTotalTicks) * remainingCost
                    + static_cast<std::uint64_t>(order.cost) - 1U)
                / static_cast<std::uint64_t>(order.cost)
            );
        } else {
            const std::uint64_t remainingProgress =
                runtimeProductionProgressMicros
                - order.zeroCostProgressMicros;
            order.remainingTicks = static_cast<int>(
                (static_cast<std::uint64_t>(newTotalTicks)
                    * remainingProgress
                    + runtimeProductionProgressMicros - 1U)
                / runtimeProductionProgressMicros
            );
            if (order.zeroCostProgressMicros > 0
                && order.zeroCostProgressMicros
                    < runtimeProductionProgressMicros) {
                order.rateDivisionDenominator = 1;
            }
        }
        return true;
    };

    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        RuntimeProductionLaneState& laneState =
            candidate.lanes.lanes[runtimeProductionLaneIndex(lane)];
        for (RuntimeProductionLaneOrder& order : laneState.queued) {
            if (!migrateOrder(lane, order, 'Q')) {
                return false;
            }
        }
        if (laneState.active.has_value()
            && !migrateOrder(lane, *laneState.active, 'A')) {
            return false;
        }
        if (laneState.ready.has_value()
            && !migrateOrder(lane, *laneState.ready, 'R')) {
            return false;
        }
    }
    return true;
}

bool parseRuntimeV15ProductionByOwner(
    const IniFile& ini,
    RuntimeSavedProductionByOwner& productionByOwner,
    std::string& errorMessage) {
    RuntimeSavedProductionByOwner parsed;
    const int count = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerHouses", "Count", "-1"),
        -1
    );
    if (count <= 0 || count > 8) {
        errorMessage = "invalid multiplayer human house count";
        return false;
    }
    for (int index = 0; index < count; ++index) {
        const std::string section =
            "MultiplayerHouse" + std::to_string(index);
        std::string owner;
        std::string serialized;
        if (!iniHexDecode(
                trimAsciiLocal(ini.getValue(section, "OwnerHex", "")),
                owner,
                256U
            )
            || trimAsciiLocal(owner).empty()
            || !runtimeHexDecode(
                trimAsciiLocal(
                    ini.getValue(section, "ProductionDataHex", "")
                ),
                serialized
            )) {
            errorMessage = "invalid multiplayer house production identity";
            return false;
        }
        RuntimeSavedProductionCandidate candidate;
        if (!runtimeDeserializeProductionLanes(
                serialized,
                candidate.lanes,
                &errorMessage
            )) {
            return false;
        }
        std::uint64_t stateHash = 0;
        if (!parseRuntimeUint64Strict(
                ini.getValue(section, "ProductionStateHash", ""),
                stateHash
            )
            || stateHash != runtimeProductionLanesStateHash(candidate.lanes)) {
            errorMessage = "multiplayer house production hash mismatch";
            return false;
        }
        const std::string selectionText = trimAsciiLocal(
            ini.getValue(section, "ProductionSelection", "")
        );
        if (!selectionText.empty()) {
            const std::vector<std::string> selection =
                splitRuntimeSaveFields(selectionText);
            std::uint64_t serial = 0;
            const std::optional<RuntimeProductionLane> lane =
                selection.size() == 2
                    ? runtimeProductionLaneFromName(selection[0])
                    : std::nullopt;
            if (!lane.has_value()
                || !parseRuntimeUint64Strict(selection[1], serial)
                || serial == 0U) {
                errorMessage =
                    "invalid multiplayer house production selection";
                return false;
            }
            candidate.selectedPlacementLane = *lane;
            candidate.selectedPlacementSerial = serial;
        }
        const std::string ownerKey =
            runtimeCanonicalHumanHouseOwner(owner);
        if (ownerKey.empty()
            || !parsed.emplace(ownerKey, std::move(candidate)).second) {
            errorMessage = "duplicate multiplayer human house owner";
            return false;
        }
    }
    productionByOwner = std::move(parsed);
    return true;
}

bool migrateRuntimeLegacyProductionCandidate(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage) {
    RuntimeSavedProductionCandidate migrated;
    const std::string activeText = trimAsciiLocal(
        ini.getValue("Economy", "ActiveOrder", "")
    );
    std::optional<RuntimeBuildOrder> active;
    if (!activeText.empty()) {
        active = parseRuntimeBuildOrder(state, activeText);
        const int legacyTotalTicks = active.has_value()
            ? runtimeLegacyBuildTimeTicks(
                active->item.cost,
                active->item.wall
            )
            : 0;
        if (!active.has_value()
            || !active->paid
            || active->remainingTicks < 0
            || active->remainingTicks > legacyTotalTicks) {
            errorMessage = "invalid legacy active production order";
            return false;
        }
    }

    const bool placementReady = parseRuntimeBool(
        ini.getValue("Economy", "PlacementReady", "no")
    );
    std::optional<RuntimeBuildCatalogItem> placementItem;
    if (placementReady) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Economy", "PlacementItem", "")
        );
        if (tokens.size() != 2) {
            errorMessage = "invalid legacy ready placement item";
            return false;
        }
        placementItem = runtimeCatalogItemByKindAndType(
            state,
            runtimeBuildKindFromName(tokens[0]),
            tokens[1]
        );
        if (!placementItem.has_value()
            || !runtimeBuildKindUsesSingleStructureSlot(
                placementItem->kind
            )
            || active.has_value()) {
            errorMessage = "impossible legacy placement production state";
            return false;
        }
    }

    const auto queueLegacy = [
        &state,
        &migrated,
        &errorMessage
    ](
        const RuntimeBuildCatalogItem& item,
        RuntimeProductionLane& lane,
        std::uint64_t& serial
    ) {
        const std::optional<RuntimeProductionLane> resolvedLane =
            runtimeProductionLaneForCatalogItem(state, item);
        if (!resolvedLane.has_value()) {
            errorMessage = "legacy production item has no lane";
            return false;
        }
        RuntimeProductionLaneOrder order;
        order.itemKey = item.typeId;
        order.cost = std::max(0, item.cost);
        order.totalTicks = std::max(1, item.buildTicks);
        const RuntimeProductionQueueResult result =
            runtimeQueueProductionOrder(
                migrated.lanes,
                *resolvedLane,
                std::move(order),
                state.rules.getMaximumQueuedObjects(),
                std::numeric_limits<int>::max()
            );
        if (result.decision != RuntimeProductionQueueDecision::Accepted) {
            errorMessage = "legacy production lane exceeds capacity";
            return false;
        }
        lane = *resolvedLane;
        serial = result.serial;
        return true;
    };
    const auto simulateLegacyProgress = [
        &migrated,
        &errorMessage
    ](
        RuntimeProductionLane lane,
        std::uint64_t serial,
        int elapsedTicks,
        int availableCredits,
        bool onHold,
        int& unusedCredits
    ) {
        RuntimeProductionAdvanceHooks hooks;
        hooks.productionRate = [elapsedTicks](
            RuntimeProductionLane,
            const RuntimeProductionLaneOrder&
        ) {
            return elapsedTicks == 0
                ? RuntimeProductionRate{0, 1}
                : RuntimeProductionRate{1, 1};
        };
        int credits = std::max(0, availableCredits);
        const int iterations = std::max(1, elapsedTicks);
        for (int tick = 0; tick < iterations; ++tick) {
            runtimeAdvanceProductionLanes(migrated.lanes, credits, hooks);
        }
        RuntimeProductionLaneState& laneState =
            migrated.lanes.lanes[runtimeProductionLaneIndex(lane)];
        const bool serialPresent =
            (laneState.active.has_value()
                && laneState.active->serial == serial)
            || (laneState.ready.has_value()
                && laneState.ready->serial == serial);
        if (!serialPresent) {
            errorMessage = "legacy production progress migration failed";
            return false;
        }
        if (onHold) {
            if (!laneState.active.has_value()
                || laneState.active->serial != serial
                || !runtimeSetProductionHold(
                    migrated.lanes,
                    lane,
                    true
                )) {
                errorMessage = "invalid legacy held production state";
                return false;
            }
        }
        unusedCredits = credits;
        return true;
    };

    if (active.has_value()) {
        RuntimeProductionLane lane;
        std::uint64_t serial = 0;
        if (!queueLegacy(active->item, lane, serial)) {
            return false;
        }
        const int legacyTotalTicks = runtimeLegacyBuildTimeTicks(
            active->item.cost,
            active->item.wall
        );
        const int elapsedLegacyTicks = std::max(
            0,
            legacyTotalTicks - active->remainingTicks
        );
        const int elapsedTicks = static_cast<int>(
            (static_cast<std::uint64_t>(elapsedLegacyTicks)
                * static_cast<std::uint64_t>(
                    std::max(1, active->item.buildTicks)
                ))
            / static_cast<std::uint64_t>(legacyTotalTicks)
        );
        int refund = 0;
        if (!simulateLegacyProgress(
                lane,
                serial,
                elapsedTicks,
                active->item.cost,
                active->onHold,
                refund
            )) {
            return false;
        }
        migrated.legacyCreditRefund = refund;
    } else if (placementItem.has_value()) {
        RuntimeProductionLane lane;
        std::uint64_t serial = 0;
        if (!queueLegacy(*placementItem, lane, serial)) {
            return false;
        }
        int unusedCredits = 0;
        if (!simulateLegacyProgress(
                lane,
                serial,
                std::max(1, placementItem->buildTicks),
                placementItem->cost,
                false,
                unusedCredits
            )) {
            return false;
        }
        const RuntimeProductionLaneState& laneState =
            migrated.lanes.lanes[runtimeProductionLaneIndex(lane)];
        if (!laneState.ready.has_value()
            || laneState.ready->serial != serial
            || unusedCredits != 0) {
            errorMessage = "legacy placement did not migrate to ready";
            return false;
        }
        migrated.selectedPlacementLane = lane;
        migrated.selectedPlacementSerial = serial;
    }

    const int queueCount = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "QueueCount", "0"),
        -1
    );
    if (queueCount < 0 || queueCount > 65536) {
        errorMessage = "invalid legacy production queue count";
        return false;
    }
    for (int index = 0; index < queueCount; ++index) {
        const std::string value = ini.getValue(
            "Queue",
            std::to_string(index),
            ""
        );
        const std::optional<RuntimeBuildOrder> order =
            parseRuntimeBuildOrder(state, value);
        if (!order.has_value()
            || order->paid
            || order->onHold
            || order->remainingTicks
                != runtimeLegacyBuildTimeTicks(
                    order->item.cost,
                    order->item.wall
                )) {
            errorMessage = "invalid legacy queued production order";
            return false;
        }
        RuntimeProductionLane lane;
        std::uint64_t serial = 0;
        if (!queueLegacy(order->item, lane, serial)) {
            return false;
        }
    }

    if (!validateRuntimeProductionCatalogState(
            state,
            migrated,
            errorMessage
        )) {
        return false;
    }
    candidate = std::move(migrated);
    return true;
}

void swapRuntimeLoadedProductionLanes(
    RuntimeProductionLanesState& live,
    RuntimeProductionLanesState& loaded) noexcept {
    for (std::size_t index = 0; index < live.lanes.size(); ++index) {
        swapRuntimeLoadedField(
            live.lanes[index].queued,
            loaded.lanes[index].queued
        );
        swapRuntimeLoadedField(
            live.lanes[index].active,
            loaded.lanes[index].active
        );
        swapRuntimeLoadedField(
            live.lanes[index].ready,
            loaded.lanes[index].ready
        );
        swapRuntimeLoadedField(
            live.lanes[index].onHold,
            loaded.lanes[index].onHold
        );
    }
    swapRuntimeLoadedField(live.nextSerial, loaded.nextSerial);
}

void swapRuntimeLoadedEconomy(
    RuntimeEconomyState& live,
    RuntimeEconomyState& loaded) noexcept {
#define RUNTIME_ECONOMY_SWAP(field) \
    swapRuntimeLoadedField(live.field, loaded.field)

    RUNTIME_ECONOMY_SWAP(owner);
    RUNTIME_ECONOMY_SWAP(techLevel);
    RUNTIME_ECONOMY_SWAP(credits);
    RUNTIME_ECONOMY_SWAP(powerProduced);
    RUNTIME_ECONOMY_SWAP(powerUsed);
    RUNTIME_ECONOMY_SWAP(harvesters);
    RUNTIME_ECONOMY_SWAP(refineries);
    RUNTIME_ECONOMY_SWAP(incomeCooldownTicks);
    RUNTIME_ECONOMY_SWAP(storedResourceUnits);
    RUNTIME_ECONOMY_SWAP(storedResourceValue);
    RUNTIME_ECONOMY_SWAP(storedResourceGemUnits);
    RUNTIME_ECONOMY_SWAP(resourceStorageCapacity);
    RUNTIME_ECONOMY_SWAP(lastResourceStoredUnits);
    RUNTIME_ECONOMY_SWAP(lastResourceStoredValue);
    RUNTIME_ECONOMY_SWAP(lastResourceStoredGemUnits);
    RUNTIME_ECONOMY_SWAP(lastResourceStorageOverflowUnits);
    RUNTIME_ECONOMY_SWAP(lastResourceStorageCapacity);
    RUNTIME_ECONOMY_SWAP(catalog);
    swapRuntimeLoadedProductionLanes(
        live.localProductionLanes,
        loaded.localProductionLanes
    );
    RUNTIME_ECONOMY_SWAP(localProductionLanesInitialized);
    RUNTIME_ECONOMY_SWAP(selectedPlacementLane);
    RUNTIME_ECONOMY_SWAP(selectedPlacementSerial);
    RUNTIME_ECONOMY_SWAP(limitedBuildHistoryByOwner);
    RUNTIME_ECONOMY_SWAP(placementCell);
    RUNTIME_ECONOMY_SWAP(placementValid);
    RUNTIME_ECONOMY_SWAP(status);
    RUNTIME_ECONOMY_SWAP(completedBuildings);
    RUNTIME_ECONOMY_SWAP(completedUnits);
    RUNTIME_ECONOMY_SWAP(completedInfantry);
    RUNTIME_ECONOMY_SWAP(resourceTrips);
    RUNTIME_ECONOMY_SWAP(harvesterTeleports);
    RUNTIME_ECONOMY_SWAP(slaveMinerLoads);
    RUNTIME_ECONOMY_SWAP(lastResourceBaseValue);
    RUNTIME_ECONOMY_SWAP(lastResourcePurifierBonus);
    RUNTIME_ECONOMY_SWAP(lastResourcePurifierCount);
    RUNTIME_ECONOMY_SWAP(lastHarvesterGatherCell);
    RUNTIME_ECONOMY_SWAP(lastHarvesterGatherBeforeBails);
    RUNTIME_ECONOMY_SWAP(lastHarvesterGatherAfterBails);
    RUNTIME_ECONOMY_SWAP(lastHarvesterGatherOverlayBefore);
    RUNTIME_ECONOMY_SWAP(lastHarvesterGatherOverlayAfter);
    RUNTIME_ECONOMY_SWAP(lastRefinerySmokeCell);
    RUNTIME_ECONOMY_SWAP(lastRefinerySmokeEffects);
    RUNTIME_ECONOMY_SWAP(lastRefinerySmokeParticleSystem);
    RUNTIME_ECONOMY_SWAP(lastRefinerySmokeParticle);
    RUNTIME_ECONOMY_SWAP(lastRefinerySmokeImage);
    RUNTIME_ECONOMY_SWAP(lastHarvesterTooFarApproachTarget);
    RUNTIME_ECONOMY_SWAP(lastHarvesterTooFarDistance);
    RUNTIME_ECONOMY_SWAP(lastHarvesterTooFarThreshold);
    RUNTIME_ECONOMY_SWAP(lastHarvesterTooFarChrono);
    RUNTIME_ECONOMY_SWAP(simplifiedHarvesterIncomeEnabled);
    RUNTIME_ECONOMY_SWAP(skirmishStartApplied);
    RUNTIME_ECONOMY_SWAP(skirmishStartObjectId);
    RUNTIME_ECONOMY_SWAP(skirmishStartCell);
    RUNTIME_ECONOMY_SWAP(skirmishStatus);

#undef RUNTIME_ECONOMY_SWAP
}

bool runtimeObjectIsHarvester(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    return unit != nullptr && unit->harvester;
}

bool runtimeObjectIsChronoHarvester(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object) {
    const UnitType* unit = findRuntimeUnitType(state.rules, object);
    if (unit == nullptr || !unit->harvester) {
        return false;
    }

    const std::string locomotor = toLowerAsciiLocal(unit->locomotor);
    return unit->teleporter
        || locomotor.find("4a582747") != std::string::npos
        || toUpperAsciiLocal(object.type) == "CMIN";
}

int runtimeHarvesterCargoCapacity(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester) {
    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    if (unit != nullptr && unit->storage > 0) {
        return std::max(1, unit->storage);
    }
    return 20;
}

int runtimeHarvesterDefaultLoadTicks(const RuntimeGameSessionState& state) {
    // Retail UnitClass waits for nine animation stages. Each stage uses
    // General.HarvesterLoadRate (constructor default 2 rule frames).
    return runtimeRuleFramesToTicks(
        9 * state.rules.getHarvesterCadenceRules().loadRate,
        state.config.fixedTickHz);
}

int runtimeHarvesterLoadTicks(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester) {
    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    if (unit != nullptr && unit->harvestRate > 0) {
        return runtimeRuleFramesToTicks(unit->harvestRate, state.config.fixedTickHz);
    }
    return runtimeHarvesterDefaultLoadTicks(state);
}

int runtimeHarvesterUnloadTicks(const RuntimeGameSessionState& state) {
    // The original compares its integer stage counter with DumpRate * 900;
    // ceil must precede conversion to simulation ticks (0.016 -> 15 frames).
    const int frames = std::max(1, static_cast<int>(std::ceil(
        state.rules.getHarvesterCadenceRules().dumpRateMinutes * 900.0)));
    return runtimeRuleFramesToTicks(frames, state.config.fixedTickHz);
}

int runtimeHarvesterThinkCooldownTicks(const RuntimeGameSessionState& state) {
    return std::max(8, state.config.fixedTickHz / 3);
}

bool runtimeCellHasHarvestableResource(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
    return terrain != nullptr
        && terrain->hasOverlay
        && (terrain->overlayIsOre || terrain->overlayIsGem);
}

std::string runtimeResourceTwoDigitSuffix(int value) {
    value = std::clamp(value, 1, 20);
    return (value < 10 ? "0" : "") + std::to_string(value);
}

int runtimeResourceMaxBailsForFamily(const std::string& family) {
    (void)family;
    return runtimeOreMaxBails;
}

std::string runtimeResourceFamilyForTerrain(const TerrainCell& terrain) {
    const std::string overlayTypeLower =
        toLowerAsciiLocal(terrain.overlayType);
    if (terrain.overlayIsGem
        || (terrain.overlayIndex >= 27 && terrain.overlayIndex <= 38)
        || overlayTypeLower.rfind("gem", 0) == 0) {
        return "GEM";
    }
    if ((terrain.overlayIndex >= 127 && terrain.overlayIndex <= 146)
        || overlayTypeLower.rfind("tib2_", 0) == 0) {
        return "TIB2_";
    }
    if ((terrain.overlayIndex >= 147 && terrain.overlayIndex <= 166)
        || overlayTypeLower.rfind("tib3_", 0) == 0) {
        return "TIB3_";
    }
    return "TIB";
}

int runtimeTiberiumImageForResourceFamily(const std::string& family) {
    const std::string normalized =
        toUpperAsciiLocal(trimAsciiLocal(family));
    if (normalized == "GEM") {
        return 2;
    }
    if (normalized == "TIB2_") {
        return 3;
    }
    if (normalized == "TIB3_") {
        return 4;
    }
    return 1;
}

const TiberiumType* runtimeTiberiumTypeForResourceFamily(
    const RulesDatabase& rules,
    const std::string& family) {
    return runtimeTiberiumTypeForImage(
        rules,
        runtimeTiberiumImageForResourceFamily(family)
    );
}

int runtimeTrailingResourceNumber(const std::string& value) {
    if (value.empty()) {
        return -1;
    }

    int end = static_cast<int>(value.size()) - 1;
    while (end >= 0
        && !std::isdigit(static_cast<unsigned char>(value[end]))) {
        --end;
    }
    if (end < 0) {
        return -1;
    }

    int begin = end;
    while (begin > 0
        && std::isdigit(static_cast<unsigned char>(value[begin - 1]))) {
        --begin;
    }

    try {
        return std::stoi(value.substr(
            static_cast<std::size_t>(begin),
            static_cast<std::size_t>(end - begin + 1)
        ));
    } catch (...) {
        return -1;
    }
}

std::string runtimeResourceOverlayDiagnosticName(const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return "none";
    }
    if (!terrain.overlayType.empty()) {
        return terrain.overlayType;
    }
    if (!terrain.overlayImage.empty()) {
        return terrain.overlayImage;
    }
    const std::string family = runtimeResourceFamilyForTerrain(terrain);
    const std::string suffix = runtimeResourceTwoDigitSuffix(
        std::clamp(
            runtimeResourceVariantFromOverlayIndex(terrain),
            1,
            family == "GEM" ? 12 : 20
        )
    );
    if (family == "GEM") {
        return "GEM" + suffix;
    }
    if (family == "TIB2_") {
        return "TIB2_" + suffix;
    }
    if (family == "TIB3_") {
        return "TIB3_" + suffix;
    }
    return "TIB" + suffix;
}

int runtimeResourceBailCountFromTerrainIdentity(
    const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return 0;
    }

    const int maxBails = runtimeResourceMaxBailsForFamily(
        runtimeResourceFamilyForTerrain(terrain)
    );
    return std::clamp(terrain.overlayFrame + 1, 1, maxBails);
}

int runtimeResourceMaxBailsForTerrain(const TerrainCell& terrain) {
    return runtimeResourceMaxBailsForFamily(
        runtimeResourceFamilyForTerrain(terrain)
    );
}

void setRuntimeResourceTerrainVariant(
    TerrainCell& terrain,
    const std::string& family,
    int variant) {
    const int maxVariant = family == "GEM" ? 12 : 20;
    variant = std::clamp(variant, 1, maxVariant);
    const std::string suffix = runtimeResourceTwoDigitSuffix(variant);
    terrain.overlayIsGem = family == "GEM";
    terrain.overlayIsOre = !terrain.overlayIsGem;
    if (family == "GEM") {
        terrain.overlayIndex = 26 + variant;
        terrain.overlayType = "GEM" + suffix;
        terrain.overlayImage = terrain.overlayType;
    } else if (family == "TIB2_") {
        terrain.overlayIndex = 126 + variant;
        terrain.overlayType = "TIB2_" + suffix;
        terrain.overlayImage = "TIB" + suffix;
    } else if (family == "TIB3_") {
        terrain.overlayIndex = 146 + variant;
        terrain.overlayType = "TIB3_" + suffix;
        terrain.overlayImage = "TIB" + suffix;
    } else {
        terrain.overlayIndex = 101 + variant;
        terrain.overlayType = "TIB" + suffix;
        terrain.overlayImage = terrain.overlayType;
    }
}

void setRuntimeResourceTerrainBails(
    TerrainCell& terrain,
    const std::string& family,
    int bails) {
    const int maxBails = runtimeResourceMaxBailsForFamily(family);
    if (bails <= 0) {
        terrain.hasOverlay = false;
        terrain.overlayIndex = -1;
        terrain.overlayFrame = 0;
        terrain.overlayType.clear();
        terrain.overlayImage.clear();
        terrain.overlayIsOre = false;
        terrain.overlayIsGem = false;
        return;
    }

    bails = std::clamp(bails, 1, maxBails);
    const bool preserveVariant =
        terrain.hasOverlay
        && (terrain.overlayIsOre || terrain.overlayIsGem)
        && runtimeResourceFamilyForTerrain(terrain) == family;
    const int variant = preserveVariant
        ? runtimeResourceVariantForTerrain(terrain)
        : runtimeGeneratedResourceVariant(terrain, family);
    terrain.hasOverlay = true;
    terrain.overlayFrame = bails - 1;
    setRuntimeResourceTerrainVariant(terrain, family, variant);
}

int runtimeResourceDensityForTerrain(const TerrainCell& terrain) {
    if (!terrain.hasOverlay || (!terrain.overlayIsOre && !terrain.overlayIsGem)) {
        return -1;
    }
    return runtimeResourceBailCountFromTerrainIdentity(terrain) - 1;
}

int runtimeResourceBailCountForTerrain(const TerrainCell& terrain) {
    const int density = runtimeResourceDensityForTerrain(terrain);
    return density < 0 ? 0 : density + 1;
}

void invalidateRuntimeResourceCaches(
    RuntimeProductionHostState& state,
    bool navigationChanged,
    bool radarAppearanceChanged) {
    if (radarAppearanceChanged) {
        state.radarTerrainKey.clear();
        ++state.resourceStateRevision;
    }
    if (navigationChanged) {
        state.pathTerrainCache.valid = false;
        state.pathTerrainCache.terrainCellCount = 0;
        state.pathTerrainCache.staticCells.clear();
        state.pathTerrainCache.dynamicValid = false;
        state.pathTerrainCache.dynamicCells.clear();
    }
}

int runtimeResourceDensityAtCell(
    const RuntimeGameSessionState& state,
    const RuntimeCellPoint& cell) {
    const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
    return terrain == nullptr ? -1 : runtimeResourceDensityForTerrain(*terrain);
}

int runtimeHarvesterDepartureRefineryId(
    const RuntimeGameSessionState& state, const WorldObjectInstance& harvester,
    const std::string* owner) {
    if (const auto worker = state.slaveMinerTasks.find(harvester.instanceId);
        worker != state.slaveMinerTasks.end()) {
        const auto* parent = findObjectById(state, worker->second.refineryId);
        if (parent && parent->owner == harvester.owner
            && runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(harvester), *parent) == 0.0)
            return parent->instanceId;
    }
    const auto task = state.harvesterTasks.find(harvester.instanceId);
    if (task == state.harvesterTasks.end()) return -1;
    const auto* refinery = findObjectById(state, task->second.targetRefineryId);
    // The dock can replace impassable ground with the refinery's drive row.
    // Retain that same passage for the first journey out after unloading.
    return refinery != nullptr
        && runtimeHarvesterCanUnloadAtRefinery(state, harvester, *refinery, owner)
        && runtimeStructureCellIsVehicleDriveRow(
            state, *refinery, runtimeCellPointForObject(harvester), harvester)
        ? refinery->instanceId : -1;
}

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    RuntimeHarvesterResourceAccess& access,
    const RuntimeCellPoint& resource) {
    const auto& state = access.state;
    const auto& harvester = access.harvester;
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    if (access.canGatherFrom(from, resource)) return from;
    // Invalid resource terrain cannot become reachable by choosing a distant
    // fallback destination. Check it before doing any path searches.
    if (!access.canGatherFrom(resource, resource)) return std::nullopt;
    const auto& occupied = access.occupiedForPath();
    std::vector<RuntimeCellPoint> approaches{resource};
    const auto neighbors = runtimeNeighborsOf(resource);
    approaches.insert(approaches.end(), neighbors.begin(), neighbors.end());
    for (const auto& approach : approaches) {
        if (!access.canGatherFrom(approach, resource)
            || !runtimeCellIsPassable(state, *access.terrain, occupied, approach, harvester))
            continue;
        if (access.reachableAfterFailure
            && !access.reachableAfterFailure->contains(runtimeCellKey(approach.x, approach.y)))
            continue;
        const auto path = findRuntimePath(
            state, harvester, from, approach, access.departureRefineryId);
        if (!path.empty() && path.back() == approach) return approach;
        access.rememberFailedSearch();
    }
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    const RuntimeProductionHostState& state, const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resource,
    const std::string* owner) {
    RuntimeHarvesterResourceAccess access(state, harvester, owner);
    return findRuntimeHarvesterResourceApproach(access, resource);
}

std::optional<RuntimeCellPoint> findHarvestableRuntimeResourceNear(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& preferred,
    const std::string* owner) {
    const RuntimeCellPoint current = runtimeCellPointForObject(harvester);
    RuntimeHarvesterResourceAccess access(state, harvester, owner);
    if (access.canGatherFrom(current, preferred)) {
        return preferred;
    }
    if (access.canGatherFrom(current, current)) {
        return current;
    }

    struct NearbyResourceCandidate {
        RuntimeCellPoint cell;
        int density = -1;
        int priority = 0;
    };
    std::optional<NearbyResourceCandidate> bestCandidate;
    constexpr int priorityMatrix[3][3] = {
        {8, 5, 6},
        {3, 0, 2},
        {7, 4, 1},
    };

    constexpr int radius = 1;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::abs(dx) != radius && std::abs(dy) != radius) {
                continue;
            }
            const RuntimeCellPoint candidate{
                current.x + dx,
                current.y + dy
            };
            const int density = runtimeResourceDensityAtCell(state, candidate);
            if (density < 0 || !access.canGatherFrom(current, candidate)) {
                continue;
            }
            const int priority = priorityMatrix[dy + 1][dx + 1];
            if (!bestCandidate.has_value()
                || density > bestCandidate->density
                || (density == bestCandidate->density
                    && priority < bestCandidate->priority)) {
                bestCandidate = NearbyResourceCandidate{
                    candidate,
                    density,
                    priority
                };
            }
        }
    }
    if (bestCandidate.has_value()) {
        return bestCandidate->cell;
    }
    return std::nullopt;
}

bool runtimeAnyOreFieldRuleDue(const RuntimeGameSessionState& state) {
    for (const TiberiumType& tiberium : state.rules.getTiberiumTypes()) {
        if (runtimeOreRuleDue(state, tiberium, false)
            || runtimeOreRuleDue(state, tiberium, true)) {
            return true;
        }
    }
    return false;
}

RuntimeOreFieldGrowthResult advanceRuntimeOreFieldGrowth(
    RuntimeProductionHostState& state,
    const std::unordered_map<int, int>& occupiedByCell,
    const std::unordered_set<int>& terrainObjectCells) {
    RuntimeOreFieldGrowthResult result;
    if (!runtimeAnyOreFieldRuleDue(state)) {
        return result;
    }

    std::vector<RuntimeOreFieldSource> sources;
    sources.reserve(128);
    for (const TerrainCell& terrain : state.world.getTerrainCells()) {
        const std::optional<RuntimeOreOverlayIdentity> identity =
            runtimeOreIdentityFromTerrain(terrain);
        if (!identity.has_value()) {
            continue;
        }

        const TiberiumType* tiberium =
            runtimeTiberiumTypeForOreIdentity(state.rules, *identity);
        if (tiberium == nullptr) {
            continue;
        }

        const bool growthDue = runtimeOreRuleDue(state, *tiberium, false);
        const bool spreadDue = runtimeOreRuleDue(state, *tiberium, true);
        if (!growthDue && !spreadDue) {
            continue;
        }

        sources.push_back({
            {terrain.x, terrain.y},
            *identity,
            tiberium,
            growthDue,
            spreadDue
        });
    }

    for (const RuntimeOreFieldSource& source : sources) {
        ++result.sources;
        if (source.growthDue
            && runtimeCellCanIncreaseOreDensity(state, source.cell)
            && runtimeOreRuleChancePass(
                state,
                source.cell,
                source.identity,
                *source.tiberium,
                state.tick,
                false
            )
            && growRuntimeOreAtCell(state, source.cell, source.identity)) {
            ++result.densified;
            result.changed = true;
        }

        if (!source.spreadDue
            || !runtimeOreRuleChancePass(
                state,
                source.cell,
                source.identity,
                *source.tiberium,
                state.tick,
                true
            )) {
            continue;
        }

        const std::optional<RuntimeCellPoint> target =
            findRuntimeOreSpreadTarget(
                state,
                source.cell,
                source.identity,
                occupiedByCell,
                terrainObjectCells,
                state.tick
            );
        if (!target.has_value()) {
            continue;
        }
        if (growRuntimeOreAtCell(
                state,
                *target,
                source.identity,
                runtimeOreFieldSpreadSpawnDensityFrame + 1
            )) {
            ++result.spread;
            result.changed = true;
        }
    }

    return result;
}

bool initializeRuntimeInitialOreFields(RuntimeGameSessionState& state) {
    if (state.initialOreFieldsSeeded) {
        return false;
    }
    state.initialOreFieldsSeeded = true;
    state.initialOreCellsSeeded = 0;
    state.initialOreCellsDensified = 0;
    spawnRuntimeOreTwinkleEffects(state);
    return false;
}

bool issueRuntimeHarvesterMoveToCell(
    RuntimeProductionHostState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& targetCell,
    const std::string& mission,
    std::optional<RuntimeCellPoint>* actualTarget,
    int allowedOccupiedObjectId,
    bool exactTarget,
    const std::string* owner) {
    const bool miningApproach = mission == "Harvest"
        && (runtimeObjectIsHarvester(state, harvester) || state.slaveMinerTasks.contains(harvester.instanceId));
    if (miningApproach && allowedOccupiedObjectId < 0)
        allowedOccupiedObjectId = runtimeHarvesterDepartureRefineryId(state, harvester, owner);
    const std::optional<RuntimeCellPoint> passableTarget =
        miningApproach
            ? findRuntimeHarvesterResourceApproach(state, harvester, targetCell, owner)
            : exactTarget
            ? findRuntimePassableCellNear(
                state,
                harvester,
                targetCell,
                0,
                allowedOccupiedObjectId
            )
            : findNearestRuntimePassableCell(
                state,
                harvester,
                targetCell,
                allowedOccupiedObjectId
            );
    if (!passableTarget.has_value()) {
        return false;
    }
    if (actualTarget != nullptr) {
        *actualTarget = passableTarget;
    }

    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = *passableTarget;
    if (state.slaveMinerTasks.contains(harvester.instanceId)) {
        std::optional<RuntimeObjectBehavior> previous;
        if (const auto old = state.behaviors.find(harvester.instanceId); old != state.behaviors.end())
            previous = old->second;
        eraseRuntimeBehavior(state, harvester.instanceId);
        if (previous) {
            behavior.weaponCooldownTicks = previous->weaponCooldownTicks;
            if (previous->moveCooldownTicks > 0)
                inheritRuntimeVehicleMoveSegment(harvester, behavior, *previous);
        }
    }
    harvester.mission = mission;
    state.behaviors[harvester.instanceId] = std::move(behavior);
    RuntimeObjectBehavior& queuedBehavior =
        state.behaviors[harvester.instanceId];
    submitRuntimeMovePathRequest(
        state,
        harvester,
        queuedBehavior,
        runtimeCellPointForObject(harvester),
        queuedBehavior.moveTarget,
        allowedOccupiedObjectId,
        nullptr,
        !exactTarget && !miningApproach
    );
    return true;
}

const WorldObjectInstance* findRuntimeHarvesterRefineryById(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    int objectId,
    const std::string* owner) {
    const WorldObjectInstance* object = findObjectById(state, objectId);
    if (object == nullptr
        || !object->alive
        || object->owner != (owner ? *owner : state.economy.owner)
        || object->kind != WorldObjectKind::Structure) {
        return nullptr;
    }
    if (!runtimeHarvesterCanUnloadAtRefinery(state, harvester, *object, owner)) {
        return nullptr;
    }
    return object;
}

std::optional<int> findNearestRuntimeRefineryId(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const std::string* owner) {
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<int> bestObjectId;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != (owner ? *owner : state.economy.owner)
            || object.kind != WorldObjectKind::Structure) {
            continue;
        }

        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr
            || !runtimeHarvesterCanUnloadAtRefinery(
                state,
                harvester,
                object, owner
            )) {
            continue;
        }

        const double distance =
            runtimeCellDistance(from, runtimeCellPointForObject(object));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestObjectId = object.instanceId;
        }
    }
    return bestObjectId;
}

bool runtimeHarvesterDockCellReservedByOther(
    const RuntimeGameSessionState& state,
    int harvesterId,
    int refineryId,
    const RuntimeCellPoint& candidate) {
    for (const auto& [otherHarvesterId, task] : state.harvesterTasks) {
        if (otherHarvesterId == harvesterId
            || task.targetRefineryId != refineryId
            || task.dockCell.x < 0
            || task.dockCell != candidate) {
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::ReturningToRefinery
            || task.phase == RuntimeHarvesterPhase::Docking
            || task.phase == RuntimeHarvesterPhase::PreparingToUnload
            || task.phase == RuntimeHarvesterPhase::Unloading) {
            return true;
        }
    }
    return false;
}

bool runtimeHarvesterQueueCellReservedByOther(
    const RuntimeGameSessionState& state,
    int harvesterId,
    int refineryId,
    const RuntimeCellPoint& candidate) {
    for (const auto& [otherHarvesterId, task] : state.harvesterTasks) {
        if (otherHarvesterId == harvesterId
            || task.targetRefineryId != refineryId
            || task.queueCell.x < 0
            || task.queueCell != candidate) {
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::WaitingForRefineryDock) {
            return true;
        }
    }
    return false;
}

RuntimeCellPoint runtimeRefineryDefaultDockCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery) {
    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    return {
        origin.x + std::max(1, footprint.width) - 1,
        origin.y + (std::max(1, footprint.height) / 2)
    };
}

bool runtimeHarvesterCanReachCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int allowedOccupiedObjectId) {
    RuntimePerfTraceScope trace("harvesterReachability", state.tick);
    const std::optional<RuntimeCellPoint> passable =
        findRuntimePassableCellNear(
            state,
            harvester,
            target,
            0,
            allowedOccupiedObjectId
        );
    if (!passable.has_value() || *passable != target) {
        return false;
    }

    const RuntimeCellPoint harvesterCell = runtimeCellPointForObject(harvester);
    const std::vector<RuntimeCellPoint> path = findRuntimePath(
        state,
        harvester,
        harvesterCell,
        target,
        allowedOccupiedObjectId
    );
    return !path.empty() || harvesterCell == target;
}

bool runtimeChronoHarvesterCanTeleportToCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int allowedOccupiedObjectId) {
    if (!runtimeObjectIsChronoHarvester(state, harvester)) {
        return false;
    }

    const RuntimeCellPoint harvesterCell = runtimeCellPointForObject(harvester);
    const double distance = runtimeCellDistance(harvesterCell, target);
    if (distance <= 1.0
        || distance > state.rules.getChronoHarvTooFarDistance()) {
        return false;
    }

    const std::optional<RuntimeCellPoint> passable =
        findRuntimePassableCellNear(
            state,
            harvester,
            target,
            0,
            allowedOccupiedObjectId
        );
    return passable.has_value() && *passable == target;
}

double runtimeHarvesterTooFarDistanceForObject(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester) {
    return runtimeObjectIsChronoHarvester(state, harvester)
        ? state.rules.getChronoHarvTooFarDistance()
        : state.rules.getHarvesterTooFarDistance();
}

bool teleportRuntimeHarvesterToCell(
    RuntimeProductionHostState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& target,
    int* teleportCounter) {
    if (!handleRuntimeParasiteHostEvent(
            state,
            harvester.instanceId,
            ParasiteHostEventKind::Teleporting,
            std::max(1, state.config.fixedTickHz)
        )) {
        return false;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoOutSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoOutSound)
        );
    }
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpOut", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    state.behaviors.erase(harvester.instanceId);
    harvester.x = target.x;
    harvester.y = target.y;
    harvester.cell = runtimeCellKey(target.x, target.y);
    harvester.level = terrainLevelAtRuntimeCell(state, target.x, target.y);
    harvester.onBridge = runtimeCellIsBridge(state, target.x, target.y);
    clearRuntimeRenderPosition(harvester);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    invalidateRuntimeObjectProjectionCache(state);
    ++(teleportCounter ? *teleportCounter : state.economy.harvesterTeleports);
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpIn", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoInSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoInSound)
        );
    }
    return true;
}

std::optional<RuntimeCellPoint> findRuntimeRefineryQueueCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery) {
    RuntimePerfTraceScope trace("harvesterQueue", state.tick);
    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const ArtType* art = runtimeArtTypeForObject(state, refinery);
    if (art != nullptr && art->hasQueueingCell) {
        const RuntimeCellPoint candidate{
            origin.x + art->queueingCell.x,
            origin.y + art->queueingCell.y
        };
        if (!runtimeHarvesterQueueCellReservedByOther(
                state,
                harvester.instanceId,
                refinery.instanceId,
                candidate
            )
            && runtimeHarvesterCanReachCell(state, harvester, candidate, -1)) {
            return candidate;
        }
    }

    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const RuntimeCellPoint dockCell =
        runtimeRefineryDefaultDockCell(state, refinery);
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<RuntimeCellPoint> bestCell;

    for (int radius = 1; radius <= 8; ++radius) {
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const RuntimeCellPoint candidate{x, y};
                if (candidate == dockCell
                    || runtimeHarvesterQueueCellReservedByOther(
                        state,
                        harvester.instanceId,
                        refinery.instanceId,
                        candidate
                    )
                    || !runtimeHarvesterCanReachCell(
                        state,
                        harvester,
                        candidate,
                        -1
                    )) {
                    continue;
                }

                const double distance = runtimeCellDistance(
                    runtimeCellPointForObject(harvester),
                    candidate
                );
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestCell = candidate;
                }
            }
        }
        if (bestCell.has_value()) {
            return bestCell;
        }
    }

    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeRefineryDockCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery) {
    RuntimePerfTraceScope trace("harvesterDock", state.tick);
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, refinery.type);
    const int dockLimit =
        building == nullptr
            ? 0
            : runtimeBuildingHarvesterDockCount(*building);
    if (dockLimit <= 0) {
        return std::nullopt;
    }

    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const RuntimeCellPoint harvesterCell = runtimeCellPointForObject(harvester);
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<RuntimeCellPoint> bestCell;

    const RuntimeCellPoint defaultDock =
        runtimeRefineryDefaultDockCell(state, refinery);
    if (!runtimeStructureCellIsVehicleDriveRow(
            state,
            refinery,
            defaultDock,
            harvester
        )) {
        return std::nullopt;
    }
    const bool defaultDockReserved =
        runtimeHarvesterDockCellReservedByOther(
            state,
            harvester.instanceId,
            refinery.instanceId,
            defaultDock
        );
    if (!defaultDockReserved
        && (runtimeHarvesterCanReachCell(
            state,
            harvester,
            defaultDock,
            refinery.instanceId
        )
        || runtimeChronoHarvesterCanTeleportToCell(
            state,
            harvester,
            defaultDock,
            refinery.instanceId
        ))) {
        return defaultDock;
    }
    if (dockLimit <= 1) {
        return std::nullopt;
    }

    int candidateCount = 1;
    for (int radius = 1; radius <= 8; ++radius) {
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const RuntimeCellPoint candidate{x, y};
                ++candidateCount;
                if (candidateCount > dockLimit) {
                    continue;
                }
                const std::optional<RuntimeCellPoint> passable =
                    findRuntimePassableCellNear(
                        state,
                        harvester,
                        candidate,
                        0
                );
                if (!passable.has_value()) {
                    continue;
                }
                if (runtimeHarvesterDockCellReservedByOther(
                        state,
                        harvester.instanceId,
                        refinery.instanceId,
                        *passable
                    )) {
                    continue;
                }
                const std::vector<RuntimeCellPoint> path = findRuntimePath(
                    state,
                    harvester,
                    harvesterCell,
                    *passable
                );
                if (path.empty()
                    && harvesterCell != *passable
                    && !runtimeChronoHarvesterCanTeleportToCell(
                        state,
                        harvester,
                        *passable,
                        refinery.instanceId
                    )) {
                    continue;
                }

                const double distance =
                    runtimeCellDistance(harvesterCell, *passable);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestCell = *passable;
                }
            }
        }
        if (bestCell.has_value()) {
            return bestCell;
        }
    }
    return std::nullopt;
}

void normalizeRuntimeHarvesterTask(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    RuntimeHarvesterTask& task) {
    // Capacity is rules-derived state, not persistent cargo state.  Recompute it
    // so older saves made with the former compressed 3/4-unit capacity migrate
    // to the actual Storage= value (for example CMIN=20 and HARV=40).
    task.cargoCapacity = runtimeHarvesterCargoCapacity(state, harvester);
    task.cargoValue = std::max(0, task.cargoValue);
    task.cargoUnits = std::clamp(task.cargoUnits, 0, task.cargoCapacity);
    task.cargoGemUnits =
        std::clamp(task.cargoGemUnits, 0, task.cargoUnits);
    if (task.cargoValue <= 0) {
        task.cargoUnits = 0;
        task.cargoGemUnits = 0;
        task.cargoHasGems = false;
    }
    task.workTicksRemaining = std::max(0, task.workTicksRemaining);
}

bool runtimeHarvesterIsAtDock(
    const WorldObjectInstance& harvester,
    const RuntimeHarvesterTask& task) {
    return task.dockCell.x >= 0
        && runtimeCellPointForObject(harvester) == task.dockCell;
}

int runtimeHarvesterRefineryDockFacing(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const RuntimeCellPoint& dockCell) {
    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const ArtType* art = runtimeArtTypeForObject(state, refinery);
    if (art != nullptr && art->hasQueueingCell) {
        const int queueX = origin.x + art->queueingCell.x;
        const int queueY = origin.y + art->queueingCell.y;
        const int queueFacing = runtimeFacingFromDelta(
            queueX - dockCell.x,
            queueY - dockCell.y
        );
        if (queueFacing >= 0) {
            return queueFacing;
        }
    }

    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    const int outwardX =
        ((dockCell.x - origin.x) * 2)
        - (std::max(1, footprint.width) - 1);
    const int outwardY =
        ((dockCell.y - origin.y) * 2)
        - (std::max(1, footprint.height) - 1);
    const int outwardFacing =
        runtimeFacingFromDelta(outwardX, outwardY);
    return outwardFacing >= 0
        ? outwardFacing
        : runtimeFacingFromDelta(1, 0);
}

bool alignRuntimeHarvesterToResource(
    const RuntimeGameSessionState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& resourceCell) {
    const int desiredFacing = runtimeFacingFromDelta(
        resourceCell.x - harvester.x,
        resourceCell.y - harvester.y
    );
    if (desiredFacing < 0) {
        return true;
    }

    const int currentFacing =
        harvester.facing >= 0 ? harvester.facing : desiredFacing;
    harvester.facing = runtimeAdvanceBodyFacing(state, harvester, currentFacing, desiredFacing);
    if (runtimeFacingAbsDifference(harvester.facing, desiredFacing) > 4) {
        return false;
    }

    harvester.facing = desiredFacing;
    return true;
}

void spawnRuntimeHarvesterGatherEffect(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resourceCell) {
    // OREGATH depicts a vehicle intake. Infantry use their authored Shovel
    // sequence, including when an older saved task resumes harvesting.
    if (harvester.kind != WorldObjectKind::Unit) return;
    RuntimeImpactEffect effect;
    effect.attackerObjectId = harvester.instanceId;
    effect.owner = harvester.owner;
    effect.totalTicks = std::max(
        8,
        runtimeHarvesterLoadTicks(state, harvester)
    );
    effect.remainingTicks = effect.totalTicks;
    effect.radius = 10;
    effect.lifecycleState = "HarvestGather";
    effect.renderIntent = "oreGather";
    effect.animId = "OREGATH";
    effect.particleId = "ore";
    effect.facing = harvester.facing >= 0 ? harvester.facing : 0;
    const double originX = harvester.hasRenderPosition
        ? harvester.renderX
        : static_cast<double>(harvester.x);
    const double originY = harvester.hasRenderPosition
        ? harvester.renderY
        : static_cast<double>(harvester.y);
    // Place the intake at the hull's nose on the ground. The renderer removes
    // each direction strip's canvas padding, keeping its first puff at this
    // attachment point while retaining the authored animation motion.
    const std::array<double, 2> forward =
        runtimeWorldForwardForFacing(effect.facing);
    constexpr double kGatherEffectNoseDistanceCells = 0.65;
    effect.hasWorldPosition = true;
    effect.worldX = originX + forward[0] * kGatherEffectNoseDistanceCells;
    effect.worldY = originY + forward[1] * kGatherEffectNoseDistanceCells;
    const RuntimeCellPoint vehicleCell = runtimeCellPointForObject(harvester);
    const int resourceDx = resourceCell.x - vehicleCell.x;
    const int resourceDy = resourceCell.y - vehicleCell.y;
    const bool adjacentResource =
        resourceCell.x >= 0
        && resourceCell.y >= 0
        && std::abs(resourceDx) <= 1
        && std::abs(resourceDy) <= 1;
    constexpr std::array<int, 8> kFacingCellDx{
        -1, 0, 1, 1, 1, 0, -1, -1
    };
    constexpr std::array<int, 8> kFacingCellDy{
        -1, -1, -1, 0, 1, 1, 1, 0
    };
    const int cellFacingBucket = ((effect.facing + 16) % 256) / 32;
    effect.cell = adjacentResource
        ? resourceCell
        : RuntimeCellPoint{
            vehicleCell.x + kFacingCellDx[cellFacingBucket],
            vehicleCell.y + kFacingCellDy[cellFacingBucket]
        };
    // OREGATH is a ground-contact effect.  Do not inherit vehicle/bridge or
    // future flight height. Sample an adjacent resource cell when available;
    // a remote target falls back to the discrete vehicle-nose terrain cell.
    effect.level = terrainLevelAtRuntimeCell(
        state,
        effect.cell.x,
        effect.cell.y
    );
    effect.worldLevel = effect.level;
    state.impactEffects.push_back(std::move(effect));
}

void spawnRuntimeRefinerySmokeEffects(
    RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const WorldObjectInstance& harvester,
    RuntimeEconomyState* economyFeedback) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, refinery.type);
    if (building == nullptr
        || building->refinerySmoke.offsets.empty()
        || building->refinerySmoke.particleSystem.empty()) {
        return;
    }

    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    // RefinerySmokeOffset is authored relative to the SHP's foundation origin,
    // the same anchor used by the building renderer, not the foundation center.
    const double baseX = static_cast<double>(origin.x);
    const double baseY = static_cast<double>(origin.y);
    ensureRuntimeParticlesInitialized(state);
    int spawned = 0;
    std::size_t offsetOrdinal = 0;

    for (const RefinerySmokeOffset& offset :
            building->refinerySmoke.offsets) {
        RuntimeParticleSpawnContext particleContext;
        particleContext.sourceObjectId = refinery.instanceId;
        particleContext.owner = refinery.owner;
        particleContext.sourceTag = "refinery-smoke:"
            + building->refinerySmoke.particleSystem;
        // The stock refinery chimney art aligns half a lepton cell below
        // the centered canvas origin in the vertical projection. Keep this
        // attachment correction local to refinery smoke.
        particleContext.position = {
            (baseX * 256.0) + static_cast<double>(offset.x),
            (baseY * 256.0) + static_cast<double>(offset.y),
            (static_cast<double>(refinery.level)
                + runtimeWeaponHeightInLevels(offset.z - 128)) * 256.0
        };
        particleContext.seed =
            (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(refinery.instanceId)
             ) << 32U)
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(harvester.instanceId)
              )
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(state.tick)
              )
            ^ ((static_cast<std::uint64_t>(offsetOrdinal) + 1ULL)
                * 0x9e3779b97f4a7c15ULL)
            ^ 0x726566696e657279ULL;
        particleContext.systemLifetimeOverrideFrames = std::max(
            8,
            building->refinerySmoke.frames
        );
        ++offsetOrdinal;

        if (state.particles.spawnSystem(
                state.rules,
                building->refinerySmoke.particleSystem,
                particleContext
            ) != 0) {
            ++spawned;
        }
    }

    if (spawned > 0) {
        auto& feedback = economyFeedback ? *economyFeedback : state.economy;
        feedback.lastRefinerySmokeCell = origin;
        feedback.lastRefinerySmokeEffects = spawned;
        feedback.lastRefinerySmokeParticleSystem =
            building->refinerySmoke.particleSystem;
        feedback.lastRefinerySmokeParticle =
            building->refinerySmoke.particle;
        feedback.lastRefinerySmokeImage =
            building->refinerySmoke.image;
    }
}

std::optional<RuntimeCellPoint> findNearestRuntimeStructureResourceCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& structure,
    int maxRadius,
    const WorldObjectInstance& worker, const std::string* owner) {
    RuntimeHarvesterResourceAccess access(state, worker, owner);
    const RuntimeCellPoint origin = runtimeCellPointForObject(structure);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, structure);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    const std::vector<RuntimeCellPoint> footprintCells =
        runtimeStructureFootprintCells(origin, footprint);
    const int radiusLimit = std::max(1, maxRadius);

    struct ResourceCandidate {
        RuntimeCellPoint cell;
        long long distanceScore = 0;
        int value = 0;
        int density = 0;
    };

    for (int radius = 0; radius <= radiusLimit; ++radius) {
        std::vector<ResourceCandidate> candidates;
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    radius == 0
                    || x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const RuntimeCellPoint candidateCell{x, y};
                const TerrainCell* terrain =
                    findRuntimeTerrainCellAt(state, candidateCell);
                if (terrain == nullptr
                    || !terrain->hasOverlay
                    || (!terrain->overlayIsOre && !terrain->overlayIsGem)) {
                    continue;
                }

                candidates.push_back(ResourceCandidate{
                    candidateCell,
                    runtimeSquaredDistanceToCells(
                        candidateCell,
                        footprintCells
                    ),
                    runtimeResourceValueForCell(state, *terrain),
                    runtimeResourceDensityForTerrain(*terrain)
                });
            }
        }

        if (candidates.empty()) {
            continue;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const ResourceCandidate& left,
                const ResourceCandidate& right) {
                if (left.distanceScore != right.distanceScore) {
                    return left.distanceScore < right.distanceScore;
                }
                if (left.value != right.value) {
                    return left.value > right.value;
                }
                if (left.density != right.density) {
                    return left.density > right.density;
                }
                return RuntimeCellPointLess{}(left.cell, right.cell);
            }
        );
        for (const auto& candidate : candidates)
            if (findRuntimeHarvesterResourceApproach(access, candidate.cell)) return candidate.cell;
    }

    return std::nullopt;
}

bool runtimeObjectIsMobileSlaveMiner(
    const RuntimeGameSessionState& state, const WorldObjectInstance& object) {
    const auto* unit = object.kind == WorldObjectKind::Unit
        ? findRuntimeUnitType(state.rules, object) : nullptr;
    return unit != nullptr && runtimeUnitIsMobileSlaveMiner(*unit);
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerDeployCellNearOre(
    const RuntimeProductionHostState& state, const WorldObjectInstance& miner,
    const RuntimeCellPoint& ore,
    const RuntimeManualFormationMappedFilter* manualMappedFilter, const std::string* owner) {
    const auto* unit = findRuntimeUnitType(state.rules, miner);
    if (unit == nullptr || !runtimeUnitIsMobileSlaveMiner(*unit)
        || !runtimeCellHasHarvestableResource(state, ore)) return std::nullopt;
    const auto* building = findRuntimeBuildingTypeById(state.rules, unit->deploysInto);
    if (building == nullptr) return std::nullopt;
    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Building;
    item.typeId = building->id;
    item.waterBound = building->waterBound;
    const auto footprint = runtimeStructureFootprintForBuildItem(state, item);
    const int width = std::max(1, footprint.width), height = std::max(1, footprint.height);
    const auto occupied = buildRuntimeOccupancyLookup(state, miner.instanceId);
    const auto origin = runtimeCellPointForObject(miner);
    std::vector<RuntimeCellPoint> candidates;
    // Keep the resource outside the foundation, immediately beside its edge.
    for (int y = ore.y - height; y <= ore.y + 1; ++y) {
        for (int x = ore.x - width; x <= ore.x + 1; ++x) {
            if (x <= ore.x && ore.x < x + width && y <= ore.y && ore.y < y + height)
                continue;
            const RuntimeCellPoint cell{x, y};
            if (terrainLevelAtRuntimeCell(state, x, y)
                    != terrainLevelAtRuntimeCell(state, ore.x, ore.y)
                || !runtimeCellCanPlaceBuilding(state, item, cell, miner.instanceId, &occupied, owner ? *owner : std::string{}))
                continue;
            // Vehicle routes, manual mapping and deployment requests use
            // center anchors; the preceding foundation check alone uses a corner.
            candidates.push_back(runtimePlacementAnchorCellForFootprint(cell, footprint));
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](const auto& left, const auto& right) {
        const auto distance = [&](const auto& cell) {
            const long long dx = cell.x - origin.x, dy = cell.y - origin.y;
            return dx * dx + dy * dy;
        };
        if (distance(left) != distance(right)) return distance(left) < distance(right);
        return RuntimeCellPointLess{}(left, right);
    });
    for (const auto& cell : candidates) {
        if (cell != origin) {
            const auto path = findRuntimePath(state, miner, origin, cell);
            if (path.empty() || path.back() != cell) continue;
        }
        // Check the workers' ground route as well as the vehicle route.
        WorldObjectInstance worker = miner;
        worker.instanceId = -1;
        worker.kind = WorldObjectKind::Infantry;
        worker.type = building->enslaves.empty() ? "SLAV" : building->enslaves;
        worker.x = cell.x;
        worker.y = cell.y;
        worker.cell = runtimeCellKey(cell.x, cell.y);
        worker.onBridge = false;
        if (findRuntimeHarvesterResourceApproach(state, worker, ore, owner).has_value()
            && (manualMappedFilter == nullptr || manualMappedFilter->allows(cell))) return cell;
    }
    return std::nullopt;
}

void transferRuntimeSlaveMinerWorkers(
    RuntimeProductionHostState& state, int sourceId, const WorldObjectInstance& target) {
    if (const auto regen = state.slaveMinerRegenTicks.find(sourceId);
        regen != state.slaveMinerRegenTicks.end()) {
        const int ticks = regen->second;
        state.slaveMinerRegenTicks.erase(regen);
        state.slaveMinerRegenTicks[target.instanceId] = ticks;
    }
    for (auto& [workerId, task] : state.slaveMinerTasks) {
        if (task.refineryId != sourceId) continue;
        task.refineryId = target.instanceId;
        task.status = "miner relocated";
        if (auto behavior = state.behaviors.find(workerId); behavior != state.behaviors.end()
            && behavior->second.pendingAllowedOccupiedObjectId == sourceId)
            behavior->second.pendingAllowedOccupiedObjectId = target.instanceId;
        // Do not cancel a live gather/return segment or replace its ore goal,
        // cargo, shovel clock, pause state or phase during the transformation.
        if (task.phase == RuntimeSlaveMinerPhase::Stored) {
            if (auto* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
                worker && worker->health > 0)
                synchronizeRuntimeStoredSlaveMinerWorker(state, *worker, target);
        }
    }
}

bool advanceRuntimeMobileSlaveMinerEconomy(RuntimeProductionHostState& state,
    HouseEconomyContext& context, RuntimeEconomyState& feedback) {
    RuntimePerfTraceScope trace("advanceRuntimeMobileSlaveMinerEconomy", state.tick);
    const int scanInterval = std::max(1, std::min(
        runtimeRuleFramesToTicks(state.rules.getSlaveMinerKickFrameDelay(), state.config.fixedTickHz), state.config.fixedTickHz / 2));
    if (state.tick <= 0 || (state.tick % scanInterval) != 0) {
        return false;
    }

    std::vector<int> minerIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != context.owner
            || object.kind != WorldObjectKind::Unit
            || object.buildAnimTicksRemaining > 0
            || state.behaviors.find(object.instanceId)
                != state.behaviors.end()
            || object.harvestAutoSearchSuspended
            || runtimeMissionIsDeploying(object)
            || runtimeMissionIsUndeploying(object)) {
            continue;
        }

        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit != nullptr && runtimeUnitIsMobileSlaveMiner(*unit)
            && runtimeObjectCanMove(state, object)) {
            minerIds.push_back(object.instanceId);
        }
    }

    bool changed = false;
    for (int minerId : minerIds) {
        WorldObjectInstance* miner = findMutableObjectById(state, minerId);
        if (miner == nullptr
            || !miner->alive
            || state.behaviors.find(minerId) != state.behaviors.end()) {
            continue;
        }

        const UnitType* unit = findRuntimeUnitType(state.rules, *miner);
        if (unit == nullptr || !runtimeUnitIsMobileSlaveMiner(*unit)) {
            continue;
        }

        std::optional<RuntimeCellPoint> deployCell;
        const std::optional<RuntimeCellPoint> nearbyOre =
            findNearestRuntimeMobileSlaveMinerOreCell(
                state,
                *miner,
                state.rules.getSlaveMinerShortScan(),
                &deployCell, &context.owner
            );
        if (nearbyOre.has_value() && deployCell == runtimeCellPointForObject(*miner)) {
            const bool keepSelection = selectionContains(state, minerId);
            if (deployRuntimeMcv(state, minerId, keepSelection, deployCell, context.computerControlled ? &feedback : nullptr)) {
                feedback.status =
                    unit->id + " deployed near ore "
                    + std::to_string(nearbyOre->x)
                    + ","
                    + std::to_string(nearbyOre->y);
                changed = true;
                continue;
            }
        }

        const std::optional<RuntimeCellPoint> distantOre = nearbyOre.has_value()
            ? nearbyOre : findNearestRuntimeMobileSlaveMinerOreCell(
                state,
                *miner,
                state.rules.getSlaveMinerLongScan(),
                &deployCell, &context.owner
            );
        if (!distantOre.has_value()) {
            miner->mission = "Guard";
            feedback.status = unit->id + " found no ore";
            continue;
        }

        std::optional<RuntimeCellPoint> actualTarget;
        if (issueRuntimeHarvesterMoveToCell(
                state,
                *miner,
                *deployCell,
                "Move",
                &actualTarget,
                -1,
                true, &context.owner
            )) {
            const RuntimeCellPoint target =
                actualTarget.value_or(*distantOre);
            feedback.status =
                unit->id + " moving to ore "
                + std::to_string(target.x)
                + ","
                + std::to_string(target.y);
            changed = true;
        }
    }

    return changed;
}

bool advanceRuntimeMobileSlaveMinerEconomy(RuntimeProductionHostState& state) {
    if (runtimeMobileSlaveMiningDiagnosticOverride) return runtimeMobileSlaveMiningDiagnosticOverride(state);
    auto context = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    return advanceRuntimeMobileSlaveMinerEconomy(state, context, state.economy);
}

void synchronizeRuntimeStoredSlaveMinerWorker(
    RuntimeProductionHostState& state, WorldObjectInstance& worker,
    const WorldObjectInstance& refinery) {
    releaseRuntimeInfantrySubcellReservation(state, worker.instanceId);
    worker.x = refinery.x;
    worker.y = refinery.y;
    worker.cell = refinery.cell;
    worker.level = refinery.level;
    worker.onBridge = refinery.onBridge;
    worker.airborne = false;
    worker.flightLevel = refinery.level;
    worker.subCell = -1;
    worker.alive = false;
    worker.mission = "InsideSlaveMiner";
    std::erase(state.selectedObjectIds, worker.instanceId);
    // There is no separate body in storage; spatial audio uses the live host.
    clearRuntimeRenderPosition(worker);
}

const BuildingType* runtimeSlaveMinerParentBuilding(
    const RuntimeGameSessionState& state, const WorldObjectInstance& refinery) {
    const auto* mobile = runtimeObjectIsMobileSlaveMiner(state, refinery)
        ? findRuntimeUnitType(state.rules, refinery) : nullptr;
    if (!mobile && refinery.kind != WorldObjectKind::Structure) return nullptr;
    const auto* building = findRuntimeBuildingTypeById(
        state.rules, mobile ? mobile->deploysInto : refinery.type);
    return building && building->storage > 0
        && runtimeBuildingCanGatherResources(*building) && !building->enslaves.empty()
        ? building : nullptr;
}

bool runtimeSlaveMinerWorkerIsStored(
    const RuntimeGameSessionState& state, const WorldObjectInstance& worker) {
    const auto task = state.slaveMinerTasks.find(worker.instanceId);
    return task != state.slaveMinerTasks.end()
        && task->second.phase == RuntimeSlaveMinerPhase::Stored
        && !worker.alive && worker.health > 0 && worker.mission == "InsideSlaveMiner";
}

void synchronizeRuntimeStoredSlaveMinerWorkers(RuntimeProductionHostState& state) {
    for (auto& worker : state.world.accessObjects()) {
        if (!runtimeSlaveMinerWorkerIsStored(state, worker)) continue;
        const auto& task = state.slaveMinerTasks.at(worker.instanceId);
        const auto* refinery = findObjectById(state, task.refineryId);
        if (!refinery || !refinery->alive || refinery->health <= 0
            || !runtimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker.owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) continue;
        synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
    }
}

int runtimeSlaveMinerWorkerCapacity(const UnitType& worker) {
    return std::max(1, worker.storage > 0 ? worker.storage : 4);
}

int runtimeSlaveMinerWorkerHarvestTicks(
    const RuntimeGameSessionState& state,
    const UnitType& worker,
    const BuildingType& building) {
    if (worker.harvestRate > 0) {
        return runtimeRuleFramesToTicks(worker.harvestRate, state.config.fixedTickHz);
    }
    if (building.harvestRate > 0) {
        return runtimeRuleFramesToTicks(building.harvestRate, state.config.fixedTickHz);
    }
    return std::max(12, runtimeSlaveMinerGatherIntervalTicks(state, building));
}

int runtimeSlaveMinerWorkerRegenTicks(
    const RuntimeGameSessionState& state, const BuildingType& building) {
    return runtimeRuleFramesToTicks(building.slaveRegenRate > 0
        ? building.slaveRegenRate : 500, state.config.fixedTickHz);
}

bool runtimeObjectIsSlaveMinerWorker(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& object,
    const std::string& workerTypeId, const std::string* owner) {
    if ((!object.alive && !runtimeSlaveMinerWorkerIsStored(state, object))
        || object.health <= 0
        || runtimeCanonicalHumanHouseOwner(object.owner)
            != runtimeCanonicalHumanHouseOwner(owner ? *owner : state.economy.owner)
        || object.kind != WorldObjectKind::Infantry
        || toUpperAsciiLocal(object.type) != toUpperAsciiLocal(workerTypeId)) {
        return false;
    }

    const UnitType* worker = findRuntimeUnitType(state.rules, object);
    return worker != nullptr && worker->slaved;
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerSpawnCell(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId) {
    WorldObjectInstance probe;
    probe.instanceId = -1;
    probe.kind = WorldObjectKind::Infantry;
    probe.owner = refinery.owner;
    probe.type = workerTypeId;
    probe.alive = true;
    probe.level = refinery.level;
    probe.onBridge = refinery.onBridge;
    for (const auto& cell : runtimeStructureFootprintCellsForObject(state, refinery)) {
        if (findRuntimePassableCellNear(state, probe, cell, 0, refinery.instanceId)) {
            return cell;
        }
    }
    // Occupied interior cells delay the next worker; never substitute nearby
    // ground and make it appear outside the building.
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeSlaveMinerWorkerExitCell(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& refinery,
    const std::string& workerTypeId, const std::string* owner) {
    WorldObjectInstance probe;
    probe.instanceId = -1;
    probe.kind = WorldObjectKind::Infantry;
    probe.owner = owner ? *owner : state.economy.owner;
    probe.type = workerTypeId;
    probe.alive = true;
    probe.level = refinery.level;
    probe.onBridge = refinery.onBridge;
    RuntimeHarvesterResourceAccess access(state, probe, owner);

    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    const RuntimeStructureFootprint footprint =
        runtimeStructureFootprintForObject(state, refinery);
    const int width = std::max(1, footprint.width);
    const int height = std::max(1, footprint.height);
    for (int radius = 1; radius <= 6; ++radius) {
        for (int y = origin.y - radius;
            y <= origin.y + height - 1 + radius;
            ++y) {
            for (int x = origin.x - radius;
                x <= origin.x + width - 1 + radius;
                ++x) {
                const bool onBorder =
                    x == origin.x - radius
                    || x == origin.x + width - 1 + radius
                    || y == origin.y - radius
                    || y == origin.y + height - 1 + radius;
                if (!onBorder) {
                    continue;
                }

                const std::optional<RuntimeCellPoint> passable =
                    findRuntimePassableCellNear(state, probe, {x, y}, 0);
                if (passable.has_value()) {
                    const auto neighbors = runtimeNeighborsOf(*passable);
                    if (std::any_of(neighbors.begin(), neighbors.end(), [&](const auto& next) {
                        return runtimeCellTransitionIsPassable(state, *access.terrain,
                            access.staticOccupancy, *passable, next, probe, -1, nullptr, false);
                    })) return passable;
                }
            }
        }
    }

    return std::nullopt;
}

bool ensureRuntimeSlaveMinerWorkers(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& refinery,
    const BuildingType& building, const std::string* owner) {
    const int refineryId = refinery.instanceId;
    const int refineryFacing = refinery.facing;
    const std::string refineryOwner = refinery.owner;
    const std::string workerTypeId = runtimeSlaveMinerWorkerTypeId(building);
    const UnitType* workerType =
        findRuntimeUnitTypeById(state.rules, workerTypeId);
    if (workerType == nullptr || !workerType->slaved) {
        return false;
    }

    const int wantedWorkers = runtimeSlaveMinerGatherBatchSize(building);
    int& regenTicks = state.slaveMinerRegenTicks[refineryId];
    if (regenTicks > 0) {
        --regenTicks;
    }

    int activeWorkers = 0;
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        if (task.refineryId != refineryId) {
            continue;
        }
        const WorldObjectInstance* worker = findRuntimeObjectByIdIncludingInactive(state, workerId);
        if (worker != nullptr
            && runtimeObjectIsSlaveMinerWorker(state, *worker, workerTypeId, owner)) {
            ++activeWorkers;
        }
    }
    bool changed = false;
    if (activeWorkers < wantedWorkers && regenTicks > 0) {
        return false;
    }
    if (!runtimeObjectIsMobileSlaveMiner(state, refinery)
        && refinery.buildAnimTicksRemaining > 0) return false;

    while (activeWorkers < wantedWorkers) {
        const WorldObjectInstance* currentRefinery =
            findObjectById(state, refineryId);
        if (currentRefinery == nullptr || !currentRefinery->alive) {
            break;
        }
        const WorldObjectInstance parentSnapshot = *currentRefinery;
        const bool mobile = runtimeObjectIsMobileSlaveMiner(state, parentSnapshot);
        const std::optional<RuntimeCellPoint> spawnCell = mobile
            ? std::optional<RuntimeCellPoint>(runtimeCellPointForObject(parentSnapshot))
            : findRuntimeSlaveMinerWorkerSpawnCell(state, parentSnapshot, workerTypeId);
        if (!spawnCell.has_value()) {
            break;
        }

        WorldObjectInstance* worker = addRuntimeObjectInstanceForOwner(
            state,
            refineryOwner,
            RuntimeBuildKind::Infantry,
            workerTypeId,
            *spawnCell,
            std::nullopt,
            true,
            mobile,
            mobile ? -1 : refineryId
        );
        if (worker == nullptr) {
            break;
        }
        worker->mission = "Harvest";
        worker->facing = refineryFacing;
        RuntimeSlaveMinerTask task;
        task.refineryId = refineryId;
        task.phase = mobile ? RuntimeSlaveMinerPhase::Stored : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = mobile ? "regenerated in mobile miner" : "spawned";
        state.slaveMinerTasks[worker->instanceId] = std::move(task);
        if (mobile) synchronizeRuntimeStoredSlaveMinerWorker(state, *worker, parentSnapshot);
        ++activeWorkers;
        changed = true;
    }

    if (activeWorkers >= wantedWorkers) {
        state.slaveMinerRegenTicks.erase(refineryId);
    }
    if (changed) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
    }
    return changed;
}

int releaseRuntimeSlaveMinerWorkersForRefinery(
    RuntimeProductionHostState& state,
    int refineryId,
    const std::string& reason,
    const RuntimeObjectExitRequest* request, RuntimeEconomyState* miningEconomy) {
    auto& feedback = miningEconomy ? *miningEconomy : state.economy;
    int released = 0;
    state.slaveMinerRegenTicks.erase(refineryId);
    std::optional<WorldObjectInstance> parentSnapshot;
    if (const auto* parent = findRuntimeObjectByIdIncludingInactive(state, refineryId))
        parentSnapshot = *parent;
    std::vector<int> workerIds;
    for (const auto& [workerId, task] : state.slaveMinerTasks)
        if (task.refineryId == refineryId) workerIds.push_back(workerId);
    std::sort(workerIds.begin(), workerIds.end());
    for (const int workerId : workerIds) {
        auto* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
        const bool stored = worker && runtimeSlaveMinerWorkerIsStored(state, *worker);
        if (!worker || worker->health <= 0 || (!worker->alive && !stored)) {
            state.slaveMinerTasks.erase(workerId);
            continue;
        }
        // BridgeCollapsed is the existing collapse mission carried by the
        // public exit request. A hidden occupant cannot escape that hazard.
        const bool collapsed = stored && request
            && request->kind == RuntimeObjectExitKind::Destroyed
            && request->mission == "BridgeCollapsed";
        std::optional<RuntimeCellPoint> exitCell;
        if (stored && !collapsed) exitCell = findRuntimeSlaveMinerWorkerExitCell(
            state, parentSnapshot ? *parentSnapshot : *worker, worker->type, miningEconomy ? &feedback.owner : nullptr);
        if (stored && (collapsed || !exitCell)) {
            RuntimeObjectExitRequest exit;
            exit.objectId = workerId;
            exit.kind = RuntimeObjectExitKind::Destroyed;
            exit.tick = state.tick;
            exit.mission = collapsed ? "BridgeCollapsed" : "Destroyed";
            exit.reason = collapsed ? "stored slave lost with collapsed bridge"
                : "stored slave has no legal refinery escape";
            exit.wasAliveBeforeDamage = true;
            exit.suppressCrew = true;
            if (request) {
                exit.sourceObjectId = request->sourceObjectId;
                exit.sourceOwner = request->sourceOwner;
                exit.deathWeaponCause = request->deathWeaponCause;
                exit.warheadId = request->warheadId;
                exit.unitDeathSoundPosition = request->unitDeathSoundPosition;
                // A failed escape inherited from removal or sale is cleanup,
                // and keeps the existing quiet dependent-exit policy.
                exit.unitDeathSoundHandled = request->kind != RuntimeObjectExitKind::Destroyed;
                exit.suppressDeathWeapon = request->suppressDeathWeapon
                    || request->kind != RuntimeObjectExitKind::Destroyed;
            }
            if (!exit.unitDeathSoundPosition && parentSnapshot)
                exit.unitDeathSoundPosition = runtimeSpatialAudioPositionForObject(*parentSnapshot);
            // Queue before removing the relationship. The parent batch drains
            // this genuine death through the common once-only death path.
            (void)queueRuntimeObjectExit(state, std::move(exit));
            worker->health = 0;
            worker->alive = false;
            state.slaveMinerTasks.erase(workerId);
            continue;
        }

        const bool hadPose = worldObjectHasGroundRenderPose(*worker);
        const std::array<double, 3> position{worker->renderX, worker->renderY, worker->renderLevel};
        const bool onBridge = worker->renderOnBridge;
        eraseRuntimeBehavior(state, workerId);
        state.slaveMinerTasks.erase(workerId);
        worker->alive = true;
        worker->mission = "Guard";
        if (stored) {
            clearRuntimeRenderPosition(*worker);
            setRuntimeObjectCell(state, *worker, *exitCell);
            assignRuntimeInfantrySubcellAtCurrentLocation(state, *worker);
        } else if (hadPose) {
            worker->hasRenderPosition = true;
            worker->renderMoveActive = false;
            worker->renderX = position[0];
            worker->renderY = position[1];
            worker->renderLevel = position[2];
            worker->renderOnBridge = onBridge;
        }
        ++released;
    }

    if (released > 0) {
        feedback.status =
            "released " + std::to_string(released)
            + " slave miners"
            + (reason.empty() ? "" : " " + reason);
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        emitRuntimeSound(state, "SlavesFreed");
    }
    return released;
}

void cleanRuntimeSlaveMinerTasks(RuntimeProductionHostState& state, RuntimeEconomyState* miningEconomy) {
    auto& feedback = miningEconomy ? *miningEconomy : state.economy;
    const std::string currentOwner =
        runtimeCanonicalHumanHouseOwner(feedback.owner);
    std::unordered_set<int> lostRefineryIds;
    for (auto taskIt = state.slaveMinerTasks.begin();
        taskIt != state.slaveMinerTasks.end();) {
        const WorldObjectInstance* worker = findRuntimeObjectByIdIncludingInactive(state, taskIt->first);
        const WorldObjectInstance* refinery =
            findObjectById(state, taskIt->second.refineryId);
        const bool workerOwned = worker != nullptr
            && runtimeCanonicalHumanHouseOwner(worker->owner) == currentOwner;
        const bool refineryOwned = refinery != nullptr
            && runtimeCanonicalHumanHouseOwner(refinery->owner) == currentOwner;
        if (!workerOwned && !refineryOwned) {
            if (worker == nullptr && refinery == nullptr) {
                taskIt = state.slaveMinerTasks.erase(taskIt);
            } else {
                ++taskIt;
            }
            continue;
        }
        if (worker == nullptr || worker->health <= 0
            || (!worker->alive && !runtimeSlaveMinerWorkerIsStored(state, *worker))) {
            if (refinery != nullptr && refinery->alive) {
                if (const BuildingType* building = runtimeSlaveMinerParentBuilding(state, *refinery)) {
                    int& regenTicks =
                        state.slaveMinerRegenTicks[refinery->instanceId];
                    if (regenTicks <= 0) {
                        regenTicks =
                            runtimeSlaveMinerWorkerRegenTicks(state, *building);
                    }
                }
            }
            taskIt = state.slaveMinerTasks.erase(taskIt);
        } else if (refinery == nullptr
            || !refinery->alive
            || !runtimeSlaveMinerParentBuilding(state, *refinery)
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(refinery->owner)) {
            lostRefineryIds.insert(taskIt->second.refineryId);
            ++taskIt;
        } else {
            ++taskIt;
        }
    }
    for (int refineryId : lostRefineryIds) {
        releaseRuntimeSlaveMinerWorkersForRefinery(
            state,
            refineryId,
            "refinery gone", nullptr, miningEconomy
        );
    }
    if (!lostRefineryIds.empty() && !state.drainingObjectExits)
        (void)drainRuntimeObjectExitQueue(state, miningEconomy);
}

bool runtimeSlaveMinerWorkerAtRefinery(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& refinery) {
    // A committed navigation cell is ahead of the walking body. Both bodies
    // must have finished their real segments before cargo can change hands.
    if (worker.renderMoveActive || refinery.renderMoveActive
        || worker.level != refinery.level || worker.onBridge != refinery.onBridge)
        return false;
    const auto bodyCell = [](const WorldObjectInstance& object) {
        return worldObjectHasGroundRenderPose(object)
            ? RuntimeCellPoint{static_cast<int>(std::lround(object.renderX)),
                static_cast<int>(std::lround(object.renderY))}
            : RuntimeCellPoint{object.x, object.y};
    };
    if (runtimeObjectIsMobileSlaveMiner(state, refinery))
        return runtimeCellPointForObject(worker) == runtimeCellPointForObject(refinery)
            && bodyCell(worker) == bodyCell(refinery);
    return runtimeCellDistanceToObjectFootprint(
            state, runtimeCellPointForObject(worker), refinery) == 0.0
        && runtimeCellDistanceToObjectFootprint(state, bodyCell(worker), refinery) == 0.0;
}

bool issueRuntimeSlaveMinerReturn(
    RuntimeProductionHostState& state,
    WorldObjectInstance& worker,
    const WorldObjectInstance& refinery, const std::string* owner) {
    worker.mission = "Return";
    const auto entry = runtimeObjectIsMobileSlaveMiner(state, refinery)
        ? std::optional<RuntimeCellPoint>(runtimeCellPointForObject(refinery))
        : findRuntimeEngineerEntryCell(state, worker, refinery, "slave-delivery", 2);
    if (entry && issueRuntimeHarvesterMoveToCell(
            state, worker, *entry, "Return", nullptr, refinery.instanceId, true, owner)) return true;

    // A blocked return still replaces an old attack/entry order. Finish any
    // already committed walking segment instead of snapping the body forward.
    std::optional<RuntimeObjectBehavior> continuation;
    if (const auto old = state.behaviors.find(worker.instanceId);
        old != state.behaviors.end() && old->second.moveCooldownTicks > 0)
        continuation = old->second;
    eraseRuntimeBehavior(state, worker.instanceId);
    if (continuation) {
        RuntimeObjectBehavior waiting;
        waiting.kind = RuntimeBehaviorKind::Move;
        waiting.state = RuntimeBehaviorState::Queued;
        waiting.moveTarget = runtimeCellPointForObject(worker);
        waiting.weaponCooldownTicks = continuation->weaponCooldownTicks;
        inheritRuntimeVehicleMoveSegment(worker, waiting, *continuation);
        state.behaviors[worker.instanceId] = std::move(waiting);
    }
    worker.mission = "Return";
    return false;
}

bool advanceRuntimeSlaveMinerWorkerTask(
    RuntimeProductionHostState& state,
    WorldObjectInstance& worker,
    RuntimeSlaveMinerTask& task, HouseEconomyContext& context, RuntimeEconomyState& feedback) {
    WorldObjectInstance* refinery =
        findMutableObjectById(state, task.refineryId);
    if (refinery == nullptr || !refinery->alive) {
        return false;
    }

    const bool mobile = runtimeObjectIsMobileSlaveMiner(state, *refinery);
    const BuildingType* building = runtimeSlaveMinerParentBuilding(state, *refinery);
    const UnitType* workerType = findRuntimeUnitType(state.rules, worker);
    if (building == nullptr || workerType == nullptr || !workerType->slaved) {
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Stored) {
        if (mobile) {
            synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            return true;
        }
        if (refinery->buildAnimTicksRemaining > 0) return false;
        const auto spawn = findRuntimeSlaveMinerWorkerSpawnCell(state, *refinery, worker.type);
        if (!spawn) return false;
        clearRuntimeRenderPosition(worker);
        worker.alive = true;
        worker.mission = "Harvest";
        setRuntimeObjectCell(state, worker, *spawn);
        assignRuntimeInfantrySubcellAtCurrentLocation(state, worker);
        task.phase = task.cargoUnits > 0
            ? RuntimeSlaveMinerPhase::Returning : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = "released after deployment";
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
        return true;
    }
    if (!worker.alive || worker.health <= 0 || worker.harvestAutoSearchSuspended)
        return false;

    const int cargoCapacity = runtimeSlaveMinerWorkerCapacity(*workerType);
    task.cargoUnits = std::clamp(task.cargoUnits, 0, cargoCapacity);
    task.cargoGemUnits =
        std::clamp(task.cargoGemUnits, 0, task.cargoUnits);
    task.cargoValue = std::max(0, task.cargoValue);

    if (const auto behavior = state.behaviors.find(worker.instanceId);
        behavior != state.behaviors.end()) {
        if (mobile && task.phase == RuntimeSlaveMinerPhase::Returning
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && (behavior->second.pendingAllowedOccupiedObjectId != refinery->instanceId
                || behavior->second.moveTarget != runtimeCellPointForObject(*refinery))) {
            // The home can move after the worker committed its old approach.
            // Reuse the return order to retain that segment and follow the home.
            return issueRuntimeSlaveMinerReturn(state, worker, *refinery, &context.owner);
        }
        return false;
    }

    if (task.phase == RuntimeSlaveMinerPhase::Returning
        && runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
        if (!mobile && refinery->buildAnimTicksRemaining > 0) return false;
        if (task.cargoValue <= 0) {
            task.cargoUnits = task.cargoGemUnits = 0;
            task.workTicksRemaining = 0;
            if (mobile) {
                eraseRuntimeBehavior(state, worker.instanceId);
                task.phase = RuntimeSlaveMinerPhase::Stored;
                task.targetOre = {-1, -1};
                task.status = "returned empty to mobile miner";
                synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
                refreshRuntimeObjectOccupancyAfterLocalChange(state);
            } else {
                task.phase = RuntimeSlaveMinerPhase::SeekingOre;
                task.status = "returned empty";
                worker.mission = "Harvest";
            }
            return true;
        }
        int purifierCount = 0;
        const int purifierBonus = runtimeHarvesterHousePurifierBonus(
            state, context, feedback,
            task.cargoValue,
            &purifierCount
        );
        const int deliveredValue = task.cargoValue + purifierBonus;
        context.credits = runtimeCreditBalanceAfterDeposit(context.credits, deliveredValue);
        recordRuntimeHouseResourceDelivery(
            state, context, feedback,
            task.cargoUnits,
            task.cargoGemUnits,
            deliveredValue
        );
        ++context.resourceTrips;
        feedback.lastResourceBaseValue = task.cargoValue;
        feedback.lastResourcePurifierBonus = purifierBonus;
        feedback.lastResourcePurifierCount = purifierCount;
        if (!mobile) {
            spawnRuntimeRefinerySmokeEffects(state, *refinery, worker, &feedback);
            refinery->mission = "Harvest";
        }
        worker.mission = "Harvest";
        feedback.status =
            worker.type + " delivered $"
            + std::to_string(deliveredValue)
            + " to " + refinery->type
            + " credits=" + std::to_string(context.credits);
        task.cargoValue = 0;
        task.cargoUnits = 0;
        task.cargoGemUnits = 0;
        task.workTicksRemaining = 0;
        task.phase = mobile ? RuntimeSlaveMinerPhase::Stored : RuntimeSlaveMinerPhase::SeekingOre;
        task.status = mobile ? "delivered and stored" : "delivered";
        if (mobile) {
            task.targetOre = {-1, -1};
            synchronizeRuntimeStoredSlaveMinerWorker(state, worker, *refinery);
            refreshRuntimeObjectOccupancyAfterLocalChange(state);
        }
        return true;
    }

    const bool cargoFull = task.cargoUnits >= cargoCapacity;
    if (cargoFull || task.phase == RuntimeSlaveMinerPhase::Returning) {
        task.phase = RuntimeSlaveMinerPhase::Returning;
        task.status = "returning";
        return issueRuntimeSlaveMinerReturn(state, worker, *refinery, &context.owner);
    }

    const auto targetStillHasOre =
        [&state](const RuntimeCellPoint& cell) {
            return cell.x >= 0
                && cell.y >= 0
                && runtimeCellHasHarvestableResource(state, cell);
        };

    bool acquiredNewTarget = false;
    if (!targetStillHasOre(task.targetOre)) {
        const std::optional<RuntimeCellPoint> resourceCell =
            findNearestRuntimeStructureResourceCell(
                state,
                *refinery,
                runtimeSlaveMinerScanRadius(state), worker, &context.owner
            );
        if (!resourceCell.has_value()) {
            if (task.cargoValue > 0) {
                task.phase = RuntimeSlaveMinerPhase::Returning;
                task.status = "returning partial load: no ore";
                return issueRuntimeSlaveMinerReturn(state, worker, *refinery, &context.owner);
            }
            worker.mission = "Guard";
            task.phase = RuntimeSlaveMinerPhase::SeekingOre;
            task.status = "no ore";
            if (runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)) {
                const auto exit = findRuntimeSlaveMinerWorkerExitCell(state, *refinery, worker.type, &context.owner);
                if (exit.has_value()) return issueRuntimeHarvesterMoveToCell(
                    state, worker, *exit, "Move", nullptr, refinery->instanceId, true, &context.owner);
            }
            return false;
        }
        task.targetOre = *resourceCell;
        task.phase = RuntimeSlaveMinerPhase::MovingToOre;
        acquiredNewTarget = true;
    }

    const RuntimeCellPoint workerCell = runtimeCellPointForObject(worker);
    if (runtimeCellDistanceToObjectFootprint(state, workerCell, *refinery) == 0.0) {
        // Unloading permission is not a mining position. Retain the original
        // ore goal while leaving the foundation; native path/subcell checks
        // may wait for other workers, but must not finish at the home cell or
        // replace this departure with a vehicle's adjacent intake approach.
        task.phase = RuntimeSlaveMinerPhase::MovingToOre;
        task.status = "leaving refinery for ore";
        RuntimeObjectBehavior departure;
        departure.kind = RuntimeBehaviorKind::Move;
        departure.state = RuntimeBehaviorState::Queued;
        departure.moveTarget = task.targetOre;
        eraseRuntimeBehavior(state, worker.instanceId);
        worker.mission = "Harvest";
        state.behaviors[worker.instanceId] = std::move(departure);
        auto& queued = state.behaviors.at(worker.instanceId);
        submitRuntimeMovePathRequest(state, worker, queued, workerCell,
            queued.moveTarget, refinery->instanceId, nullptr, false);
        return true;
    }
    if (!runtimeSlaveMinerWorkerAtRefinery(state, worker, *refinery)
        && RuntimeHarvesterResourceAccess(state, worker, &context.owner).canGatherFrom(workerCell, task.targetOre)) {
        if (acquiredNewTarget || task.phase == RuntimeSlaveMinerPhase::MovingToOre
            || task.phase == RuntimeSlaveMinerPhase::SeekingOre) {
            task.workTicksRemaining =
                runtimeSlaveMinerWorkerHarvestTicks(
                    state,
                    *workerType,
                    *building
                );
        }
        if (!alignRuntimeHarvesterToResource(
                state,
                worker,
                task.targetOre
            )) {
            worker.mission = "HarvestAlign";
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.status = "aligning to ore";
            return true;
        }
        if (task.workTicksRemaining > 0) {
            --task.workTicksRemaining;
            worker.mission = "HarvestLoad";
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.status = "digging";
            return true;
        }

        bool wasGem = false;
        const int value =
            consumeRuntimeOreAtCell(state, task.targetOre, &wasGem);
        if (value <= 0) {
            task.targetOre = RuntimeCellPoint{};
            task.phase =
                task.cargoValue > 0
                    ? RuntimeSlaveMinerPhase::Returning
                    : RuntimeSlaveMinerPhase::SeekingOre;
            task.status =
                task.cargoValue > 0
                    ? "returning partial load: ore depleted"
                    : "ore depleted";
            return true;
        }

        task.cargoValue += value;
        ++task.cargoUnits;
        if (wasGem) {
            ++task.cargoGemUnits;
        }
        ++feedback.slaveMinerLoads;
        worker.mission = "HarvestLoad";
        task.status =
            "loaded " + std::to_string(task.cargoUnits)
            + "/" + std::to_string(cargoCapacity);
        // The worker's HarvestLoad mission plays the original Shovel strip.
        if (task.cargoUnits >= cargoCapacity) {
            task.phase = RuntimeSlaveMinerPhase::Returning;
            task.workTicksRemaining = 0;
        } else {
            task.phase = RuntimeSlaveMinerPhase::Harvesting;
            task.workTicksRemaining =
                runtimeSlaveMinerWorkerHarvestTicks(
                    state,
                    *workerType,
                    *building
                );
        }
        return true;
    }

    task.phase = RuntimeSlaveMinerPhase::MovingToOre;
    task.status =
        "moving to ore "
        + std::to_string(task.targetOre.x)
        + ","
        + std::to_string(task.targetOre.y);
    // A worker leaving the unloading point may cross its own refinery's
    // foundation, but must walk back out to the ore before digging again.
    if (issueRuntimeHarvesterMoveToCell(
            state, worker, task.targetOre, "Harvest", nullptr, refinery->instanceId, true, &context.owner)) return true;
    // A depleted edge or a blocked deposit must not keep the worker attached
    // to an unreachable ore cell forever. The next search checks real access.
    task.targetOre = {-1, -1};
    task.phase = RuntimeSlaveMinerPhase::SeekingOre;
    return false;
}

bool advanceRuntimeSlaveMinerEconomy(RuntimeProductionHostState& state,
    HouseEconomyContext& context, RuntimeEconomyState& feedback) {
    RuntimePerfTraceScope trace("advanceRuntimeSlaveMinerEconomy", state.tick);
    cleanRuntimeSlaveMinerTasks(state, context.computerControlled ? &feedback : nullptr);

    std::vector<int> refineryIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive || object.health <= 0
            || runtimeCanonicalHumanHouseOwner(object.owner)
                != runtimeCanonicalHumanHouseOwner(context.owner)) {
            continue;
        }

        const BuildingType* building = runtimeSlaveMinerParentBuilding(state, object);
        if (building != nullptr) {
            refineryIds.push_back(object.instanceId);
        }
    }

    std::sort(refineryIds.begin(), refineryIds.end());
    bool changed = false;
    for (int refineryId : refineryIds) {
        const WorldObjectInstance* refinery = findObjectById(state, refineryId);
        const BuildingType* building =
            refinery == nullptr ? nullptr : runtimeSlaveMinerParentBuilding(state, *refinery);
        if (refinery == nullptr || building == nullptr) {
            continue;
        }
        changed =
            ensureRuntimeSlaveMinerWorkers(state, *refinery, *building, &context.owner)
            || changed;
    }

    std::vector<int> workerIds;
    workerIds.reserve(state.slaveMinerTasks.size());
    for (const auto& [workerId, task] : state.slaveMinerTasks) {
        workerIds.push_back(workerId);
    }
    std::sort(workerIds.begin(), workerIds.end());

    for (int workerId : workerIds) {
        WorldObjectInstance* worker = findMutableRuntimeObjectByIdIncludingInactive(state, workerId);
        auto taskIt = state.slaveMinerTasks.find(workerId);
        if (worker == nullptr
            || worker->health <= 0
            || (!worker->alive && !runtimeSlaveMinerWorkerIsStored(state, *worker))
            || runtimeCanonicalHumanHouseOwner(worker->owner)
                != runtimeCanonicalHumanHouseOwner(context.owner)
            || taskIt == state.slaveMinerTasks.end()) {
            continue;
        }
        changed =
            advanceRuntimeSlaveMinerWorkerTask(state, *worker, taskIt->second, context, feedback)
            || changed;
    }

    return changed;
}

bool advanceRuntimeSlaveMinerEconomy(RuntimeProductionHostState& state) {
    if (runtimeSlaveWorkerMiningDiagnosticOverride) return runtimeSlaveWorkerMiningDiagnosticOverride(state);
    auto context = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    return advanceRuntimeSlaveMinerEconomy(state, context, state.economy);
}

int runtimeHarvesterHousePurifierBonus(const RuntimeGameSessionState& state,
    const HouseEconomyContext& context, const RuntimeEconomyState& metrics,
    int baseValue, int* purifierCountOutput) {
    int owned = 0;
    const bool lowPower = metrics.powerProduced - metrics.powerUsed < 0
        || runtimeDiskDrainOwnerBlackout(state, context.owner)
        || state.spies.blackoutUntilTick(runtimeRepairDepotCanonicalOwnerId(state, context.owner)) > state.tick;
    if (!lowPower) for (const auto& object : state.world.getObjects()) {
        if (!object.alive || object.owner != context.owner || object.kind != WorldObjectKind::Structure) continue;
        const auto* building = findRuntimeBuildingTypeById(state.rules, object.type);
        if (building && building->orePurifier) ++owned;
    }
    int virtualCount = 0;
    if (context.computerControlled) {
        if (state.campaignMode) {
            const std::array<int, 3> campaignCounts{8, 4, 2};
            virtualCount = campaignCounts[runtimeComputerMiningDifficultyIndex(state.config.campaignDifficulty)];
        } else {
            const auto ai = state.computerAiByOwner.find(runtimeRepairDepotCanonicalOwnerId(state, context.owner));
            if (ai != state.computerAiByOwner.end()) virtualCount = state.rules.getComputerMiningRules().virtualPurifiers[
                runtimeComputerMiningDifficultyIndex(ai->second.difficulty)];
        }
    }
    const int count = owned + virtualCount;
    if (purifierCountOutput) *purifierCountOutput = count;
    return houseOrePurifierBonusValue(baseValue, count, state.rules.getPurifierBonus());
}

bool advanceRuntimeHarvesterEconomy(RuntimeProductionHostState& state, HouseEconomyContext& context, RuntimeEconomyState& feedback) {
    RuntimePerfTraceScope trace("advanceRuntimeHarvesterEconomy", state.tick);
    const std::string currentOwner =
        runtimeCanonicalHumanHouseOwner(context.owner);
    for (auto taskIt = state.harvesterTasks.begin();
        taskIt != state.harvesterTasks.end();) {
        const WorldObjectInstance* taskObject =
            state.objectIndex.find(state.world.getObjects(), taskIt->first);
        // Contained vehicles are hidden, but their ore remains persistent cargo.
        if (taskObject == nullptr || taskObject->health <= 0
            || (!taskObject->alive && toLowerAsciiLocal(trimAsciiLocal(taskObject->mission)) != "insidetransport")) {
            taskIt = state.harvesterTasks.erase(taskIt);
        } else if (runtimeCanonicalHumanHouseOwner(taskObject->owner)
                == currentOwner
            && !runtimeObjectIsHarvester(state, *taskObject)) {
            taskIt = state.harvesterTasks.erase(taskIt);
        } else {
            ++taskIt;
        }
    }
    if (context.harvesters <= 0) {
        return false;
    }

    bool changed = false;
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive
            || object.owner != context.owner
            || !runtimeObjectIsHarvester(state, object)
            || object.harvestAutoSearchSuspended
            || state.behaviors.find(object.instanceId) != state.behaviors.end()) {
            continue;
        }

        RuntimeHarvesterTask& task = state.harvesterTasks[object.instanceId];
        normalizeRuntimeHarvesterTask(state, object, task);
        if (task.phase == RuntimeHarvesterPhase::Docking
            || task.phase == RuntimeHarvesterPhase::PreparingToUnload
            || task.phase == RuntimeHarvesterPhase::Unloading) {
            if (findRuntimeHarvesterRefineryById(state, object, task.targetRefineryId, &context.owner) == nullptr
                || !runtimeHarvesterIsAtDock(object, task)) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.targetRefineryId = -1;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick;
                task.status = "refinery lost before delivery";
                object.mission = "Harvest";
                changed = true;
            }
        }
        if ((task.phase == RuntimeHarvesterPhase::Idle
                || task.phase == RuntimeHarvesterPhase::SeekingOre)
            && task.cargoValue <= 0
            && task.nextThinkTick > state.tick) {
            continue;
        }
        if ((task.phase == RuntimeHarvesterPhase::ReturningToRefinery
                || task.phase == RuntimeHarvesterPhase::WaitingForRefineryDock)
            && task.nextThinkTick > state.tick
            && !runtimeHarvesterIsAtDock(object, task)) {
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::Loading) {
            const int intakeFacing = runtimeFacingFromDelta(
                task.targetOre.x - object.x, task.targetOre.y - object.y);
            if (intakeFacing >= 0
                && runtimeFacingAbsDifference(object.facing, intakeFacing) > 4) {
                // A restored task or another order may have changed the hull
                // facing. Realign the hull before restarting the pickup.
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick + 1;
                task.status = "realigning intake";
                object.mission = "HarvestAlign";
                changed = true;
                continue;
            }
            if (task.workTicksRemaining > 0) {
                --task.workTicksRemaining;
                object.mission = "HarvestLoad";
                task.status = "loading ore";
                changed = true;
                if (task.workTicksRemaining > 0) continue;
            }

            // Consume only the cell that the intake was aligned to. If another
            // miner exhausted it, select and align a new target on the next tick.
            const std::optional<RuntimeCellPoint> resourceCell =
                RuntimeHarvesterResourceAccess(state, object, &context.owner).canGatherFrom(
                    runtimeCellPointForObject(object), task.targetOre)
                    ? std::make_optional(task.targetOre) : std::nullopt;
            if (!resourceCell.has_value()) {
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.status = "ore cell unavailable";
                task.nextThinkTick = state.tick + 1;
                changed = true;
                continue;
            }

            const TerrainCell* beforeGatherTerrain =
                findRuntimeTerrainCellAt(state, *resourceCell);
            const int beforeGatherBails =
                beforeGatherTerrain == nullptr
                    ? 0
                    : runtimeResourceBailCountForTerrain(*beforeGatherTerrain);
            const std::string beforeGatherOverlay =
                beforeGatherTerrain == nullptr
                    ? "none"
                    : runtimeResourceOverlayDiagnosticName(
                        *beforeGatherTerrain
                    );
            bool wasGem = false;
            const int value =
                consumeRuntimeOreAtCell(state, *resourceCell, &wasGem);
            if (value <= 0) {
                task.phase =
                    task.cargoValue > 0
                        ? RuntimeHarvesterPhase::ReturningToRefinery
                        : RuntimeHarvesterPhase::SeekingOre;
                task.status = "ore disappeared";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                changed = true;
                continue;
            }
            const TerrainCell* afterGatherTerrain =
                findRuntimeTerrainCellAt(state, *resourceCell);
            feedback.lastHarvesterGatherCell = *resourceCell;
            feedback.lastHarvesterGatherBeforeBails =
                beforeGatherBails;
            feedback.lastHarvesterGatherAfterBails =
                afterGatherTerrain == nullptr
                    ? 0
                    : runtimeResourceBailCountForTerrain(*afterGatherTerrain);
            feedback.lastHarvesterGatherOverlayBefore =
                beforeGatherOverlay;
            feedback.lastHarvesterGatherOverlayAfter =
                afterGatherTerrain == nullptr
                    ? "none"
                    : runtimeResourceOverlayDiagnosticName(
                        *afterGatherTerrain
                    );

            task.cargoValue += value;
            ++task.cargoUnits;
            if (wasGem) {
                ++task.cargoGemUnits;
            }
            task.cargoHasGems = task.cargoHasGems || wasGem;
            task.status =
                "loaded " + std::to_string(task.cargoUnits)
                + "/" + std::to_string(task.cargoCapacity)
                + " cargo $" + std::to_string(task.cargoValue);
            feedback.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " " + task.status;
            changed = true;

            if (task.cargoUnits >= task.cargoCapacity) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.workTicksRemaining = 0;
            } else if (afterGatherTerrain != nullptr
                && runtimeResourceBailCountForTerrain(*afterGatherTerrain) > 0) {
                // Restart at the pickup boundary, without adding one/two
                // simulation ticks per bail for an unchanged intake target.
                task.phase = RuntimeHarvesterPhase::Loading;
                task.workTicksRemaining = runtimeHarvesterLoadTicks(state, object);
                spawnRuntimeHarvesterGatherEffect(state, object, *resourceCell);
            } else {
                // Keep mining this deposit until it is depleted. Re-ranking
                // neighboring cells after each bail caused needless body turns.
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.workTicksRemaining = 0;
                task.nextThinkTick = state.tick + 1;
            }
            changed = true;
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::Docking) {
            int refineryDockFacing = runtimeFacingFromDelta(1, 0);
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                    refineryDockFacing =
                        runtimeHarvesterRefineryDockFacing(
                            state,
                            *refinery,
                            task.dockCell
                        );
                }
            }

            const int currentFacing =
                object.facing >= 0 ? object.facing : refineryDockFacing;
            object.facing = runtimeAdvanceBodyFacing(state, object, currentFacing, refineryDockFacing);
            object.mission = "Dock";
            task.status = "docking at refinery";
            feedback.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " docking at refinery";
            if (runtimeFacingAbsDifference(
                    object.facing,
                    refineryDockFacing
                ) <= 4) {
                object.facing = refineryDockFacing;
                task.phase = RuntimeHarvesterPhase::PreparingToUnload;
                task.workTicksRemaining = runtimeHarvesterUnloadTicks(state);
                task.status = "preparing to unload";
            }
            changed = true;
            continue;
        }

        if (task.phase == RuntimeHarvesterPhase::PreparingToUnload) {
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                }
            }
            if (task.workTicksRemaining > 0) {
                --task.workTicksRemaining;
                object.mission = "Unload";
                task.status = "preparing to unload";
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " preparing to unload $"
                    + std::to_string(task.cargoValue);
                changed = true;
                if (task.workTicksRemaining > 0) continue;
            }
            task.phase = RuntimeHarvesterPhase::Unloading;
            task.status = "unloading at refinery";
            changed = true;
        }

        if (task.phase == RuntimeHarvesterPhase::Unloading) {
            // Retail TiberiumStorage unloads the first nonempty resource type
            // in full, then restarts its dump timer for the next type. Our two
            // cargo classes are Riparius (ore) followed by Cruentus (gems).
            const int oreUnits = task.cargoUnits - task.cargoGemUnits;
            const bool oreBatch = oreUnits > 0;
            const int batchUnits = oreBatch ? oreUnits : task.cargoGemUnits;
            const int batchGemUnits = oreBatch ? 0 : task.cargoGemUnits;
            const bool finalBatch = batchUnits >= task.cargoUnits;
            const int batchValue = finalBatch ? task.cargoValue : std::clamp(
                static_cast<int>(std::min<long long>(
                    static_cast<long long>(oreUnits)
                        * runtimeTiberiumBailValue(state.rules, "Riparius", 25),
                    std::numeric_limits<int>::max())),
                0, task.cargoValue);
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive
                    && refinery->kind == WorldObjectKind::Structure) {
                    refinery->mission = "Unload";
                    if (task.cargoValue > 0) {
                        spawnRuntimeRefinerySmokeEffects(
                            state,
                            *refinery,
                            object, &feedback
                        );
                    }
                }
            }
            if (batchValue > 0) {
                int purifierCount = 0;
                const int purifierBonus = runtimeHarvesterHousePurifierBonus(
                    state, context, feedback,
                    batchValue,
                    &purifierCount
                );
                const int deliveredValue = batchValue + purifierBonus;
                context.credits = runtimeCreditBalanceAfterDeposit(context.credits, deliveredValue);
                recordRuntimeHouseResourceDelivery(
                    state, context, feedback,
                    batchUnits,
                    batchGemUnits,
                    deliveredValue
                );
                if (finalBatch) ++context.resourceTrips;
                feedback.lastResourceBaseValue = batchValue;
                feedback.lastResourcePurifierBonus = purifierBonus;
                feedback.lastResourcePurifierCount = purifierCount;
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " delivered $" + std::to_string(deliveredValue)
                    + " base=$" + std::to_string(batchValue)
                    + " purifier=$" + std::to_string(purifierBonus)
                    + " credits=" + std::to_string(context.credits);
                emitRuntimeSound(state, "harvest unload");
            }
            task.cargoValue -= batchValue;
            task.cargoUnits -= batchUnits;
            task.cargoGemUnits -= batchGemUnits;
            task.cargoHasGems = task.cargoGemUnits > 0;
            if (!finalBatch && task.cargoUnits > 0 && task.cargoValue > 0) {
                task.phase = RuntimeHarvesterPhase::PreparingToUnload;
                task.workTicksRemaining = runtimeHarvesterUnloadTicks(state);
                task.status = "preparing next resource type";
                changed = true;
                continue;
            }
            task.cargoValue = 0;
            task.cargoUnits = 0;
            task.cargoGemUnits = 0;
            task.cargoHasGems = false;
            task.phase = RuntimeHarvesterPhase::SeekingOre;
            task.workTicksRemaining = 0;
            task.nextThinkTick = state.tick + 1;
            task.status = "delivered cargo";
            object.mission = "Guard";
            if (WorldObjectInstance* refinery =
                    findMutableObjectById(state, task.targetRefineryId)) {
                if (refinery->alive && refinery->mission == "Unload") {
                    refinery->mission = "Guard";
                }
            }
            changed = true;
            continue;
        }

        const bool cargoFull =
            task.cargoUnits >= task.cargoCapacity;
        if (cargoFull
            || task.phase == RuntimeHarvesterPhase::ReturningToRefinery
            || task.phase == RuntimeHarvesterPhase::WaitingForRefineryDock) {
            if (context.refineries <= 0) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status = "waiting for refinery";
                object.mission = "Guard";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " has cargo but no refinery";
                changed = true;
                continue;
            }

            const WorldObjectInstance* refinery =
                findRuntimeHarvesterRefineryById(
                    state,
                    object,
                    task.targetRefineryId, &context.owner
                );
            if (refinery == nullptr) {
                const std::optional<int> refineryId =
                    findNearestRuntimeRefineryId(state, object, &context.owner);
                task.targetRefineryId = refineryId.value_or(-1);
                refinery = findRuntimeHarvesterRefineryById(
                    state,
                    object,
                    task.targetRefineryId, &context.owner
                );
            }
            if (refinery == nullptr) {
                task.status = "no refinery found";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                changed = true;
                continue;
            }

            const double tooFarThreshold =
                runtimeHarvesterTooFarDistanceForObject(state, object);
            const double refineryDistance =
                runtimeCellDistance(
                    runtimeCellPointForObject(object),
                    runtimeCellPointForObject(*refinery)
                );
            if (tooFarThreshold > 0.0
                && refineryDistance > tooFarThreshold
                && !runtimeHarvesterIsAtDock(object, task)) {
                std::optional<RuntimeCellPoint> approachTarget =
                    findRuntimeRefineryQueueCell(state, object, *refinery);
                if (!approachTarget.has_value()) {
                    approachTarget =
                        findRuntimePassableCellNear(
                            state,
                            object,
                            runtimeCellPointForObject(*refinery),
                            8,
                            -1
                        );
                }
                if (approachTarget.has_value()) {
                    std::optional<RuntimeCellPoint> actualTarget;
                    if (issueRuntimeHarvesterMoveToCell(
                            state,
                            object,
                            *approachTarget,
                            "Return",
                            &actualTarget,
                            -1,
                            true, &context.owner
                        )) {
                        task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                        task.dockCell = RuntimeCellPoint{};
                        task.queueCell =
                            actualTarget.value_or(*approachTarget);
                        task.status =
                            "approaching refinery #"
                            + std::to_string(task.targetRefineryId);
                        task.nextThinkTick = state.tick + 1;
                        feedback.lastHarvesterTooFarApproachTarget =
                            task.queueCell;
                        feedback.lastHarvesterTooFarDistance =
                            refineryDistance;
                        feedback.lastHarvesterTooFarThreshold =
                            tooFarThreshold;
                        feedback.lastHarvesterTooFarChrono =
                            runtimeObjectIsChronoHarvester(state, object);
                        feedback.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " approaching refinery before dock";
                        changed = true;
                        continue;
                    }
                }
            }

            const std::optional<RuntimeCellPoint> dockCell =
                findRuntimeRefineryDockCell(state, object, *refinery);
            if (dockCell.has_value()) {
                task.dockCell = *dockCell;
                task.queueCell = RuntimeCellPoint{};
            } else if (!runtimeHarvesterIsAtDock(object, task)) {
                task.dockCell = runtimeRefineryDefaultDockCell(state, *refinery);
                const std::optional<RuntimeCellPoint> queueCell =
                    findRuntimeRefineryQueueCell(state, object, *refinery);
                if (queueCell.has_value()) {
                    task.queueCell = *queueCell;
                    if (runtimeCellPointForObject(object) == *queueCell) {
                        task.phase =
                            RuntimeHarvesterPhase::WaitingForRefineryDock;
                        task.status = "waiting for refinery dock";
                        task.nextThinkTick =
                            state.tick + runtimeHarvesterThinkCooldownTicks(state);
                        object.mission = "Return";
                        feedback.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " waiting for refinery dock";
                        changed = true;
                        continue;
                    }

                    std::optional<RuntimeCellPoint> actualQueueTarget;
                    if (issueRuntimeHarvesterMoveToCell(
                            state,
                            object,
                            *queueCell,
                            "Return",
                            &actualQueueTarget,
                            -1,
                            true, &context.owner
                        )) {
                        if (actualQueueTarget.has_value()) {
                            task.queueCell = *actualQueueTarget;
                        }
                        task.phase =
                            RuntimeHarvesterPhase::WaitingForRefineryDock;
                        task.status =
                            "queueing for refinery #"
                            + std::to_string(task.targetRefineryId);
                        task.nextThinkTick = state.tick + 1;
                        feedback.status =
                            "harvester #"
                            + std::to_string(object.instanceId)
                            + " queueing for refinery dock";
                        changed = true;
                        continue;
                    }
                }

                task.status = "refinery dock blocked";
                task.nextThinkTick =
                    state.tick + runtimeHarvesterThinkCooldownTicks(state);
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " cannot dock at " + refinery->type;
                changed = true;
                continue;
            }

            if (runtimeHarvesterIsAtDock(object, task)) {
                task.phase = RuntimeHarvesterPhase::Docking;
                task.workTicksRemaining = 0;
                task.status = "dock reached";
                object.mission = "Dock";
                if (WorldObjectInstance* mutableRefinery =
                        findMutableObjectById(state, task.targetRefineryId)) {
                    mutableRefinery->mission = "Unload";
                }
                changed = true;
                continue;
            }

            if (runtimeChronoHarvesterCanTeleportToCell(
                    state,
                    object,
                    task.dockCell,
                    task.targetRefineryId
                )) {
                if (!teleportRuntimeHarvesterToCell(
                        state,
                        object,
                        task.dockCell, &context.harvesterTeleports
                    )) {
                    task.status = "chrono parasite release rejected";
                    changed = true;
                    continue;
                }
                task.phase = RuntimeHarvesterPhase::Docking;
                task.workTicksRemaining = 0;
                task.status = "chrono teleported to refinery";
                object.mission = "Dock";
                if (WorldObjectInstance* mutableRefinery =
                        findMutableObjectById(state, task.targetRefineryId)) {
                    if (mutableRefinery->alive
                        && mutableRefinery->kind == WorldObjectKind::Structure) {
                        mutableRefinery->mission = "Unload";
                    }
                }
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " chrono teleported to refinery";
                changed = true;
                continue;
            }

            std::optional<RuntimeCellPoint> actualTarget;
            if (issueRuntimeHarvesterMoveToCell(
                    state,
                    object,
                    task.dockCell,
                    "Return",
                    &actualTarget,
                    task.targetRefineryId,
                    true, &context.owner
                )) {
                if (actualTarget.has_value()) {
                    task.dockCell = *actualTarget;
                }
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status =
                    "returning to refinery #"
                    + std::to_string(task.targetRefineryId);
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " returning cargo $" + std::to_string(task.cargoValue);
                changed = true;
            }
            continue;
        }

        task.phase = RuntimeHarvesterPhase::SeekingOre;
        std::optional<RuntimeCellPoint> loadResourceCell =
            findHarvestableRuntimeResourceNear(state, object, task.targetOre, &context.owner);
        const std::optional<RuntimeCellPoint> scanOrigin =
            task.targetOre.x >= 0 && task.targetOre.y >= 0
                ? std::make_optional(task.targetOre)
                : std::nullopt;
        const std::optional<RuntimeCellPoint> oreCell = loadResourceCell.has_value()
            ? loadResourceCell : findNearestRuntimeOreCell(state, object, scanOrigin, true, &context.owner);
        if (!oreCell.has_value()) {
            if (task.cargoValue > 0) {
                task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
                task.status = "returning partial load: no ore found";
                task.nextThinkTick = state.tick + 1;
                object.mission = "Return";
                feedback.status =
                    "harvester #"
                    + std::to_string(object.instanceId)
                    + " returning partial cargo $"
                    + std::to_string(task.cargoValue);
                changed = true;
                continue;
            }
            task.status = "no ore found";
            task.nextThinkTick =
                state.tick + runtimeHarvesterThinkCooldownTicks(state);
            object.mission = "Guard";
            feedback.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " no ore found";
            changed = true;
            continue;
        }

        task.targetOre = *oreCell;
        if (!loadResourceCell.has_value()) {
            loadResourceCell = findHarvestableRuntimeResourceNear(state, object, task.targetOre, &context.owner);
        }
        if (loadResourceCell.has_value()) {
            // Store the actual adjacent resource before turning, including when
            // the original order referred to a more distant part of the field.
            task.targetOre = *loadResourceCell;
            if (!alignRuntimeHarvesterToResource(
                    state,
                    object,
                    *loadResourceCell
                )) {
                task.phase = RuntimeHarvesterPhase::SeekingOre;
                task.status = "aligning to ore";
                task.nextThinkTick = state.tick + 1;
                object.mission = "HarvestAlign";
                changed = true;
                continue;
            }
            task.phase = RuntimeHarvesterPhase::Loading;
            task.workTicksRemaining =
                runtimeHarvesterLoadTicks(state, object);
            task.status = "loading ore";
            task.nextThinkTick = state.tick + 1;
            object.mission = "HarvestLoad";
            spawnRuntimeHarvesterGatherEffect(state, object, *loadResourceCell);
            changed = true;
        } else if (issueRuntimeHarvesterMoveToCell(
                state,
                object,
                *oreCell,
                "Harvest", nullptr, -1, false, &context.owner
            )) {
            task.phase = RuntimeHarvesterPhase::MovingToOre;
            task.workTicksRemaining = 0;
            task.status =
                "moving to ore "
                + std::to_string(oreCell->x)
                + ","
                + std::to_string(oreCell->y);
            task.nextThinkTick = state.tick + 1;
            feedback.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " moving to ore "
                + std::to_string(oreCell->x)
                + ","
                + std::to_string(oreCell->y);
            changed = true;
        } else {
            task.status = "ore unreachable";
            task.nextThinkTick =
                state.tick + runtimeHarvesterThinkCooldownTicks(state);
            feedback.status =
                "harvester #"
                + std::to_string(object.instanceId)
                + " ore unreachable";
            changed = true;
        }
    }

    return changed;
}

bool advanceRuntimeHarvesterEconomy(RuntimeProductionHostState& state) {
    auto context = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    return advanceRuntimeHarvesterEconomy(state, context, state.economy);
}

bool advanceRuntimeProductionEconomy(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeProductionEconomy", state.tick);
    const auto resolveCatalog = [&state](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) -> std::optional<RuntimeProductionCatalogAdapter> {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, order.itemKey);
        if (!catalog.has_value()
            || !runtimeProductionOrderMatchesCatalogSnapshot(
                state,
                lane,
                order,
                *catalog
            )) {
            return std::nullopt;
        }
        return catalog;
    };
    const auto poweredCompatibleFactoryCount = [&state, &resolveCatalog](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        if (!catalog.has_value()) {
            return 0;
        }
        if (runtimeBuildItemUsesPadAircraft(state, *catalog->item)) {
            const std::optional<RuntimeAircraftProductionAssignment>
                assignment = runtimeAircraftProductionAssignmentForSerial(
                    state,
                    state.economy.owner,
                    order.serial
                );
            const WorldObjectInstance* host = assignment.has_value()
                ? findObjectById(state, assignment->hostId)
                : nullptr;
            const BuildingType* building = host == nullptr
                ? nullptr
                : findRuntimeBuildingTypeById(state.rules, host->type);
            if (!assignment.has_value()
                || !runtimeProductionItemKeysMatch(
                    assignment->typeId,
                    catalog->item->typeId
                )
                || host == nullptr
                || building == nullptr
                || (building->powered && !host->poweredOperational)) {
                return 0;
            }

            // The exact assigned host gates progress, but every additional
            // compatible powered aircraft factory still contributes the
            // authored MultipleFactory speed multiplier. Count structures,
            // not docks: NumberOfDocks is physical capacity, not a factory
            // multiplier.
            int compatiblePoweredHosts = 0;
            for (const WorldObjectInstance& candidate :
                    state.world.getObjects()) {
                const BuildingType* candidateBuilding =
                    findRuntimeBuildingTypeById(
                        state.rules,
                        candidate.type
                    );
                if (candidateBuilding == nullptr
                    || (candidateBuilding->powered
                        && !candidate.poweredOperational)
                    || !runtimeAircraftCanDockAtStructureForOwner(
                        state,
                        *catalog->item,
                        candidate,
                        state.economy.owner
                    )) {
                    continue;
                }
                ++compatiblePoweredHosts;
                if (compatiblePoweredHosts
                    == runtimeProductionTimingMaximumFactories) {
                    break;
                }
            }
            return compatiblePoweredHosts;
        }
        return static_cast<int>(findRuntimeFactoryAnchorCandidatesForOwner(
            state,
            *catalog->item,
            state.economy.owner,
            false
        ).size());
    };
    RuntimeProductionAdvanceHooks hooks;
    hooks.canStart = [&state, &resolveCatalog, &poweredCompatibleFactoryCount](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        return catalog.has_value()
            && evaluateRuntimeTechTreeDecision(
                state,
                *catalog->item
            ).canQueue
            && poweredCompatibleFactoryCount(lane, order) > 0;
    };
    hooks.canContinue = [&resolveCatalog, &poweredCompatibleFactoryCount](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        return resolveCatalog(lane, order).has_value()
            && poweredCompatibleFactoryCount(lane, order) > 0;
    };
    hooks.productionRate = [
        &state,
        &resolveCatalog,
        &poweredCompatibleFactoryCount
    ](
        RuntimeProductionLane lane,
        const RuntimeProductionLaneOrder& order
    ) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            resolveCatalog(lane, order);
        if (!catalog.has_value()) {
            return RuntimeProductionRate{0, 0};
        }
        RuntimeBuildCatalogItem pricedItem = *catalog->item;
        pricedItem.cost = order.cost;
        const RuntimeProductionTimingResult timing =
            runtimeProductionTimingForItem(
                state,
                pricedItem,
                poweredCompatibleFactoryCount(lane, order),
                static_cast<double>(state.economy.powerProduced),
                static_cast<double>(state.economy.powerUsed)
            );
        if (!timing.valid) {
            return RuntimeProductionRate{0, 0};
        }
        return runtimeSidebarProductionRate(pricedItem.kind, timing.productionRate);
    };
    hooks.completeImmediately = [&state](RuntimeProductionLane, const RuntimeProductionLaneOrder&) {
        return runtimeNoCooldownForOwner(state, state.economy.owner);
    };
    auto house = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    auto stage = advanceRuntimeProductionFixedStep(state.economy.localProductionLanes, house, hooks);
    if (!stage.accepted) {
        state.economy.status = "production ledger transaction rejected";
        return false;
    }
    bool changed = stage.changed;
    const auto& events = stage.events;

    for (const RuntimeProductionAdvanceEvent& event : events) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, event.itemKey);
        const RuntimeBuildCatalogItem* item =
            catalog.has_value() ? catalog->item : nullptr;
        switch (event.kind) {
        case RuntimeProductionAdvanceKind::Started:
            state.economy.status =
                "building " + event.itemKey
                + " ticks=" + std::to_string(event.remainingTicks);
            emitRuntimeSound(
                state,
                item != nullptr
                    && item->kind == RuntimeBuildKind::Infantry
                    ? runtimeSoundIntentTrainingStarted
                    : runtimeSoundIntentBuildStarted
            );
            break;
        case RuntimeProductionAdvanceKind::WaitingForCredits:
            state.economy.status =
                "waiting credits for " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::Blocked:
            state.economy.status =
                "production paused " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::Held:
            state.economy.status = "on hold " + event.itemKey;
            break;
        case RuntimeProductionAdvanceKind::PlacementReady:
            state.economy.status =
                event.itemKey
                + " ready: click sidebar icon to place; sidebar right-click cancels production";
            state.interactionStatus = state.economy.status;
            emitRuntimeSound(state, runtimeSoundIntentBuildComplete);
            break;
        default:
            break;
        }
    }

    clearRuntimeInvalidPlacementSelection(state);
    const std::vector<RuntimeProductionCompletedOrder> completed =
        runtimeEnumerateCompletedProduction(
            state.economy.localProductionLanes
        );
    for (const RuntimeProductionCompletedOrder& pending : completed) {
        const std::optional<RuntimeProductionCatalogAdapter> catalog =
            runtimeProductionCatalogLookup(state, pending.order.itemKey);
        if (!catalog.has_value()
            || catalog->item == nullptr
            || catalog->lane != pending.lane) {
            state.economy.status =
                "production completion catalog mismatch "
                + pending.order.itemKey;
            continue;
        }
        const std::optional<RuntimeProductionLaneOrder> preflight =
            runtimePeekCompletedProduction(
                state.economy.localProductionLanes,
                pending.lane
            );
        if (!preflight.has_value()
            || preflight->serial != pending.order.serial) {
            state.economy.status =
                "production completion serial changed before commit";
            continue;
        }
        if (!completeRuntimeProductionOrder(
                state,
                *catalog->item,
                false,
                pending.lane,
                pending.order.serial
            )) {
            continue;
        }
        const std::optional<RuntimeProductionLaneOrder> committed =
            runtimeCommitCompletedProduction(
                state.economy.localProductionLanes,
                pending.lane,
                pending.order.serial
            );
        changed = committed.has_value() || changed;
    }
    return changed;
}

void runtimeMultiplayerAuthoritativeMixEconomy(
    std::uint64_t& hash,
    const RuntimeEconomyState& economy) {
    runtimeStableHashMixString(
        hash,
        runtimeCanonicalHumanHouseOwner(economy.owner)
    );
    runtimeStableHashMix(hash, economy.techLevel);
    runtimeStableHashMix(hash, economy.credits);
    runtimeStableHashMix(hash, economy.powerProduced);
    runtimeStableHashMix(hash, economy.powerUsed);
    runtimeStableHashMix(hash, economy.harvesters);
    runtimeStableHashMix(hash, economy.refineries);
    runtimeStableHashMix(hash, economy.incomeCooldownTicks);
    runtimeStableHashMix(hash, economy.storedResourceUnits);
    runtimeStableHashMix(hash, economy.storedResourceValue);
    runtimeStableHashMix(hash, economy.storedResourceGemUnits);
    runtimeStableHashMix(hash, economy.resourceStorageCapacity);
    runtimeStableHashMix(hash, economy.lastResourceStoredUnits);
    runtimeStableHashMix(hash, economy.lastResourceStoredValue);
    runtimeStableHashMix(hash, economy.lastResourceStoredGemUnits);
    runtimeStableHashMix(hash, economy.lastResourceStorageOverflowUnits);
    runtimeStableHashMix(hash, economy.lastResourceStorageCapacity);

    std::vector<std::pair<std::string, std::uint64_t>> catalog;
    catalog.reserve(economy.catalog.size());
    for (const RuntimeBuildCatalogItem& item : economy.catalog) {
        catalog.emplace_back(
            runtimeMultiplayerCanonicalIdentifier(item.typeId),
            runtimeMultiplayerBuildCatalogItemHash(item)
        );
    }
    std::sort(catalog.begin(), catalog.end());
    runtimeStableHashMix(hash, static_cast<long long>(catalog.size()));
    for (const auto& [typeId, itemHash] : catalog) {
        runtimeStableHashMixString(hash, typeId);
        runtimeMultiplayerAuthoritativeMixUnsigned(hash, itemHash);
    }

    runtimeMultiplayerAuthoritativeMixBool(
        hash,
        economy.localProductionLanesInitialized
    );
    runtimeMultiplayerAuthoritativeMixUnsigned(
        hash,
        runtimeProductionLanesStateHash(economy.localProductionLanes)
    );
    runtimeMultiplayerAuthoritativeMixBool(
        hash,
        economy.selectedPlacementLane.has_value()
    );
    if (economy.selectedPlacementLane.has_value()) {
        runtimeStableHashMix(
            hash,
            static_cast<int>(*economy.selectedPlacementLane)
        );
    }
    runtimeMultiplayerAuthoritativeMixUnsigned(
        hash,
        economy.selectedPlacementSerial
    );

    std::vector<std::tuple<std::string, std::string, int>> buildHistory;
    for (const auto& [owner, typeCounts] :
            economy.limitedBuildHistoryByOwner) {
        for (const auto& [typeId, count] : typeCounts) {
            buildHistory.emplace_back(
                runtimeCanonicalHumanHouseOwner(owner),
                runtimeMultiplayerCanonicalIdentifier(typeId),
                count
            );
        }
    }
    std::sort(buildHistory.begin(), buildHistory.end());
    runtimeStableHashMix(
        hash,
        static_cast<long long>(buildHistory.size())
    );
    for (const auto& [owner, typeId, count] : buildHistory) {
        runtimeStableHashMixString(hash, owner);
        runtimeStableHashMixString(hash, typeId);
        runtimeStableHashMix(hash, count);
    }

    runtimeMultiplayerAuthoritativeMixCell(hash, economy.placementCell);
    runtimeMultiplayerAuthoritativeMixBool(hash, economy.placementValid);
    runtimeStableHashMix(hash, economy.completedBuildings);
    runtimeStableHashMix(hash, economy.completedUnits);
    runtimeStableHashMix(hash, economy.completedInfantry);
    runtimeStableHashMix(hash, economy.resourceTrips);
    runtimeStableHashMix(hash, economy.harvesterTeleports);
    runtimeStableHashMix(hash, economy.slaveMinerLoads);
    runtimeMultiplayerAuthoritativeMixBool(
        hash,
        economy.simplifiedHarvesterIncomeEnabled
    );
    runtimeMultiplayerAuthoritativeMixBool(hash, economy.skirmishStartApplied);
    runtimeStableHashMix(hash, economy.skirmishStartObjectId);
    runtimeMultiplayerAuthoritativeMixCell(hash, economy.skirmishStartCell);
}

std::string runtimeMultiplayerSnapshotEconomyScalars(
    const RuntimeEconomyState& economy) {
    std::ostringstream output;
    output << economy.techLevel << '|'
           << economy.credits << '|'
           << economy.powerProduced << '|'
           << economy.powerUsed << '|'
           << economy.harvesters << '|'
           << economy.refineries << '|'
           << economy.incomeCooldownTicks << '|'
           << economy.storedResourceUnits << '|'
           << economy.storedResourceValue << '|'
           << economy.storedResourceGemUnits << '|'
           << economy.resourceStorageCapacity << '|'
           << economy.lastResourceStoredUnits << '|'
           << economy.lastResourceStoredValue << '|'
           << economy.lastResourceStoredGemUnits << '|'
           << economy.lastResourceStorageOverflowUnits << '|'
           << economy.lastResourceStorageCapacity << '|'
           << economy.placementCell.x << '|'
           << economy.placementCell.y << '|'
           << runtimeBoolText(economy.placementValid) << '|'
           << economy.completedBuildings << '|'
           << economy.completedUnits << '|'
           << economy.completedInfantry << '|'
           << economy.resourceTrips << '|'
           << economy.harvesterTeleports << '|'
           << economy.slaveMinerLoads << '|'
           << runtimeBoolText(economy.simplifiedHarvesterIncomeEnabled)
           << '|'
           << runtimeBoolText(economy.skirmishStartApplied) << '|'
           << economy.skirmishStartObjectId << '|'
           << economy.skirmishStartCell.x << '|'
           << economy.skirmishStartCell.y;
    return output.str();
}

bool parseRuntimeMultiplayerSnapshotEconomyScalars(
    const std::string& value,
    RuntimeEconomyState& economy) {
    const std::vector<std::string> fields = splitRuntimeSaveFields(value);
    if (fields.size() != 30U) {
        return false;
    }
    std::size_t cursor = 0;
    const auto readInt = [&fields, &cursor](int& target) {
        return cursor < fields.size()
            && parseRuntimeInt(fields[cursor++], target);
    };
    const auto readBool = [&fields, &cursor](bool& target) {
        return cursor < fields.size()
            && parseRuntimeStrictBool(fields[cursor++], target);
    };
    return readInt(economy.techLevel)
        && readInt(economy.credits)
        && readInt(economy.powerProduced)
        && readInt(economy.powerUsed)
        && readInt(economy.harvesters)
        && readInt(economy.refineries)
        && readInt(economy.incomeCooldownTicks)
        && readInt(economy.storedResourceUnits)
        && readInt(economy.storedResourceValue)
        && readInt(economy.storedResourceGemUnits)
        && readInt(economy.resourceStorageCapacity)
        && readInt(economy.lastResourceStoredUnits)
        && readInt(economy.lastResourceStoredValue)
        && readInt(economy.lastResourceStoredGemUnits)
        && readInt(economy.lastResourceStorageOverflowUnits)
        && readInt(economy.lastResourceStorageCapacity)
        && readInt(economy.placementCell.x)
        && readInt(economy.placementCell.y)
        && readBool(economy.placementValid)
        && readInt(economy.completedBuildings)
        && readInt(economy.completedUnits)
        && readInt(economy.completedInfantry)
        && readInt(economy.resourceTrips)
        && readInt(economy.harvesterTeleports)
        && readInt(economy.slaveMinerLoads)
        && readBool(economy.simplifiedHarvesterIncomeEnabled)
        && readBool(economy.skirmishStartApplied)
        && readInt(economy.skirmishStartObjectId)
        && readInt(economy.skirmishStartCell.x)
        && readInt(economy.skirmishStartCell.y)
        && cursor == fields.size()
        && economy.techLevel >= 0
        && economy.credits >= 0
        && economy.powerProduced >= 0
        && economy.powerUsed >= 0
        && economy.harvesters >= 0
        && economy.refineries >= 0
        && economy.incomeCooldownTicks >= 0
        && economy.storedResourceUnits >= 0
        && economy.storedResourceValue >= 0
        && economy.storedResourceGemUnits >= 0
        && economy.resourceStorageCapacity >= 0;
}

bool advanceRuntimeEconomyForCurrentHumanHouse(
    RuntimeProductionHostState& state,
    bool refreshSecretLabCatalog) {
    if (state.remoteHumanHouseScopeActive) {
        refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    bool changed = false;
    changed = advanceRuntimeHarvesterEconomy(state) || changed;
    changed = advanceRuntimeMobileSlaveMinerEconomy(state) || changed;
    changed = advanceRuntimeSlaveMinerEconomy(state) || changed;
    changed = advanceRuntimeStructureRepairs(state) || changed;
    if (refreshSecretLabCatalog
        && addRuntimeSecretLabBuildCatalog(state) > 0) {
        emitRuntimeSound(state, runtimeSoundIntentNewConstructionOptions);
        changed = true;
    }

    auto house = makeRuntimeHouseEconomyContext(state.economy, state.computerMiningScopeActive);
    const auto incomeStage = advanceRuntimeHouseEconomyFixedStep(house,
        state.economy.simplifiedHarvesterIncomeEnabled, state.economy.incomeCooldownTicks);
    if (incomeStage.changed) {
        state.economy.status = incomeStage.status;
        changed = true;
    }

    changed = advanceRuntimeProductionEconomy(state) || changed;
    return changed;
}

bool advanceRuntimeEconomy(RuntimeProductionHostState& state) {
    bool changed = false;
    // Resource growth and object healing are world-global authored effects.
    // Advance them once before entering any per-house facade scope.
    changed = advanceRuntimeOreGrowth(state) || changed;
    changed = advanceRuntimeTiberiumHealing(state) || changed;
    changed = advanceRuntimeSelfHealing(state) || changed;
    changed = advanceRuntimeSpecialBuildings(state) || changed;

    if (state.humanHouseAuthorities.empty()) {
        changed = advanceRuntimeEconomyForCurrentHumanHouse(state, false) || changed;
        return advanceRuntimeComputerMining(state) || changed;
    }

    // This is the only full grid/UI refresh. Remote scopes use a metric-only
    // path, so their low-power state cannot drive the local sidebar or audio.
    rebuildRuntimeEconomyPower(state);
    bool remoteChanged = false;
    std::vector<std::string> owners;
    owners.reserve(state.humanHouseAuthorities.size());
    for (const RuntimeHumanHouseAuthority& authority :
            state.humanHouseAuthorities) {
        owners.push_back(authority.owner);
    }
    for (const std::string& owner : owners) {
        RuntimeHumanHouseScope scope(state, owner);
        if (!scope) {
            continue;
        }
        const bool ownerChanged = advanceRuntimeEconomyForCurrentHumanHouse(
            state,
            scope.remote()
        );
        remoteChanged = (scope.remote() && ownerChanged) || remoteChanged;
        changed = ownerChanged || changed;
    }
    if (remoteChanged) {
        rebuildRuntimeEconomyPower(state);
    }
    return advanceRuntimeComputerMining(state) || changed;
}

std::vector<RuntimeSpecialBuildingCredit>
collectRuntimeSpecialBuildingOwnerTransitionCredits(
    const RuntimeGameSessionState& state,
    const std::map<int, std::string>& initialOwners,
    const std::map<int, std::string>& finalOwners) {
    const RuntimeSpecialBuildingContext context =
        makeRuntimeSpecialBuildingContext(state);
    std::vector<RuntimeSpecialBuildingCredit> output;
    for (const auto& [objectId, newOwner] : finalOwners) {
        const auto previous = initialOwners.find(objectId);
        if (previous == initialOwners.end()) {
            continue;
        }
        const std::vector<RuntimeSpecialBuildingCredit> credits =
            RuntimeSpecialBuildingSystem::captureCredits(
                context,
                objectId,
                previous->second,
                newOwner,
                state.tick
            );
        output.insert(output.end(), credits.begin(), credits.end());
    }
    return output;
}

std::optional<RuntimeBuildCatalogItem> runtimeBuildItemForBuilding(
    const RuntimeGameSessionState& state,
    const std::string& buildingId, const TechTreeReport* technology) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, buildingId);
    if (building == nullptr) {
        return std::nullopt;
    }

    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Building;
    item.typeId = building->id;
    item.label = building->name.empty() ? building->id : building->name;
    item.prerequisite = building->prerequisite;
    item.requiredFactory = "BuildingType";
    item.cost = std::max(0, building->cost);
    item.powerDelta = building->power;
    item.owner = building->owner;
    item.techLevel = building->techLevel;
    item.buildLimit = building->buildLimit;
    item.waterBound = building->waterBound;
    item.naval = building->naval;
    item.wall = building->wall;
    item.buildTicks = runtimeBaseBuildTimeTicks(state, item);
    if (const TechTreeNode* node = runtimeFindTechTreeNode(
            technology ? *technology : state.techTree,
            building->id
        )) {
        item.techTreeKind = node->kind;
        item.techTreeAllowed = node->buildableByRules;
        item.techTreeBlockedReason = node->blockedReason;
        item.owner = node->owner;
        item.techLevel = node->techLevel;
        item.prerequisite = node->prerequisite;
        item.prerequisiteOverride = node->prerequisiteOverride;
        item.requiredFactory = node->requiredFactory;
    }
    return item;
}

bool runtimeSlaveMinerDeliveryTargetLegal(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& worker,
    const WorldObjectInstance& target) {
    const auto task = state.slaveMinerTasks.find(worker.instanceId);
    if (!worker.alive || worker.health <= 0
        || worker.kind != WorldObjectKind::Infantry
        || runtimeGateObjectIsLimbo(worker)
        || runtimeObjectIsEnslavedSlaveWorker(state, worker)
        || !runtimeObjectCanReceiveMoveCommand(state, worker)
        || !target.alive || target.health <= 0
        || runtimeGateObjectIsLimbo(target)
        || task == state.slaveMinerTasks.end()
        || task->second.refineryId != target.instanceId
        || runtimeCanonicalHumanHouseOwner(worker.owner)
            != runtimeCanonicalHumanHouseOwner(state.economy.owner)
        || runtimeCanonicalHumanHouseOwner(target.owner)
            != runtimeCanonicalHumanHouseOwner(worker.owner)) return false;

    const auto* workerType = findRuntimeUnitType(state.rules, worker);
    if (!workerType || !workerType->slaved) return false;

    std::string enslaves;
    if (target.kind == WorldObjectKind::Unit) {
        const auto* unit = findRuntimeUnitType(state.rules, target);
        if (!unit || !runtimeUnitIsMobileSlaveMiner(*unit)) return false;
        enslaves = unit->enslaves;
        if (enslaves.empty()) {
            const auto* building = findRuntimeBuildingTypeById(state.rules, unit->deploysInto);
            if (building) enslaves = building->enslaves;
        }
    } else if (target.kind == WorldObjectKind::Structure) {
        const auto* building = findRuntimeBuildingTypeById(state.rules, target.type);
        if (!building || building->storage <= 0
            || !runtimeBuildingCanGatherResources(*building)) return false;
        enslaves = building->enslaves;
    } else return false;

    return !enslaves.empty()
        && toUpperAsciiLocal(enslaves) == toUpperAsciiLocal(worker.type);
}

bool runtimeSelectionOffersSlaveDelivery(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& target) {
    for (const int objectId : state.selectedObjectIds) {
        const auto* worker = findObjectById(state, objectId);
        if (worker && runtimeSlaveMinerDeliveryTargetLegal(state, *worker, target))
            return true;
    }
    return false;
}

bool queueRuntimeSlaveMinerDeliveryTarget(
    RuntimeProductionHostState& state,
    const WorldObjectInstance& target) {
    if (!runtimeSelectionOffersSlaveDelivery(state, target)) return false;
    const int targetId = target.instanceId;
    const std::string targetType = target.type;
    resetLastRuntimeCommand(state, "return", runtimeCellPointForObject(target), targetId, targetType);
    auto selectedIds = state.selectedObjectIds;
    std::sort(selectedIds.begin(), selectedIds.end());
    selectedIds.erase(std::unique(selectedIds.begin(), selectedIds.end()), selectedIds.end());
    std::optional<std::string> acceptedVoice;
    for (const int objectId : selectedIds) {
        auto* worker = findMutableObjectById(state, objectId);
        const auto* home = findObjectById(state, targetId);
        if (!worker || !home || !runtimeSlaveMinerDeliveryTargetLegal(state, *worker, *home)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }

        auto& task = state.slaveMinerTasks.at(objectId);
        task.phase = RuntimeSlaveMinerPhase::Returning;
        task.targetOre = {-1, -1};
        task.workTicksRemaining = 0;
        task.status = "player delivery";
        worker->harvestAutoSearchSuspended = false;
        // The shared return helper cancels the old order while retaining the
        // current walking segment. Cargo and home stay in the existing task.
        if (!issueRuntimeSlaveMinerReturn(state, *worker, *home)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        ++state.lastCommand.acceptedCount;
        state.specialMechanisms.notifyActivity(objectId, state.tick, "move");
        if (!acceptedVoice) {
            if (const auto* unit = findRuntimeUnitType(state.rules, *worker))
                acceptedVoice = runtimeVoiceEventFromField(state, *worker, "move", unit->voiceMove);
        }
    }

    state.interactionStatus = state.lastCommand.acceptedCount > 0
        ? "returning to " + targetType + " #" + std::to_string(targetId)
            + " workers=" + std::to_string(state.lastCommand.acceptedCount)
        : "workers cannot reach " + targetType + " #" + std::to_string(targetId);
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback)
        emitRuntimeSound(state, state.lastCommand.acceptedCount > 0
            ? acceptedVoice.value_or("command move") : "blocked");
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
    return true;
}

void debitRuntimeRepairDepotCredits(
    RuntimeProductionHostState& state,
    const std::string& canonicalOwner,
    int amount) {
    const int debit = std::max(0, amount);
    if (debit <= 0) {
        return;
    }
    const std::string ownerKey =
        runtimeRepairDepotCanonicalOwnerId(state, canonicalOwner);
    if (ownerKey == runtimeRepairDepotCanonicalOwnerId(
            state,
            state.economy.owner
        )) {
        (void)spendRuntimeCredits(state, debit);
        return;
    }
    if (RuntimeEconomyState* humanEconomy =
            runtimeHumanHouseEconomy(state, ownerKey)) {
        humanEconomy->credits = std::max(
            0,
            humanEconomy->credits - debit
        );
        return;
    }
    if (state.campaignMode) {
        const auto house =
            state.triggers.campaignHouseProduction.find(ownerKey);
        if (house != state.triggers.campaignHouseProduction.end()) {
            if (house->second.playerControlled) {
                (void)spendRuntimeCredits(state, debit);
            } else {
                spendRuntimeComputerStoredOre(state, ownerKey, std::min(debit, house->second.credits));
                house->second.credits =
                    std::max(0, house->second.credits - debit);
            }
            return;
        }
    }
    const auto ai = state.computerAiByOwner.find(ownerKey);
    if (ai != state.computerAiByOwner.end()) {
        spendRuntimeComputerStoredOre(state, ownerKey, std::min(debit, ai->second.credits));
        ai->second.credits = std::max(0, ai->second.credits - debit);
        ai->second.creditsSpent = std::min(
            std::numeric_limits<int>::max(),
            ai->second.creditsSpent > std::numeric_limits<int>::max() - debit
                ? std::numeric_limits<int>::max()
                : ai->second.creditsSpent + debit
        );
    }
}

void depositRuntimeRepairDepotCredits(
    RuntimeProductionHostState& state,
    const std::string& canonicalOwner,
    int amount) {
    const int credit = std::max(0, amount);
    if (credit <= 0) {
        return;
    }
    const std::string ownerKey =
        runtimeRepairDepotCanonicalOwnerId(state, canonicalOwner);
    if (ownerKey == runtimeRepairDepotCanonicalOwnerId(
            state,
            state.economy.owner
        )) {
        depositRuntimeCredits(state, credit);
        return;
    }
    if (RuntimeEconomyState* humanEconomy =
            runtimeHumanHouseEconomy(state, ownerKey)) {
        humanEconomy->credits = runtimeCreditBalanceAfterDeposit(
            humanEconomy->credits,
            credit
        );
        return;
    }
    if (state.campaignMode) {
        const auto house =
            state.triggers.campaignHouseProduction.find(ownerKey);
        if (house != state.triggers.campaignHouseProduction.end()) {
            if (house->second.playerControlled) {
                depositRuntimeCredits(state, credit);
            } else {
                house->second.credits = runtimeCreditBalanceAfterDeposit(
                    house->second.credits,
                    credit
                );
            }
            return;
        }
    }
    const auto ai = state.computerAiByOwner.find(ownerKey);
    if (ai != state.computerAiByOwner.end()) {
        ai->second.credits = runtimeCreditBalanceAfterDeposit(
            ai->second.credits,
            credit
        );
    }
}

bool runtimeHarvesterManualUnloadEligible(
    const RuntimeGameSessionState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery) {
    if (harvester.kind != WorldObjectKind::Unit
        || harvester.health <= 0
        || refinery.health <= 0
        || !runtimeObjectIsHarvester(state, harvester)
        || !runtimeObjectCanReceiveMoveCommand(state, harvester)
        || runtimeObjectDisabledByParasite(state, harvester.instanceId)
        || !runtimeHarvesterCanUnloadAtRefinery(state, harvester, refinery)) {
        return false;
    }
    // An explicit return is valid before the first load as well. The normal
    // dock/unload state machine already credits only a nonempty cargo batch.
    return true;
}

bool runtimeSelectionOffersHarvesterUnload(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& targetObject) {
    for (int objectId : state.selectedObjectIds) {
        const WorldObjectInstance* selected = findObjectById(state, objectId);
        if (selected != nullptr
            && runtimeHarvesterManualUnloadEligible(state, *selected, targetObject)) {
            return true;
        }
    }
    return false;
}

bool queueRuntimeHarvesterUnloadCommand(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& targetCell,
    const WorldObjectInstance& targetObject) {
    if (!runtimeSelectionOffersHarvesterUnload(state, targetObject)) {
        return false;
    }
    resetLastRuntimeCommand(
        state, "harvesterUnload", targetCell,
        targetObject.instanceId, targetObject.type
    );
    std::vector<int> commandObjectIds = state.selectedObjectIds;
    std::sort(commandObjectIds.begin(), commandObjectIds.end());
    commandObjectIds.erase(
        std::unique(commandObjectIds.begin(), commandObjectIds.end()),
        commandObjectIds.end()
    );
    std::optional<std::string> acceptedMoveVoice;
    for (int objectId : commandObjectIds) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr
            || !runtimeHarvesterManualUnloadEligible(state, *object, targetObject)
            || !releaseRuntimeRepairDepotForCommand(
                state, objectId, "manual harvester unload")) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        const auto previousTask = state.harvesterTasks.find(objectId);
        const bool ownsPreviousDockClock = previousTask != state.harvesterTasks.end()
            && (previousTask->second.phase == RuntimeHarvesterPhase::Docking
                || previousTask->second.phase == RuntimeHarvesterPhase::PreparingToUnload
                || previousTask->second.phase == RuntimeHarvesterPhase::Unloading)
            && runtimeHarvesterIsAtDock(*object, previousTask->second);
        const bool keepDockClock = ownsPreviousDockClock
            && previousTask->second.targetRefineryId == targetObject.instanceId;
        const auto oldBehavior = state.behaviors.find(objectId);
        std::optional<RuntimeObjectBehavior> surfaceContinuation;
        if (oldBehavior != state.behaviors.end()) {
            clearRuntimeTemporalAttack(
                state, objectId, oldBehavior->second,
                "manual harvester unload interrupted attack"
            );
            if (oldBehavior->second.moveCooldownTicks > 0 && !object->airborne) {
                surfaceContinuation = oldBehavior->second;
            }
        }
        const bool cancellingStructureEntry =
            runtimeCancelledStructureEntryNeedsRecovery(state, *object);
        if (!eraseRuntimeBehavior(state, objectId)) {
            ++state.lastCommand.rejectedCount;
            continue;
        }
        if (cancellingStructureEntry) surfaceContinuation.reset();
        RuntimeHarvesterTask& task = state.harvesterTasks[objectId];
        normalizeRuntimeHarvesterTask(state, *object, task);
        if (surfaceContinuation) {
            RuntimeObjectBehavior continuation;
            continuation.kind = RuntimeBehaviorKind::Move;
            continuation.state = RuntimeBehaviorState::Queued;
            continuation.moveTarget = runtimeCellPointForObject(*object);
            continuation.weaponCooldownTicks =
                std::max(0, surfaceContinuation->weaponCooldownTicks);
            inheritRuntimeVehicleMoveSegment(*object, continuation, *surfaceContinuation);
            state.behaviors[objectId] = std::move(continuation);
        }
        object->harvestAutoSearchSuspended = false;
        if (!keepDockClock) {
            if (WorldObjectInstance* previousRefinery =
                    findMutableObjectById(state, task.targetRefineryId);
                ownsPreviousDockClock && previousRefinery != nullptr
                    && previousRefinery->mission == "Unload") {
                setRuntimeMissionIntent(*previousRefinery, RuntimeMissionIntent::Idle);
            }
            task.targetOre = RuntimeCellPoint{};
            task.dockCell = RuntimeCellPoint{};
            task.queueCell = RuntimeCellPoint{};
            task.targetRefineryId = targetObject.instanceId;
            task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
            task.workTicksRemaining = 0;
            task.nextThinkTick = state.tick;
            task.status = "manual refinery return";
            object->mission = "Harvest";
        }
        ++state.lastCommand.acceptedCount;
        if (!acceptedMoveVoice) {
            const UnitType* unit = findRuntimeUnitType(state.rules, *object);
            if (unit != nullptr) {
                acceptedMoveVoice = runtimeVoiceEventFromField(
                    state, *object, "move", unit->voiceMove);
            }
        }
    }
    state.interactionStatus = state.lastCommand.acceptedCount > 0
        ? "harvester unload command accepted"
        : "harvester unload command rejected";
    state.economy.status = state.interactionStatus;
    if (!state.runtimeSuppressPlayerAudioFeedback) {
        emitRuntimeSound(state, state.lastCommand.acceptedCount > 0
            ? acceptedMoveVoice.value_or("command move") : "blocked");
    }
    recordRuntimeCommandReplayEvent(state);
    refreshRuntimeActionSummary(state);
    return true;
}

}
