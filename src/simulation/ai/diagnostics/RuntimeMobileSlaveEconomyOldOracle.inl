std::optional<RuntimeCellPoint> s17OldFindRuntimeSlaveMinerDeployCellNearOre(
    const RuntimeWindowState& state, const WorldObjectInstance& miner,
    const RuntimeCellPoint& ore,
    const RuntimeManualFormationMappedFilter* manualMappedFilter = nullptr
) {
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
                || !runtimeCellCanPlaceBuilding(state, item, cell, miner.instanceId, &occupied))
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
        if (findRuntimeHarvesterResourceApproach(state, worker, ore).has_value()
            && (manualMappedFilter == nullptr || manualMappedFilter->allows(cell))) return cell;
    }
    return std::nullopt;
}

std::optional<RuntimeCellPoint> s17OldFindNearestRuntimeMobileSlaveMinerOreCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& miner,
    int maxRadius,
    std::optional<RuntimeCellPoint>* deployCell = nullptr
) {
    const RuntimeCellPoint origin = runtimeCellPointForObject(miner);
    const int radiusLimit = std::max(1, maxRadius);

    struct ResourceCandidate {
        RuntimeCellPoint cell;
        long long distanceScore = 0;
        int value = 0;
        int density = 0;
    };

    const auto candidateIfHarvestable =
        [&](const RuntimeCellPoint& cell)
            -> std::optional<ResourceCandidate> {
            const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
            if (terrain == nullptr
                || !terrain->hasOverlay
                || (!terrain->overlayIsOre && !terrain->overlayIsGem)) {
                return std::nullopt;
            }

            const long long dx =
                static_cast<long long>(cell.x - origin.x);
            const long long dy =
                static_cast<long long>(cell.y - origin.y);
            return ResourceCandidate{
                cell,
                (dx * dx) + (dy * dy),
                runtimeResourceValueForCell(state, *terrain),
                runtimeResourceDensityForTerrain(*terrain)
            };
        };

    const auto reachable =
        [&](const RuntimeCellPoint& cell) {
            const auto target = s17OldFindRuntimeSlaveMinerDeployCellNearOre(state, miner, cell);
            if (deployCell != nullptr) *deployCell = target;
            return target.has_value();
        };

    for (int radius = 0; radius <= radiusLimit; ++radius) {
        std::vector<ResourceCandidate> candidates;
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0
                    && std::abs(dx) != radius
                    && std::abs(dy) != radius) {
                    continue;
                }

                if (const std::optional<ResourceCandidate> candidate =
                        candidateIfHarvestable({
                            origin.x + dx,
                            origin.y + dy
                        })) {
                    candidates.push_back(*candidate);
                }
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

        for (const ResourceCandidate& candidate : candidates) {
            if (reachable(candidate.cell)) {
                return candidate.cell;
            }
        }
    }

    return std::nullopt;
}

std::optional<RuntimeBuildCatalogItem> s17OldRuntimeBuildItemForBuilding(
    const RuntimeWindowState& state,
    const std::string& buildingId
) {
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
            state.techTree,
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

bool s17OldDeployRuntimeMcv(
    RuntimeWindowState& state,
    int objectId,
    bool selectResult,
    std::optional<RuntimeCellPoint> requestedCell
) {
    WorldObjectInstance* mcv = findMutableObjectById(state, objectId);
    if (mcv == nullptr || !mcv->alive || mcv->kind != WorldObjectKind::Unit
        || state.specialMechanisms.isChronoMovementLocked(objectId, state.tick)) {
        state.economy.status = "deploy failed: no selected MCV";
        return false;
    }

    const UnitType* unit = findRuntimeUnitType(state.rules, *mcv);
    if (unit == nullptr || unit->deploysInto.empty()) {
        state.economy.status =
            "deploy failed: " + mcv->type + " has no DeploysInto";
        return false;
    }

    const std::optional<RuntimeBuildCatalogItem> buildingItem =
        s17OldRuntimeBuildItemForBuilding(state, unit->deploysInto);
    if (!buildingItem.has_value()) {
        state.economy.status =
            "deploy failed: missing building " + unit->deploysInto;
        return false;
    }

    // Every deployment request names a center anchor cell, including planned
    // AI and slave-miner sites. Only foundation checks and creation use corners.
    const RuntimeStructureFootprint deployFootprint =
        runtimeStructureFootprintForBuildItem(state, *buildingItem);
    // Manual transforms stay at the current center for every unit family.
    // Autonomous relocation still supplies an explicitly planned center.
    const RuntimeCellPoint deployCenter = requestedCell.value_or(runtimeCellPointForObject(*mcv));
    const std::optional<RuntimeCellPoint> deployCell =
        runtimeCellCanPlaceBuilding(state, *buildingItem,
            runtimePlacementTopLeftForAnchor(deployCenter, deployFootprint), objectId, nullptr, mcv->owner)
        ? std::optional<RuntimeCellPoint>(deployCenter) : std::nullopt;

    if (!deployCell.has_value()) {
        state.economy.status =
            "deploy failed: no legal footprint for " + unit->deploysInto;
        state.interactionStatus = state.economy.status;
        return false;
    }

    const int unlockedOptionsBefore =
        runtimeUnlockedBuildOptionCount(state);

    const int oldMcvId = mcv->instanceId;
    const int oldVeterancy = mcv->veterancy;
    const int oldVeterancyExperience = mcv->veterancyExperience;
    const std::string oldMcvType = mcv->type;
    const WorldObjectInstance oldMcvSnapshot = *mcv;
    const int resolvedDeployFacing = runtimeMcvResolvedDeployFacing(state, *mcv, *unit);

    WorldObjectInstance* constructionYard = addRuntimeObjectInstanceForOwner(
        state,
        oldMcvSnapshot.owner,
        RuntimeBuildKind::Building,
        unit->deploysInto,
        runtimePlacementTopLeftForAnchor(*deployCell, deployFootprint)
    );
    if (constructionYard == nullptr) {
        state.economy.status = "deploy failed: placement add failed";
        return false;
    }
    constructionYard->facing = resolvedDeployFacing;
    constructionYard->turretFacing = resolvedDeployFacing;
    constructionYard->veterancy = oldVeterancy;
    constructionYard->veterancyExperience = oldVeterancyExperience;
    runtimeTransferCrateModifiers(state, oldMcvSnapshot, *constructionYard);
    constructionYard->health = std::clamp(static_cast<int>(
        static_cast<long long>(oldMcvSnapshot.health)
            * runtimeMaxHealthForObject(state, *constructionYard)
            / std::max(1, runtimeMaxHealthForObject(state, oldMcvSnapshot))),
        1, std::max(1, runtimeMaxHealthForObject(state, *constructionYard)));
    if (runtimeUnitIsMobileSlaveMiner(*unit)) {
        constructionYard->harvestAutoSearchSuspended = oldMcvSnapshot.harvestAutoSearchSuspended;
        constructionYard->hasBeenCaptured = oldMcvSnapshot.hasBeenCaptured;
        transferRuntimeSlaveMinerWorkers(state, oldMcvId, *constructionYard);
    }
    if (selectResult) {
        addSelectedObject(state, constructionYard->instanceId);
    }
    RuntimeObjectExitRequest objectExit;
    objectExit.objectId = oldMcvId;
    objectExit.kind = RuntimeObjectExitKind::TransformSource;
    objectExit.tick = state.tick;
    objectExit.mission = "Deployed";
    objectExit.reason = "MCV deployed";
    objectExit.sourceObjectId = constructionYard->instanceId;
    objectExit.sourceOwner = constructionYard->owner;
    objectExit.suppressDeathWeapon = true;
    if (queueRuntimeObjectExit(state, std::move(objectExit))
            != RuntimeObjectExitQueueResult::Queued
        || !drainRuntimeObjectExitQueue(state)) {
        state.economy.status = "deploy failed: source exit rejected";
        return false;
    }

    // Delayed deployment runs outside the command's house scope.
    // Attribute human builds to the saved source owner, retaining legacy contexts.
    RuntimeEconomyState* deploymentEconomy =
        runtimeHumanHouseEconomy(state, oldMcvSnapshot.owner);
    ++(deploymentEconomy != nullptr ? *deploymentEconomy : state.economy)
        .completedBuildings;
    state.economy.status =
        "deployed " + oldMcvType + " -> " + constructionYard->type
        + " #"
        + std::to_string(constructionYard->instanceId);
    state.interactionStatus = state.economy.status;
    state.lastCommand.valid = false;
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    rebuildRuntimeEconomyPower(state);
    if (runtimeUnlockedBuildOptionCount(state) > unlockedOptionsBefore) {
        emitRuntimeSound(
            state,
            runtimeSoundIntentNewConstructionOptions
        );
    }
    return true;
}

bool s17OldAdvanceRuntimeMobileSlaveMinerEconomy(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("s17OldAdvanceRuntimeMobileSlaveMinerEconomy", state.tick);
    const int scanInterval = std::max(1, std::min(
        runtimeRuleFramesToTicks(state.rules.getSlaveMinerKickFrameDelay(), state.config.fixedTickHz), state.config.fixedTickHz / 2));
    if (state.tick <= 0 || (state.tick % scanInterval) != 0) {
        return false;
    }

    std::vector<int> minerIds;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
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
            s17OldFindNearestRuntimeMobileSlaveMinerOreCell(
                state,
                *miner,
                state.rules.getSlaveMinerShortScan(),
                &deployCell
            );
        if (nearbyOre.has_value() && deployCell == runtimeCellPointForObject(*miner)) {
            const bool keepSelection = selectionContains(state, minerId);
            if (s17OldDeployRuntimeMcv(state, minerId, keepSelection, deployCell)) {
                state.economy.status =
                    unit->id + " deployed near ore "
                    + std::to_string(nearbyOre->x)
                    + ","
                    + std::to_string(nearbyOre->y);
                changed = true;
                continue;
            }
        }

        const std::optional<RuntimeCellPoint> distantOre = nearbyOre.has_value()
            ? nearbyOre : s17OldFindNearestRuntimeMobileSlaveMinerOreCell(
                state,
                *miner,
                state.rules.getSlaveMinerLongScan(),
                &deployCell
            );
        if (!distantOre.has_value()) {
            miner->mission = "Guard";
            state.economy.status = unit->id + " found no ore";
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
                true
            )) {
            const RuntimeCellPoint target =
                actualTarget.value_or(*distantOre);
            state.economy.status =
                unit->id + " moving to ore "
                + std::to_string(target.x)
                + ","
                + std::to_string(target.y);
            changed = true;
        }
    }

    return changed;
}

// Legacy facade for the two remaining slave consumers. Ordinary computer
// harvesters run first with an explicit HouseEconomyContext. A supplied work
// record already has the original pre-mining metrics and must not be refreshed
// again here; its net balance is committed after both slave paths finish.
class S17OldRuntimeComputerMiningScope {
public:
    S17OldRuntimeComputerMiningScope(RuntimeWindowState& state, const std::string& owner,
        const RuntimeEconomyState* ordinaryWork = nullptr)
        : state_(state), owner_(runtimeRepairDepotCanonicalOwnerId(state, owner)),
          startingCredits_(runtimeRepairDepotCreditBalance(state, owner)),
          ledger_(state.computerMiningByOwner[owner_]),
          interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          lastCommand_(state.lastCommand),
          economy_(std::move(state.economy)), techTree_(std::move(state.techTree)),
          techTreeLoaded_(state.techTreeLoaded),
          suppressAudio_(state.runtimeSuppressPlayerAudioFeedback) {
        state.economy = RuntimeEconomyState{};
        state.economy.owner = owner;
        state.economy.credits = startingCredits_;
        state.economy.storedResourceUnits = ledger_.storedResourceUnits;
        state.economy.storedResourceValue = ledger_.storedResourceValue;
        state.economy.storedResourceGemUnits = ledger_.storedResourceGemUnits;
        state.economy.resourceTrips = ledger_.resourceTrips;
        state.economy.harvesterTeleports = ledger_.harvesterTeleports;
        state.economy.slaveMinerLoads = ledger_.slaveMinerLoads;
        state.economy.skirmishStartApplied = economy_.skirmishStartApplied;
        if (ordinaryWork) state.economy = *ordinaryWork;
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        state.humanHouseScopeActive = true;
        state.remoteHumanHouseScopeActive = true;
        state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
        if (!ordinaryWork) refreshRuntimeEconomyMetricsForCurrentOwner(state);
    }
    S17OldRuntimeComputerMiningScope(const S17OldRuntimeComputerMiningScope&) = delete;
    S17OldRuntimeComputerMiningScope& operator=(const S17OldRuntimeComputerMiningScope&) = delete;
    ~S17OldRuntimeComputerMiningScope() {
        const int income = std::max(0, state_.economy.credits - startingCredits_);
        ledger_.storedResourceUnits = state_.economy.storedResourceUnits;
        ledger_.storedResourceValue = state_.economy.storedResourceValue;
        ledger_.storedResourceGemUnits = state_.economy.storedResourceGemUnits;
        ledger_.resourceTrips = state_.economy.resourceTrips;
        ledger_.harvesterTeleports = state_.economy.harvesterTeleports;
        ledger_.slaveMinerLoads = state_.economy.slaveMinerLoads;
        state_.economy = std::move(economy_);
        state_.techTree = std::move(techTree_);
        state_.techTreeLoaded = techTreeLoaded_;
        state_.interactionStatus = std::move(interaction_);
        state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(lastCommand_);
        state_.humanHouseScopeActive = false;
        state_.remoteHumanHouseScopeActive = false;
        state_.computerMiningScopeActive = false;
        state_.runtimeSuppressPlayerAudioFeedback = suppressAudio_;
        // Restore the facade first: this routes the deposit to its sole real
        // account, rather than back into the temporary harvesting balance.
        depositRuntimeRepairDepotCredits(state_, owner_, income);
        const auto ai = state_.computerAiByOwner.find(owner_);
        if (!state_.campaignMode && ai != state_.computerAiByOwner.end()) {
            ai->second.incomeEarned = static_cast<int>(std::min<long long>(
                std::numeric_limits<int>::max(),
                static_cast<long long>(ai->second.incomeEarned) + income));
        }
    }
private:
    RuntimeWindowState& state_;
    std::string owner_;
    int startingCredits_;
    RuntimeComputerMiningState& ledger_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) lastCommand_;
    RuntimeEconomyState economy_;
    TechTreeReport techTree_;
    bool techTreeLoaded_, suppressAudio_;
};

// The old enclosing Scope committed a partial ordinary balance during stack
// unwinding too. Preserve that synchronous owner-account boundary on failure.
void s17OldCommitRuntimeComputerMiningWorkAfterOrdinaryFailure(RuntimeWindowState& state,
    const std::string& owner, const RuntimeEconomyState& work, int startingCredits) {
    const std::string key = runtimeRepairDepotCanonicalOwnerId(state, owner);
    auto& ledger = state.computerMiningByOwner[key];
    ledger.storedResourceUnits = work.storedResourceUnits;
    ledger.storedResourceValue = work.storedResourceValue;
    ledger.storedResourceGemUnits = work.storedResourceGemUnits;
    ledger.resourceTrips = work.resourceTrips;
    ledger.harvesterTeleports = work.harvesterTeleports;
    ledger.slaveMinerLoads = work.slaveMinerLoads;
    const int income = std::max(0, work.credits - startingCredits);
    depositRuntimeRepairDepotCredits(state, key, income);
    const auto ai = state.computerAiByOwner.find(key);
    if (!state.campaignMode && ai != state.computerAiByOwner.end()) {
        ai->second.incomeEarned = static_cast<int>(std::min<long long>(std::numeric_limits<int>::max(),
            static_cast<long long>(ai->second.incomeEarned) + income));
    }
}

// Preserve the old synchronous feedback policy without moving either the
// player's economy or technology tree during ordinary computer harvesting.
class S17OldRuntimeComputerMiningFeedbackScope {
public:
    explicit S17OldRuntimeComputerMiningFeedbackScope(RuntimeWindowState& state)
        : state_(state), interaction_(state.interactionStatus), simulation_(state.simulationStatus),
          command_(state.lastCommand), human_(state.humanHouseScopeActive),
          remote_(state.remoteHumanHouseScopeActive), computer_(state.computerMiningScopeActive),
          suppress_(state.runtimeSuppressPlayerAudioFeedback) {
        state.humanHouseScopeActive = state.remoteHumanHouseScopeActive = state.computerMiningScopeActive = true;
        state.runtimeSuppressPlayerAudioFeedback = true;
    }
    ~S17OldRuntimeComputerMiningFeedbackScope() {
        state_.interactionStatus = std::move(interaction_); state_.simulationStatus = std::move(simulation_);
        state_.lastCommand = std::move(command_); state_.humanHouseScopeActive = human_;
        state_.remoteHumanHouseScopeActive = remote_; state_.computerMiningScopeActive = computer_;
        state_.runtimeSuppressPlayerAudioFeedback = suppress_;
    }
    S17OldRuntimeComputerMiningFeedbackScope(const S17OldRuntimeComputerMiningFeedbackScope&) = delete;
    S17OldRuntimeComputerMiningFeedbackScope& operator=(const S17OldRuntimeComputerMiningFeedbackScope&) = delete;
private:
    RuntimeWindowState& state_;
    std::string interaction_, simulation_;
    decltype(RuntimeWindowState::lastCommand) command_;
    bool human_, remote_, computer_, suppress_;
};

bool s17OldAdvanceRuntimeComputerMining(RuntimeWindowState& state) {
    if (!state.rulesLoaded || state.humanHouseScopeActive
        || state.skirmishMatchFinished) return false;
    std::vector<std::string> owners;
    if (state.campaignMode) {
        for (const auto& [key, house] : state.triggers.campaignHouseProduction) {
            if (!house.playerControlled && house.side >= 0
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(house.owner);
        }
    } else {
        if (!runtimeComputerAiEnabledForSession(state)
            || !state.economy.skirmishStartApplied) return false;
        ensureRuntimeComputerAiStates(state);
        for (const auto& [key, ai] : state.computerAiByOwner) {
            if (ai.initialized && !findRuntimeHumanHouseAuthority(state, ai.owner)
                && runtimeRepairDepotCanonicalOwnerId(state, state.economy.owner) != key)
                owners.push_back(ai.owner);
        }
    }
    std::sort(owners.begin(), owners.end());
    bool changed = false;
    for (const std::string& owner : owners) {
        RuntimeEconomyState ordinaryWork;
        ordinaryWork.owner = owner;
        ordinaryWork.credits = runtimeRepairDepotCreditBalance(state, owner);
        const auto& ledger = state.computerMiningByOwner[runtimeRepairDepotCanonicalOwnerId(state, owner)];
        ordinaryWork.storedResourceUnits = ledger.storedResourceUnits;
        ordinaryWork.storedResourceValue = ledger.storedResourceValue;
        ordinaryWork.storedResourceGemUnits = ledger.storedResourceGemUnits;
        ordinaryWork.resourceTrips = ledger.resourceTrips;
        ordinaryWork.harvesterTeleports = ledger.harvesterTeleports;
        ordinaryWork.slaveMinerLoads = ledger.slaveMinerLoads;
        ordinaryWork.skirmishStartApplied = state.economy.skirmishStartApplied;
        refreshRuntimeEconomyMetricsForOwner(state, ordinaryWork);
        const int ordinaryStartingCredits = ordinaryWork.credits;
        try {
            S17OldRuntimeComputerMiningFeedbackScope feedback(state);
            auto context = makeRuntimeHouseEconomyContext(ordinaryWork, true);
            changed = advanceRuntimeHarvesterEconomy(state, context, ordinaryWork) || changed;
        } catch (...) {
            s17OldCommitRuntimeComputerMiningWorkAfterOrdinaryFailure(state, owner, ordinaryWork, ordinaryStartingCredits);
            throw;
        }
        // Only the two slave consumers borrow the legacy facade. Carry the
        // ordinary balance into that scope without depositing early: one net
        // delta is still committed after both slave paths, in the original order.
        S17OldRuntimeComputerMiningScope scope(state, owner, &ordinaryWork);
        changed = s17OldAdvanceRuntimeMobileSlaveMinerEconomy(state) || changed;
        changed = advanceRuntimeSlaveMinerEconomy(state) || changed;
    }
    return changed;
}
