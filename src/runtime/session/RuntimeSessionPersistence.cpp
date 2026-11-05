#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

bool loadRuntimeIniSource(
    const RuntimeConfig& config,
    RuntimeCampaignIniKind kind,
    IniFile& ini,
    RuntimeIniSource* resolvedSource) {
    RuntimeIniSource source = resolveRuntimeIniSource(config, kind);
    if (resolvedSource != nullptr) {
        *resolvedSource = source;
    }
    return source.available() && source.load(ini);
}

RuntimeOwnerRemapColorTable loadRuntimeRemapColorTable(
    const RuntimeConfig& config) {
    IniFile rulesIni;
    const bool loaded = loadRuntimeIniSource(config, RuntimeCampaignIniKind::Rules, rulesIni);
    return buildRuntimeRemapColorTable(loaded ? &rulesIni : nullptr);
}

RuntimeMapColorValue loadRuntimeLocalRadarColor(const RuntimeConfig& config) {
    constexpr RuntimeMapColorValue defaultLocalRadarColor = sessionPackedColor_rgb(0, 255, 0);
    IniFile rulesIni;
    if (!loadRuntimeIniSource(
            config,
            RuntimeCampaignIniKind::Rules,
            rulesIni
        )) {
        return defaultLocalRadarColor;
    }
    return runtimeColorFromRgbString(
        rulesIni.getValue("General", "LocalRadarColor", "")
    ).value_or(defaultLocalRadarColor);
}

RuntimeOwnerRemapColorTable loadRuntimeMapOwnerRemapColors(
    const RuntimeConfig& config,
    std::unordered_set<std::string>* neutralOwners) {
    RuntimeOwnerRemapColorTable ownerColors;
    if (neutralOwners != nullptr) {
        neutralOwners->clear();
    }
    IniFile mapIni;
    if (config.defaultMapPath.empty()
        || !mapIni.loadFromFile(config.defaultMapPath)) {
        return ownerColors;
    }

    const RuntimeOwnerRemapColorTable colorTable =
        loadRuntimeRemapColorTable(config);
    const auto addOwnerColor = [&](
        const std::string& ownerId,
        const std::string& colorName
    ) {
        const std::string ownerKey =
            toLowerAsciiLocal(trimAsciiLocal(ownerId));
        const std::string colorKey =
            toLowerAsciiLocal(trimAsciiLocal(colorName));
        if (ownerKey.empty() || colorKey.empty()) {
            return;
        }

        const auto found = colorTable.find(colorKey);
        if (found != colorTable.end()) {
            ownerColors[ownerKey] = found->second;
            return;
        }

        const std::optional<RuntimeMapColorValue> inlineColor =
            runtimeColorFromHsvString(colorName);
        if (inlineColor.has_value()) {
            ownerColors[ownerKey] = *inlineColor;
        }
    };

    for (const std::string& houseId : runtimeIndexedIds(mapIni, "Houses")) {
        std::string colorName = mapIni.getValue(houseId, "Color", "");
        if (trimAsciiLocal(colorName).empty()) {
            const std::string country =
                mapIni.getValue(houseId, "Country", "");
            colorName = mapIni.getValue(country, "Color", "");
        }
        addOwnerColor(houseId, colorName);
    }

    for (const std::string& sectionName : mapIni.getSectionNames()) {
        addOwnerColor(sectionName, mapIni.getValue(sectionName, "Color", ""));
    }

    const auto neutralCountryToken = [](const std::string& countryId) {
        const std::string countryKey =
            toLowerAsciiLocal(trimAsciiLocal(countryId));
        return countryKey == "neutral"
            || countryKey == "special"
            || countryKey == "civilian"
            || countryKey == "civilians";
    };
    const auto countryIsNeutral =
        [&mapIni, &neutralCountryToken](std::string countryId) {
        for (int depth = 0; depth < 16; ++depth) {
            countryId = trimAsciiLocal(countryId);
            const std::string countryKey = toLowerAsciiLocal(countryId);
            if (neutralCountryToken(countryId)) {
                return true;
            }
            if (countryId.empty()) {
                return false;
            }

            const std::string parent = trimAsciiLocal(
                mapIni.getValue(countryId, "ParentCountry", "")
            );
            if (parent.empty()
                || toLowerAsciiLocal(parent) == countryKey) {
                return false;
            }
            countryId = parent;
        }
        return false;
    };

    constexpr RuntimeMapColorValue neutralStructureColor = sessionPackedColor_rgb(255, 255, 255);
    const auto markNeutralOwner = [&](const std::string& ownerId) {
        const std::string ownerKey =
            toLowerAsciiLocal(trimAsciiLocal(ownerId));
        if (ownerKey.empty()) {
            return;
        }
        ownerColors[ownerKey] = neutralStructureColor;
        if (neutralOwners != nullptr) {
            neutralOwners->insert(ownerKey);
        }
    };
    markNeutralOwner("neutral");
    markNeutralOwner("special");
    markNeutralOwner("civilian");
    markNeutralOwner("civilians");
    for (const std::string& houseId : runtimeIndexedIds(mapIni, "Houses")) {
        const std::string countryId =
            mapIni.getValue(houseId, "Country", houseId);
        std::string colorName = mapIni.getValue(houseId, "Color", "");
        if (trimAsciiLocal(colorName).empty()) {
            colorName = mapIni.getValue(countryId, "Color", "");
        }
        const std::string colorKey =
            toLowerAsciiLocal(trimAsciiLocal(colorName));
        const bool neutralHouseColor = colorKey.empty()
            || colorKey == "grey"
            || colorKey == "lightgrey"
            || colorKey == "white"
            || colorKey == "neutral";
        const bool directNeutral = neutralCountryToken(houseId)
            || neutralCountryToken(countryId);
        const bool derivedNeutral = countryIsNeutral(countryId)
            && neutralHouseColor;
        if (directNeutral || derivedNeutral) {
            markNeutralOwner(houseId);
        }
    }

    return ownerColors;
}

void loadRuntimeCampaignAiTriggers(
    RuntimeTriggerState& state,
    const IniFile& ini) {
    const IniFile::Section* definitions = ini.getSection("AITriggerTypes");
    if (definitions == nullptr) {
        return;
    }
    const IniFile::Section* enabled = ini.getSection("AITriggerTypesEnable");
    for (const auto& [id, value] : *definitions) {
        RuntimeCampaignAiTriggerDefinition parsed =
            parseRuntimeCampaignAiTrigger(id, value);
        if (enabled != nullptr) {
            for (const auto& [enabledId, enabledValue] : *enabled) {
                if (toLowerAsciiLocal(trimAsciiLocal(enabledId))
                    == toLowerAsciiLocal(trimAsciiLocal(id))) {
                    parsed.enabled = parseRuntimeBool(enabledValue);
                    break;
                }
            }
        }
        const std::string key = toLowerAsciiLocal(trimAsciiLocal(id));
        const auto existing = std::find_if(
            state.campaignAiTriggers.begin(),
            state.campaignAiTriggers.end(),
            [&key](const RuntimeCampaignAiTriggerDefinition& candidate) {
                return toLowerAsciiLocal(trimAsciiLocal(candidate.id)) == key;
            }
        );
        if (existing == state.campaignAiTriggers.end()) {
            state.campaignAiTriggers.push_back(std::move(parsed));
        } else {
            *existing = std::move(parsed);
        }
    }
}

void commitRuntimeProjectilesFromSave(
    RuntimeProductionHostState& state,
    RuntimeProjectileSaveCandidate candidate) {
    state.projectiles = std::move(candidate.projectiles);
    state.nextProjectileActorId = std::max(1, candidate.nextActorId);
    state.recentExpiredProjectiles.clear();
    state.impactEffects.clear();
    state.recentReplayEvents.clear();
    state.nextReplayEventSequence = 1;
    state.snapshotCache.replayEventSnapshotsDirty = true;
    state.actionSummary.projectilesInFlight =
        static_cast<int>(state.projectiles.size());
}

bool validateRuntimeSavedPsychicStatuses(
    const RuntimeGameSessionState& liveState,
    RuntimePsychicStatusSystemState& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    std::string& errorMessage) {
    const auto sameText = [](const std::string& left,
                             const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    RuntimePsychicStatusReconcileInput reconcileInput;
    reconcileInput.currentTick = savedTick;
    reconcileInput.validateSources = true;
    reconcileInput.validateTargets = true;
    reconcileInput.objects.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
        reconcileInput.objects.push_back(
            runtimePsychicObjectSnapshot(liveState, object)
        );
    }
    std::unordered_set<std::uint64_t> inFlightProjectileActorIds;
    inFlightProjectileActorIds.reserve(savedProjectiles.projectiles.size());
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        inFlightProjectileActorIds.insert(
            static_cast<std::uint64_t>(projectile.actorId)
        );
    }

    for (const RuntimePsychicStatusRecord& status : candidate.statuses()) {
        const WarheadType* warhead =
            liveState.rules.findWarheadType(status.warheadId);
        if (!status.sourceValidatedAtLaunch
            || warhead == nullptr
            || !warhead->psychedelic
            || !runtimePsychicSourceRegistrationValid(
                liveState.rules,
                status.sourceKind,
                status.sourceTypeId
            )
            || status.lastHitEventId == 0
            || status.lastHitEventId
                >= static_cast<std::uint64_t>(savedProjectiles.nextActorId)
            || status.startedTick > status.lastRefreshTick
            || status.lastRefreshTick > savedTick
            || savedTick >= status.expiresTick
            || static_cast<long long>(status.expiresTick)
                    - static_cast<long long>(status.lastRefreshTick)
                > 1000000LL) {
            errorMessage = "invalid persisted psychic status attribution";
            return false;
        }
        const auto target = objectsById.find(status.targetObjectId);
        if (target == objectsById.end() || target->second == nullptr) {
            errorMessage = "persisted psychic target is missing";
            return false;
        }
        const RuntimePsychicObjectSnapshot targetSnapshot =
            runtimePsychicObjectSnapshot(liveState, *target->second);
        if (targetSnapshot.kind != status.targetKind
            || !sameText(targetSnapshot.owner, status.targetOwner)
            || !sameText(targetSnapshot.typeId, status.targetTypeId)) {
            errorMessage = "persisted psychic target identity mismatch";
            return false;
        }
        const auto source = objectsById.find(status.sourceObjectId);
        if (source != objectsById.end() && source->second != nullptr) {
            const RuntimePsychicObjectSnapshot sourceSnapshot =
                runtimePsychicObjectSnapshot(liveState, *source->second);
            if (sourceSnapshot.kind != status.sourceKind
                || !sameText(sourceSnapshot.owner, status.sourceOwner)
                || !sameText(sourceSnapshot.typeId, status.sourceTypeId)) {
                errorMessage = "persisted psychic source identity mismatch";
                return false;
            }
        }
    }
    for (const RuntimePsychicStatusHitEventRecord& event :
            candidate.hitEvents()) {
        const WarheadType* warhead =
            liveState.rules.findWarheadType(event.warheadId);
        if (event.projectileActorId == 0
            || event.projectileActorId
                >= static_cast<std::uint64_t>(savedProjectiles.nextActorId)
            || event.acceptedTick > savedTick
            || inFlightProjectileActorIds.contains(
                event.projectileActorId
            )
            || warhead == nullptr
            || !warhead->psychedelic
            || !runtimePsychicSourceRegistrationValid(
                liveState.rules,
                event.sourceKind,
                event.sourceTypeId
            )) {
            errorMessage = "invalid persisted psychic hit receipt";
            return false;
        }
    }

    const RuntimePsychicStatusReconcileReport report =
        candidate.reconcileSavedWorld(
            reconcileInput,
            savedObjects.size()
        );
    if (report.inputRejected
        || report.actionLimitReached
        || report.ended != 0U
        || !report.sourceIdentityChangedStatusIds.empty()) {
        errorMessage = "persisted psychic state failed world reconciliation";
        return false;
    }
    return true;
}

bool validateRuntimeSavedMagBeams(
    const RuntimeGameSessionState& liveState,
    const IniFile& ini,
    RuntimeMagBeamSystemState& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    const RuntimeSpecialMechanismState& specialMechanisms,
    const RuntimeRepairDepotSystemState& repairDepots,
    const RuntimeFactoryDeliveryState& factoryDeliveries,
    const RuntimeAircraftServiceSystemState& aircraftServices,
    std::string& errorMessage) {
    const auto sameText = [](const std::string& left,
                             const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    const auto sameDouble = [](double left, double right) {
        return std::abs(left - right) <= 1.0e-9;
    };
    const int lastAdvancedTick = candidate.lastAdvancedTick();
    if ((candidate.bindings().empty()
            && lastAdvancedTick != -1
            && lastAdvancedTick != savedTick)
        || (!candidate.bindings().empty()
            && lastAdvancedTick != savedTick)) {
        errorMessage = "RuntimeMagBeam tick does not match saved world";
        return false;
    }

    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }

    // A locomotor projectile is an exclusive mechanism even before impact.
    // Reject malformed mixed producers here rather than allowing a loaded
    // orphan bullet to silently choose a different consumer next tick.
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        const WeaponType* weapon = liveState.rules.findWeaponType(
            projectile.weaponId
        );
        const WarheadType* warhead = liveState.rules.findWarheadType(
            projectile.warheadId
        );
        if (warhead == nullptr || !warhead->isLocomotor) {
            continue;
        }
        if (weapon == nullptr
            || !sameText(weapon->warhead, warhead->id)
            || warhead->psychedelic || warhead->parasite
            || warhead->airstrike || weapon->limboLaunch
            || projectile.particleDamagePulse
            || projectile.suppressWarheadParticleSpawn
            || !trimAsciiLocal(warhead->particle).empty()
            || !trimAsciiLocal(warhead->particleSystem).empty()) {
            errorMessage =
                "persisted locomotor projectile mixes exclusive mechanisms";
            return false;
        }
    }

    const SpecialMechanismRules& rules =
        liveState.rules.getSpecialMechanismRules();
    std::unordered_set<int> bindingTargetIds;
    bindingTargetIds.reserve(candidate.bindings().size());
    for (const RuntimeMagBeamBindingState& binding : candidate.bindings()) {
        if (!bindingTargetIds.insert(binding.targetObjectId).second) {
            errorMessage = "RuntimeMagBeam has duplicate target authority";
            return false;
        }
        const auto targetIt = objectsById.find(binding.targetObjectId);
        if (targetIt == objectsById.end() || targetIt->second == nullptr) {
            errorMessage = "RuntimeMagBeam target is missing";
            return false;
        }
        const WorldObjectInstance& target = *targetIt->second;
        const UnitType* targetRules = nullptr;
        const RuntimeMagBeamTargetKind targetKind =
            runtimeMagBeamTargetKindForObject(
                liveState,
                target,
                targetRules
            );
        if (!target.alive || target.health <= 0 || targetRules == nullptr
            || targetKind != binding.targetKind
            || !sameText(target.owner, binding.targetOwner)
            || !sameText(target.type, binding.targetTypeId)
            || !sameText(target.mission, "MagBeamSuspended")
            || target.submerged
            || target.dockedToObjectId >= 0 || target.dockIndex >= 0
            || target.x != binding.kinematics.location.x
            || target.y != binding.kinematics.location.y
            || target.cell != binding.kinematics.location.cell
            || target.cell != runtimeCellKey(target.x, target.y)
            || target.level != binding.kinematics.location.level
            || target.onBridge != binding.kinematics.location.onBridge
            || runtimeMaxHealthForObject(liveState, target)
                != binding.targetMaximumStrength
            || targetRules->suppressionThreshold
                != binding.targetSuppressionThreshold
            || !sameDouble(targetRules->weight, binding.targetWeight)
            || !binding.originalLocomotorKnown
            || !sameText(
                targetRules->locomotor,
                binding.originalLocomotorId
            )) {
            errorMessage =
                "RuntimeMagBeam target world/rules identity mismatch";
            return false;
        }

        const MapCellFacts* cellFacts = runtimeMapQueryCell(
            liveState,
            {target.x, target.y}
        );
        constexpr std::int64_t q16One = 1LL << 16U;
        const std::int64_t worldXQ16 = static_cast<std::int64_t>(
            std::llround(binding.kinematics.worldX * q16One)
        );
        const std::int64_t worldYQ16 = static_cast<std::int64_t>(
            std::llround(binding.kinematics.worldY * q16One)
        );
        const std::int64_t worldZQ16 = static_cast<std::int64_t>(
            std::llround(binding.kinematics.worldZ * q16One)
        );
        const std::int64_t groundQ16 =
            static_cast<std::int64_t>(target.level) * q16One;
        const bool expectedOffGround = worldZQ16
            > groundQ16 + (q16One / 256);
        if (cellFacts == nullptr
            || static_cast<int>(std::llround(
                static_cast<double>(worldXQ16) / q16One
            )) != target.x
            || static_cast<int>(std::llround(
                static_cast<double>(worldYQ16) / q16One
            )) != target.y
            || terrainLevelAtRuntimeCell(liveState, target.x, target.y)
                != target.level
            || runtimeCellIsBridge(liveState, target.x, target.y)
                != binding.kinematics.location.onBridge
            || cellFacts->isWater != binding.kinematics.location.water
            || target.airborne != expectedOffGround
            || target.flightLevel != (expectedOffGround
                ? static_cast<int>(std::llround(
                    binding.kinematics.worldZ
                ))
                : target.level)
            || worldZQ16 < groundQ16) {
            errorMessage = "RuntimeMagBeam kinematics mismatch saved map"
                " phase="
                + std::string(runtimeMagBeamPhaseName(binding.phase))
                + " source=" + std::to_string(binding.sourceObjectId)
                + " target=" + std::to_string(binding.targetObjectId)
                + " q16=" + std::to_string(worldXQ16) + ","
                + std::to_string(worldYQ16) + ","
                + std::to_string(worldZQ16)
                + " location="
                + std::to_string(binding.kinematics.location.x) + ","
                + std::to_string(binding.kinematics.location.y) + ","
                + std::to_string(binding.kinematics.location.level)
                + "/" + runtimeBoolText(
                    binding.kinematics.location.onBridge
                )
                + "/" + runtimeBoolText(
                    binding.kinematics.location.water
                )
                + " object=" + std::to_string(target.x) + ","
                + std::to_string(target.y) + ","
                + std::to_string(target.level)
                + "/air=" + runtimeBoolText(target.airborne)
                + "/flight=" + std::to_string(target.flightLevel)
                + "/render=" + runtimeBoolText(target.hasRenderPosition)
                + "," + std::to_string(target.renderX)
                + "," + std::to_string(target.renderY)
                + "," + std::to_string(target.renderLevel)
                + " map=" + runtimeBoolText(cellFacts != nullptr)
                + "/terrain=" + std::to_string(
                    terrainLevelAtRuntimeCell(liveState, target.x, target.y)
                )
                + "/bridge=" + runtimeBoolText(
                    runtimeCellIsBridge(liveState, target.x, target.y)
                )
                + "/water=" + (cellFacts == nullptr
                    ? std::string("missing")
                    : runtimeBoolText(cellFacts->isWater))
                + "/offGround=" + runtimeBoolText(expectedOffGround)
                + "/groundQ16=" + std::to_string(groundQ16);
            return false;
        }

        const WeaponType* weapon = liveState.rules.findWeaponType(
            binding.weaponId
        );
        const WarheadType* warhead = liveState.rules.findWarheadType(
            binding.warheadId
        );
        if (weapon == nullptr || warhead == nullptr
            || !sameText(weapon->warhead, warhead->id)
            || !warhead->isLocomotor
            || !sameText(warhead->locomotor, binding.locomotorId)
            || weapon->damage != binding.rawDamage
            || weapon->magBeam != binding.magBeamVisual
            || !(static_cast<double>(weapon->damage) > targetRules->weight)
            || warhead->psychedelic || warhead->parasite
            || warhead->airstrike || weapon->limboLaunch
            || !trimAsciiLocal(warhead->particle).empty()
            || !trimAsciiLocal(warhead->particleSystem).empty()
            || !sameDouble(
                binding.fallingDamageMultiplier,
                rules.fallingDamageMultiplier
            )
            || binding.currentStrengthDamage
                != rules.currentStrengthDamage) {
            errorMessage = "RuntimeMagBeam weapon/rules mismatch";
            return false;
        }

        if (binding.phase != RuntimeMagBeamPhase::Falling) {
            const auto sourceIt = objectsById.find(binding.sourceObjectId);
            if (sourceIt == objectsById.end() || sourceIt->second == nullptr
                || !sourceIt->second->alive
                || sourceIt->second->health <= 0
                || !sameText(sourceIt->second->owner, binding.sourceOwner)
                || !sameText(sourceIt->second->type, binding.sourceTypeId)) {
                errorMessage = "RuntimeMagBeam live source identity mismatch";
                return false;
            }
        }

        bool sectionValid = false;
        for (const std::string& section : {
                std::string("Behaviors"),
                std::string("SubcellReservations"),
                std::string("EngineerIntents"),
                std::string("OccupyIntents"),
                std::string("TransportIntents"),
                std::string("TimedBombIntents")}) {
            const bool referenced = runtimeSavedSectionReferencesActor(
                ini,
                section,
                binding.targetObjectId,
                sectionValid
            );
            if (!sectionValid || referenced) {
                errorMessage =
                    "RuntimeMagBeam target retains competing saved intent";
                return false;
            }
        }
        if (std::any_of(
                repairDepots.relations().begin(),
                repairDepots.relations().end(),
                [&binding](const RuntimeRepairDepotRelation& relation) {
                    return relation.unitId == binding.targetObjectId;
                }
            )
            || std::any_of(
                factoryDeliveries.leases().begin(),
                factoryDeliveries.leases().end(),
                [&binding](const RuntimeFactoryDeliveryLease& lease) {
                    return lease.producedObjectId == binding.targetObjectId;
                }
            )
            || std::any_of(
                aircraftServices.dockBindings().begin(),
                aircraftServices.dockBindings().end(),
                [&binding](
                    const RuntimeAircraftServiceDockBinding& dock
                ) {
                    return dock.aircraftId == binding.targetObjectId;
                }
            )
            || specialMechanisms.isChronoInTransit(binding.targetObjectId)
            || specialMechanisms.isTemporalFrozen(binding.targetObjectId)
            || specialMechanisms.parasiteLinks().contains(
                binding.targetObjectId
            )
            || specialMechanisms.isCarrierChild(binding.targetObjectId)) {
            errorMessage =
                "RuntimeMagBeam target retains competing runtime authority";
            return false;
        }
    }
    for (const WorldObjectInstance& object : savedObjects) {
        const bool suspended = sameText(
            object.mission,
            "MagBeamSuspended"
        );
        if (suspended != bindingTargetIds.contains(object.instanceId)) {
            errorMessage =
                "RuntimeMagBeam target mission/authority is not bidirectional";
            return false;
        }
    }

    return true;
}

bool restoreRuntimeGattlingTimersFromSave(
    RuntimeGameSessionState& state,
    const IniFile& ini,
    std::string& errorMessage,
    int saveVersion) {
    state.gattlingWeaponTimers.clear();
    if (ini.getSection("GattlingWeaponTimers") == nullptr) {
        return true;
    }

    int schema = 0;
    int count = 0;
    if (!parseRuntimeInt(
            ini.getValue("GattlingWeaponTimers", "Schema", ""),
            schema
        )
        || schema != 1
        || !parseRuntimeInt(
            ini.getValue("GattlingWeaponTimers", "Count", ""),
            count
        )
        || count < 0
        || count > 100000) {
        errorMessage = "invalid [GattlingWeaponTimers] header";
        return false;
    }

    std::unordered_set<int> seenObjectIds;
    for (int index = 0; index < count; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue(
                "GattlingWeaponTimers",
                std::to_string(index),
                ""
            )
        );
        int objectId = -1;
        int timer = 0;
        if (tokens.size() != 2U
            || !parseRuntimeInt(tokens[0], objectId)
            || !parseRuntimeInt(tokens[1], timer)
            || objectId < 0
            || !seenObjectIds.insert(objectId).second) {
            errorMessage = "invalid [GattlingWeaponTimers] record "
                + std::to_string(index);
            return false;
        }
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object == nullptr || !object->alive) {
            if (saveVersion >= 13) {
                errorMessage =
                    "v13 gattling timer references unavailable object";
                return false;
            }
            continue;
        }
        const RuntimeCachedCapabilityProfile& cached =
            runtimeCachedCapabilityProfileForObject(state, *object);
        if (!cached.gattling) {
            if (saveVersion >= 13) {
                errorMessage =
                    "v13 gattling timer references non-gattling object";
                return false;
            }
            continue;
        }
        const int timerCap = runtimeGattlingTimerCap(
            cached,
            object->veterancy >= 2
        );
        if (saveVersion >= 13 && (timer < 0 || timer > timerCap)) {
            errorMessage = "v13 gattling timer is outside authored range";
            return false;
        }
        timer = std::clamp(
            timer,
            0,
            timerCap
        );
        if (timer > 0) {
            state.gattlingWeaponTimers.emplace(objectId, timer);
        }
    }
    return true;
}

bool restoreRuntimeCratesFromSave(
    RuntimeGameSessionState& state,
    const IniFile& ini,
    std::string& errorMessage) {
    if (ini.getSection("Crates") == nullptr) {
        state.skirmishCrates = state.campaignMode
            ? std::vector<RuntimeCrateState>{}
            : state.initialSkirmishCrates;
        return true;
    }

    int schema = 0;
    int count = 0;
    if (!parseRuntimeInt(ini.getValue("Crates", "Schema", ""), schema)
        || (schema != 1 && schema != 2 && schema != 3 && schema != 4)
        || !parseRuntimeInt(ini.getValue("Crates", "Count", ""), count)
        || count < 0
        || count > 100000) {
        errorMessage = "invalid [Crates] header";
        return false;
    }

    std::vector<RuntimeCrateState> restored;
    restored.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Crates", std::to_string(index), "")
        );
        RuntimeCrateState crate;
        const std::size_t expectedFields = schema == 1
            ? 5U
            : (schema == 2 ? 9U : (schema == 3 ? 10U : 11U));
        if (tokens.size() != expectedFields
            || !parseRuntimeInt(tokens[0], crate.cell.x)
            || !parseRuntimeInt(tokens[1], crate.cell.y)
            || !parseRuntimeStrictBool(tokens[2], crate.active)
            || !parseRuntimeInt(tokens[3], crate.respawnTick)
            || !parseRuntimeInt(tokens[4], crate.value)
            || crate.respawnTick < 0
            || crate.value < 0
            || (!state.terrainByCell.empty()
                && state.terrainByCell.find(
                    runtimeCellKey(crate.cell.x, crate.cell.y)
                ) == state.terrainByCell.end())) {
            errorMessage = "invalid [Crates] record "
                + std::to_string(index);
            return false;
        }
        if (schema >= 2) {
            if (!parseRuntimeStrictBool(tokens[5], crate.water)
                || !parseRuntimeFiniteDouble(tokens[8], crate.powerupData)
                || crate.powerupData < -runtimeCratePowerupDataLimit
                || crate.powerupData > runtimeCratePowerupDataLimit) {
                errorMessage = "invalid [Crates] powerup record "
                    + std::to_string(index);
                return false;
            }
            crate.powerupType = trimAsciiLocal(tokens[6]);
            crate.powerupAnimation = trimAsciiLocal(tokens[7]);
            if (schema >= 3) {
                crate.imageId = trimAsciiLocal(tokens[9]);
                if (schema >= 4 && !parseRuntimeStrictBool(tokens[10], crate.authored)) {
                    errorMessage = "invalid [Crates] authored record " + std::to_string(index);
                    return false;
                }
            } else {
                crate.imageId = runtimeCrateArtImageId(
                    state.rules,
                    crate.water
                        ? state.rules.getCrateRules().waterImage
                        : state.rules.getCrateRules().image
                );
            }
        } else {
            const auto terrain = state.terrainInfoByCell.find(
                runtimeCellKey(crate.cell.x, crate.cell.y)
            );
            crate.water = terrain != state.terrainInfoByCell.end()
                && terrain->second.water
                && !terrain->second.bridge;
            crate.imageId = runtimeCrateArtImageId(
                state.rules,
                crate.water
                    ? state.rules.getCrateRules().waterImage
                    : state.rules.getCrateRules().image
            );
        }
        if (schema < 4) crate.authored = state.campaignMode || crate.respawnTick == 0;
        restored.push_back(crate);
    }
    if (schema >= 4 && (!parseRuntimeStrictBool(ini.getValue("Crates", "TruckCrate", ""), state.triggers.truckCrate)
        || !parseRuntimeStrictBool(ini.getValue("Crates", "TrainCrate", ""), state.triggers.trainCrate))) {
        errorMessage = "invalid [Crates] map switches";
        return false;
    }
    state.skirmishCrates = std::move(restored);
    return true;
}

bool restoreRuntimeSmudgesFromSave(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage) {
    if (ini.getSection("Smudges") == nullptr) {
        // Legacy saves inherit the map-authored collection.
        return true;
    }

    int schema = 0;
    int count = 0;
    if (!parseRuntimeInt(ini.getValue("Smudges", "Schema", ""), schema)
        || (schema != 1 && schema != 2)
        || !parseRuntimeInt(ini.getValue("Smudges", "Count", ""), count)
        || count < 0
        || count > 100000) {
        errorMessage = "invalid [Smudges] header";
        return false;
    }

    std::vector<WorldSmudge> restored;
    restored.reserve(static_cast<std::size_t>(count));
    std::unordered_map<int, bool> occupiedCells;
    for (int index = 0; index < count; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Smudges", std::to_string(index), "")
        );
        WorldSmudge smudge;
        if (tokens.size() != (schema == 2 ? 8U : 7U)
            || !parseRuntimeInt(tokens[2], smudge.x)
            || !parseRuntimeInt(tokens[3], smudge.y)
            || !parseRuntimeInt(tokens[4], smudge.cell)
            || !parseRuntimeInt(tokens[5], smudge.level)
            || !parseRuntimeInt(tokens[6], smudge.data)) {
            errorMessage = "invalid [Smudges] record "
                + std::to_string(index);
            return false;
        }
        smudge.mapId = trimAsciiLocal(tokens[0]);
        smudge.type = trimAsciiLocal(tokens[1]);
        if (schema == 2) {
            const std::string source = toLowerAsciiLocal(
                trimAsciiLocal(tokens[7])
            );
            if (source != "yes" && source != "no"
                && source != "true" && source != "false"
                && source != "1" && source != "0") {
                errorMessage = "invalid [Smudges] source "
                    + std::to_string(index);
                return false;
            }
            smudge.authored = source == "yes"
                || source == "true"
                || source == "1";
        } else {
            smudge.authored = toLowerAsciiLocal(smudge.mapId).rfind(
                "runtime:",
                0
            ) != 0;
        }
        const int expectedCell = runtimeCellKey(smudge.x, smudge.y);
        const auto terrain = state.terrainByCell.find(expectedCell);
        const std::string wantedType = toUpperAsciiLocal(smudge.type);
        const auto smudgeType = std::find_if(
            state.rules.getSmudgeTypes().begin(),
            state.rules.getSmudgeTypes().end(),
            [&wantedType](const SmudgeType& candidate) {
                return toUpperAsciiLocal(trimAsciiLocal(candidate.id))
                    == wantedType;
            }
        );
        if (smudge.type.empty()
            || expectedCell < 0
            || smudge.cell != expectedCell
            || smudge.data != 0
            || smudgeType == state.rules.getSmudgeTypes().end()
            || terrain == state.terrainByCell.end()
            || terrain->second == nullptr
            || smudge.level != terrain->second->level) {
            errorMessage = "invalid [Smudges] value "
                + std::to_string(index);
            return false;
        }
        const int width = std::max(1, smudgeType->width);
        const int height = std::max(1, smudgeType->height);
        for (int dy = 0; dy < height; ++dy) {
            for (int dx = 0; dx < width; ++dx) {
                const int footprintCell = runtimeCellKey(
                    smudge.x + dx,
                    smudge.y + dy
                );
                const auto footprintTerrain = state.terrainByCell.find(
                    footprintCell
                );
                const auto occupied = occupiedCells.find(footprintCell);
                const bool overlapAllowed = occupied != occupiedCells.end()
                    && occupied->second
                    && smudge.authored;
                if (footprintCell < 0
                    || footprintTerrain == state.terrainByCell.end()
                    || footprintTerrain->second == nullptr
                    || footprintTerrain->second->level != smudge.level
                    || (occupied != occupiedCells.end() && !overlapAllowed)) {
                    errorMessage = "invalid [Smudges] footprint "
                        + std::to_string(index);
                    return false;
                }
                if (occupied == occupiedCells.end()) {
                    occupiedCells.emplace(footprintCell, smudge.authored);
                }
            }
        }
        restored.push_back(std::move(smudge));
    }
    state.world.accessSmudges() = std::move(restored);
    state.repaintRequested = true;
    return true;
}

void restoreRuntimeTerrainObjectsFromSave(
    RuntimeProductionHostState& state,
    const IniFile& ini) {
    captureRuntimeInitialTerrainObjectState(state);
    std::vector<WorldTerrainObject>& terrainObjects =
        state.world.accessTerrainObjects();
    for (std::size_t index = 0; index < terrainObjects.size(); ++index) {
        WorldTerrainObject& object = terrainObjects[index];
        const auto initial = state.terrainObjectInitialStateByKey.find(
            runtimeTerrainObjectStateKey(object, index)
        );
        if (initial == state.terrainObjectInitialStateByKey.end()) {
            const int maximumHealth = std::max(1, object.strength);
            object.health = std::clamp(object.health, 0, maximumHealth);
            object.alive = object.alive && object.health > 0;
        } else {
            object.health = initial->second.health;
            object.alive = initial->second.alive;
        }
        if (!object.alive) {
            object.health = 0;
        }
    }

    if (ini.getSection("TerrainObjects") != nullptr) {
        std::unordered_map<std::string, WorldTerrainObject*> objectsByKey;
        objectsByKey.reserve(terrainObjects.size());
        for (std::size_t index = 0; index < terrainObjects.size(); ++index) {
            objectsByKey.emplace(
                runtimeTerrainObjectStateKey(terrainObjects[index], index),
                &terrainObjects[index]
            );
        }
        const int count = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue("TerrainObjects", "Count", "0"),
                0
            )
        );
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "TerrainObjects",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 6) {
                continue;
            }
            const auto found = objectsByKey.find(tokens[0]);
            if (found == objectsByKey.end() || found->second == nullptr) {
                continue;
            }
            WorldTerrainObject& object = *found->second;
            if (toLowerAsciiLocal(trimAsciiLocal(object.type))
                    != toLowerAsciiLocal(trimAsciiLocal(tokens[1]))
                || object.x != parseRuntimeIntOrDefault(tokens[2], -1)
                || object.y != parseRuntimeIntOrDefault(tokens[3], -1)) {
                continue;
            }
            const int maximumHealth = std::max(1, object.strength);
            object.health = std::clamp(
                parseRuntimeIntOrDefault(tokens[4], object.health),
                0,
                maximumHealth
            );
            object.alive = parseRuntimeBool(tokens[5])
                && object.health > 0;
            if (!object.alive) {
                object.health = 0;
            }
        }
    }

    state.attackApproachCache.clear();
    state.pathFailureCache.clear();
    state.placementChecksCacheTick = -1;
    invalidateRuntimeTickOccupancyCache(state);
    invalidateRuntimeObjectProjectionCache(state);
    state.navigationRefreshPending = true;
    state.radarTerrainKey.clear();
    state.repaintRequested = true;
}

bool materializeRuntimeSavedTerrainProjection(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage) {
    if (!resetRuntimeOverlayWallsForSavedProjection(state, errorMessage)) return false;
    const int resourceOverlayCount = parseRuntimeIntOrDefault(
        ini.getValue("ResourceOverlays", "Count", "-1"),
        -1
    );
    const std::string initialOreSeededRaw = trimAsciiLocal(
        ini.getValue("RuntimeSave", "InitialOreFieldsSeeded", "")
    );
    state.initialOreFieldsSeeded = !initialOreSeededRaw.empty()
        ? parseRuntimeBool(initialOreSeededRaw)
        : resourceOverlayCount >= 0;
    state.initialOreCellsSeeded = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "InitialOreCellsSeeded", "0"),
        0
    );
    state.initialOreCellsDensified = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "InitialOreCellsDensified", "0"),
        0
    );
    if (resourceOverlayCount >= 0) {
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            if (!terrain.overlayIsOre && !terrain.overlayIsGem) {
                continue;
            }
            terrain.hasOverlay = false;
            terrain.overlayIndex = -1;
            terrain.overlayFrame = 0;
            terrain.overlayType.clear();
            terrain.overlayImage.clear();
            terrain.overlayIsOre = false;
            terrain.overlayIsGem = false;
        }

        std::unordered_map<int, TerrainCell*> terrainByCell;
        terrainByCell.reserve(state.world.accessTerrainCells().size());
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            terrainByCell[runtimeCellKey(terrain.x, terrain.y)] = &terrain;
        }
        for (int index = 0; index < resourceOverlayCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("ResourceOverlays", std::to_string(index), "")
            );
            if (tokens.size() < 8U) {
                continue;
            }
            const int x = parseRuntimeIntOrDefault(tokens[0], -1);
            const int y = parseRuntimeIntOrDefault(tokens[1], -1);
            const auto terrainIt = terrainByCell.find(runtimeCellKey(x, y));
            if (terrainIt == terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            TerrainCell& terrain = *terrainIt->second;
            terrain.hasOverlay = true;
            terrain.overlayIndex = parseRuntimeIntOrDefault(tokens[2], -1);
            terrain.overlayFrame = parseRuntimeIntOrDefault(tokens[3], 0);
            terrain.overlayType = tokens[4];
            terrain.overlayImage = tokens[5];
            terrain.overlayIsOre = parseRuntimeBool(tokens[6]);
            terrain.overlayIsGem = parseRuntimeBool(tokens[7]);
        }
    }

    const int bridgeOverlayCount = parseRuntimeIntOrDefault(
        ini.getValue("BridgeOverlays", "Count", "-1"),
        -1
    );
    if (bridgeOverlayCount >= 0) {
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            if (!terrain.hasOverlay || !runtimeOverlayLooksBridge(terrain)) {
                continue;
            }
            terrain.hasOverlay = false;
            terrain.overlayIndex = -1;
            terrain.overlayFrame = 0;
            terrain.overlayType.clear();
            terrain.overlayImage.clear();
            terrain.overlayIsOre = false;
            terrain.overlayIsGem = false;
        }

        std::unordered_map<int, TerrainCell*> terrainByCell;
        terrainByCell.reserve(state.world.accessTerrainCells().size());
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            terrainByCell[runtimeCellKey(terrain.x, terrain.y)] = &terrain;
        }
        for (int index = 0; index < bridgeOverlayCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("BridgeOverlays", std::to_string(index), "")
            );
            if (tokens.size() < 8U) {
                continue;
            }
            const int x = parseRuntimeIntOrDefault(tokens[0], -1);
            const int y = parseRuntimeIntOrDefault(tokens[1], -1);
            const auto terrainIt = terrainByCell.find(runtimeCellKey(x, y));
            if (terrainIt == terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            TerrainCell& terrain = *terrainIt->second;
            terrain.hasOverlay = true;
            terrain.overlayIndex = parseRuntimeIntOrDefault(tokens[2], -1);
            terrain.overlayFrame = parseRuntimeIntOrDefault(tokens[3], 0);
            terrain.overlayType = tokens[4];
            terrain.overlayImage = tokens[5];
            terrain.overlayIsOre = parseRuntimeBool(tokens[6]);
            terrain.overlayIsGem = parseRuntimeBool(tokens[7]);
        }
    }

    state.bridgeHealthByCell.clear();
    const int bridgeHealthCount = parseRuntimeIntOrDefault(
        ini.getValue("BridgeHealth", "Count", "0"),
        0
    );
    for (int index = 0; index < bridgeHealthCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("BridgeHealth", std::to_string(index), "")
        );
        if (tokens.size() < 2U) {
            continue;
        }
        const int cellKey = parseRuntimeIntOrDefault(tokens[0], -1);
        if (cellKey < 0) {
            continue;
        }
        state.bridgeHealthByCell[cellKey] = std::max(
            0,
            parseRuntimeIntOrDefault(tokens[1], 0)
        );
    }

    if (!restoreRuntimeOverlayWallStates(state, ini, errorMessage)) return false;
    restoreRuntimeTerrainObjectsFromSave(state, ini);
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    rebuildRuntimeTerrainInfoCache(state);
    return restoreRuntimeSmudgesFromSave(state, ini, errorMessage);
}

bool runtimeSavedAuthoredInactivePlaceholder(const RuntimeGameSessionState& context,
    const WorldObjectInstance& object) {
    // C1A02MD contains a zero-health CALA02 whose stock rules section was
    // commented out by Westwood. Keep that exact inert map record on restore;
    // it cannot create a unit, revive the placeholder or acquire new rules.
    if (object.kind != WorldObjectKind::Structure || object.authoredAircraft
        || object.alive || object.health != 0 || object.mapId.empty()) return false;
    IniFile mapIni;
    MapDatabase map;
    if (!mapIni.loadFromFile(context.config.defaultMapPath) || !map.loadFromIni(mapIni)) return false;
    return std::any_of(map.getStructures().begin(), map.getStructures().end(),
        [&](const auto& authored) {
            return authored.id == object.mapId && authored.type == object.type && authored.health == 0
                && authored.owner == object.owner
                && authored.x == object.x && authored.y == object.y;
        });
}

bool materializeRuntimeSavedTransportManifests(
    RuntimeGameSessionState& context,
    const IniFile& ini,
    int saveVersion,
    std::string& errorMessage) {
    if (saveVersion >= 13
        && ini.getSection("TransportManifests") == nullptr) {
        errorMessage = "v13 save is missing transport manifests";
        return false;
    }
    int manifestCount = 0;
    if (!parseRuntimeInt(
            ini.getValue("TransportManifests", "Count", "0"),
            manifestCount
        )
        || manifestCount < 0 || manifestCount > 100000) {
        errorMessage = "invalid transport manifest header";
        return false;
    }

    std::vector<WorldObjectInstance>& objects = context.world.accessObjects();
    std::unordered_map<int, WorldObjectInstance*> objectsById;
    objectsById.reserve(objects.size());
    for (WorldObjectInstance& object : objects) {
        object.transportPassengerIds.clear();
        objectsById.emplace(object.instanceId, &object);
    }
    std::unordered_set<int> transportIds;
    std::unordered_set<int> passengerIds;
    for (int index = 0; index < manifestCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TransportManifests", std::to_string(index), "")
        );
        int transportId = -1;
        const bool strictRecord = tokens.size() == 2U
            && parseRuntimeInt(tokens[0], transportId);
        const auto transportFound = strictRecord
            ? objectsById.find(transportId)
            : objectsById.end();
        WorldObjectInstance* transport = transportFound == objectsById.end()
            ? nullptr
            : transportFound->second;
        const RuntimeTransportProfile profile = transport == nullptr
            ? RuntimeTransportProfile{}
            : runtimeTransportProfileForObject(context, *transport);
        const bool validTransport = strictRecord
            && transport != nullptr
            && transport->health > 0
            && (transport->alive || (transport->kind == WorldObjectKind::Unit
                && toLowerAsciiLocal(trimAsciiLocal(transport->mission)) == "insidetransport"))
            && (transport->kind == WorldObjectKind::Unit
                || runtimeInfantryAbsorberType(context, *transport) != nullptr)
            && profile.canTransport
            && profile.passengerCapacity > 0
            && transportIds.insert(transportId).second;
        if (!validTransport) {
            if (saveVersion >= 13) {
                errorMessage = "invalid v13 transport manifest host";
                return false;
            }
            continue;
        }

        std::stringstream passengers(tokens[1]);
        std::string passengerToken;
        bool foundPassenger = false;
        while (std::getline(passengers, passengerToken, ',')) {
            int passengerId = -1;
            const bool parsedPassenger = parseRuntimeInt(
                passengerToken,
                passengerId
            );
            const auto passengerFound = parsedPassenger
                ? objectsById.find(passengerId)
                : objectsById.end();
            WorldObjectInstance* passenger =
                passengerFound == objectsById.end()
                    ? nullptr
                    : passengerFound->second;
            const bool capacityAvailable = passenger != nullptr
                && runtimeTransportPassengerSpace(context, *transport, *passenger)
                    <= profile.passengerCapacity - runtimeTransportUsedSpace(context, *transport);
            const bool validPassenger = parsedPassenger
                && passengerId >= 0
                && passenger != nullptr
                && passenger != transport
                && runtimeTransportPassengerTypeAllowed(context, *passenger, *transport)
                && passenger->health > 0
                && !passenger->alive
                && toLowerAsciiLocal(trimAsciiLocal(passenger->mission))
                    == "insidetransport"
                && passenger->dockedToObjectId < 0
                && passenger->dockIndex < 0
                && !passenger->airborne
                && passenger->subCell == -1
                && capacityAvailable
                && passengerIds.insert(passengerId).second;
            if (!validPassenger) {
                if (saveVersion >= 13) {
                    errorMessage = "invalid v13 transport manifest passenger";
                    return false;
                }
                continue;
            }
            foundPassenger = true;
            passenger->x = transport->x;
            passenger->y = transport->y;
            passenger->cell = transport->cell;
            passenger->level = transport->level;
            passenger->onBridge = transport->onBridge;
            passenger->subCell = -1;
            clearRuntimeRenderPosition(*passenger);
            transport->transportPassengerIds.push_back(passengerId);
        }
        if (saveVersion >= 13 && !foundPassenger) {
            errorMessage = "empty v13 transport manifest";
            return false;
        }
        refreshRuntimeTransportGunnerState(context, *transport);
    }

    if (saveVersion >= 13) {
        for (const WorldObjectInstance& object : objects) {
            const bool contained = !object.alive && object.health > 0
                && toLowerAsciiLocal(trimAsciiLocal(object.mission))
                    == "insidetransport";
            if (contained != passengerIds.contains(object.instanceId)) {
                errorMessage = "v13 transport containment is not bidirectional";
                return false;
            }
        }
    }
    for (int hostId : transportIds) {
        if (runtimeTransportContainsCargo(context, *objectsById.at(hostId), hostId)) {
            errorMessage = "cyclic transport containment";
            return false;
        }
    }
    for (const auto& object : objects) {
        if (object.alive) syncRuntimeTransportCargoPositions(context, object);
    }
    return true;
}

bool validateRuntimeSavedBehaviorRecords(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    std::string& errorMessage) {
    if (saveVersion < 9) {
        return true;
    }

    int behaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0
        || behaviorCount > 100000) {
        errorMessage = "invalid behavior count";
        return false;
    }

    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    std::unordered_set<int> behaviorObjectIds;
    behaviorObjectIds.reserve(static_cast<std::size_t>(behaviorCount));
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int savedObjectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], savedObjectId)) {
            errorMessage = "invalid behavior object id";
            return false;
        }
        const auto object = objectsById.find(savedObjectId);
        int parsedObjectId = -1;
        RuntimeObjectBehavior behavior;
        if (object == objectsById.end()
            || object->second == nullptr
            || !object->second->alive
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *object->second,
                saveVersion,
                parsedObjectId,
                behavior
            )
            || !runtimeOverlayWallBehaviorTargetIsValid(state, behavior)
            || parsedObjectId != savedObjectId
            || !behaviorObjectIds.insert(savedObjectId).second) {
            errorMessage = "invalid behavior record";
            return false;
        }
    }
    return true;
}

bool validateRuntimeSavedRepairDepots(
    const RuntimeProductionHostState& liveState,
    const IniFile& ini,
    int saveVersion,
    const RuntimeGateSystemState& savedGates,
    RuntimeRepairDepotSystemState& validatedState,
    bool& hasExplicitSection,
    std::string& errorMessage) {
    hasExplicitSection =
        ini.getSection("RepairDepotRuntime") != nullptr;
    validatedState.reset(std::max(1, liveState.config.fixedTickHz));
    if (!hasExplicitSection) {
        return true;
    }

    int declaredRelationCount = -1;
    int declaredAnimationCount = -1;
    int emptyStateSavedTick = -1;
    if (parseRuntimeInt(
            ini.getValue(
                "RepairDepotRuntime",
                "RelationCount",
                ""
            ),
            declaredRelationCount
        )
        && declaredRelationCount == 0
        && parseRuntimeInt(ini.getValue("RepairDepotRuntime", "AnimationCount", "0"), declaredAnimationCount)
        && declaredAnimationCount == 0
        && parseRuntimeInt(
            ini.getValue("RuntimeSave", "Tick", ""),
            emptyStateSavedTick
        )
        && emptyStateSavedTick >= 0) {
        const std::vector<WorldObjectInstance> emptyObjects;
        RuntimeRepairDepotContext emptyContext;
        emptyContext.rules = &liveState.rules;
        emptyContext.objects = &emptyObjects;
        RuntimeRepairDepotReconcileReport emptyReport;
        std::string emptyError;
        if (!validatedState.load(
                ini,
                emptyContext,
                &emptyError,
                &emptyReport
            )) {
            errorMessage =
                "invalid empty repair depot runtime: " + emptyError;
            return false;
        }
        if (validatedState.fixedTickHz()
                != std::max(1, liveState.config.fixedTickHz)
            || validatedState.lastAdvancedTick() > emptyStateSavedTick
            || !validatedState.relations().empty()
            || emptyReport.removedHostTimers != 0U
            || emptyReport.removedHostAnimations != 0U
            || !emptyReport.actions.empty()) {
            errorMessage =
                "empty repair depot runtime diverged during staged load";
            return false;
        }
        return true;
    }

    int savedTick = -1;
    int behaviorCount = -1;
    int savedCredits = -1;
    const std::string savedOwner = trimAsciiLocal(
        ini.getValue("Economy", "Owner", "")
    );
    if (!parseRuntimeInt(
            ini.getValue("RuntimeSave", "Tick", ""),
            savedTick
        )
        || savedTick < 0
        || !parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0
        || behaviorCount > 100000
        || savedOwner.empty()
        || !parseRuntimeInt(
            ini.getValue("Economy", "Credits", ""),
            savedCredits
        )
        || savedCredits < 0
        || savedCredits > runtimeMaximumCreditBalance) {
        errorMessage =
            "invalid saved world, behavior, or economy header for repair depot";
        return false;
    }

    auto candidateOwned = std::make_unique<RuntimeProductionHostState>(*liveState.constructionUiClock, *liveState.constructionAudioClock, liveState.geometry);
    RuntimeProductionHostState& candidate = *candidateOwned;
    candidate.config = liveState.config;
    candidate.report = liveState.report;
    candidate.rules = liveState.rules;
    candidate.art = liveState.art;
    candidate.mapFacts = liveState.mapFacts;
    candidate.rulesLoaded = liveState.rulesLoaded;
    candidate.artLoaded = liveState.artLoaded;
    candidate.mapFactsLoaded = liveState.mapFactsLoaded;
    candidate.campaignMode = liveState.campaignMode;
    candidate.destroyableBridges = liveState.destroyableBridges;
    candidate.skirmishCountryByOwner = liveState.skirmishCountryByOwner;
    candidate.skirmishTeamByOwner = liveState.skirmishTeamByOwner;
    candidate.skirmishDifficultyByOwner =
        liveState.skirmishDifficultyByOwner;
    candidate.cooperativeSession = liveState.cooperativeSession;
    candidate.cooperativeSessionStatus =
        liveState.cooperativeSessionStatus;
    candidate.structureFootprints = liveState.structureFootprints;
    candidate.terrainInfoByCell = liveState.terrainInfoByCell;
    candidate.bridgeHealthByCell = liveState.bridgeHealthByCell;
    candidate.economy = liveState.economy;
    candidate.economy.owner = savedOwner;
    candidate.economy.credits = savedCredits;
    candidate.triggers = liveState.triggers;
    candidate.computerAiByOwner.clear();
    candidate.world = liveState.world;
    candidate.terrainObjectInitialStateByKey =
        liveState.terrainObjectInitialStateByKey;
    candidate.tick = savedTick;

    RuntimeGarrisonSystemState candidateGarrisons(
        runtimeSavedGarrisonConfig(liveState)
    );
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        RuntimeGarrisonReconcileReport garrisonReport;
        std::string garrisonError;
        if (!candidateGarrisons.load(
                ini,
                candidate.rules,
                candidate.world.accessObjects(),
                &garrisonError,
                &garrisonReport
            )) {
            errorMessage =
                "invalid garrison state for repair depot runtime: "
                + garrisonError;
            return false;
        }
    }

    // Garrison load commits its staged object vector with move assignment.
    // Build pointer indexes only after that replacement so behavior and
    // repair validation never retain addresses into the discarded buffer.
    std::unordered_map<int, WorldObjectInstance*> objectsById;
    objectsById.reserve(candidate.world.accessObjects().size());
    for (WorldObjectInstance& object : candidate.world.accessObjects()) {
        objectsById.emplace(object.instanceId, &object);
    }

    candidate.behaviors.clear();
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int savedObjectId = -1;
        if (tokens.size() < 11U
            || !parseRuntimeInt(tokens[0], savedObjectId)) {
            errorMessage = "invalid behavior record for repair depot runtime";
            return false;
        }
        const auto object = objectsById.find(savedObjectId);
        int parsedObjectId = -1;
        RuntimeObjectBehavior behavior;
        if (object == objectsById.end()
            || object->second == nullptr
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *object->second,
                saveVersion,
                parsedObjectId,
                behavior
            )
            || parsedObjectId != savedObjectId
            || !candidate.behaviors.emplace(
                savedObjectId,
                std::move(behavior)
            ).second) {
            errorMessage = "invalid behavior state for repair depot runtime";
            return false;
        }
    }

    int computerAiCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("ComputerAI", "Count", "0"),
            computerAiCount
        )
        || computerAiCount < 0
        || computerAiCount > 100000) {
        errorMessage = "invalid AI economy header for repair depot runtime";
        return false;
    }
    for (int index = 0; index < computerAiCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("ComputerAI", std::to_string(index), "")
        );
        bool initialized = false;
        int credits = -1;
        const std::string owner = tokens.size() >= 2U
            ? trimAsciiLocal(tokens[1])
            : std::string();
        if (tokens.size() < 28U
            || owner.empty()
            || !parseRuntimeStrictBool(tokens[0], initialized)
            || !parseRuntimeInt(tokens[6], credits)
            || credits < 0
            || credits > runtimeMaximumCreditBalance) {
            errorMessage = "invalid AI economy record for repair depot runtime";
            return false;
        }
        RuntimeComputerAiState ai;
        ai.initialized = initialized;
        ai.owner = owner;
        ai.credits = credits;
        if (!candidate.computerAiByOwner.emplace(
                toLowerAsciiLocal(owner),
                std::move(ai)
            ).second) {
            errorMessage = "duplicate AI economy owner for repair depot runtime";
            return false;
        }
    }

    if (candidate.campaignMode) {
        int houseCount = -1;
        if (!parseRuntimeInt(
                ini.getValue("CampaignHouseAI", "Count", "0"),
                houseCount
            )
            || houseCount < 0
            || houseCount > 100000) {
            errorMessage =
                "invalid campaign economy header for repair depot runtime";
            return false;
        }
        for (int index = 0; index < houseCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "CampaignHouseAI",
                    std::to_string(index),
                    ""
                )
            );
            int credits = -1;
            bool playerControlled = false;
            if (tokens.size() < 12U
                || trimAsciiLocal(tokens[0]).empty()
                || !parseRuntimeInt(tokens[5], credits)
                || credits < 0
                || !parseRuntimeStrictBool(tokens[9], playerControlled)) {
                errorMessage =
                    "invalid campaign economy record for repair depot runtime";
                return false;
            }
            const std::string owner = runtimeCampaignHouseName(
                candidate,
                tokens[0]
            );
            const auto house =
                candidate.triggers.campaignHouseProduction.find(
                    toLowerAsciiLocal(trimAsciiLocal(owner))
                );
            if (house != candidate.triggers.campaignHouseProduction.end()) {
                house->second.credits = credits;
                house->second.playerControlled = playerControlled;
            }
        }
    }

    candidate.gates = savedGates;
    candidate.gatesInitialized = true;
    rebuildRuntimeGateFootprintIndex(candidate);
    candidate.terrainByCell = buildRuntimeTerrainLookup(candidate.world);

    RuntimeRepairDepotReconcileReport reconcileReport;
    std::string repairError;
    RuntimeRepairDepotContext context = makeRuntimeRepairDepotContext(
        candidate,
        &validatedState
    );
    // A queued relation is a persisted waiting state, not a request for the
    // loader to acquire a newly free dock.  Joint production/repair authority
    // is validated later on the complete load candidate; promotion belongs to
    // the first normal simulation tick after a successful commit.
    context.allowQueuePromotion = false;
    // RuntimeRepairDepotSystemState::load reconciles a private staged value
    // before assigning it to validatedState.  Validation callbacks must not
    // consult validatedState here: it is intentionally still empty.  Match
    // the saved movement authority directly instead.
    const auto savedMoveIsActive = [&candidate](
        int unitId,
        const RuntimeRepairDepotCell& target,
        int allowedObjectId
    ) {
        const WorldObjectInstance* unit = findObjectById(candidate, unitId);
        if (unit == nullptr || !unit->alive) {
            return false;
        }
        if (unit->x == target.x
            && unit->y == target.y
            && unit->onBridge == target.onBridge) {
            return true;
        }
        const auto behavior = candidate.behaviors.find(unitId);
        return behavior != candidate.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && behavior->second.state != RuntimeBehaviorState::Complete
            && behavior->second.moveTarget
                == RuntimeCellPoint{target.x, target.y}
            && behavior->second.pendingAllowedOccupiedObjectId
                == allowedObjectId;
    };
    context.isReservationActive = [
        &candidate,
        savedMoveIsActive
    ](
        int hostId,
        int phaseSlotIndex,
        int unitId
    ) {
        const std::optional<RuntimeRepairDepotCell> dock =
            runtimeRepairDepotDockCell(
                candidate,
                hostId,
                phaseSlotIndex
            );
        if (dock.has_value()
            && savedMoveIsActive(unitId, *dock, hostId)) {
            return true;
        }
        const std::optional<RuntimeRepairDepotCell> queue =
            runtimeRepairDepotQueueCell(
                candidate,
                hostId,
                phaseSlotIndex,
                unitId
            );
        return queue.has_value()
            && savedMoveIsActive(unitId, *queue, -1);
    };
    context.canPathTo = [&candidate](
        int unitId,
        const RuntimeRepairDepotCell& target
    ) {
        const WorldObjectInstance* unit = findObjectById(candidate, unitId);
        if (unit == nullptr || !unit->alive) {
            return false;
        }
        if (unit->x == target.x
            && unit->y == target.y
            && unit->onBridge == target.onBridge) {
            return true;
        }
        const auto behavior = candidate.behaviors.find(unitId);
        return behavior != candidate.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && behavior->second.state != RuntimeBehaviorState::Complete
            && behavior->second.moveTarget
                == RuntimeCellPoint{target.x, target.y};
    };
    context.resolveRallyCell = {};
    if (!validatedState.load(
            ini,
            context,
            &repairError,
            &reconcileReport
        )) {
        errorMessage =
            "invalid repair depot runtime: " + repairError;
        return false;
    }
    if (validatedState.fixedTickHz()
            != std::max(1, liveState.config.fixedTickHz)
        || validatedState.lastAdvancedTick() > savedTick
        || reconcileReport.releasedRelations != 0U
        || reconcileReport.removedHostTimers != 0U
        || reconcileReport.removedHostAnimations != 0U
        || !reconcileReport.actions.empty()) {
        std::ostringstream diagnostic;
        diagnostic
            << "repair depot runtime diverged during strict staged reconcile"
            << " fixedHz=" << validatedState.fixedTickHz()
            << "/" << std::max(1, liveState.config.fixedTickHz)
            << " lastAdvanced=" << validatedState.lastAdvancedTick()
            << " savedTick=" << savedTick
            << " retained=" << reconcileReport.retainedRelations
            << " released=" << reconcileReport.releasedRelations
            << " removedTimers=" << reconcileReport.removedHostTimers
            << " removedAnimations=" << reconcileReport.removedHostAnimations
            << " actions=" << reconcileReport.actions.size();
        for (const RuntimeRepairDepotAction& action :
                reconcileReport.actions) {
            diagnostic
                << " [" << runtimeRepairDepotActionName(action.kind)
                << " unit=" << action.unitId
                << " host=" << action.hostId
                << " reason=" << action.reason << "]";
        }
        errorMessage = diagnostic.str();
        return false;
    }
    for (const RuntimeRepairDepotRelation& relation :
            validatedState.relations()) {
        if (relation.reservedTick > savedTick
            || relation.attachedTick > savedTick) {
            errorMessage =
                "repair depot runtime contains future relation ticks";
            return false;
        }
    }
    return true;
}

bool validateRuntimeSavedAircraftServices(
    const RuntimeProductionHostState& liveState,
    const IniFile& ini,
    int saveVersion,
    RuntimeAircraftServiceSystemState& validatedState,
    bool& hasExplicitSection,
    std::string& errorMessage) {
    hasExplicitSection =
        ini.getSection("AircraftServiceRuntime") != nullptr;
    validatedState.reset(std::max(1, liveState.config.fixedTickHz));
    if (!hasExplicitSection) {
        if (saveVersion >= 7) {
            errorMessage =
                "version 7 save is missing aircraft service runtime";
            return false;
        }
        return true;
    }

    int savedTick = -1;
    int savedCredits = -1;
    const std::string savedOwner = trimAsciiLocal(
        ini.getValue("Economy", "Owner", "")
    );
    if (!parseRuntimeInt(
            ini.getValue("RuntimeSave", "Tick", ""),
            savedTick
        )
        || savedTick < 0
        || savedOwner.empty()
        || !parseRuntimeInt(
            ini.getValue("Economy", "Credits", ""),
            savedCredits
        )
        || savedCredits < 0
        || savedCredits > runtimeMaximumCreditBalance) {
        errorMessage =
            "invalid saved world or economy header for aircraft service";
        return false;
    }

    std::vector<WorldObjectInstance> candidateObjects(
        liveState.world.getObjects().begin(),
        liveState.world.getObjects().end()
    );

    const std::string savedOwnerKey =
        runtimeRepairDepotCanonicalOwnerId(liveState, savedOwner);
    RuntimeAircraftServiceContext context;
    context.rules = &liveState.rules;
    context.objects = &candidateObjects;
    context.areAllied = [&liveState](
        const std::string& left,
        const std::string& right
    ) {
        return runtimeRepairDepotCanonicalOwnerId(liveState, left)
            == runtimeRepairDepotCanonicalOwnerId(liveState, right);
    };
    context.canonicalOwnerId = [&liveState](const std::string& owner) {
        return runtimeRepairDepotCanonicalOwnerId(liveState, owner);
    };
    context.creditBalance = [
        &liveState,
        savedOwnerKey,
        savedCredits
    ](const std::string& owner) {
        const std::string key =
            runtimeRepairDepotCanonicalOwnerId(liveState, owner);
        return key == savedOwnerKey
            ? savedCredits
            : runtimeRepairDepotCreditBalance(liveState, key);
    };

    RuntimeAircraftServiceReconcileReport report;
    std::string loadError;
    if (!validatedState.load(ini, context, &loadError, &report)) {
        errorMessage = "invalid aircraft service runtime: " + loadError;
        return false;
    }
    if (!report.contextValid
        || validatedState.fixedTickHz()
            != std::max(1, liveState.config.fixedTickHz)
        || validatedState.lastAdvancedTick() > savedTick
        || report.addedHosts != 0U
        || report.removedHosts != 0U
        || report.resetHosts != 0U
        || report.addedBindings != 0U
        || report.removedBindings != 0U
        || report.duplicateDockBindings != 0U
        || report.duplicateAircraftBindings != 0U
        || report.skippedHostsByLimit != 0U
        || report.skippedBindingsByLimit != 0U
        || report.retainedHosts != validatedState.hosts().size()
        || report.retainedBindings
            != validatedState.dockBindings().size()) {
        errorMessage =
            "aircraft service runtime diverged during strict staged reconcile";
        return false;
    }
    return true;
}

bool validateRuntimeSavedTimedBombIntents(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    std::vector<std::pair<int, RuntimeTimedBombIntent>>& savedIntents,
    std::string& errorMessage) {
    savedIntents.clear();
    const bool hasSection = ini.getSection("TimedBombIntents") != nullptr;
    if (!hasSection) {
        if (saveVersion >= 10) {
            errorMessage = "missing timed bomb intent section";
            return false;
        }
        return true;
    }

    int schema = -1;
    int intentCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("TimedBombIntents", "Schema", ""),
            schema
        )
        || schema != 1
        || !parseRuntimeInt(
            ini.getValue("TimedBombIntents", "Count", ""),
            intentCount
        )
        || intentCount < 0
        || intentCount > 4096) {
        errorMessage = "invalid timed bomb intent header";
        return false;
    }

    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }

    std::unordered_set<int> sourceIds;
    std::unordered_set<int> targetIds;
    savedIntents.reserve(static_cast<std::size_t>(intentCount));
    for (int index = 0; index < intentCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TimedBombIntents", std::to_string(index), "")
        );
        int sourceId = -1;
        int targetId = -1;
        const std::optional<RuntimeTimedBombIntentKind> kind =
            tokens.size() == 3U
                ? runtimeTimedBombIntentKindFromName(tokens[2])
                : std::nullopt;
        if (tokens.size() != 3U
            || !parseRuntimeInt(tokens[0], sourceId)
            || !parseRuntimeInt(tokens[1], targetId)
            || sourceId < 0
            || targetId < 0
            || sourceId == targetId
            || !kind.has_value()
            || !sourceIds.insert(sourceId).second
            || !targetIds.insert(targetId).second) {
            errorMessage = "invalid timed bomb intent record";
            return false;
        }
        savedIntents.emplace_back(
            sourceId,
            RuntimeTimedBombIntent{*kind, targetId}
        );
    }
    std::sort(
        savedIntents.begin(),
        savedIntents.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        }
    );
    if (savedIntents.empty()) {
        return true;
    }

    std::unordered_map<int, RuntimeObjectBehavior> behaviorsById;
    int behaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0
        || behaviorCount > 100000) {
        errorMessage = "invalid behavior count for timed bomb intents";
        return false;
    }
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int candidateSourceId = -1;
        if (tokens.empty()
            || !parseRuntimeInt(tokens[0], candidateSourceId)
            || sourceIds.find(candidateSourceId) == sourceIds.end()) {
            continue;
        }
        const auto source = objectsById.find(candidateSourceId);
        RuntimeObjectBehavior behavior;
        int parsedSourceId = -1;
        if (source == objectsById.end()
            || source->second == nullptr
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *source->second,
                saveVersion,
                parsedSourceId,
                behavior
            )
            || parsedSourceId != candidateSourceId
            || !behaviorsById.emplace(parsedSourceId, behavior).second) {
            errorMessage = "invalid behavior for timed bomb intent";
            return false;
        }
    }

    std::unordered_set<int> conflictingSourceIds;
    for (const std::string& section : {
            std::string("EngineerIntents"),
            std::string("OccupyIntents"),
            std::string("TransportIntents")}) {
        const int count = parseRuntimeIntOrDefault(
            ini.getValue(section, "Count", "0"),
            -1
        );
        if (count < 0 || count > 100000) {
            errorMessage = "invalid conflicting intent count";
            return false;
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(section, std::to_string(index), "")
            );
            int objectId = -1;
            if (!tokens.empty() && parseRuntimeInt(tokens[0], objectId)
                && objectId >= 0) {
                conflictingSourceIds.insert(objectId);
            }
        }
    }

    for (const auto& [sourceId, intent] : savedIntents) {
        const auto sourceIt = objectsById.find(sourceId);
        const auto targetIt = objectsById.find(intent.targetObjectId);
        const auto behaviorIt = behaviorsById.find(sourceId);
        if (sourceIt == objectsById.end()
            || targetIt == objectsById.end()
            || behaviorIt == behaviorsById.end()
            || sourceIt->second == nullptr
            || targetIt->second == nullptr
            || conflictingSourceIds.find(sourceId)
                != conflictingSourceIds.end()) {
            errorMessage = "timed bomb intent references missing or conflicting state";
            return false;
        }
        const WorldObjectInstance& source = *sourceIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        [[maybe_unused]] const RuntimeObjectBehavior& behavior = behaviorIt->second;
        const bool sourceCapable =
            runtimeObjectHasC4Capability(state.rules, source);
        const std::string mission =
            toLowerAsciiLocal(trimAsciiLocal(source.mission));
        const double approachDistance = runtimeCellDistanceToObjectFootprint(
            state,
            behavior.moveTarget,
            target
        );
        if (!source.alive
            || source.health <= 0
            || runtimeRepairDepotCanonicalOwnerId(state, source.owner)
                != runtimeRepairDepotCanonicalOwnerId(
                    state,
                    state.economy.owner
                )
            || !target.alive
            || target.health <= 0
            || !sourceCapable
            || !runtimeTargetAcceptsTimedBombIntent(
                state.rules,
                target
            )
            || savedSpecialMechanisms.timedBombs().find(
                intent.targetObjectId
            ) != savedSpecialMechanisms.timedBombs().end()
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.state == RuntimeBehaviorState::Complete
            || (behavior.pendingAllowedOccupiedObjectId != intent.targetObjectId
                && !(behavior.pendingAllowedOccupiedObjectId == -1
                    && behavior.targetObjectId == intent.targetObjectId
                    && runtimeC4CellOutsideFoundation(state, behavior.moveTarget, target)))
            || behavior.moveTarget.x < 0
            || behavior.moveTarget.y < 0
            || approachDistance > 2.001
            || (mission != "enter" && mission != "entering")) {
            errorMessage = "invalid source, target, or move for timed bomb intent";
            return false;
        }
    }
    return true;
}

bool validateRuntimeSavedTimedBombState(
    const RuntimeGameSessionState& state,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    int savedTick,
    std::string& errorMessage) {
    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    for (const auto& [targetId, bomb] :
            savedSpecialMechanisms.timedBombs()) {
        const auto source = objectsById.find(bomb.sourceObjectId);
        const auto target = objectsById.find(targetId);
        if (bomb.targetObjectId != targetId
            || source == objectsById.end()
            || target == objectsById.end()
            || source->second == nullptr
            || target->second == nullptr) {
            errorMessage = "timed bomb references a missing saved object";
            return false;
        }
        const WorldObjectInstance& sourceObject = *source->second;
        const WorldObjectInstance& targetObject = *target->second;
        const UnitType* targetUnit = targetObject.kind
                == WorldObjectKind::Structure
            ? nullptr
            : findRuntimeUnitType(state.rules, targetObject);
        const BuildingType* targetBuilding = targetObject.kind
                == WorldObjectKind::Structure
            ? findRuntimeBuildingTypeById(state.rules, targetObject.type)
            : nullptr;
        const bool validSource = bomb.kind == TimedBombKind::C4
            ? runtimeObjectHasC4Capability(state.rules, sourceObject)
            : runtimeObjectHasIvanCapability(state.rules, sourceObject);
        const bool validTarget = bomb.kind == TimedBombKind::C4
            ? targetBuilding != nullptr
            : targetBuilding != nullptr
                || (targetUnit != nullptr && targetUnit->bombable);
        if (!targetObject.alive
            || targetObject.health <= 0
            || !validSource
            || !validTarget
            || bomb.owner != sourceObject.owner
            || bomb.attachedTick < 0
            || bomb.attachedTick > savedTick
            || bomb.detonateTick <= savedTick
            || bomb.detonateTick <= bomb.attachedTick
            || bomb.damage < 0
            || state.rules.findWarheadType(bomb.warheadId) == nullptr
            || (bomb.kind == TimedBombKind::C4
                && !bomb.forceDestruction)
            || (bomb.kind == TimedBombKind::Ivan
                && bomb.forceDestruction)) {
            errorMessage = "invalid timed bomb saved identity or lifetime";
            return false;
        }
    }

    RuntimeSpecialMechanismState reconciled = savedSpecialMechanisms;
    std::vector<WorldObjectInstance> reconciledObjects = savedObjects;
    std::vector<SpecialMechanismAction> reconcileActions;
    const std::uint64_t beforeHash = reconciled.stateHash();
    reconciled.reconcile(
        reconciledObjects,
        savedTick,
        SpecialEventSink{},
        &reconcileActions
    );
    if (!reconcileActions.empty() || reconciled.stateHash() != beforeHash) {
        errorMessage = "special mechanism state requires destructive reconcile";
        return false;
    }
    return true;
}

bool validateRuntimeSavedCarrierState(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    std::string& errorMessage) {
    if (saveVersion < 13) {
        return true;
    }
    if (ini.getSection("SpecialCarrierParents") == nullptr
        || ini.getSection("SpecialCarrierChildren") == nullptr) {
        errorMessage = "v13 save is missing carrier runtime sections";
        return false;
    }

    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        if (object.instanceId <= 0
            || !objectsById.emplace(object.instanceId, &object).second) {
            errorMessage = "carrier runtime object identity is invalid";
            return false;
        }
    }

    const auto carrierOwnerDriftIsAuthorityBound = [
        &savedSpecialMechanisms
    ](int objectId) {
        if (savedSpecialMechanisms.isChronoInTransit(objectId)
            || savedSpecialMechanisms.hasTemporalAuthorityForObject(objectId)
            || savedSpecialMechanisms.isParasiteSourceLimboed(objectId)
            || savedSpecialMechanisms.mindControlLinks().contains(objectId)) {
            return true;
        }
        return std::any_of(
            savedSpecialMechanisms.mindControlLinks().begin(),
            savedSpecialMechanisms.mindControlLinks().end(),
            [objectId](const auto& entry) {
                return entry.second.controllerObjectId == objectId;
            }
        );
    };

    std::unordered_set<int> referencedChildIds;
    for (const auto& [parentId, parentState] :
            savedSpecialMechanisms.carrierParents()) {
        const auto parentObjectIt = objectsById.find(parentId);
        if (parentState.parentObjectId != parentId
            || parentObjectIt == objectsById.end()
            || parentObjectIt->second == nullptr
            || !parentObjectIt->second->alive
            || parentObjectIt->second->health <= 0
            || parentObjectIt->second->kind == WorldObjectKind::Structure) {
            errorMessage = "carrier parent references an invalid world object";
            return false;
        }
        const WorldObjectInstance& parentObject = *parentObjectIt->second;
        const UnitType* parentRules = findRuntimeUnitType(
            state.rules,
            parentObject
        );
        const int expectedCapacity = parentRules == nullptr
            ? -1
            : std::max(1, parentRules->spawnsNumber);
        const int expectedRegenRate = parentRules == nullptr
            ? -1
            : std::max(0, parentRules->spawnRegenRate);
        const int expectedReloadRate = parentRules == nullptr
            ? -1
            : std::max(0, parentRules->spawnReloadRate);
        bool parentOwnerDriftIsAuthorityBound = false;
        if (parentState.owner != parentObject.owner) {
            for (const auto& [slot, childId] : parentState.childBySlot) {
                (void)slot;
                const auto childObject = objectsById.find(childId);
                if (childId > 0
                    && childObject != objectsById.end()
                    && childObject->second != nullptr
                    && childObject->second->owner == parentState.owner
                    && childObject->second->owner != parentObject.owner
                    && carrierOwnerDriftIsAuthorityBound(childId)) {
                    parentOwnerDriftIsAuthorityBound = true;
                    break;
                }
            }
        }
        if (parentRules == nullptr
            || trimAsciiLocal(parentRules->spawns).empty()
            || parentRules->spawnsNumber <= 0
            || (state.rules.findUnitType(
                    "AircraftType",
                    parentRules->spawns
                ) == nullptr
                && state.rules.findUnitType(
                    "UnitType",
                    parentRules->spawns
                ) == nullptr
                && state.rules.findUnitType(
                    "InfantryType",
                    parentRules->spawns
                ) == nullptr)
            || parentState.childType != parentRules->spawns
            || (parentState.owner != parentObject.owner
                && !parentOwnerDriftIsAuthorityBound)
            || parentState.capacity != expectedCapacity
            || parentState.regenRate != expectedRegenRate
            || parentState.reloadRate != expectedReloadRate
            || parentState.childBySlot.size()
                != static_cast<std::size_t>(expectedCapacity)
            || parentState.slotReadyTick.size()
                != static_cast<std::size_t>(expectedCapacity)) {
            errorMessage = "carrier parent diverges from world or rules";
            return false;
        }
        for (int slot = 0; slot < expectedCapacity; ++slot) {
            const auto childSlot = parentState.childBySlot.find(slot);
            const auto readySlot = parentState.slotReadyTick.find(slot);
            if (childSlot == parentState.childBySlot.end()
                || readySlot == parentState.slotReadyTick.end()
                || childSlot->second < -1
                || readySlot->second < 0) {
                errorMessage = "carrier parent slot map is incomplete";
                return false;
            }
            if (childSlot->second < 0) {
                continue;
            }
            const auto childState =
                savedSpecialMechanisms.carrierChildren().find(
                    childSlot->second
                );
            if (childState
                    == savedSpecialMechanisms.carrierChildren().end()
                || childState->second.parentObjectId != parentId
                || childState->second.slot != slot
                || !referencedChildIds.insert(childSlot->second).second) {
                errorMessage = "carrier parent/child graph is not bijective";
                return false;
            }
        }
    }

    for (const auto& [childId, childState] :
            savedSpecialMechanisms.carrierChildren()) {
        const auto parentState =
            savedSpecialMechanisms.carrierParents().find(
                childState.parentObjectId
            );
        const auto childObjectIt = objectsById.find(childId);
        if (childState.childObjectId != childId
            || parentState == savedSpecialMechanisms.carrierParents().end()
            || childObjectIt == objectsById.end()
            || childObjectIt->second == nullptr
            || !childObjectIt->second->alive
            || childObjectIt->second->health <= 0
            || !referencedChildIds.contains(childId)) {
            errorMessage = "carrier child references invalid graph or world state";
            return false;
        }
        const WorldObjectInstance& childObject = *childObjectIt->second;
        const bool exactAircraft =
            state.rules.findUnitType("AircraftType", childObject.type)
                != nullptr;
        const bool exactVehicle =
            state.rules.findUnitType("UnitType", childObject.type)
                != nullptr;
        const bool exactInfantry =
            state.rules.findUnitType("InfantryType", childObject.type)
                != nullptr;
        const bool kindMatchesRules = exactAircraft
            ? childObject.kind == WorldObjectKind::Unit
                && childObject.authoredAircraft
            : exactVehicle
                ? childObject.kind == WorldObjectKind::Unit
                    && !childObject.authoredAircraft
                : exactInfantry
                    && childObject.kind == WorldObjectKind::Infantry
                    && !childObject.authoredAircraft;
        const bool docked = childState.phase == CarrierChildPhase::Docked;
        const auto parentWorld = objectsById.find(childState.parentObjectId);
        const bool threeRoundAircraft = parentWorld != objectsById.end() && parentWorld->second
            && ((toUpperAsciiLocal(parentWorld->second->type) == "CARRIER"
                    && toUpperAsciiLocal(childObject.type) == "HORNET")
                || (toUpperAsciiLocal(parentWorld->second->type) == "DEST"
                    && toUpperAsciiLocal(childObject.type) == "ASW"));
        if ((!threeRoundAircraft && childState.sortieShotsRemaining > 1)
            || childObject.type != parentState->second.childType
            || (childObject.owner != parentState->second.owner
                && !carrierOwnerDriftIsAuthorityBound(childId))
            || !kindMatchesRules
            || childState.phase == CarrierChildPhase::Destroyed
            || (docked
                && (childObject.dockedToObjectId
                        != childState.parentObjectId
                    || childObject.dockIndex != childState.slot))
            || (!docked
                && (childObject.dockedToObjectId >= 0
                    || childObject.dockIndex >= 0))) {
            errorMessage = "carrier child diverges from world or rules";
            return false;
        }
    }
    for (const auto& [id, missile] : savedSpecialMechanisms.spawnedMissiles()) {
        const auto child = objectsById.find(id);
        const auto* profile = state.rules.findSpawnedMissileProfile(missile.typeId);
        const auto* unit = state.rules.findUnitType("AircraftType", missile.typeId);
        const auto* launcher = state.rules.findUnitType("UnitType", missile.parentType);
        const auto* weapon = state.rules.findWeaponType(missile.weaponId);
        if (child == objectsById.end() || child->second == nullptr || !child->second->alive
            || child->second->health <= 0 || child->second->type != missile.typeId
            || child->second->owner != missile.owner || !child->second->authoredAircraft
            || profile == nullptr || unit == nullptr || !unit->missileSpawn
            || launcher == nullptr || launcher->spawns != missile.typeId
            || missile.slot >= launcher->spawnsNumber || weapon == nullptr || !weapon->spawner
            || (missile.warheadId != profile->warhead && missile.warheadId != profile->eliteWarhead)
            || (missile.damage != profile->damage && missile.damage != profile->eliteDamage)
            || (missile.released && (!child->second->airborne || child->second->dockedToObjectId >= 0
                || child->second->dockIndex >= 0))) {
            errorMessage = "spawned missile diverges from world or rules";
            return false;
        }
    }
    return true;
}

bool validateRuntimeSavedParasiteState(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const RuntimeSpecialMechanismState& savedSpecialMechanisms,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    int savedTick,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    const RuntimeFactoryDeliveryState& savedFactoryDeliveries,
    std::unordered_map<int, int>& savedStuns,
    std::string& errorMessage) {
    savedStuns.clear();
    if (saveVersion < 11) {
        return true;
    }
    const auto sameText = [](const std::string& left,
                             const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    const auto fail = [&errorMessage](const std::string& detail) {
        errorMessage = "invalid persisted parasite state: " + detail;
        return false;
    };

    std::vector<WorldObjectInstance> completeObjects = savedObjects;
    std::unordered_map<int, WorldObjectInstance*> objectsById;
    objectsById.reserve(completeObjects.size());
    for (WorldObjectInstance& object : completeObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    int objectCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Objects", "Count", ""),
            objectCount
        )
        || objectCount != static_cast<int>(completeObjects.size())) {
        return fail("object set is incomplete");
    }
    for (int index = 0; index < objectCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Objects", std::to_string(index), "")
        );
        int objectId = -1;
        int veterancy = -1;
        int dockedToObjectId = -2;
        int dockIndex = -2;
        int flightLevel = -1;
        bool airborne = false;
        // v14 accepts the published 39-field authority and its append-only
        // runtime extensions (crate, special-building, prone, and weapon
        // cooldown state).
        const bool validObjectFieldCount = tokens.size() == 39U
            || (saveVersion >= 14
                && (tokens.size() == 42U
                    || tokens.size() == 43U
                    || tokens.size() == 44U
                    || tokens.size() == 46U
                    || tokens.size() == 47U
                    || tokens.size() == 48U
                    || tokens.size() == 49U
                    || tokens.size() == 50U
                    || tokens.size() == 54U
                    || tokens.size() == 55U || tokens.size() == 56U || tokens.size() == 57U || tokens.size() == 58U || tokens.size() == 59U || tokens.size() == 60U || tokens.size() == 62U || tokens.size() == 63U || tokens.size() == 64U));
        bool prone = false;
        int proneTicksRemaining = 0;
        const bool proneAuthorityValid = tokens.size() < 46U
            || (parseRuntimeStrictBool(tokens[44], prone)
                && parseRuntimeInt(tokens[45], proneTicksRemaining)
                && proneTicksRemaining >= 0
                && prone == (proneTicksRemaining > 0));
        int weaponCooldownTicksRemaining = 0;
        const bool weaponCooldownAuthorityValid = tokens.size() < 47U
            || (parseRuntimeInt(
                    tokens[46],
                    weaponCooldownTicksRemaining
                )
                && weaponCooldownTicksRemaining >= 0);
        bool crateCloakable = false;
        const bool crateCloakAuthorityValid = tokens.size() < 48U
            || parseRuntimeStrictBool(tokens[47], crateCloakable);
        int deployStateTicksRemaining = -1;
        const bool deployStateTimerAuthorityValid = tokens.size() < 49U
            || (parseRuntimeInt(tokens[48], deployStateTicksRemaining)
                && deployStateTicksRemaining >= -1);
        int chargeTurretStartTick = -1;
        const bool chargeTurretAuthorityValid = tokens.size() < 50U
            || (parseRuntimeInt(tokens[49], chargeTurretStartTick)
                && chargeTurretStartTick >= -1);
        if (!validObjectFieldCount
            || !proneAuthorityValid
            || !weaponCooldownAuthorityValid
            || !crateCloakAuthorityValid
            || !deployStateTimerAuthorityValid
            || !chargeTurretAuthorityValid
            || !parseRuntimeInt(tokens[1], objectId)
            || !parseRuntimeInt(tokens[20], veterancy)
            || !parseRuntimeInt(tokens[23], dockedToObjectId)
            || !parseRuntimeInt(tokens[24], dockIndex)
            || !parseRuntimeStrictBool(tokens[25], airborne)
            || !parseRuntimeInt(tokens[26], flightLevel)
            || veterancy < 0 || veterancy > 2
            || dockedToObjectId < -1 || dockIndex < -1
            || ((dockedToObjectId < 0) != (dockIndex < 0))
            || flightLevel < 0) {
            return fail("object authority fields are malformed");
        }
        const auto object = objectsById.find(objectId);
        if (object == objectsById.end() || object->second == nullptr) {
            return fail("object authority identity is missing");
        }
        object->second->veterancy = veterancy;
        object->second->dockedToObjectId = dockedToObjectId;
        object->second->dockIndex = dockIndex;
        object->second->airborne = airborne;
        object->second->flightLevel = flightLevel;
        if (tokens.size() >= 46U) {
            object->second->prone = prone;
            object->second->proneTicksRemaining = proneTicksRemaining;
        }
        if (tokens.size() >= 47U) {
            object->second->weaponCooldownTicksRemaining =
                weaponCooldownTicksRemaining;
        }
        if (tokens.size() >= 48U) {
            object->second->crateCloakable = crateCloakable;
        }
        if (tokens.size() >= 49U) {
            object->second->deployStateTicksRemaining =
                deployStateTicksRemaining;
        }
        if (tokens.size() >= 50U) {
            object->second->chargeTurretStartTick = chargeTurretStartTick;
        }
        if (tokens.size() >= 54U) {
            auto& item = *object->second;
            if (!parseRuntimeInt(tokens[50], item.tankBunkerHostId)
                || !parseRuntimeInt(tokens[51], item.tankBunkerOccupantId)
                || !parseRuntimeInt(tokens[52], item.tankBunkerAnimationStartTick)
                || !parseRuntimeStrictBool(tokens[53], item.tankBunkerLowering)
                || item.tankBunkerHostId < -1 || item.tankBunkerOccupantId < -1
                || item.tankBunkerAnimationStartTick < -1) return fail("malformed tank bunker authority");
        }
        if (tokens.size() >= 55U
            && !parseRuntimeStrictBool(tokens[54], object->second->harvestAutoSearchSuspended)) {
            return fail("malformed harvester command authority");
        }
        if (tokens.size() >= 56U && (!parseRuntimeInt(tokens[55], object->second->factoryExitHostId)
                || object->second->factoryExitHostId < -1)) {
            return fail("malformed factory exit host authority");
        }
        if (tokens.size() >= 57U && (!parseRuntimeInt(tokens[56], object->second->radiationEruptionCooldownTicksRemaining)
                || object->second->radiationEruptionCooldownTicksRemaining < 0)) {
            return fail("malformed radiation eruption cooldown authority");
        }
        if (tokens.size() >= 58U && !parseRuntimeStrictBool(tokens[57], object->second->hasBeenCaptured)) {
            return fail("malformed capture history authority");
        }
        if (tokens.size() >= 59U
            && (!parseRuntimeStrictBool(tokens[58], object->second->rubble)
                || (object->second->rubble && (object->second->kind != WorldObjectKind::Structure
                    || object->second->alive || object->second->health != 0)))) {
            return fail("malformed building rubble authority");
        }
        if (tokens.size() >= 63U) {
            const BuildingType* absorber = object->second->kind == WorldObjectKind::Structure
                ? findRuntimeBuildingTypeById(state.rules, object->second->type) : nullptr;
            if (!parseRuntimeInt(tokens[62], object->second->bioReactorAnimationStartTick)
                || object->second->bioReactorAnimationStartTick < -1
                || object->second->bioReactorAnimationStartTick > savedTick
                || (object->second->bioReactorAnimationStartTick >= 0
                    && (absorber == nullptr || !absorber->infantryAbsorb))) {
                return fail("malformed bio-reactor animation authority");
            }
        }
        if (tokens.size() >= 64U
            && (!parseRuntimeInt(tokens[63], object->second->bioReactorAnimationPoweredStartTick)
                || object->second->bioReactorAnimationPoweredStartTick < -1
                || object->second->bioReactorAnimationPoweredStartTick > savedTick
                || (object->second->bioReactorAnimationPoweredStartTick >= 0
                    && (object->second->bioReactorAnimationStartTick < 0
                        || object->second->bioReactorAnimationPoweredStartTick > object->second->bioReactorAnimationStartTick)))) {
            return fail("malformed bio-reactor powered animation authority");
        }
    }

    std::unordered_set<int> behaviorObjectIds;
    std::unordered_map<int, RuntimeObjectBehavior> mobileParasiteHostMoves;
    std::unordered_set<int> mobileParasiteHostAttacks;
    int behaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0 || behaviorCount > 100000) {
        return fail("behavior header is malformed");
    }
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int objectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], objectId)
            || !behaviorObjectIds.insert(objectId).second) {
            return fail("behavior identity is malformed");
        }
        const auto object = objectsById.find(objectId);
        RuntimeObjectBehavior behavior;
        int parsedId = -1;
        if (object != objectsById.end() && object->second != nullptr
            && parseRuntimeSavedBehaviorRecord(tokens, *object->second, saveVersion, parsedId, behavior)
            && parsedId == objectId) {
            if (behavior.kind == RuntimeBehaviorKind::Move
                && behavior.targetObjectId < 0 && !behavior.attackBridge
                && !behavior.attackGround && !behavior.forceFire
                && behavior.moveTarget.x >= 0 && behavior.moveTarget.x < 512
                && behavior.moveTarget.y >= 0 && behavior.moveTarget.y < 512) {
                mobileParasiteHostMoves.emplace(objectId, std::move(behavior));
            } else if (behavior.kind == RuntimeBehaviorKind::Attack
                && !behavior.attackBridge && !behavior.attackGround
                && behavior.targetObjectId >= 0 && behavior.targetObjectId != objectId
                && (behavior.pendingAllowedOccupiedObjectId < 0
                    || behavior.pendingAllowedOccupiedObjectId == behavior.targetObjectId)) {
                // Attack-path requests reserve their target's occupied cell.
                // This is ordinary combat authority, not a second host for
                // the parasite. A Limbo/dead/missing target is still invalid.
                const auto attacked = objectsById.find(behavior.targetObjectId);
                if (attacked != objectsById.end() && attacked->second != nullptr
                    && attacked->second->alive && attacked->second->health > 0) {
                    mobileParasiteHostAttacks.insert(objectId);
                }
            }
        }
    }

    std::unordered_set<int> containedOrHostingObjectIds;
    int manifestCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("TransportManifests", "Count", "0"),
            manifestCount
        )
        || manifestCount < 0 || manifestCount > 100000) {
        return fail("transport authority header is malformed");
    }
    for (int index = 0; index < manifestCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TransportManifests", std::to_string(index), "")
        );
        int hostId = -1;
        if (tokens.size() != 2U || !parseRuntimeInt(tokens[0], hostId)
            || hostId < 0) {
            return fail("transport authority record is malformed");
        }
        containedOrHostingObjectIds.insert(hostId);
        std::stringstream passengers(tokens[1]);
        std::string passengerToken;
        while (std::getline(passengers, passengerToken, ',')) {
            int passengerId = -1;
            if (!parseRuntimeInt(passengerToken, passengerId)
                || passengerId < 0) {
                return fail("transport passenger identity is malformed");
            }
            containedOrHostingObjectIds.insert(passengerId);
        }
    }
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        int hostCount = -1;
        if (!parseRuntimeInt(
                ini.getValue("GarrisonRuntime", "HostCount", ""),
                hostCount
            )
            || hostCount < 0 || hostCount > 100000) {
            return fail("garrison authority header is malformed");
        }
        for (int index = 0; index < hostCount; ++index) {
            const std::string section = "GarrisonRuntime.Host."
                + std::to_string(index);
            int hostId = -1;
            if (ini.getSection(section) == nullptr
                || !parseRuntimeInt(
                    ini.getValue(section, "HostId", ""),
                    hostId
                )
                || hostId < 0) {
                return fail("garrison host identity is malformed");
            }
            containedOrHostingObjectIds.insert(hostId);
            std::stringstream occupants(
                ini.getValue(section, "Occupants", "")
            );
            std::string occupantToken;
            while (std::getline(occupants, occupantToken, ',')) {
                if (trimAsciiLocal(occupantToken).empty()) {
                    continue;
                }
                int occupantId = -1;
                if (!parseRuntimeInt(occupantToken, occupantId)
                    || occupantId < 0) {
                    return fail("garrison occupant identity is malformed");
                }
                containedOrHostingObjectIds.insert(occupantId);
            }
        }
    }

    std::unordered_set<int> intentObjectIds;
    for (const std::string& section : {
            std::string("EngineerIntents"),
            std::string("OccupyIntents"),
            std::string("TransportIntents"),
            std::string("TimedBombIntents")}) {
        int count = -1;
        if (!parseRuntimeInt(
                ini.getValue(section, "Count", "0"),
                count
            )
            || count < 0 || count > 100000) {
            return fail("exclusive intent header is malformed");
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(section, std::to_string(index), "")
            );
            int sourceId = -1;
            int targetId = -1;
            if (tokens.size() < 2U
                || !parseRuntimeInt(tokens[0], sourceId)
                || !parseRuntimeInt(tokens[1], targetId)
                || sourceId < 0 || targetId < 0) {
                return fail("exclusive intent identity is malformed");
            }
            intentObjectIds.insert(sourceId);
            (void)targetId;
        }
    }

    std::unordered_set<int> repairOrDeliveryObjectIds;
    for (const RuntimeRepairDepotRelation& relation :
            savedRepairDepots.relations()) {
        repairOrDeliveryObjectIds.insert(relation.hostId);
        repairOrDeliveryObjectIds.insert(relation.unitId);
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            savedFactoryDeliveries.leases()) {
        repairOrDeliveryObjectIds.insert(lease.hostId);
        repairOrDeliveryObjectIds.insert(lease.producedObjectId);
    }
    const auto hasExclusiveAuthority = [
        &objectsById,
        &behaviorObjectIds,
        &containedOrHostingObjectIds,
        &intentObjectIds,
        &repairOrDeliveryObjectIds,
        &savedSpecialMechanisms
    ](int objectId, bool rejectBehavior, bool asHost = false) {
        const auto object = objectsById.find(objectId);
        if (object == objectsById.end() || object->second == nullptr) {
            return true;
        }
        return object->second->dockedToObjectId >= 0
            || object->second->dockIndex >= 0
            || containedOrHostingObjectIds.contains(objectId)
            || intentObjectIds.contains(objectId)
            || repairOrDeliveryObjectIds.contains(objectId)
            || (rejectBehavior && behaviorObjectIds.contains(objectId))
            || savedSpecialMechanisms.isCarrierChild(objectId)
            || (!asHost && savedSpecialMechanisms.carrierParents().contains(objectId));
    };

    // A mechanical, non-paralyzing parasite does not own its host's movement
    // or ordinary object attack. Repair still requires its own exclusive
    // move/dock authority; accepting Attack must never bypass that relation.
    const auto mobileParasiteHostAuthorityIsSafe = [&](const WorldObjectInstance& target) {
        const int id = target.instanceId;
        if (containedOrHostingObjectIds.contains(id) || intentObjectIds.contains(id)
            || savedSpecialMechanisms.isCarrierChild(id)) return false;
        for (const auto& lease : savedFactoryDeliveries.leases())
            if (lease.hostId == id || lease.producedObjectId == id) return false;
        const RuntimeRepairDepotRelation* repair = nullptr;
        for (const auto& relation : savedRepairDepots.relations()) {
            if (relation.hostId == id) return false;
            if (relation.unitId == id) {
                if (repair != nullptr) return false;
                repair = &relation;
            }
        }
        const auto move = mobileParasiteHostMoves.find(id);
        if (repair == nullptr) {
            return target.dockedToObjectId < 0 && target.dockIndex < 0
                && ((move != mobileParasiteHostMoves.end()
                        && move->second.pendingAllowedOccupiedObjectId < 0)
                    || mobileParasiteHostAttacks.contains(id));
        }
        if (behaviorObjectIds.contains(id) && move == mobileParasiteHostMoves.end()) return false;
        const auto host = objectsById.find(repair->hostId);
        if (host == objectsById.end() || host->second == nullptr || !host->second->alive
            || host->second->kind != WorldObjectKind::Structure
            || repair->domain != RuntimeRepairDepotDomain::Ground
            || !sameText(repair->unitType, target.type)) return false;
        if (repair->phase == RuntimeRepairDepotRelationPhase::Docked) {
            return !behaviorObjectIds.contains(id)
                && target.dockedToObjectId == repair->hostId && target.dockIndex == repair->dockIndex
                && target.x == repair->dockCell.x && target.y == repair->dockCell.y
                && target.onBridge == repair->dockCell.onBridge;
        }
        const bool queued = repair->phase == RuntimeRepairDepotRelationPhase::Queued;
        const auto& cell = queued ? repair->queueCell : repair->dockCell;
        if (target.dockedToObjectId >= 0 || target.dockIndex >= 0) return false;
        if (move == mobileParasiteHostMoves.end())
            return target.x == cell.x && target.y == cell.y && target.onBridge == cell.onBridge;
        return move->second.moveTarget == RuntimeCellPoint{cell.x, cell.y}
            && move->second.pendingAllowedOccupiedObjectId == (queued ? -1 : repair->hostId);
    };

    std::unordered_map<int, const RuntimeProjectileEffect*> projectilesById;
    projectilesById.reserve(savedProjectiles.projectiles.size());
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        if (!projectilesById.emplace(
                projectile.actorId,
                &projectile
            ).second) {
            return fail("duplicate projectile actor identity");
        }
    }

    const int normalThreshold = static_cast<int>(std::lround(
        std::clamp(
            savedSpecialMechanisms.rules().conditionRed,
            0.0,
            1.0
        ) * 10000.0
    ));
    const int eliteThreshold = static_cast<int>(std::lround(
        std::clamp(
            savedSpecialMechanisms.rules().conditionYellow,
            0.0,
            1.0
        ) * 10000.0
    ));
    const auto strictParasiteRules = [
        &state,
        &sameText
    ](
        const WorldObjectInstance& source,
        const std::string& weaponId,
        const std::string& warheadId,
        const WeaponType*& weapon,
        const WarheadType*& warhead,
        const ProjectileType*& projectile
    ) {
        weapon = findRuntimeWeaponType(state.rules, weaponId);
        warhead = findRuntimeWarheadType(state.rules, warheadId);
        projectile = weapon == nullptr
            ? nullptr
            : findRuntimeProjectileType(state.rules, weapon->projectile);
        return weapon != nullptr && warhead != nullptr
            && projectile != nullptr
            && sameText(weapon->id, weaponId)
            && sameText(weapon->warhead, warheadId)
            && sameText(warhead->id, warheadId)
            && runtimeParasiteWarheadIsExclusive(*weapon, *warhead)
            && runtimeSpawnedMissileProfileForWeapon(
                state,
                source,
                *weapon
            ) == nullptr
            && !projectile->airburst
            && projectile->shrapnelCount <= 0
            && trimAsciiLocal(projectile->airburstWeapon).empty()
            && trimAsciiLocal(projectile->shrapnelWeapon).empty();
    };

    std::unordered_set<int> limboSourceIds;
    for (const auto& [projectileId, pending] :
            savedSpecialMechanisms.pendingParasiteLaunches()) {
        const auto sourceIt = objectsById.find(pending.sourceObjectId);
        const auto targetIt = objectsById.find(pending.targetObjectId);
        const auto projectileIt = projectilesById.find(projectileId);
        if (projectileId <= 0
            || projectileId >= savedProjectiles.nextActorId
            || sourceIt == objectsById.end()
            || targetIt == objectsById.end()
            || projectileIt == projectilesById.end()
            || sourceIt->second == nullptr || targetIt->second == nullptr
            || projectileIt->second == nullptr) {
            return fail("pending launch lacks its exact causal objects");
        }
        const WorldObjectInstance& source = *sourceIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        const RuntimeProjectileEffect& projectile = *projectileIt->second;
        const UnitType* sourceType = findRuntimeUnitType(state.rules, source);
        const UnitType* targetType = findRuntimeUnitType(state.rules, target);
        const WeaponType* weapon = nullptr;
        const WarheadType* warhead = nullptr;
        const ProjectileType* projectileRules = nullptr;
        const bool infantryTarget = target.kind
            == WorldObjectKind::Infantry;
        const bool vehicleTarget = target.kind == WorldObjectKind::Unit
            && !target.authoredAircraft && !target.airborne;
        if (!strictParasiteRules(
                source,
                pending.weaponId,
                pending.warheadId,
                weapon,
                warhead,
                projectileRules
            )) {
            return fail("pending launch has an unsafe rules combination");
        }
        const int strictInterval = runtimeParasiteRuleFramesToTicks(
            sourceType != nullptr && sourceType->organic
                ? 36
                : std::max(1, weapon->rof),
            state.config.fixedTickHz
        );
        const bool expectedParasiteable = infantryTarget
            || (vehicleTarget && targetType != nullptr
                && targetType->parasiteable);
        const int expectedCullingThreshold = source.veterancy >= 2
            ? eliteThreshold
            : normalThreshold;
        if (sourceType == nullptr || targetType == nullptr
            || source.alive || source.health <= 0
            || source.mission != "ParasiteLimbo"
            || !target.alive || target.health <= 0
            || (!infantryTarget && !vehicleTarget)
            || !expectedParasiteable
            || hasExclusiveAuthority(source.instanceId, true)
            || hasExclusiveAuthority(target.instanceId, false, true)
            || !sameText(source.owner, pending.owner)
            || !sameText(source.type, pending.sourceTypeId)
            || !sameText(target.type, pending.targetTypeId)
            || pending.targetKind != (infantryTarget
                ? ParasiteTargetKind::Infantry
                : ParasiteTargetKind::Vehicle)
            || pending.launchedTick < 0
            || pending.launchedTick > savedTick
            || pending.damage != std::max(0, weapon->damage)
            || pending.damageIntervalTicks != strictInterval
            || pending.paralyzes != std::max(0, warhead->paralyzes)
            || pending.targetMaximumHealth
                != std::max(1, targetType->strength)
            || pending.targetParasiteable != expectedParasiteable
            || pending.organic != sourceType->organic
            || pending.sourceElite != (source.veterancy >= 2)
            || pending.culling != warhead->culling
            || pending.cullingThresholdPerMyriad
                != expectedCullingThreshold) {
            return fail("pending launch rules or world snapshot diverged");
        }
        if (projectile.actorId != projectileId
            || projectile.parentActorId != -1
            || projectile.generation != 0
            || projectile.attackerObjectId != source.instanceId
            || projectile.targetObjectId != target.instanceId
            || projectile.sourceAnchorObjectId != -1
            || !sameText(projectile.owner, pending.owner)
            || !sameText(projectile.weaponId, pending.weaponId)
            || !sameText(projectile.warheadId, pending.warheadId)
            || !sameText(projectile.projectileId, weapon->projectile)
            || projectile.spawnTick != pending.launchedTick
            || projectile.damage != pending.damage
            || projectile.spawnedMissileDamage
            || projectile.shrapnel || projectile.shrapnelReleased
            || projectile.deathWeaponCascadeId != 0
            || projectile.deathWeaponRequestId != 0
            || projectile.deathWeaponCascadeDepth != 0
            || projectile.airstrikeStrikeId != 0
            || projectile.airstrikePlaneToken != 0
            || projectile.particleDamagePulse
            || projectile.suppressWarheadParticleSpawn
            || !projectile.launchSourceValidated
            || projectile.launchSourceKind
                != static_cast<int>(source.kind)
            || projectile.launchSourceAuthoredAircraft
                != source.authoredAircraft
            || !sameText(projectile.launchSourceType, source.type)
            || (projectile.lifecycle
                    != RuntimeProjectileLifecycleState::Spawned
                && projectile.lifecycle
                    != RuntimeProjectileLifecycleState::Traveling)
            || projectile.aa != projectileRules->aa
            || projectile.ag != projectileRules->ag) {
            return fail("pending launch projectile identity diverged");
        }
        limboSourceIds.insert(source.instanceId);
    }

    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        const WeaponType* weapon = findRuntimeWeaponType(
            state.rules,
            projectile.weaponId
        );
        const WarheadType* warhead = findRuntimeWarheadType(
            state.rules,
            projectile.warheadId
        );
        const bool parasiteDeclaration = (weapon != nullptr
                && weapon->limboLaunch)
            || (warhead != nullptr && warhead->parasite);
        if (parasiteDeclaration
            && savedSpecialMechanisms.pendingParasiteLaunches().find(
                projectile.actorId
            ) == savedSpecialMechanisms.pendingParasiteLaunches().end()) {
            return fail("parasite projectile has no pending authority");
        }
    }

    std::unordered_set<int> paralyzedLinkTargetIds;
    for (const auto& [targetId, link] :
            savedSpecialMechanisms.parasiteLinks()) {
        const auto sourceIt = objectsById.find(link.sourceObjectId);
        const auto targetIt = objectsById.find(targetId);
        if (link.projectileId <= 0
            || link.projectileId >= savedProjectiles.nextActorId
            || sourceIt == objectsById.end()
            || targetIt == objectsById.end()
            || sourceIt->second == nullptr || targetIt->second == nullptr
            || projectilesById.contains(link.projectileId)) {
            return fail("attached parasite has conflicting causal identity");
        }
        const WorldObjectInstance& source = *sourceIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        const UnitType* sourceType = findRuntimeUnitType(state.rules, source);
        const UnitType* targetType = findRuntimeUnitType(state.rules, target);
        const WeaponType* weapon = nullptr;
        const WarheadType* warhead = nullptr;
        const ProjectileType* projectileRules = nullptr;
        if (!strictParasiteRules(
                source,
                link.weaponId,
                link.warheadId,
                weapon,
                warhead,
                projectileRules
            )) {
            return fail("attached parasite has an unsafe rules combination");
        }
        (void)projectileRules;
        const int expectedInterval = runtimeParasiteRuleFramesToTicks(
            sourceType != nullptr && sourceType->organic
                ? 36
                : std::max(1, weapon->rof),
            state.config.fixedTickHz
        );
        const int expectedThreshold = source.veterancy >= 2
            ? eliteThreshold
            : normalThreshold;
        if (sourceType == nullptr || targetType == nullptr
            || source.alive || source.health <= 0
            || source.mission != "ParasiteLimbo"
            || !target.alive || target.health <= 0
            || target.kind != WorldObjectKind::Unit
            || target.authoredAircraft || target.airborne
            || !targetType->parasiteable
            || hasExclusiveAuthority(source.instanceId, true)
            || (hasExclusiveAuthority(target.instanceId, true, true)
                && !(!sourceType->organic && !link.organic && link.paralyzes == 0
                    && mobileParasiteHostAuthorityIsSafe(target)))
            || link.targetObjectId != targetId
            || !sameText(source.owner, link.owner)
            || !sameText(source.type, link.sourceTypeId)
            || !sameText(target.type, link.targetTypeId)
            || link.damage != std::max(0, weapon->damage)
            || link.damageIntervalTicks != expectedInterval
            || link.paralyzes != std::max(0, warhead->paralyzes)
            || link.targetMaximumHealth
                != std::max(1, targetType->strength)
            || link.organic != sourceType->organic
            || link.sourceElite != (source.veterancy >= 2)
            || link.culling != warhead->culling
            || link.cullingThresholdPerMyriad != expectedThreshold
            || !link.sourceExpectedInLimbo
            || link.attachedTick < 0 || link.attachedTick > savedTick
            || link.nextDamageTick <= savedTick
            || static_cast<long long>(link.nextDamageTick)
                    - static_cast<long long>(savedTick)
                > link.damageIntervalTicks
            || ((link.paralyzes > 0)
                != (target.mission == "ParasiteParalyzed"))) {
            return fail("attached parasite rules or world snapshot diverged");
        }
        if (link.paralyzes > 0) {
            paralyzedLinkTargetIds.insert(targetId);
        }
        limboSourceIds.insert(source.instanceId);
    }
    for (const WorldObjectInstance& object : completeObjects) {
        const bool limboMission = object.mission == "ParasiteLimbo";
        if (limboMission != limboSourceIds.contains(object.instanceId)
            || (limboMission && (object.alive || object.health <= 0))) {
            return fail("limbo source mission lacks an exact authority");
        }
        if ((object.mission == "ParasiteParalyzed")
            != paralyzedLinkTargetIds.contains(object.instanceId)) {
            return fail("paralyzed host mission lacks an exact link");
        }
    }

    int resolvedCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("SpecialParasiteResolved", "Count", ""),
            resolvedCount
        )
        || resolvedCount < 0 || resolvedCount > 4096) {
        return fail("resolved receipt header is malformed");
    }
    struct SavedParasiteResolvedReceipt {
        int sourceId = -1;
        int targetId = -1;
        int resolvedTick = -1;
        bool hostTerminated = false;
    };
    std::unordered_map<int, SavedParasiteResolvedReceipt>
        resolvedReceiptsByProjectile;
    resolvedReceiptsByProjectile.reserve(
        static_cast<std::size_t>(resolvedCount)
    );
    std::unordered_set<int> resolvedProjectileIds;
    for (int index = 0; index < resolvedCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue(
                "SpecialParasiteResolved",
                std::to_string(index),
                ""
            )
        );
        int projectileId = -1;
        int sourceId = -1;
        int targetId = -1;
        int resolvedTick = -1;
        bool hostTerminated = false;
        if (tokens.size() != 5U
            || !parseRuntimeInt(tokens[0], projectileId)
            || !parseRuntimeInt(tokens[1], sourceId)
            || !parseRuntimeInt(tokens[2], targetId)
            || !parseRuntimeInt(tokens[3], resolvedTick)
            || !parseRuntimeStrictBool(tokens[4], hostTerminated)
            || projectileId <= 0
            || projectileId >= savedProjectiles.nextActorId
            || sourceId < 0 || targetId < 0 || sourceId == targetId
            || resolvedTick < 0 || resolvedTick > savedTick
            || !resolvedProjectileIds.insert(projectileId).second
            || projectilesById.contains(projectileId)
            || savedSpecialMechanisms.pendingParasiteLaunches().contains(
                projectileId
            )) {
            return fail("resolved receipt identity is malformed");
        }
        const auto source = objectsById.find(sourceId);
        const auto target = objectsById.find(targetId);
        if (source == objectsById.end() || target == objectsById.end()
            || source->second == nullptr || target->second == nullptr) {
            return fail("resolved receipt object identity is missing");
        }
        const auto link = savedSpecialMechanisms.parasiteLinks().find(
            targetId
        );
        if (link != savedSpecialMechanisms.parasiteLinks().end()) {
            if (hostTerminated
                || link->second.projectileId != projectileId
                || link->second.sourceObjectId != sourceId
                || link->second.targetObjectId != targetId
                || resolvedTick != link->second.attachedTick) {
                return fail("resolved receipt conflicts with attached link");
            }
        } else if (source->second->mission == "ParasiteLimbo"
            && !limboSourceIds.contains(sourceId)) {
            return fail("terminal receipt leaves an orphaned limbo source");
        }
        resolvedReceiptsByProjectile.emplace(
            projectileId,
            SavedParasiteResolvedReceipt{
                sourceId,
                targetId,
                resolvedTick,
                hostTerminated
            }
        );
    }
    for (const auto& [targetId, link] :
            savedSpecialMechanisms.parasiteLinks()) {
        if (link.attachedTick != savedTick) {
            continue;
        }
        const auto receipt = resolvedReceiptsByProjectile.find(
            link.projectileId
        );
        if (receipt == resolvedReceiptsByProjectile.end()
            || receipt->second.sourceId != link.sourceObjectId
            || receipt->second.targetId != targetId
            || receipt->second.resolvedTick != link.attachedTick
            || receipt->second.hostTerminated) {
            return fail("new attached link lacks its terminal receipt");
        }
    }

    const bool hasStunSection = ini.getSection("ParasiteStuns") != nullptr;
    if (hasStunSection) {
        int schema = -1;
        int count = -1;
        if (!parseRuntimeInt(
                ini.getValue("ParasiteStuns", "Schema", ""),
                schema
            )
            || schema != 1
            || !parseRuntimeInt(
                ini.getValue("ParasiteStuns", "Count", ""),
                count
            )
            || count < 0 || count > 4096) {
            return fail("stun receipt header is malformed");
        }
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("ParasiteStuns", std::to_string(index), "")
            );
            int objectId = -1;
            int untilTick = -1;
            const auto pendingForObject = [
                &savedSpecialMechanisms,
                &objectId
            ]() {
                for (const auto& [unusedId, pending] :
                        savedSpecialMechanisms.pendingParasiteLaunches()) {
                    (void)unusedId;
                    if (pending.sourceObjectId == objectId
                        || pending.targetObjectId == objectId) {
                        return true;
                    }
                }
                for (const auto& [unusedId, link] :
                        savedSpecialMechanisms.parasiteLinks()) {
                    (void)unusedId;
                    if (link.sourceObjectId == objectId
                        || link.targetObjectId == objectId) {
                        return true;
                    }
                }
                return false;
            };
            if (tokens.size() != 2U
                || !parseRuntimeInt(tokens[0], objectId)
                || !parseRuntimeInt(tokens[1], untilTick)
                || objectId < 0 || untilTick <= savedTick
                || static_cast<long long>(untilTick)
                        - static_cast<long long>(savedTick)
                    > 1000000LL
                || savedStuns.contains(objectId)) {
                return fail("stun receipt identity is malformed");
            }
            const auto object = objectsById.find(objectId);
            if (object == objectsById.end() || object->second == nullptr
                || !object->second->alive || object->second->health <= 0
                || object->second->mission != "ParasiteStunned"
                || hasExclusiveAuthority(objectId, true)
                || pendingForObject()) {
                return fail("stun receipt conflicts with world authority");
            }
            savedStuns.emplace(objectId, untilTick);
        }
    }
    for (const WorldObjectInstance& object : completeObjects) {
        if ((object.mission == "ParasiteStunned")
            != savedStuns.contains(object.instanceId)) {
            return fail("stun receipt/world mission is not bidirectional");
        }
    }
    return true;
}

bool validateRuntimeSavedOccupyIntents(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    const std::vector<WorldObjectInstance>& completeObjects,
    std::vector<std::pair<int, int>>& savedIntents,
    bool& hasExplicitSection,
    std::string& errorMessage) {
    hasExplicitSection = ini.getSection("OccupyIntents") != nullptr;
    savedIntents.clear();
    if (!hasExplicitSection) {
        return true;
    }

    int schema = -1;
    int intentCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("OccupyIntents", "Schema", ""),
            schema
        )
        || schema != 1
        || !parseRuntimeInt(
            ini.getValue("OccupyIntents", "Count", ""),
            intentCount
        )
        || intentCount < 0
        || intentCount > 100000) {
        errorMessage = "invalid occupy intent header";
        return false;
    }

    std::unordered_set<int> savedOccupierIds;
    savedIntents.reserve(static_cast<std::size_t>(intentCount));
    for (int index = 0; index < intentCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("OccupyIntents", std::to_string(index), "")
        );
        int occupierId = -1;
        int targetId = -1;
        if (tokens.size() != 2U
            || !parseRuntimeInt(tokens[0], occupierId)
            || !parseRuntimeInt(tokens[1], targetId)
            || occupierId < 0
            || targetId < 0
            || !savedOccupierIds.insert(occupierId).second) {
            errorMessage = "invalid occupy intent record";
            return false;
        }
        savedIntents.emplace_back(occupierId, targetId);
    }
    std::sort(savedIntents.begin(), savedIntents.end());
    if (savedIntents.empty()) {
        return true;
    }

    std::vector<WorldObjectInstance> validationObjects(
        completeObjects.begin(),
        completeObjects.end()
    );

    RuntimeGarrisonSystemState validationGarrisons(
        runtimeSavedGarrisonConfig(state)
    );
    RuntimeGarrisonReconcileReport garrisonReport;
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        std::string garrisonError;
        if (!validationGarrisons.load(
                ini,
                state.rules,
                validationObjects,
                &garrisonError,
                &garrisonReport
            )) {
            errorMessage = "invalid garrison state for occupy intents: "
                + garrisonError;
            return false;
        }
    } else {
        garrisonReport = validationGarrisons.reconcile(
            state.rules,
            validationObjects
        );
    }

    std::unordered_map<int, WorldObjectInstance*> objectsById;
    objectsById.reserve(validationObjects.size());
    for (WorldObjectInstance& object : validationObjects) {
        objectsById.emplace(object.instanceId, &object);
    }

    int behaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", ""),
            behaviorCount
        )
        || behaviorCount < 0
        || behaviorCount > 100000) {
        errorMessage = "invalid behavior count for occupy intents";
        return false;
    }
    std::unordered_map<int, RuntimeObjectBehavior> behaviorsById;
    behaviorsById.reserve(static_cast<std::size_t>(behaviorCount));
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int savedBehaviorObjectId = -1;
        if (tokens.empty()
            || !parseRuntimeInt(tokens[0], savedBehaviorObjectId)
            || savedOccupierIds.find(savedBehaviorObjectId)
                == savedOccupierIds.end()) {
            continue;
        }
        int objectId = -1;
        const auto objectIt = objectsById.find(savedBehaviorObjectId);
        RuntimeObjectBehavior behavior;
        if (objectIt == objectsById.end()
            || objectIt->second == nullptr
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *objectIt->second,
                saveVersion,
                objectId,
                behavior
            )
            || !behaviorsById.emplace(objectId, behavior).second) {
            errorMessage = "invalid behavior record for occupy intents";
            return false;
        }
    }

    std::unordered_set<int> conflictingIntentIds;
    const auto collectConflicts = [&ini, &conflictingIntentIds](
        const std::string& section
    ) {
        const int count = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(section, "Count", "0"),
                0
            )
        );
        for (int index = 0; index < count; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(section, std::to_string(index), "")
            );
            if (!tokens.empty()) {
                const int objectId =
                    parseRuntimeIntOrDefault(tokens[0], -1);
                if (objectId >= 0) {
                    conflictingIntentIds.insert(objectId);
                }
            }
        }
    };
    collectConflicts("EngineerIntents");
    collectConflicts("TransportIntents");

    std::unordered_map<int, int> reservationsByTarget;
    for (const auto& [occupierId, targetId] : savedIntents) {
        const auto occupierIt = objectsById.find(occupierId);
        const auto targetIt = objectsById.find(targetId);
        const auto behaviorIt = behaviorsById.find(occupierId);
        if (occupierIt == objectsById.end()
            || targetIt == objectsById.end()
            || behaviorIt == behaviorsById.end()
            || occupierIt->second == nullptr
            || targetIt->second == nullptr
            || conflictingIntentIds.find(occupierId)
                != conflictingIntentIds.end()) {
            errorMessage = "occupy intent references missing or conflicting state";
            return false;
        }

        const WorldObjectInstance& occupier = *occupierIt->second;
        const WorldObjectInstance& target = *targetIt->second;
        [[maybe_unused]] const RuntimeObjectBehavior& behavior = behaviorIt->second;
        const UnitType* occupierType =
            findRuntimeUnitType(state.rules, occupier);
        const BuildingType* targetType =
            findRuntimeBuildingTypeById(state.rules, target.type);
        const bool pendingOccupyMission =
            runtimeMissionAllowsPendingOccupyIntent(occupier);
        if (!occupier.alive
            || occupier.kind != WorldObjectKind::Infantry
            || occupier.garrisonHostObjectId >= 0
            || target.kind != WorldObjectKind::Structure
            || !target.alive
            || !runtimeGarrisonOwnersAllowEntry(occupier, target)
            || occupierType == nullptr
            || !occupierType->occupier
            || targetType == nullptr
            || !targetType->canBeOccupied
            || targetType->maxNumberOccupants <= 0
            || behavior.kind != RuntimeBehaviorKind::Move
            || behavior.pendingAllowedOccupiedObjectId != targetId
            || behavior.moveTarget.x < 0
            || behavior.moveTarget.y < 0
            || !pendingOccupyMission) {
            std::ostringstream diagnostic;
            diagnostic
                << "invalid occupier, target, or move behavior"
                << " occupier=" << occupierId
                << " alive=" << runtimeBoolText(occupier.alive)
                << " kind=" << toString(occupier.kind)
                << " garrisonHost=" << occupier.garrisonHostObjectId
                << " owner=" << occupier.owner
                << " type=" << occupier.type
                << " occupierRule="
                << runtimeBoolText(
                    occupierType != nullptr && occupierType->occupier
                )
                << " mission=" << occupier.mission
                << " pendingMission="
                << runtimeBoolText(pendingOccupyMission)
                << " target=" << targetId
                << " targetAlive=" << runtimeBoolText(target.alive)
                << " targetKind=" << toString(target.kind)
                << " targetOwner=" << target.owner
                << " targetType=" << target.type
                << " targetRule="
                << runtimeBoolText(
                    targetType != nullptr
                    && targetType->canBeOccupied
                    && targetType->maxNumberOccupants > 0
                )
                << " behavior="
                << runtimeBehaviorKindName(behavior.kind)
                << " allowed="
                << behavior.pendingAllowedOccupiedObjectId
                << " move="
                << behavior.moveTarget.x
                << ","
                << behavior.moveTarget.y;
            errorMessage = diagnostic.str();
            return false;
        }

        const int reserved = ++reservationsByTarget[targetId];
        if (target.garrisonOccupantIds.size()
                + static_cast<std::size_t>(reserved)
            > static_cast<std::size_t>(targetType->maxNumberOccupants)) {
            errorMessage = "occupy intent exceeds target capacity";
            return false;
        }
    }
    return true;
}

bool saveRuntimeStateWithWriter(
    const RuntimeGameSessionState& state,
    const std::string& savePath,
    const RuntimeAtomicFileWriter& writer,
    std::string& errorMessage) {
    // Saving a half-finalized ObjectExit graph would publish a state whose
    // ownership and containment authorities disagree.  Keep this rejection
    // ahead of every filesystem mutation, including parent/temp creation.
    if (state.drainingObjectExits
        || !state.pendingObjectExits.empty()
        || !state.pendingObjectExitIds.empty()) {
        errorMessage = "cannot save while object exits are pending";
        return false;
    }
    if (!validateRuntimePadAircraftProductionProjection(
            state,
            errorMessage
        )) {
        return false;
    }
    try {
        const std::filesystem::path path(savePath);
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
        return runtimeWriteFileAtomically(path, writer, errorMessage);
    } catch (const std::exception& exception) {
        errorMessage = exception.what();
        return false;
    }
}

bool validateRuntimeSavedFactoryDeliveries(
    const RuntimeProductionHostState& state,
    const IniFile& ini,
    int saveVersion,
    int savedTick,
    const RuntimeSavedProductionCandidate& savedProduction,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeRepairDepotSystemState& savedRepairDepots,
    RuntimeFactoryDeliveryState& validatedState,
    std::string& errorMessage,
    const RuntimeSavedProductionByOwner* savedProductionByOwner) {
    RuntimeFactoryDeliveryState candidate;
    if (!candidate.load(ini, &errorMessage)) {
        return false;
    }

    if (savedTick < 0) {
        errorMessage = "invalid saved tick for factory delivery";
        return false;
    }

    std::unordered_map<int, const WorldObjectInstance*> savedObjectsById;
    savedObjectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        savedObjectsById.emplace(object.instanceId, &object);
    }

    for (const auto& grant : candidate.pendingFreeUnits()) {
        const auto host = savedObjectsById.find(grant.hostId);
        const auto* building = state.rules.findBuildingType(grant.buildingType);
        if (host == savedObjectsById.end() || !host->second->alive
            || host->second->kind != WorldObjectKind::Structure
            || host->second->owner != grant.owner || host->second->type != grant.buildingType
            || building == nullptr || building->freeUnit != grant.unitType
            || findRuntimeUnitTypeById(state.rules, grant.unitType) == nullptr) {
            errorMessage = "pending free unit does not match its live building";
            return false;
        }
    }
    std::unordered_map<int, RuntimeObjectBehavior> savedBehaviors;
    const int behaviorCount = parseRuntimeIntOrDefault(
        ini.getValue("Behaviors", "Count", "-1"),
        -1
    );
    if (!candidate.leases().empty()
        && (behaviorCount < 0 || behaviorCount > 100000)) {
        errorMessage = "invalid behavior header for factory delivery";
        return false;
    }
    for (int index = 0; index < std::max(0, behaviorCount); ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int recordObjectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], recordObjectId)) {
            errorMessage = "invalid behavior id for factory delivery";
            return false;
        }
        const auto object = savedObjectsById.find(recordObjectId);
        int parsedObjectId = -1;
        RuntimeObjectBehavior behavior;
        if (object == savedObjectsById.end()
            || object->second == nullptr
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *object->second,
                saveVersion,
                parsedObjectId,
                behavior
            )
            || parsedObjectId != recordObjectId
            || !savedBehaviors.emplace(
                recordObjectId,
                std::move(behavior)
            ).second) {
            errorMessage = "invalid behavior record for factory delivery";
            return false;
        }
    }

    // Object records deliberately start with renderMoveActive=false. A live
    // delivery may already occupy its rally navigation cell while its final
    // visible segment is still running. Only the strictly validated saved
    // segment can prove that the lease is still pending at that cell.
    std::unordered_set<int> savedActiveMoveObjectIds;
    if (!candidate.leases().empty()) {
        auto movementContext = std::make_unique<RuntimeProductionHostState>(*state.constructionUiClock, *state.constructionAudioClock, state.geometry);
        movementContext->config = state.config;
        movementContext->rules = state.rules;
        movementContext->rulesLoaded = state.rulesLoaded;
        movementContext->world.accessObjects() = savedObjects;
        for (auto& object : movementContext->world.accessObjects()) object.renderMoveActive = false;
        movementContext->behaviors = savedBehaviors;
        movementContext->factoryDeliveries = candidate;
        RuntimeSnapshotActiveStateScope movementScope(*movementContext);
        if (!loadRuntimeTransportTransferProofIntents(*movementContext, ini, errorMessage)) return false;
        if (!loadRuntimeTransportTransferPresentation(*movementContext, ini, errorMessage)) return false;
        if (!loadRuntimeVehicleArrivals(*movementContext, ini, errorMessage)) return false;
        if (!loadRuntimeInfantryWalks(*movementContext, ini, errorMessage)) return false;
        if (!validateRuntimeTransportTransferPresentation(*movementContext, errorMessage)) return false;
        for (const auto& object : movementContext->world.getObjects()) {
            if (object.renderMoveActive) savedActiveMoveObjectIds.insert(object.instanceId);
        }
    }

    const auto productionContainsSerial = [](
        const RuntimeSavedProductionCandidate& production,
        std::uint64_t serial
    ) {
        for (const RuntimeProductionLane lane : runtimeProductionLanes) {
            const RuntimeProductionLaneState& laneState =
                production.lanes.lanes[
                    runtimeProductionLaneIndex(lane)
                ];
            if (std::any_of(
                    laneState.queued.begin(),
                    laneState.queued.end(),
                    [serial](const RuntimeProductionLaneOrder& order) {
                        return order.serial == serial;
                    }
                )
                || (laneState.active.has_value()
                    && laneState.active->serial == serial)
                || (laneState.ready.has_value()
                    && laneState.ready->serial == serial)) {
                return true;
            }
        }
        return false;
    };
    RuntimeFactoryDeliveryReconcileInput input;
    std::set<int> hostIds;
    std::set<int> objectIds;
    for (const RuntimeFactoryDeliveryLease& lease : candidate.leases()) {
        const RuntimeSavedProductionCandidate* ownerProduction =
            &savedProduction;
        if (savedProductionByOwner != nullptr) {
            const auto entry = savedProductionByOwner->find(
                runtimeCanonicalHumanHouseOwner(lease.canonicalOwner)
            );
            if (entry == savedProductionByOwner->end()) {
                errorMessage =
                    "factory delivery references an unknown production owner";
                return false;
            }
            ownerProduction = &entry->second;
        }
        if (lease.acquiredTick > savedTick
            || lease.orderSerial >= ownerProduction->lanes.nextSerial
            || productionContainsSerial(
                *ownerProduction,
                lease.orderSerial
            )) {
            errorMessage =
                "factory delivery references a future or uncommitted order";
            return false;
        }
        if (savedRepairDepots.claimsDock(
                lease.hostId,
                0
            )) {
            errorMessage =
                "factory delivery conflicts with saved repair dock";
            return false;
        }

        const auto host = std::find_if(
            savedObjects.begin(),
            savedObjects.end(),
            [&lease](const WorldObjectInstance& object) {
                return object.instanceId == lease.hostId;
            }
        );
        const BuildingType* building = host == savedObjects.end()
            ? nullptr
            : findRuntimeBuildingTypeById(state.rules, host->type);
        const std::optional<RuntimeFactoryDeliveryDomain> expectedDomain =
            host == savedObjects.end()
                ? std::nullopt
                : runtimeFactoryDeliveryHostDomain(state, *host);
        const bool validFactory = building != nullptr
            && host != savedObjects.end()
            && runtimeFactoryDeliveryHostStructurallyEligibleForOwner(
                state,
                *host,
                lease.canonicalOwner,
                lease.domain
            );
        if (host == savedObjects.end()
            || !host->alive
            || host->kind != WorldObjectKind::Structure
            || !validFactory
            || !expectedDomain.has_value()
            || *expectedDomain != lease.domain) {
            errorMessage =
                "factory delivery references an invalid saved host";
            return false;
        }
        if (hostIds.insert(lease.hostId).second) {
            input.hosts.push_back({
                runtimeRepairDepotCanonicalOwnerId(state, host->owner),
                host->instanceId,
                *expectedDomain,
                true,
                false
            });
        }

        const auto objectEntry = savedObjectsById.find(
            lease.producedObjectId
        );
        const WorldObjectInstance* object =
            objectEntry == savedObjectsById.end()
                ? nullptr
                : objectEntry->second;
        const UnitType* unit = object == nullptr
            ? nullptr
            : findRuntimeUnitTypeById(state.rules, object->type);
        const bool navalUnit = unit != nullptr
            && (unit->naval || unit->waterBound);
        const bool validUnitDomain = unit != nullptr
            && (lease.lane == RuntimeProductionLane::Ships
                ? lease.domain == RuntimeFactoryDeliveryDomain::Naval
                    && navalUnit
                : lease.lane == RuntimeProductionLane::Vehicles
                    && lease.domain == RuntimeFactoryDeliveryDomain::Land
                    && !navalUnit);
        if (object == nullptr
            || !object->alive
            || object->kind != WorldObjectKind::Unit
            || !validUnitDomain) {
            errorMessage =
                "factory delivery references an invalid produced object";
            return false;
        }

        const auto behavior = savedBehaviors.find(lease.producedObjectId);
        const RuntimeCellPoint current = runtimeCellPointForObject(*object);
        const RuntimeCellPoint rally{
            lease.rallyCell.x,
            lease.rallyCell.y
        };
        const int rallyKey = runtimeCellKey(rally.x, rally.y);
        const auto cachedRallyTerrain = state.terrainByCell.find(rallyKey);
        const TerrainCell* rallyTerrain =
            cachedRallyTerrain == state.terrainByCell.end()
                ? nullptr
                : cachedRallyTerrain->second;
        if (rallyTerrain == nullptr) {
            const auto terrain = std::find_if(
                state.world.getTerrainCells().begin(),
                state.world.getTerrainCells().end(),
                [&rally](const TerrainCell& candidateTerrain) {
                    return candidateTerrain.x == rally.x
                        && candidateTerrain.y == rally.y;
                }
            );
            rallyTerrain = terrain == state.world.getTerrainCells().end()
                ? nullptr
                : &*terrain;
        }
        const RuntimeTerrainInfo rallyTerrainInfo = rallyTerrain == nullptr
            ? RuntimeTerrainInfo{}
            : runtimeCachedTerrainInfoForCell(state, *rallyTerrain);
        const bool validNavalRally =
            lease.domain != RuntimeFactoryDeliveryDomain::Naval
            || (!lease.rallyCell.onBridge
                && rallyTerrain != nullptr
                && rallyTerrainInfo.passable
                && rallyTerrainInfo.water
                && !rallyTerrainInfo.bridge);
        if (behavior == savedBehaviors.end()
            || behavior->second.kind != RuntimeBehaviorKind::Move
            || behavior->second.state == RuntimeBehaviorState::Complete
            || behavior->second.moveTarget != rally
            || behavior->second.pendingAllowedOccupiedObjectId != -1
            || (current == rally && !savedActiveMoveObjectIds.contains(object->instanceId))
            || rallyTerrain == nullptr
            || !rallyTerrainInfo.passable
            || lease.rallyCell.onBridge != rallyTerrainInfo.bridge
            || !validNavalRally) {
            errorMessage =
                "factory delivery has invalid saved rally movement";
            return false;
        }
        if (objectIds.insert(lease.producedObjectId).second) {
            input.objects.push_back({
                runtimeRepairDepotCanonicalOwnerId(state, object->owner),
                object->instanceId,
                true
            });
        }
    }

    const RuntimeFactoryDeliveryReconcileReport report =
        candidate.reconcile(input);
    if (!report.accepted || report.releasedLeases != 0) {
        errorMessage = report.error.empty()
            ? "saved factory delivery diverged during strict reconcile"
            : report.error;
        return false;
    }
    validatedState = std::move(candidate);
    return true;
}

bool loadRuntimeV8ProductionCandidate(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    RuntimeSavedProductionCandidate& candidate,
    std::string& errorMessage) {
    const int schema = parseRuntimeIntOrDefault(
        ini.getValue("ProductionLanes", "Schema", "-1"),
        -1
    );
    if (!runtimeProductionLanesSectionSchemaSupported(schema)) {
        errorMessage = "invalid production lanes section schema";
        return false;
    }
    std::string serialized;
    if (!runtimeHexDecode(
            trimAsciiLocal(
                ini.getValue("ProductionLanes", "DataHex", "")
            ),
            serialized
        )) {
        errorMessage = "invalid production lanes DataHex";
        return false;
    }
    RuntimeSavedProductionCandidate parsed;
    if (!runtimeDeserializeProductionLanes(
            serialized,
            parsed.lanes,
            &errorMessage
        )) {
        return false;
    }

    const std::string savedHashText = trimAsciiLocal(
        ini.getValue("ProductionLanes", "StateHash", "")
    );
    std::uint64_t savedHash = 0;
    if (!parseRuntimeUint64Strict(savedHashText, savedHash)
        || savedHash != runtimeProductionLanesStateHash(parsed.lanes)) {
        errorMessage = "production lanes state hash mismatch";
        return false;
    }

    const std::string selectionText = trimAsciiLocal(
        ini.getValue("ProductionLanes", "Selection", "")
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
            || serial == 0) {
            errorMessage = "invalid production placement selection";
            return false;
        }
        parsed.selectedPlacementLane = *lane;
        parsed.selectedPlacementSerial = serial;
    }
    if (schema == 1
        && !migrateRuntimeSchema1ProductionTiming(
            state,
            parsed,
            errorMessage
        )) {
        return false;
    }
    if (!validateRuntimeProductionCatalogState(
            state,
            parsed,
            errorMessage
        )) {
        return false;
    }
    candidate = std::move(parsed);
    return true;
}

bool validateRuntimeSavedAirstrikeHost(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int saveVersion,
    int savedTick,
    const RuntimeSavedAirstrikeHostCandidate& candidate,
    const std::vector<WorldObjectInstance>& savedObjects,
    const RuntimeProjectileSaveCandidate& savedProjectiles,
    const RuntimeSpecialMechanismState& specialMechanisms,
    const RuntimeMagBeamSystemState& magBeams,
    const RuntimeRepairDepotSystemState& repairDepots,
    const RuntimeFactoryDeliveryState& factoryDeliveries,
    const RuntimeAircraftServiceSystemState& aircraftServices,
    std::string& errorMessage) {
    if (saveVersion < 13) {
        return candidate.airstrikes.callers().empty()
            && candidate.airstrikes.strikes().empty()
            && candidate.airstrikes.projectileReceipts().empty()
            && candidate.bindingsByToken.empty()
            && candidate.authoritiesByActor.empty();
    }
    const auto fail = [&](const std::string& message) {
        errorMessage = message;
        return false;
    };
    const auto textMatches = [](const std::string& left,
                                const std::string& right) {
        return toLowerAsciiLocal(trimAsciiLocal(left))
            == toLowerAsciiLocal(trimAsciiLocal(right));
    };
    const bool hasAuthority = !candidate.airstrikes.callers().empty()
        || !candidate.airstrikes.strikes().empty()
        || !candidate.airstrikes.projectileReceipts().empty()
        || !candidate.bindingsByToken.empty()
        || !candidate.authoritiesByActor.empty();
    if (candidate.airstrikes.lastAdvancedTick() > savedTick
        || (hasAuthority
            && candidate.airstrikes.lastAdvancedTick() != savedTick)) {
        return fail("AirstrikeRuntime tick does not match saved world");
    }

    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    objectsById.reserve(savedObjects.size());
    for (const WorldObjectInstance& object : savedObjects) {
        objectsById.emplace(object.instanceId, &object);
    }
    std::unordered_map<int, const RuntimeProjectileEffect*>
        projectilesByActor;
    projectilesByActor.reserve(savedProjectiles.projectiles.size());
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        projectilesByActor.emplace(projectile.actorId, &projectile);
    }
    std::unordered_set<int> behaviorObjectIds;
    const int behaviorCount = parseRuntimeIntOrDefault(
        ini.getValue("Behaviors", "Count", "-1"),
        -1
    );
    if (behaviorCount < 0 || behaviorCount > 100000) {
        return fail("invalid behavior count for Airstrike authority");
    }
    for (int index = 0; index < behaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int objectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], objectId)) {
            return fail("invalid behavior identity for Airstrike authority");
        }
        behaviorObjectIds.insert(objectId);
    }

    std::unordered_set<int> competingObjectIds;
    for (const RuntimeRepairDepotRelation& relation :
            repairDepots.relations()) {
        competingObjectIds.insert(relation.unitId);
    }
    for (const RuntimeFactoryDeliveryLease& lease :
            factoryDeliveries.leases()) {
        if (lease.producedObjectId > 0) {
            competingObjectIds.insert(lease.producedObjectId);
        }
    }
    for (const RuntimeAircraftServiceDockBinding& binding :
            aircraftServices.dockBindings()) {
        competingObjectIds.insert(binding.aircraftId);
    }
    for (const RuntimeMagBeamBindingState& binding : magBeams.bindings()) {
        competingObjectIds.insert(binding.sourceObjectId);
        competingObjectIds.insert(binding.targetObjectId);
    }

    std::unordered_set<int> boundObjectIds;
    for (const auto& [planeToken, binding] : candidate.bindingsByToken) {
        const RuntimeAirstrikeStrikeState* strike = nullptr;
        const RuntimeAirstrikePlaneState* plane =
            runtimeAirstrikePlaneStateForToken(
                candidate.airstrikes,
                planeToken,
                &strike
            );
        const auto object = objectsById.find(binding.objectId);
        if (plane == nullptr || strike == nullptr
            || object == objectsById.end() || object->second == nullptr
            || !object->second->alive || object->second->health <= 0
            || object->second->kind != WorldObjectKind::Unit
            || !object->second->authoredAircraft
            || !object->second->airborne
            || object->second->dockedToObjectId >= 0
            || object->second->dockIndex >= 0
            || object->second->garrisonHostObjectId >= 0
            || !object->second->transportPassengerIds.empty()
            || behaviorObjectIds.find(binding.objectId)
                != behaviorObjectIds.end()
            || competingObjectIds.find(binding.objectId)
                != competingObjectIds.end()
            || specialMechanisms.isChronoInTransit(binding.objectId)
            || specialMechanisms.isCarrierChildDocked(binding.objectId)
            || specialMechanisms.isParasiteSourceLimboed(binding.objectId)
            || specialMechanisms.isParasiteHostParalyzed(binding.objectId)
            || !boundObjectIds.insert(binding.objectId).second
            || binding.strikeId != strike->strikeId
            || binding.callerObjectId != strike->callerObjectId
            || binding.targetObjectId != strike->targetObjectId
            || binding.elite != strike->elite
            || binding.spawnTick != plane->spawnTick
            || binding.bombTick != plane->bombTick
            || binding.despawnTick != plane->despawnTick
            || (plane->phase == RuntimeAirstrikePlanePhase::Ingress
                && binding.egressStartTick != binding.bombTick)
            || (plane->phase == RuntimeAirstrikePlanePhase::Egress
                && (binding.egressStartTick < binding.spawnTick
                    || binding.egressStartTick > savedTick))
            || binding.route.entry.x != plane->route.entry.x
            || binding.route.entry.y != plane->route.entry.y
            || binding.route.exit.x != plane->route.exit.x
            || binding.route.exit.y != plane->route.exit.y
            || (plane->phase == RuntimeAirstrikePlanePhase::Ingress
                && (binding.route.target.x != plane->route.target.x
                    || binding.route.target.y != plane->route.target.y))
            || (plane->phase != RuntimeAirstrikePlanePhase::Ingress
                && plane->phase != RuntimeAirstrikePlanePhase::Egress)
            || !textMatches(binding.owner, strike->owner)
            || !textMatches(binding.owner, object->second->owner)
            || !textMatches(
                binding.aircraftTypeId,
                strike->aircraftTypeId
            )
            || !textMatches(
                binding.aircraftTypeId,
                object->second->type
            )
            || !textMatches(binding.weaponId, strike->weaponId)
            || !textMatches(binding.warheadId, strike->warheadId)
            || (plane->phase == RuntimeAirstrikePlanePhase::Ingress
                && toLowerAsciiLocal(trimAsciiLocal(object->second->mission))
                    != "airstrikeingress")
            || (plane->phase == RuntimeAirstrikePlanePhase::Egress
                && toLowerAsciiLocal(trimAsciiLocal(object->second->mission))
                    != "airstrikeegress")) {
            return fail("Airstrike plane binding/world identity mismatch");
        }
        RuntimeAirstrikeCell from = binding.route.entry;
        RuntimeAirstrikeCell to = binding.route.target;
        int startTick = binding.spawnTick;
        int endTick = binding.bombTick;
        double fromX=from.x,fromY=from.y;
        if (plane->phase == RuntimeAirstrikePlanePhase::Egress) {
            from = binding.route.target;
            to = binding.route.exit;
            startTick = binding.egressStartTick;
            endTick = binding.despawnTick;
            fromX=from.x;fromY=from.y;
            if(binding.hasPreciseEgressOrigin) {
                const double dx=plane->route.target.x-plane->route.entry.x,dy=plane->route.target.y-plane->route.entry.y;
                const double ox=binding.preciseEgressOriginX-plane->route.entry.x,oy=binding.preciseEgressOriginY-plane->route.entry.y;
                const double lengthSquared=dx*dx+dy*dy;
                if(!strike->cancelled||candidate.airstrikes.findRootProjectileReceipt(binding.strikeId,planeToken)!=nullptr
                    ||!std::isfinite(binding.preciseEgressOriginX)||!std::isfinite(binding.preciseEgressOriginY)
                    ||std::abs(ox*dy-oy*dx)>1e-8*std::max(1.0,std::sqrt(lengthSquared))
                    ||ox*dx+oy*dy < -1e-8||ox*dx+oy*dy > lengthSquared+1e-8
                    ||std::abs(binding.preciseEgressOriginX-from.x)>0.500001
                    ||std::abs(binding.preciseEgressOriginY-from.y)>0.500001)
                    return fail("invalid exact Airstrike cancelled egress origin");
                fromX=binding.preciseEgressOriginX;fromY=binding.preciseEgressOriginY;
            }
        } else if(binding.hasPreciseEgressOrigin)return fail("exact Airstrike egress origin on non-egress plane");
        const double progress = std::clamp(
            static_cast<double>(savedTick - startTick)
                / static_cast<double>(std::max(1, endTick - startTick)),
            0.0,1.0
        );
        const int expectedX=static_cast<int>(std::lround(fromX+(to.x-fromX)*progress));
        const int expectedY=static_cast<int>(std::lround(fromY+(to.y-fromY)*progress));
        if (object->second->x != expectedX
            || object->second->y != expectedY) {
            return fail("Airstrike plane route projection mismatch");
        }
    }
    for (const RuntimeAirstrikeStrikeState& strike :
            candidate.airstrikes.strikes()) {
        const auto caller = objectsById.find(strike.callerObjectId);
        const auto target = objectsById.find(strike.targetObjectId);
        const RuntimeAirstrikeAircraftDefinition aircraft =
            runtimeAirstrikeAircraftDefinition(
                state,
                strike.aircraftTypeId,
                strike.elite
            );
        const WeaponType* weapon = findRuntimeWeaponType(
            state.rules,
            strike.weaponId
        );
        const bool callerPresent = caller != objectsById.end()
            && caller->second != nullptr;
        const bool callerValidForActive = callerPresent
            && caller->second->alive && caller->second->health > 0
            && runtimeAirstrikeCallerMatchesStrike(
                state,
                *caller->second,
                strike
            );
        const bool callerValidForCancelled = strike.cancelled
            && (callerPresent
                || strike.cancelReason
                    == RuntimeAirstrikeCancelReason::CallerMissing);
        if ((!strike.cancelled && !callerValidForActive)
            || (strike.cancelled && !callerValidForCancelled)
            || target == objectsById.end() || target->second == nullptr
            || (!strike.cancelled
                && (!target->second->alive || target->second->health <= 0))
            || (!strike.cancelled && !strike.forceFire
                && runtimeOwnersAreAllied(
                    state,
                    strike.owner,
                    target->second->owner
                ))
            || (!strike.cancelled
                && (weapon == nullptr
                    || !runtimeWeaponCanTargetObject(
                        state,
                        *weapon,
                        *target->second
                    )))
            || !aircraft.exists || !aircraft.isAircraft
            || !aircraft.hasAttackWeapon
            || !textMatches(aircraft.weaponId, strike.weaponId)
            || !textMatches(aircraft.warheadId, strike.warheadId)) {
            return fail("Airstrike strike world/rules identity mismatch");
        }
        for (const RuntimeAirstrikePlaneState& plane : strike.planes) {
            const bool requiresBinding =
                plane.phase == RuntimeAirstrikePlanePhase::Ingress
                || plane.phase == RuntimeAirstrikePlanePhase::Egress;
            if (requiresBinding
                != (candidate.bindingsByToken.find(plane.planeToken)
                    != candidate.bindingsByToken.end())) {
                return fail("Airstrike plane/binding authority is not bijective");
            }
        }
    }
    for (const WorldObjectInstance& object : savedObjects) {
        const std::string mission = toLowerAsciiLocal(
            trimAsciiLocal(object.mission)
        );
        if ((mission == "airstrikeingress" || mission == "airstrikeegress")
            != (boundObjectIds.find(object.instanceId)
                != boundObjectIds.end())) {
            return fail("orphan Airstrike managed plane mission");
        }
    }

    const auto pairKey = [](int strikeId, int planeToken) {
        return (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(strikeId)
                ) << 32U)
            | static_cast<std::uint32_t>(planeToken);
    };
    std::unordered_map<std::uint64_t,
        const RuntimeAirstrikeRootAuthority*> authorityByPair;
    authorityByPair.reserve(candidate.authoritiesByActor.size());
    std::set<int> salvoActors;
    for (const auto& [rootActorId, authority] :
            candidate.authoritiesByActor) {
        if (rootActorId != authority.rootProjectileActorId
            || authority.launchTick > savedTick
            || authority.terminalTick > savedTick
            || authority.strikeId
                >= candidate.airstrikes.nextStrikeId()
            || authority.planeToken
                >= candidate.airstrikes.nextPlaneToken()
            || !authorityByPair.emplace(
                pairKey(authority.strikeId, authority.planeToken),
                &authority
            ).second) {
            return fail("duplicate or future Airstrike root authority");
        }
        for (const auto& round : authority.rounds) {
            if (round.actorId >= savedProjectiles.nextActorId || round.launchTick > savedTick
                || round.terminalTick > savedTick || !salvoActors.insert(round.actorId).second
                || (authority.terminalTick >= 0 && (round.terminalTick < 0
                    || round.terminalTick > authority.terminalTick)))
                return fail("duplicate or future Airstrike salvo round");
        }
        if (authority.roundsRemaining > 0 && (authority.terminalTick >= 0
            || authority.nextFireTick <= savedTick))
            return fail("invalid pending Airstrike salvo clock");
        const auto planeObject = objectsById.find(authority.planeObjectId);
        const auto callerObject = objectsById.find(authority.callerObjectId);
        const auto targetObject = objectsById.find(authority.targetObjectId);
        if (planeObject == objectsById.end()
            || planeObject->second == nullptr
            || planeObject->second->kind != WorldObjectKind::Unit
            || !planeObject->second->authoredAircraft
            || !textMatches(planeObject->second->owner, authority.owner)
            || !textMatches(
                planeObject->second->type,
                authority.aircraftTypeId
            )
            || callerObject == objectsById.end()
            || targetObject == objectsById.end()) {
            return fail("Airstrike root authority object identity mismatch");
        }
        const RuntimeAirstrikeAircraftDefinition aircraft =
            runtimeAirstrikeAircraftDefinition(
                state,
                authority.aircraftTypeId,
                authority.elite
            );
        const WeaponType* authorityWeapon = findRuntimeWeaponType(
            state.rules,
            authority.weaponId
        );
        const ProjectileType* authorityProjectile =
            authorityWeapon == nullptr
                ? nullptr
                : findRuntimeProjectileType(
                    state.rules,
                    authorityWeapon->projectile
                );
        if (!aircraft.hasAttackWeapon
            || !textMatches(aircraft.weaponId, authority.weaponId)
            || !textMatches(aircraft.warheadId, authority.warheadId)
            || authorityWeapon == nullptr
            || authorityProjectile == nullptr
            || !textMatches(
                authorityWeapon->projectile,
                authority.projectileId
            )) {
            return fail("Airstrike root authority rules identity mismatch");
        }
        const auto binding = candidate.bindingsByToken.find(
            authority.planeToken
        );
        if (authority.rounds.size() + authority.roundsRemaining
                > static_cast<std::size_t>(std::clamp(authorityWeapon->burst, 1, 256))
            || (authority.roundsRemaining > 0 && (binding == candidate.bindingsByToken.end()
                || !planeObject->second->alive || planeObject->second->health <= 0
                || !targetObject->second->alive || targetObject->second->health <= 0)))
            return fail("invalid Airstrike salvo count or pending source");
        if (binding != candidate.bindingsByToken.end()
            && (binding->second.strikeId != authority.strikeId
                || binding->second.objectId != authority.planeObjectId
                || binding->second.callerObjectId
                    != authority.callerObjectId
                || binding->second.targetObjectId
                    != authority.targetObjectId
                || binding->second.elite != authority.elite
                || !textMatches(binding->second.owner, authority.owner)
                || !textMatches(
                    binding->second.aircraftTypeId,
                    authority.aircraftTypeId
                )
                || !textMatches(
                    binding->second.weaponId,
                    authority.weaponId
                )
                || !textMatches(
                    binding->second.warheadId,
                    authority.warheadId
                )
                || binding->second.route.target.x
                    != authority.bombCell.x
                || binding->second.route.target.y
                    != authority.bombCell.y)) {
            return fail("Airstrike root/binding authority mismatch");
        }
        const RuntimeAirstrikeStrikeState* authorityStrike =
            candidate.airstrikes.findStrike(authority.strikeId);
        if (authorityStrike != nullptr) {
            const RuntimeAirstrikeStrikeState* planeStrike = nullptr;
            const RuntimeAirstrikePlaneState* authorityPlane =
                runtimeAirstrikePlaneStateForToken(
                    candidate.airstrikes,
                    authority.planeToken,
                    &planeStrike
                );
            if (authorityPlane == nullptr
                || authorityPlane->despawnTick < authorityPlane->bombTick
                || planeStrike != authorityStrike
                || authorityStrike->callerObjectId
                    != authority.callerObjectId
                || authorityStrike->targetObjectId
                    != authority.targetObjectId
                || authorityStrike->elite != authority.elite
                || !textMatches(authorityStrike->owner, authority.owner)
                || !textMatches(
                    authorityStrike->aircraftTypeId,
                    authority.aircraftTypeId
                )
                || !textMatches(
                    authorityStrike->weaponId,
                    authority.weaponId
                )
                || !textMatches(
                    authorityStrike->warheadId,
                    authority.warheadId
                )) {
                return fail("Airstrike root/core authority mismatch");
            }
        }
    }

    std::unordered_map<std::uint64_t, int> liveRootCountByPair;
    std::unordered_map<std::uint64_t, int> liveDescendantCountByPair;
    std::set<int> liveSalvoActors;
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        if (projectile.airstrikeStrikeId == 0
            && projectile.airstrikePlaneToken == 0) {
            continue;
        }
        const std::uint64_t key = pairKey(
            projectile.airstrikeStrikeId,
            projectile.airstrikePlaneToken
        );
        const auto authority = authorityByPair.find(key);
        if (authority == authorityByPair.end()
            || authority->second == nullptr
            || projectile.attackerObjectId
                != authority->second->planeObjectId
            || !projectile.launchSourceValidated
            || projectile.launchSourceKind
                != static_cast<int>(WorldObjectKind::Unit)
            || !projectile.launchSourceAuthoredAircraft
            || !textMatches(projectile.owner, authority->second->owner)
            || !textMatches(
                projectile.launchSourceType,
                authority->second->aircraftTypeId
            )) {
            return fail("Airstrike projectile has no exact host authority");
        }
        const bool root = projectile.parentActorId == -1
            && projectile.generation == 0;
        if (root) {
            ++liveRootCountByPair[key];
            const auto round = std::find_if(authority->second->rounds.begin(), authority->second->rounds.end(),
                [&](const auto& value) { return value.actorId == projectile.actorId; });
            if (round == authority->second->rounds.end() || round->terminalTick >= 0
                || !liveSalvoActors.insert(projectile.actorId).second)
                return fail("Airstrike root has no pending salvo round");
            const WeaponType* rootWeapon = findRuntimeWeaponType(
                state.rules,
                authority->second->weaponId
            );
            const ProjectileType* rootProjectileRules =
                rootWeapon == nullptr
                    ? nullptr
                    : findRuntimeProjectileType(
                        state.rules,
                        rootWeapon->projectile
                    );
            if (projectile.actorId != round->actorId
                || projectile.targetObjectId
                    != authority->second->targetObjectId
                || projectile.sourceAnchorObjectId != -1
                || projectile.spawnTick != round->launchTick
                || projectile.fromCell.x != round->sourceCell.x
                || projectile.fromCell.y != round->sourceCell.y
                || !textMatches(
                    projectile.weaponId,
                    authority->second->weaponId
                )
                || !textMatches(
                    projectile.warheadId,
                    authority->second->warheadId
                )
                || !textMatches(
                    projectile.projectileId,
                    authority->second->projectileId
                )
                || projectile.damage != round->damage
                || std::abs(
                    projectile.veteranDamageMultiplier
                        - authority->second->veteranDamageMultiplier
                ) > 0.000000001
                || projectile.spawnedMissileDamage
                || projectile.deathWeaponCascadeId != 0
                || projectile.deathWeaponRequestId != 0
                || projectile.deathWeaponCascadeDepth != 0
                || projectile.particleDamagePulse
                || projectile.suppressWarheadParticleSpawn
                || (projectile.lifecycle
                        != RuntimeProjectileLifecycleState::Spawned
                    && projectile.lifecycle
                        != RuntimeProjectileLifecycleState::Traveling)
                || rootWeapon == nullptr
                || rootProjectileRules == nullptr
                || !textMatches(
                    rootWeapon->projectile,
                    projectile.projectileId
                )
                || projectile.aa != rootProjectileRules->aa
                || projectile.ag != rootProjectileRules->ag) {
                return fail("Airstrike root projectile payload mismatch");
            }
        } else {
            ++liveDescendantCountByPair[key];
            if (salvoActors.contains(projectile.actorId)
                || projectile.actorId
                    <= authority->second->rootProjectileActorId
                || projectile.parentActorId <= 0
                || projectile.parentActorId
                    < authority->second->rootProjectileActorId
                || projectile.parentActorId >= projectile.actorId
                || projectile.generation <= 0) {
                return fail("Airstrike descendant causal identity mismatch");
            }
        }
    }
    for (const auto& [key, authority] : authorityByPair) {
        const RuntimeAirstrikeProjectileReceipt* receipt =
            candidate.airstrikes.findRootProjectileReceipt(
                authority->strikeId,
                authority->planeToken
            );
        int pendingRounds = 0;
        for (const auto& round : authority->rounds) {
            if (round.terminalTick < 0) {
                ++pendingRounds;
                if (!liveSalvoActors.contains(round.actorId))
                    return fail("Airstrike pending round has no projectile");
            }
        }
        if (authority->terminalTick < 0) {
            if (pendingRounds + authority->roundsRemaining <= 0
                || liveRootCountByPair[key] != pendingRounds
                || receipt == nullptr || receipt->terminalTick >= 0
                || receipt->launchTick != authority->launchTick) {
                return fail("pending Airstrike root receipt is not bijective");
            }
        } else {
            const int retentionTicks = std::max(
                0,
                candidate.airstrikes.limits()
                    .terminalReceiptRetentionTicks
            );
            const long long receiptAge =
                static_cast<long long>(savedTick)
                - authority->terminalTick;
            const bool receiptMustRemain = receiptAge < retentionTicks;
            if (liveRootCountByPair[key] != 0
                || liveDescendantCountByPair[key] <= 0
                || (receipt != nullptr) != receiptMustRemain
                || (receipt != nullptr
                    && (receipt->terminalTick
                            != authority->terminalTick
                        || receipt->launchTick
                            != authority->launchTick))) {
                return fail(
                    "terminal Airstrike authority retention mismatch"
                );
            }
        }
    }
    for (const RuntimeAirstrikeProjectileReceipt& receipt :
            candidate.airstrikes.projectileReceipts()) {
        if (receipt.launchTick > savedTick
            || receipt.terminalTick > savedTick) {
            return fail("future Airstrike projectile receipt");
        }
        if (receipt.terminalTick >= 0
            && static_cast<long long>(savedTick)
                    - receipt.terminalTick
                >= std::max(
                    0,
                    candidate.airstrikes.limits()
                        .terminalReceiptRetentionTicks
                )) {
            return fail("expired Airstrike projectile receipt retained");
        }
        if (receipt.terminalTick < 0) {
            const auto authority = authorityByPair.find(pairKey(
                receipt.strikeId,
                receipt.planeToken
            ));
            if (authority == authorityByPair.end()
                || authority->second == nullptr
                || authority->second->terminalTick >= 0) {
                return fail("orphan pending Airstrike projectile receipt");
            }
        }
    }
    return true;
}

bool validateRuntimeSaveOuterIdentity(
    const RuntimeGameSessionState& state,
    const IniFile& ini,
    int version,
    std::string& errorMessage) {
    const RuntimeSaveActiveIdentity identity{state.campaignMode,
        state.config.defaultMapPath, state.config.campaignId,
        state.config.campaignMissionId, state.config.campaignSourceId};
    const RuntimeHostSaveIdentityNormalization normalization;
    return ::validateRuntimeSaveOuterIdentity(identity, ini, version,
        normalization, errorMessage);
}

bool materializeRuntimeLoadStateFromIni(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage,
    bool exactOfflinePoses) {
    const int version = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "Version", "0"),
        0
    );
    if (version != 1 && version != 2 && version != 3 && version != 4
        && version != 5 && version != 6 && version != 7
        && version != 8 && version != 9 && version != 10
        && version != 11 && version != 12 && version != 13
        && version != 14 && version != 15) {
        errorMessage = "unsupported save version";
        return false;
    }

    // Bind modern saves to the already initialized map/mode before any
    // detached world or authority materialization.  The live context is the
    // authority; a save never selects or replaces its map implicitly.
    if (!validateRuntimeSaveOuterIdentity(
            state,
            ini,
            version,
            errorMessage
        )) {
        return false;
    }

    const std::string noCooldown = toLowerAsciiLocal(trimAsciiLocal(
        ini.getValue("SkirmishSession", "NoCooldown", "no")));
    if (noCooldown != "yes" && noCooldown != "no" && noCooldown != "true"
        && noCooldown != "false" && noCooldown != "1" && noCooldown != "0") {
        errorMessage = "invalid saved no-cooldown option";
        return false;
    }
    state.config.noCooldown = parseRuntimeBool(noCooldown);
    if (state.config.noCooldown && (state.campaignMode || state.config.multiplayerActive)) {
        errorMessage = "no-cooldown is restricted to offline skirmish";
        return false;
    }

    if (version >= 10
        && (ini.getSection("SpecialMechanisms") == nullptr
            || ini.getSection("SpecialTimedBombs") == nullptr)) {
        errorMessage = "missing modern special mechanism section";
        return false;
    }
    const int specialMechanismVersion = parseRuntimeIntOrDefault(
        ini.getValue("SpecialMechanisms", "Version", "-1"), -1);
    if (version >= 11
        && (ini.getSection("Projectiles") == nullptr
            || ini.getSection("RuntimePsychicStatus") == nullptr
            || ini.getSection("SpecialParasitePending") == nullptr
            || ini.getSection("SpecialParasites") == nullptr
            || ini.getSection("SpecialParasiteResolved") == nullptr
            || (specialMechanismVersion != 2 && specialMechanismVersion != 3))) {
        errorMessage =
            "missing or incompatible v11 projectile/psychic/parasite section";
        return false;
    }
    if (version >= 12
        && ini.getSection("RuntimeMagBeam") == nullptr) {
        errorMessage = "v12 save is missing RuntimeMagBeam authority";
        return false;
    }
    const int airstrikeVersion = parseRuntimeIntOrDefault(
        ini.getValue("AirstrikeRuntime", "Version", "-1"), -1);
    if (version >= 13
        && (ini.getSection("AirstrikeRuntime") == nullptr
            || ini.getSection("RuntimeAirstrikeHost") == nullptr
            || ini.getSection("FactoryDeliveryRuntime") == nullptr
            || ini.getSection("ProductionLanes") == nullptr
            || !runtimeProductionLanesModernSectionSchemaSupported(
                parseRuntimeIntOrDefault(
                    ini.getValue("ProductionLanes", "Schema", "-1"),
                    -1
                )
            )
            || (airstrikeVersion != 2 && airstrikeVersion != 3)
            || (parseRuntimeIntOrDefault(ini.getValue("RuntimeAirstrikeHost", "Schema", "-1"), -1) != 1
                && parseRuntimeIntOrDefault(ini.getValue("RuntimeAirstrikeHost", "Schema", "-1"), -1) != 2))) {
        errorMessage =
            "v13 save is missing required production or Airstrike authority";
        return false;
    }
    if (version == 14
        && (ini.getSection("RuntimeWorldAudioHost") == nullptr
            || ini.getSection("WorldAudioRuntime") == nullptr
            || ini.getSection("RuntimeLightingHost") == nullptr
            || ini.getSection("LightingRuntime") == nullptr
            || ini.getSection("RuntimeParticleHost") == nullptr
            || ini.getSection("ParticleRuntime") == nullptr
            || parseRuntimeIntOrDefault(
                ini.getValue("RuntimeWorldAudioHost", "Schema", "-1"),
                -1
            ) != 1
            || parseRuntimeIntOrDefault(
                ini.getValue("RuntimeLightingHost", "Schema", "-1"),
                -1
            ) != 1
            || parseRuntimeIntOrDefault(
                ini.getValue("RuntimeParticleHost", "Schema", "-1"),
                -1
            ) != 1)) {
        errorMessage = "v14 save is missing audiovisual authority";
        return false;
    }
    if (version == 15
        && (ini.getSection("MultiplayerSnapshot") == nullptr
            || ini.getSection("RuntimeParticleHost") == nullptr
            || ini.getSection("ParticleRuntime") == nullptr
            || parseRuntimeIntOrDefault(
                ini.getValue("RuntimeParticleHost", "Schema", "-1"),
                -1
            ) != 1)) {
        errorMessage = "v15 snapshot is missing multiplayer/particle authority";
        return false;
    }

    // Every authority validator below must observe the same terrain,
    // overlays, bridge health, and terrain-object state that will be
    // committed.  Materialize this projection before Repair/Subcell/path
    // checks rather than applying it only after those checks have passed.
    if (!materializeRuntimeSavedTerrainProjection(
            state,
            ini,
            errorMessage
        )) {
        return false;
    }
    std::unordered_set<int> explicitGattlingCooldownIds;
    std::unordered_set<int> explicitDualWeaponCooldownIds;
    if (!parseRuntimeCompleteSavedObjects(
            state,
            ini,
            version,
            errorMessage,
            &explicitGattlingCooldownIds,
            &explicitDualWeaponCooldownIds
        )
        || !materializeRuntimeSavedTransportManifests(
            state,
            ini,
            version,
            errorMessage
        )) {
        return false;
    }
    if (state.campaignMode
        && !parseRuntimeSavedCampaignAlliances(
            ini,
            version,
            state.triggers.campaignAlliedPairs,
            errorMessage
        )) {
        return false;
    }
    injectRuntimeLoadFaultForSmoke(
        RuntimeLoadFaultPointForSmoke::AfterSavedWorldProjection
    );

    RuntimeSpecialMechanismState validatedSpecialMechanisms;
    validatedSpecialMechanisms.reset(
        state.rulesLoaded
            ? state.rules.getSpecialMechanismRules()
            : SpecialMechanismRules{}
    );
    if (version >= 2
        && !validatedSpecialMechanisms.load(ini, errorMessage)) {
        return false;
    }

    RuntimeProjectileSaveCandidate savedProjectiles;
    if (!parseRuntimeProjectilesFromSave(
            ini,
            savedProjectiles,
            errorMessage
        )) {
        return false;
    }
    if (version >= 11
        && savedProjectiles.schema < runtimeProjectileSaveSchemaV3) {
        errorMessage = "v11 requires projectile schema 3 or later";
        return false;
    }

    RuntimePsychicStatusSystemState validatedPsychicStatuses;
    if (version >= 11
        && !validatedPsychicStatuses.load(ini, &errorMessage)) {
        return false;
    }

    RuntimeMagBeamSystemState validatedMagBeams;
    if (version >= 12
        && !validatedMagBeams.load(ini, &errorMessage)) {
        return false;
    }

    RuntimeSavedAirstrikeHostCandidate validatedAirstrikeHost;
    if (!parseRuntimeSavedAirstrikeHost(
            ini,
            version,
            validatedAirstrikeHost,
            errorMessage
        )) {
        return false;
    }
    RuntimeAirstrikeSystemState& validatedAirstrikes =
        validatedAirstrikeHost.airstrikes;

    RuntimeSavedProductionCandidate savedProduction;
    if (version >= 8) {
        if (!loadRuntimeV8ProductionCandidate(
                state,
                ini,
                savedProduction,
                errorMessage
            )) {
            return false;
        }
    } else if (!migrateRuntimeLegacyProductionCandidate(
            state,
            ini,
            savedProduction,
            errorMessage
        )) {
        return false;
    }

    RuntimeSavedProductionByOwner savedProductionByOwner;
    if (version == 15
        && !parseRuntimeV15ProductionByOwner(
            ini,
            savedProductionByOwner,
            errorMessage
        )) {
        return false;
    }

    const bool hasExplicitDeathWeaponRuntime =
        ini.getSection("DeathWeaponRuntime") != nullptr;
    // Every authority consumes the one complete object projection built at
    // the start of scratch materialization.  Do not parse a second, partial
    // view whose veterancy/cloak/airborne/container fields can diverge.
    std::vector<WorldObjectInstance> validationGateObjects(
        state.world.getObjects().begin(),
        state.world.getObjects().end()
    );
    if (!validateRuntimeSavedBehaviorRecords(
            state,
            ini,
            version,
            validationGateObjects,
            errorMessage
        )) {
        return false;
    }
    RuntimeCombatDetailsState validatedCombatDetails;
    if (!validatedCombatDetails.load(ini, errorMessage)
        || !validatedCombatDetails.reconcileLegacyProjectiles(
            ini, state.rules, savedProjectiles.projectiles, errorMessage)) return false;
    RuntimeDeathWeaponSystemState validatedDeathWeapons;
    RuntimeDeathWeaponContext validatedDeathWeaponContext;
    validatedDeathWeaponContext.rules = &state.rules;
    validatedDeathWeaponContext.objects = &validationGateObjects;
    validatedDeathWeaponContext.maximumCascadeDepth = 16U;
    RuntimeDeathWeaponReconcileReport validatedDeathWeaponReport;
    if (!validatedDeathWeapons.load(
            ini,
            validatedDeathWeaponContext,
            &errorMessage,
            &validatedDeathWeaponReport
        )) {
        return false;
    }
    if (hasExplicitDeathWeaponRuntime
        && (validatedDeathWeaponReport.removedMissingObjects != 0U
            || validatedDeathWeaponReport.removedIdentityMismatches != 0U)) {
        errorMessage =
            "death weapon runtime references missing or mismatched objects";
        return false;
    }
    if (hasExplicitDeathWeaponRuntime) {
        std::unordered_set<int> savedContainedObjectIds;
        if (ini.getSection("TransportManifests") != nullptr) {
            int manifestCount = -1;
            if (!parseRuntimeInt(
                    ini.getValue("TransportManifests", "Count", ""),
                    manifestCount
                )
                || manifestCount < 0
                || manifestCount > 100000) {
                errorMessage = "invalid transport manifest count";
                return false;
            }
            for (int index = 0; index < manifestCount; ++index) {
                const std::vector<std::string> tokens = splitRuntimeSaveFields(
                    ini.getValue(
                        "TransportManifests",
                        std::to_string(index),
                        ""
                    )
                );
                if (tokens.size() != 2U) {
                    errorMessage = "invalid transport manifest record";
                    return false;
                }
                std::stringstream passengers(tokens[1]);
                std::string passengerToken;
                bool foundPassenger = false;
                while (std::getline(passengers, passengerToken, ',')) {
                    int passengerId = -1;
                    if (!parseRuntimeInt(passengerToken, passengerId)
                        || passengerId < 0) {
                        errorMessage = "invalid transport passenger id";
                        return false;
                    }
                    foundPassenger = true;
                    savedContainedObjectIds.insert(passengerId);
                }
                if (!foundPassenger) {
                    errorMessage = "empty transport manifest";
                    return false;
                }
            }
        }
        if (ini.getSection("GarrisonRuntime") != nullptr) {
            int hostCount = -1;
            if (!parseRuntimeInt(
                    ini.getValue("GarrisonRuntime", "HostCount", ""),
                    hostCount
                )
                || hostCount < 0
                || hostCount > 100000) {
                errorMessage = "invalid garrison host count";
                return false;
            }
            for (int index = 0; index < hostCount; ++index) {
                const std::string section = "GarrisonRuntime.Host."
                    + std::to_string(index);
                if (ini.getSection(section) == nullptr) {
                    errorMessage = "missing garrison host record";
                    return false;
                }
                std::stringstream occupants(
                    ini.getValue(section, "Occupants", "")
                );
                std::string occupantToken;
                while (std::getline(occupants, occupantToken, ',')) {
                    if (trimAsciiLocal(occupantToken).empty()) {
                        continue;
                    }
                    int occupantId = -1;
                    if (!parseRuntimeInt(occupantToken, occupantId)
                        || occupantId < 0) {
                        errorMessage = "invalid garrison occupant id";
                        return false;
                    }
                    savedContainedObjectIds.insert(occupantId);
                }
            }
        }
        for (const RuntimeDeathWeaponTriggeredObject& triggered :
                validatedDeathWeapons.triggeredObjects()) {
            const auto savedObject = std::find_if(
                validationGateObjects.begin(),
                validationGateObjects.end(),
                [&triggered](const WorldObjectInstance& object) {
                    return object.instanceId == triggered.objectId;
                }
            );
            if (savedObject == validationGateObjects.end()
                || savedObject->alive) {
                errorMessage =
                    "death weapon tombstone references a live object";
                return false;
            }
            const std::string mission = toLowerAsciiLocal(
                trimAsciiLocal(savedObject->mission)
            );
            const bool insideGrinder = mission == "insidegrinder";
            if (savedContainedObjectIds.find(triggered.objectId)
                    != savedContainedObjectIds.end()
                || (!insideGrinder
                    && mission.find("inside") != std::string::npos)
                || mission == "occupied"
                || mission == "garrisoned") {
                errorMessage =
                    "death weapon tombstone references a contained object";
                return false;
            }
            if (insideGrinder
                && (savedObject->health <= 0
                    || savedObject->subCell != -1
                    || savedObject->dockedToObjectId >= 0
                    || savedObject->dockIndex >= 0
                    || savedObject->garrisonHostObjectId >= 0
                    || !savedObject->transportPassengerIds.empty()
                    || triggered.emittedRequestId != 0)) {
                errorMessage = "invalid InsideGrinder terminal object";
                return false;
            }
        }
        for (const WorldObjectInstance& savedObject : validationGateObjects) {
            if (toLowerAsciiLocal(trimAsciiLocal(savedObject.mission))
                    != "insidegrinder") {
                continue;
            }
            const auto triggered = std::find_if(
                validatedDeathWeapons.triggeredObjects().begin(),
                validatedDeathWeapons.triggeredObjects().end(),
                [&savedObject](
                    const RuntimeDeathWeaponTriggeredObject& candidate
                ) {
                    return candidate.objectId == savedObject.instanceId;
                }
            );
            if (savedObject.alive
                || triggered == validatedDeathWeapons.triggeredObjects().end()
                || triggered->emittedRequestId != 0) {
                errorMessage = "InsideGrinder object lacks suppressed tombstone";
                return false;
            }
        }
    }
    if (!validateAndCompleteRuntimeProjectileLaunchProvenance(
            state,
            savedProjectiles,
            validationGateObjects,
            validatedDeathWeapons,
            errorMessage,
            version >= 13 ? &validatedAirstrikes : nullptr
        )) {
        return false;
    }
    std::unordered_map<std::uint64_t, int> deathWeaponSourceByRequest;
    const std::uint64_t nextDeathWeaponRequestId =
        validatedDeathWeapons.nextRequestId();
    for (const RuntimeDeathWeaponTriggeredObject& triggered :
            validatedDeathWeapons.triggeredObjects()) {
        if (triggered.emittedRequestId == 0) {
            continue;
        }
        if (triggered.emittedRequestId >= nextDeathWeaponRequestId
            || !deathWeaponSourceByRequest.emplace(
                triggered.emittedRequestId,
                triggered.objectId
            ).second) {
            errorMessage = "invalid death weapon emitted request id";
            return false;
        }
    }
    for (const RuntimeProjectileEffect& projectile :
            savedProjectiles.projectiles) {
        if (projectile.deathWeaponCascadeId == 0) {
            continue;
        }
        const auto requestSource = deathWeaponSourceByRequest.find(
            projectile.deathWeaponRequestId
        );
        if (projectile.deathWeaponCascadeId >= nextDeathWeaponRequestId
            || projectile.deathWeaponRequestId >= nextDeathWeaponRequestId
            || deathWeaponSourceByRequest.find(
                projectile.deathWeaponCascadeId
            ) == deathWeaponSourceByRequest.end()
            || requestSource == deathWeaponSourceByRequest.end()
            || requestSource->second != projectile.attackerObjectId
            || (projectile.deathWeaponCascadeDepth == 0
                && projectile.deathWeaponRequestId
                    != projectile.deathWeaponCascadeId)
            || (projectile.deathWeaponCascadeDepth > 0
                && projectile.deathWeaponRequestId
                    <= projectile.deathWeaponCascadeId)) {
            errorMessage =
                "projectile death weapon lineage is not backed by runtime state";
            return false;
        }
    }
    RuntimeGateSystemState validatedGateState(
        runtimeGateConfigForState(state)
    );
    if (!validatedGateState.load(
            ini,
            runtimeSavedGateContext(state, validationGateObjects),
            &errorMessage
        )) {
        return false;
    }
    RuntimeSubcellContext validatedGateSubcellContext =
        runtimeSavedGateSubcellContext(validatedGateState);
    for (const auto& object : validationGateObjects)
        if (runtimeInvisibleMapBuilding(&state.rules, object))
            validatedGateSubcellContext.ignoredWholeCellObjectIds.insert(object.instanceId);

    RuntimeCooperativeSessionState validatedCooperativeSession;
    if (!readRuntimeCooperativeSession(state, ini, validatedCooperativeSession, errorMessage)) return false;
    std::map<std::string, RuntimeComputerMiningState> validatedComputerMining;
    if (!readRuntimeComputerMining(ini, validatedComputerMining, errorMessage)) return false;
    std::map<std::string, RuntimeComputerTacticsState> validatedComputerTactics;
    if (!readRuntimeComputerTactics(ini, validatedComputerTactics, errorMessage)) return false;

    RuntimeRepairDepotSystemState validatedRepairDepotState(
        std::max(1, state.config.fixedTickHz)
    );
    bool hasExplicitRepairDepotRuntime = false;
    if (!validateRuntimeSavedRepairDepots(
            state,
            ini,
            version,
            validatedGateState,
            validatedRepairDepotState,
            hasExplicitRepairDepotRuntime,
            errorMessage
        )) {
        return false;
    }

    int savedFactoryDeliveryTick = -1;
    if (!parseRuntimeInt(
            ini.getValue("RuntimeSave", "Tick", ""),
            savedFactoryDeliveryTick
        )
        || savedFactoryDeliveryTick < 0
        || savedFactoryDeliveryTick == std::numeric_limits<int>::max()) {
        errorMessage = "invalid saved tick for factory delivery";
        return false;
    }

    RuntimeWorldAudioSystemState validatedWorldAudio(
        RuntimeWorldAudioConfig{
            std::max(1, state.config.fixedTickHz), 4096U
        }
    );
    RuntimeLightingSystemState validatedLighting(
        state.mapFacts.lighting,
        runtimeLightingConfigForState(state)
    );
    RuntimeParticleSystemState validatedParticles(
        std::max(1, state.config.fixedTickHz)
    );
    std::map<std::uint64_t, RuntimeActiveLightingToken>
        validatedActiveLightingTokens;
    if (version == 14) {
        std::uint64_t savedCatalogHash = 0;
        std::uint64_t savedWorldAudioHash = 0;
        std::uint64_t savedLightingHash = 0;
        std::uint64_t savedParticleHash = 0;
        if (!parseRuntimeUint64Strict(
                ini.getValue(
                    "RuntimeWorldAudioHost", "CatalogHash", ""
                ),
                savedCatalogHash
            )
            || (savedCatalogHash != state.worldAudioCatalog.stateHash()
                && (ini.getValue("RuntimeWorldAudioHost", "CatalogVersion", "1") != "1"
                    || savedCatalogHash != state.worldAudioCatalog.stateHash(false)))
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeWorldAudioHost", "StateHash", ""),
                savedWorldAudioHash
            )
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeLightingHost", "StateHash", ""),
                savedLightingHash
            )
            || !parseRuntimeUint64Strict(
                ini.getValue("RuntimeParticleHost", "StateHash", ""),
                savedParticleHash
            )) {
            errorMessage = "invalid v14 audiovisual host hashes";
            return false;
        }

        RuntimeWorldAudioReconcileReport audioReport;
        int savedWorldAudioCommandTick = -1;
        if (!validatedWorldAudio.load(
                ini,
                runtimeWorldAudioContext(state),
                &errorMessage,
                &audioReport
            )
            || !audioReport.contextValid
            || audioReport.removedMissingSounds != 0U
            || audioReport.removedCompletedHandles != 0U
            || audioReport.rescheduledLegacyFiniteHandles != 0U
            || audioReport.removedByEventLimit != 0U
            || audioReport.removedByGlobalLimit != 0U
            || validatedWorldAudio.config().fixedTickHz
                != std::max(1, state.config.fixedTickHz)
            || validatedWorldAudio.config().maximumHandles != 4096U
            || validatedWorldAudio.lastAdvancedTick()
                != savedFactoryDeliveryTick
            || !parseRuntimeInt(
                ini.getValue(
                    "WorldAudioRuntime", "LastCommandTick", ""
                ),
                savedWorldAudioCommandTick
            )
            || savedWorldAudioCommandTick < -1
            || savedWorldAudioCommandTick > savedFactoryDeliveryTick
            || validatedWorldAudio.stateHash() != savedWorldAudioHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 world audio authority";
            }
            return false;
        }

        RuntimeLightingReconcileReport lightingReport;
        const RuntimeLightingConfig expectedLightingConfig =
            runtimeLightingConfigForState(state);
        if (!validatedLighting.load(
                ini,
                state.mapFacts.lighting,
                &errorMessage,
                &lightingReport
            )
            || !lightingReport.contextValid
            || !lightingReport.profilesMatched
            || lightingReport.stateReset
            || lightingReport.valuesSanitized
            || lightingReport.removedThresholdWatchers != 0U
            || validatedLighting.lastAdvancedTick()
                != savedFactoryDeliveryTick
            || validatedLighting.config().fixedTickHz
                != expectedLightingConfig.fixedTickHz
            || validatedLighting.stateHash() != savedLightingHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 lighting authority";
            }
            return false;
        }
        std::set<int> validLightingEventIds;
        for (const RuntimeTriggerDefinition& trigger :
                state.triggers.triggers) {
            for (const RuntimeTriggerEvent& event : trigger.events) {
                if ((event.type == 45 || event.type == 46)
                    && event.lightingEventId > 0) {
                    validLightingEventIds.insert(event.lightingEventId);
                }
            }
        }
        for (int watcherId :
                validatedLighting.thresholdWatcherEventIds()) {
            if (!validLightingEventIds.contains(watcherId)) {
                errorMessage =
                    "v14 lighting watcher is not authored by this map";
                return false;
            }
        }

        const std::array<const char*, 7> particleHeaderKeys{{
            "Version", "Tick", "FixedTickHz", "NextSystemId",
            "NextParticleId", "SystemCount", "ParticleCount"
        }};
        for (const char* key : particleHeaderKeys) {
            if (trimAsciiLocal(
                    ini.getValue("ParticleRuntime", key, "")
                ).empty()) {
                errorMessage = "v14 particle authority is missing header key ";
                errorMessage += key;
                return false;
            }
        }
        RuntimeParticleReconcileReport particleReport;
        if (!validatedParticles.load(
                ini,
                state.rules,
                &errorMessage,
                &particleReport
            )
            || particleReport.removedSystems != 0U
            || particleReport.removedParticles != 0U
            || particleReport.stoppedSystems != 0U
            || validatedParticles.currentTick()
                != savedFactoryDeliveryTick
            || validatedParticles.fixedTickHz()
                != std::max(1, state.config.fixedTickHz)
            || validatedParticles.stateHash() != savedParticleHash) {
            if (errorMessage.empty()) {
                errorMessage = "invalid v14 particle authority";
            }
            return false;
        }
        std::unordered_map<int, const WorldObjectInstance*>
            savedObjectsByParticleSourceId;
        savedObjectsByParticleSourceId.reserve(validationGateObjects.size());
        for (const WorldObjectInstance& object : validationGateObjects) {
            savedObjectsByParticleSourceId.emplace(object.instanceId, &object);
        }
        const auto savedObjectForParticleSource = [
            &savedObjectsByParticleSourceId
        ](int objectId) -> const WorldObjectInstance* {
            const auto found = savedObjectsByParticleSourceId.find(objectId);
            return found == savedObjectsByParticleSourceId.end()
                ? nullptr
                : found->second;
        };
        for (const RuntimeParticleSystemInstance& system :
                validatedParticles.systems()) {
            if (system.sourceObjectId < 0) {
                continue;
            }
            const WorldObjectInstance* source =
                savedObjectForParticleSource(system.sourceObjectId);
            if (source == nullptr) {
                errorMessage =
                    "v14 particle system has invalid source authority";
                return false;
            }
        }
        for (const RuntimeParticleInstance& particle :
                validatedParticles.particles()) {
            if (particle.sourceObjectId >= 0
                && savedObjectForParticleSource(
                    particle.sourceObjectId
                ) == nullptr) {
                errorMessage =
                    "v14 particle has missing source object";
                return false;
            }
        }

        int lightingTokenCount = -1;
        if (!parseRuntimeInt(
                ini.getValue(
                    "RuntimeLightingHost", "ActiveTokenCount", ""
                ),
                lightingTokenCount
            )
            || lightingTokenCount < 0 || lightingTokenCount > 4096) {
            errorMessage = "invalid v14 lighting token header";
            return false;
        }
        for (int index = 0; index < lightingTokenCount; ++index) {
            const std::vector<std::string> values = splitRuntimeSaveFields(
                ini.getValue(
                    "RuntimeLightingHost",
                    "Token" + std::to_string(index),
                    ""
                )
            );
            std::uint64_t token = 0;
            int startedTick = -1;
            RuntimeLightingMode mode = RuntimeLightingMode::Base;
            if (values.size() != 3U
                || !parseRuntimeUint64Strict(values[0], token)
                || token == 0
                || !runtimeLightingModeFromName(values[1], mode)
                || mode == RuntimeLightingMode::Base
                || !parseRuntimeInt(values[2], startedTick)
                || startedTick < 0
                || startedTick > savedFactoryDeliveryTick
                || !validatedActiveLightingTokens.emplace(
                    token,
                    RuntimeActiveLightingToken{token, mode, startedTick}
                ).second) {
                errorMessage = "invalid v14 lighting token record";
                return false;
            }
        }
        if (validatedLighting.mode()
            != runtimeEffectiveLightingModeForTokens(
                validatedActiveLightingTokens
            )) {
            errorMessage =
                "v14 lighting profile does not match active token authority";
            return false;
        }
    } else {
        (void)validatedWorldAudio.tick(
            runtimeWorldAudioContext(state),
            savedFactoryDeliveryTick
        );
        (void)validatedLighting.tick(savedFactoryDeliveryTick);
        if (!validatedParticles.alignEmptyStateTick(
                savedFactoryDeliveryTick
            )) {
            errorMessage = "could not migrate empty particle authority";
            return false;
        }
    }
    if (version >= 10
        && !validateRuntimeSavedTimedBombState(
            state,
            validatedSpecialMechanisms,
            validationGateObjects,
            savedFactoryDeliveryTick,
            errorMessage
        )) {
        return false;
    }
    if (!validateRuntimeSavedCarrierState(
            state,
            ini,
            version,
            validatedSpecialMechanisms,
            validationGateObjects,
            errorMessage
        )) {
        return false;
    }
    if (version >= 11
        && !validateRuntimeSavedPsychicStatuses(
            state,
            validatedPsychicStatuses,
            validationGateObjects,
            savedFactoryDeliveryTick,
            savedProjectiles,
            errorMessage
        )) {
        return false;
    }
    RuntimePlayerCommandState validatedPlayerCommands;
    const auto* playerCommandsSection = ini.getSection("PlayerCommands");
    if (playerCommandsSection != nullptr) {
        std::string playerCommandsText;
        const std::string playerCommandsHex = ini.getValue("PlayerCommands", "StateHex", "");
        if (playerCommandsHex.empty()
            || !iniHexDecode(playerCommandsHex, playerCommandsText, 32U * 1024U * 1024U)
            || playerCommandsText.empty()
            || !validatedPlayerCommands.restore(playerCommandsText, errorMessage)) {
            errorMessage = "invalid player command save state: " + errorMessage;
            return false;
        }
    }
    RuntimeFactoryDeliveryState validatedFactoryDeliveryState;
    if (!validateRuntimeSavedFactoryDeliveries(
            state,
            ini,
            version,
            savedFactoryDeliveryTick,
            savedProduction,
            validationGateObjects,
            validatedRepairDepotState,
            validatedFactoryDeliveryState,
            errorMessage,
            version == 15 ? &savedProductionByOwner : nullptr
    )) {
        return false;
    }
    std::unordered_map<int, int> validatedParasiteStuns;
    if (!validateRuntimeSavedParasiteState(
            state,
            ini,
            version,
            validatedSpecialMechanisms,
            validationGateObjects,
            savedProjectiles,
            savedFactoryDeliveryTick,
            validatedRepairDepotState,
            validatedFactoryDeliveryState,
            validatedParasiteStuns,
            errorMessage
        )) {
        return false;
    }

    RuntimeAircraftServiceSystemState validatedAircraftServiceState(
        std::max(1, state.config.fixedTickHz)
    );
    bool hasExplicitAircraftServiceRuntime = false;
    if (!validateRuntimeSavedAircraftServices(
            state,
            ini,
            version,
            validatedAircraftServiceState,
            hasExplicitAircraftServiceRuntime,
            errorMessage
        )) {
        return false;
    }
    if (version >= 12
        && !validateRuntimeSavedMagBeams(
            state,
            ini,
            validatedMagBeams,
            validationGateObjects,
            savedFactoryDeliveryTick,
            savedProjectiles,
            validatedSpecialMechanisms,
            validatedRepairDepotState,
            validatedFactoryDeliveryState,
            validatedAircraftServiceState,
            errorMessage
        )) {
        return false;
    }
    if (!validateRuntimeSavedAirstrikeHost(
            state,
            ini,
            version,
            savedFactoryDeliveryTick,
            validatedAirstrikeHost,
            validationGateObjects,
            savedProjectiles,
            validatedSpecialMechanisms,
            validatedMagBeams,
            validatedRepairDepotState,
            validatedFactoryDeliveryState,
            validatedAircraftServiceState,
            errorMessage
        )) {
        return false;
    }

    std::vector<std::pair<int, int>> savedOccupyIntents;
    bool hasExplicitOccupyIntents = false;
    if (!validateRuntimeSavedOccupyIntents(
            state,
            ini,
            version,
            validationGateObjects,
            savedOccupyIntents,
            hasExplicitOccupyIntents,
            errorMessage
        )) {
        return false;
    }

    std::vector<std::pair<int, RuntimeTimedBombIntent>>
        savedTimedBombIntents;
    if (!validateRuntimeSavedTimedBombIntents(
            state,
            ini,
            version,
            validationGateObjects,
            validatedSpecialMechanisms,
            savedTimedBombIntents,
            errorMessage
        )) {
        return false;
    }

    std::vector<RuntimeSubcellReservation> savedSubcellReservations;
    if (ini.getSection("SubcellReservations") != nullptr) {
        const int subcellSchema = parseRuntimeIntOrDefault(
            ini.getValue("SubcellReservations", "Schema", "-1"),
            -1
        );
        const int subcellReservationCount = parseRuntimeIntOrDefault(
            ini.getValue("SubcellReservations", "Count", "-1"),
            -1
        );
        if (subcellSchema != 1
            || subcellReservationCount < 0
            || subcellReservationCount > 100000) {
            errorMessage = "invalid subcell reservation header";
            return false;
        }

        std::unordered_set<int> savedSubcellMoverIds;
        savedSubcellReservations.reserve(
            static_cast<std::size_t>(subcellReservationCount)
        );
        for (int index = 0; index < subcellReservationCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "SubcellReservations",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() != 6U) {
                errorMessage = "invalid subcell reservation record";
                return false;
            }
            RuntimeSubcellReservation reservation;
            reservation.moverObjectId =
                parseRuntimeIntOrDefault(tokens[0], -1);
            reservation.destination.x =
                parseRuntimeIntOrDefault(tokens[1], -1);
            reservation.destination.y =
                parseRuntimeIntOrDefault(tokens[2], -1);
            reservation.destination.cell =
                parseRuntimeIntOrDefault(tokens[3], -1);
            reservation.destination.onBridge = parseRuntimeBool(tokens[4]);
            reservation.subCell = parseRuntimeIntOrDefault(tokens[5], -1);
            if (reservation.moverObjectId < 0
                || reservation.destination.x < 0
                || reservation.destination.y < 0
                || reservation.destination.cell != runtimeCellKey(
                    reservation.destination.x,
                    reservation.destination.y
                )
                || !RuntimeSubcellSystemState::isActiveSubCell(
                    reservation.subCell
                )
                || !savedSubcellMoverIds.insert(
                    reservation.moverObjectId
                ).second) {
                errorMessage = "invalid subcell reservation value";
                return false;
            }
            savedSubcellReservations.push_back(reservation);
        }

    }

    auto validatedLateState = std::make_unique<RuntimeProductionHostState>(*state.constructionUiClock, *state.constructionAudioClock, state.geometry);
    RuntimeProductionHostState& lateCandidate = *validatedLateState;
    lateCandidate.config = state.config;
    lateCandidate.report = state.report;
    lateCandidate.rules = state.rules;
    lateCandidate.art = state.art;
    lateCandidate.mapFacts = state.mapFacts;
    lateCandidate.rulesLoaded = state.rulesLoaded;
    lateCandidate.artLoaded = state.artLoaded;
    lateCandidate.mapFactsLoaded = state.mapFactsLoaded;
    lateCandidate.campaignMode = state.campaignMode;
    // A save predating [Crates] restores the map's initial crate seed.
    // Carry that immutable baseline through the nested load candidate.
    lateCandidate.initialSkirmishCrates = state.initialSkirmishCrates;
    lateCandidate.destroyableBridges = state.destroyableBridges;
    lateCandidate.skirmishCountryByOwner = state.skirmishCountryByOwner;
    lateCandidate.skirmishTeamByOwner = state.skirmishTeamByOwner;
    lateCandidate.skirmishDifficultyByOwner = state.skirmishDifficultyByOwner;
    lateCandidate.skirmishModeRulesLoaded = state.skirmishModeRulesLoaded;
    lateCandidate.skirmishModeAlliesAllowed = state.skirmishModeAlliesAllowed;
    lateCandidate.cooperativeSession = state.cooperativeSession;
    lateCandidate.cooperativeSessionStatus = state.cooperativeSessionStatus;
    lateCandidate.structureFootprints = state.structureFootprints;
    lateCandidate.terrainInfoByCell = state.terrainInfoByCell;
    lateCandidate.terrainObjectInitialStateByKey =
        state.terrainObjectInitialStateByKey;
    lateCandidate.triggers = state.triggers;
    lateCandidate.computerAiByOwner = state.computerAiByOwner;
    lateCandidate.computerMiningByOwner = state.computerMiningByOwner;
    lateCandidate.economy = state.economy;
    lateCandidate.economy.owner = trimAsciiLocal(
        ini.getValue("Economy", "Owner", state.economy.owner)
    );
    lateCandidate.economy.credits = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "Credits", "0"),
        0
    );
    lateCandidate.economy.localProductionLanes = savedProduction.lanes;
    lateCandidate.economy.localProductionLanesInitialized = true;
    lateCandidate.tick = savedFactoryDeliveryTick;
    lateCandidate.world = state.world;
    restoreRuntimeTerrainObjectsFromSave(lateCandidate, ini);
    lateCandidate.terrainByCell = buildRuntimeTerrainLookup(
        lateCandidate.world
    );
    lateCandidate.bridgeHealthByCell.clear();
    int stagedBridgeHealthCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("BridgeHealth", "Count", "0"),
            stagedBridgeHealthCount
        )
        || stagedBridgeHealthCount < 0
        || stagedBridgeHealthCount > 100000) {
        errorMessage = "invalid staged bridge health header";
        return false;
    }
    for (int index = 0; index < stagedBridgeHealthCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("BridgeHealth", std::to_string(index), "")
        );
        int cellKey = -1;
        int health = -1;
        if (tokens.size() != 2U
            || !parseRuntimeInt(tokens[0], cellKey)
            || !parseRuntimeInt(tokens[1], health)
            || cellKey < 0 || health < 0
            || !lateCandidate.bridgeHealthByCell.emplace(
                cellKey,
                health
            ).second) {
            errorMessage = "invalid staged bridge health record";
            return false;
        }
    }
    lateCandidate.gates = validatedGateState;
    lateCandidate.gatesInitialized = true;
    lateCandidate.specialMechanisms = validatedSpecialMechanisms;
    lateCandidate.specialMechanismsInitialized = true;
    lateCandidate.deathWeapons = validatedDeathWeapons;
    lateCandidate.combatDetails = validatedCombatDetails;
    if (!validatedCombatDetails.validateWorld(state.rules, lateCandidate.world.getObjects(),
            savedFactoryDeliveryTick, errorMessage)) return false;
    lateCandidate.psychicStatuses = validatedPsychicStatuses;
    lateCandidate.magBeams = validatedMagBeams;
    lateCandidate.airstrikes = validatedAirstrikeHost.airstrikes;
    lateCandidate.airstrikePlanesByToken =
        validatedAirstrikeHost.bindingsByToken;
    lateCandidate.airstrikeRootAuthoritiesByActor =
        validatedAirstrikeHost.authoritiesByActor;
    lateCandidate.repairDepots = validatedRepairDepotState;
    lateCandidate.repairDepotsInitialized = true;
    lateCandidate.factoryDeliveries = validatedFactoryDeliveryState;
    lateCandidate.playerCommands = validatedPlayerCommands;
    lateCandidate.aircraftServices = validatedAircraftServiceState;
    lateCandidate.aircraftServicesInitialized = true;
    rebuildRuntimeGateFootprintIndex(lateCandidate);
    if (!validateRuntimeAircraftPadSlotCapacity(
            lateCandidate,
            errorMessage
        )) {
        return false;
    }
    if (version >= 13
        && !validateRuntimePadAircraftProductionProjection(
            lateCandidate,
            errorMessage
        )) {
        return false;
    }

    std::unordered_set<int> stagedTransportPassengerIds;
    for (const WorldObjectInstance& transport :
            lateCandidate.world.getObjects()) {
        stagedTransportPassengerIds.insert(
            transport.transportPassengerIds.begin(),
            transport.transportPassengerIds.end()
        );
    }
    if (version >= 13
        && ini.getSection("GarrisonRuntime") == nullptr) {
        errorMessage = "v13 save is missing garrison runtime";
        return false;
    }
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        int hostCount = -1;
        if (!parseRuntimeInt(
                ini.getValue("GarrisonRuntime", "HostCount", ""),
                hostCount
            )
            || hostCount < 0 || hostCount > 100000) {
            errorMessage = "invalid staged garrison host count";
            return false;
        }
        for (int index = 0; index < hostCount; ++index) {
            const std::string section = "GarrisonRuntime.Host."
                + std::to_string(index);
            if (ini.getSection(section) == nullptr) {
                errorMessage = "missing staged garrison host record";
                return false;
            }
            std::stringstream occupants(
                ini.getValue(section, "Occupants", "")
            );
            std::string occupantToken;
            while (std::getline(occupants, occupantToken, ',')) {
                if (trimAsciiLocal(occupantToken).empty()) {
                    continue;
                }
                int occupantId = -1;
                if (!parseRuntimeInt(occupantToken, occupantId)
                    || occupantId < 0) {
                    errorMessage = "invalid staged garrison occupant id";
                    return false;
                }
                if (stagedTransportPassengerIds.contains(occupantId)) {
                    errorMessage =
                        "saved object has transport and garrison authority";
                    return false;
                }
                const WorldObjectInstance* occupant =
                    findRuntimeObjectByIdIncludingInactive(
                        lateCandidate,
                        occupantId
                    );
                if (version >= 13
                    && (occupant == nullptr
                        || occupant->alive
                        || occupant->health <= 0
                        || toLowerAsciiLocal(trimAsciiLocal(
                            occupant->mission
                        )) != "garrisoned"
                        || occupant->dockedToObjectId >= 0
                        || occupant->dockIndex >= 0
                        || occupant->airborne
                        || occupant->subCell != -1)) {
                    errorMessage =
                        "invalid v13 garrison occupant authority";
                    return false;
                }
            }
        }
    }

    lateCandidate.garrisons.reset(runtimeSavedGarrisonConfig(state));
    RuntimeGarrisonReconcileReport stagedGarrisonReport;
    if (ini.getSection("GarrisonRuntime") != nullptr) {
        if (!lateCandidate.garrisons.load(
                ini,
                lateCandidate.rules,
                lateCandidate.world.accessObjects(),
                &errorMessage,
                &stagedGarrisonReport
            )) {
            return false;
        }
    } else {
        stagedGarrisonReport = lateCandidate.garrisons.reconcile(
            lateCandidate.rules,
            lateCandidate.world.accessObjects()
        );
    }
    if (version >= 13
        && (stagedGarrisonReport.removedRelations != 0U
            || stagedGarrisonReport.recoveredRelations != 0U
            || stagedGarrisonReport.removedCooldowns != 0U
            || !stagedGarrisonReport.actions.empty())) {
        errorMessage = "v13 garrison state requires reconciliation";
        return false;
    }
    lateCandidate.garrisonsInitialized = true;
    for (const WorldObjectInstance& transport :
            lateCandidate.world.getObjects()) {
        for (int passengerId : transport.transportPassengerIds) {
            const WorldObjectInstance* passenger =
                findRuntimeObjectByIdIncludingInactive(
                    lateCandidate,
                    passengerId
                );
            if (passenger == nullptr
                || passenger->garrisonHostObjectId >= 0) {
                errorMessage =
                    "transport passenger has competing garrison authority";
                return false;
            }
        }
    }

    RuntimeSpySystemState validatedSpies;
    RuntimeSpyReconcileReport spyLoadReport;
    const bool hasExplicitSpyRuntime =
        ini.getSection("SpyRuntime") != nullptr;
    if (!validatedSpies.load(
            ini,
            makeRuntimeSpyContext(lateCandidate),
            savedFactoryDeliveryTick,
            &errorMessage,
            &spyLoadReport
        )
        || validatedSpies.lastAdvancedTick() > savedFactoryDeliveryTick
        || (hasExplicitSpyRuntime
            && (spyLoadReport.removedOwners != 0U
                || spyLoadReport.removedDisguises != 0U
                || spyLoadReport.removedDetections != 0U
                || !spyLoadReport.actions.empty()))) {
        if (errorMessage.empty()) {
            errorMessage = "invalid Spy runtime authority";
        }
        return false;
    }
    lateCandidate.spies = std::move(validatedSpies);
    lateCandidate.spiesInitialized = true;
    if (!hasExplicitSpyRuntime) {
        // Legacy saves predate [SpyRuntime].  Rebuild permanent disguises on
        // the detached candidate so load remains atomic and the first paint
        // cannot briefly expose a Spy's real identity.  Explicit v1/v2
        // authority is validated and preserved exactly as authored.
        ensureRuntimeSpiesInitialized(lateCandidate);
    }

    int stagedBehaviorCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("Behaviors", "Count", "0"),
            stagedBehaviorCount
        )
        || stagedBehaviorCount < 0 || stagedBehaviorCount > 100000) {
        errorMessage = "invalid staged behavior header";
        return false;
    }
    for (int index = 0; index < stagedBehaviorCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("Behaviors", std::to_string(index), "")
        );
        int savedObjectId = -1;
        if (tokens.empty() || !parseRuntimeInt(tokens[0], savedObjectId)) {
            errorMessage = "invalid staged behavior object id";
            return false;
        }
        WorldObjectInstance* object = findMutableObjectById(
            lateCandidate,
            savedObjectId
        );
        int objectId = -1;
        RuntimeObjectBehavior behavior;
        if (object == nullptr || !object->alive
            || !parseRuntimeSavedBehaviorRecord(
                tokens,
                *object,
                version,
                objectId,
                behavior
            )
            || objectId != savedObjectId
            || (behavior.factoryExitClearanceHostId >= 0
                && !runtimeFactoryClearanceBehaviorIsValid(lateCandidate, *object, behavior))) {
            if (version >= 9) {
                errorMessage = "invalid staged behavior record";
                return false;
            }
            continue;
        }
        const UnitType* deployedCooldownUnit = findRuntimeUnitType(lateCandidate.rules, *object);
        const bool hasDualDeployCooldownAuthority = explicitDualWeaponCooldownIds.contains(objectId)
            && behavior.kind == RuntimeBehaviorKind::Attack && behavior.returnToDeployed
            && deployedCooldownUnit != nullptr && deployedCooldownUnit->deployFire;
        const WeaponType* deployedCooldownWeapon = hasDualDeployCooldownAuthority
            ? runtimeWeaponProfileForObject(lateCandidate, *object,
                findObjectById(lateCandidate, behavior.targetObjectId), behavior.forceFire).weapon : nullptr;
        const bool preserveDualEruptionCooldowns = deployedCooldownWeapon != nullptr && deployedCooldownWeapon->areaFire
            && deployedCooldownWeapon->radLevel > 0;
        if (explicitGattlingCooldownIds.contains(objectId)) {
            // A pending Gatling burst leaves the behavior's old full-ROF
            // mirror untouched while the object's paid reload counts down.
            // Preserve the explicitly saved authority instead of extending
            // the firing gap after an ordinary load.
            behavior.weaponCooldownTicks = object->weaponCooldownTicksRemaining;
        } else if (!preserveDualEruptionCooldowns) {
            // Older object records lack the command-independent reload.
            // Retain their legacy mirror import and all other weapon paths.
            object->weaponCooldownTicksRemaining = std::max(
                object->weaponCooldownTicksRemaining,
                behavior.weaponCooldownTicks
            );
            behavior.weaponCooldownTicks = std::max(
                behavior.weaponCooldownTicks,
                object->weaponCooldownTicksRemaining
            );
        }
        // A genuine deployed radiation pulse mirrors the independent eruption
        // clock. Modern object rows explicitly retain both clocks, including
        // zero, so that mirror must not overwrite their ordinary weapon reload.
        // Keep the parsed behavior mirror; older rows retain the import above.
        if (behavior.kind == RuntimeBehaviorKind::Attack
            && behavior.targetObjectId < 0
            && !behavior.attackBridge && !behavior.attackGround
            && behavior.weaponCooldownTicks > 0) {
            behavior.state = RuntimeBehaviorState::Cooldown;
        }
        if (!lateCandidate.behaviors.emplace(
                objectId,
                std::move(behavior)
            ).second) {
            if (version >= 9) {
                errorMessage = "duplicate staged behavior record";
                return false;
            }
        }
    }
    for (const auto& [sourceId, intent] : savedTimedBombIntents) {
        const auto* source = findObjectById(lateCandidate, sourceId);
        const auto* target = findObjectById(lateCandidate, intent.targetObjectId);
        const auto behavior = lateCandidate.behaviors.find(sourceId);
        // Use the restored house alliances, not those of the pre-load seed.
        if (!source || !target || behavior == lateCandidate.behaviors.end()
            || !runtimeBuildingBombOrderRelationLegal(
                lateCandidate, *source, *target, behavior->second.forceFire)) {
            errorMessage = "allied building C4 intent requires a force order";
            return false;
        }
        lateCandidate.pendingTimedBombIntents.emplace(sourceId, intent);
    }

    int stagedEngineerIntentCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("EngineerIntents", "Count", "0"),
            stagedEngineerIntentCount
        )
        || stagedEngineerIntentCount < 0
        || stagedEngineerIntentCount > 100000) {
        errorMessage = "invalid staged engineer intent header";
        return false;
    }
    for (int index = 0; index < stagedEngineerIntentCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("EngineerIntents", std::to_string(index), "")
        );
        const int engineerId = tokens.size() == 3U
            ? parseRuntimeIntOrDefault(tokens[0], -1)
            : -1;
        const int targetId = tokens.size() == 3U
            ? parseRuntimeIntOrDefault(tokens[1], -1)
            : -1;
        const RuntimeEngineerAction action = tokens.size() == 3U
            ? runtimeEngineerActionFromName(tokens[2])
            : RuntimeEngineerAction::None;
        WorldObjectInstance* engineer = findMutableObjectById(
            lateCandidate,
            engineerId
        );
        WorldObjectInstance* target = findMutableObjectById(
            lateCandidate,
            targetId
        );
        auto behavior = lateCandidate.behaviors.find(engineerId);
        const bool valid = engineer != nullptr && target != nullptr
            && action != RuntimeEngineerAction::None
            && runtimeEngineerActionForStructure(
                lateCandidate,
                *engineer,
                *target
            ) == action
            && behavior != lateCandidate.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && behavior->second.pendingAllowedOccupiedObjectId == targetId
            && lateCandidate.pendingTimedBombIntents.find(engineerId)
                == lateCandidate.pendingTimedBombIntents.end()
            && lateCandidate.pendingEngineerCaptures.emplace(
                engineerId,
                targetId
            ).second
            && lateCandidate.pendingEngineerActions.emplace(
                engineerId,
                action
            ).second;
        if (!valid && version >= 13) {
            errorMessage = "invalid v13 engineer intent";
            return false;
        }
    }

    int stagedTransportIntentCount = -1;
    if (!parseRuntimeInt(
            ini.getValue("TransportIntents", "Count", "0"),
            stagedTransportIntentCount
        )
        || stagedTransportIntentCount < 0
        || stagedTransportIntentCount > 100000) {
        errorMessage = "invalid staged transport intent header";
        return false;
    }
    std::vector<std::pair<int, int>> stagedTransportIntents;
    stagedTransportIntents.reserve(
        static_cast<std::size_t>(stagedTransportIntentCount)
    );
    for (int index = 0; index < stagedTransportIntentCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("TransportIntents", std::to_string(index), "")
        );
        int passengerId = -1;
        int targetId = -1;
        if (tokens.size() != 2U
            || !parseRuntimeInt(tokens[0], passengerId)
            || !parseRuntimeInt(tokens[1], targetId)
            || !lateCandidate.pendingTransportTargets.emplace(
                passengerId,
                targetId
            ).second) {
            if (version >= 13) {
                errorMessage = "invalid v13 transport intent record";
                return false;
            }
            continue;
        }
        stagedTransportIntents.emplace_back(passengerId, targetId);
    }
    for (const auto& [passengerId, targetId] : stagedTransportIntents) {
        WorldObjectInstance* passenger = findMutableObjectById(
            lateCandidate,
            passengerId
        );
        WorldObjectInstance* transport = findMutableObjectById(
            lateCandidate,
            targetId
        );
        auto behavior = lateCandidate.behaviors.find(passengerId);
        const bool valid = passenger != nullptr && transport != nullptr
            && behavior != lateCandidate.behaviors.end()
            && behavior->second.kind == RuntimeBehaviorKind::Move
            && behavior->second.pendingAllowedOccupiedObjectId == targetId
            && lateCandidate.pendingEngineerCaptures.find(passengerId)
                == lateCandidate.pendingEngineerCaptures.end()
            && lateCandidate.pendingTimedBombIntents.find(passengerId)
                == lateCandidate.pendingTimedBombIntents.end()
            && runtimeObjectCanEnterTransport(
                lateCandidate,
                *passenger,
                *transport,
                true,
                false
            );
        if (!valid && version >= 13) {
            errorMessage = "invalid v13 transport intent authority";
            return false;
        }
    }

    if (hasExplicitOccupyIntents) {
        for (const auto& [occupierId, targetId] : savedOccupyIntents) {
            WorldObjectInstance* occupier = findMutableObjectById(
                lateCandidate,
                occupierId
            );
            WorldObjectInstance* target = findMutableObjectById(
                lateCandidate,
                targetId
            );
            auto behavior = lateCandidate.behaviors.find(occupierId);
            const bool valid = occupier != nullptr && target != nullptr
                && behavior != lateCandidate.behaviors.end()
                && behavior->second.kind == RuntimeBehaviorKind::Move
                && behavior->second.pendingAllowedOccupiedObjectId == targetId
                && lateCandidate.pendingEngineerCaptures.find(occupierId)
                    == lateCandidate.pendingEngineerCaptures.end()
                && lateCandidate.pendingTransportTargets.find(occupierId)
                    == lateCandidate.pendingTransportTargets.end()
                && lateCandidate.pendingTimedBombIntents.find(occupierId)
                    == lateCandidate.pendingTimedBombIntents.end()
                && runtimeObjectCanOccupyTarget(
                    lateCandidate,
                    *occupier,
                    *target,
                    true,
                    occupierId
                )
                && lateCandidate.pendingOccupyTargets.emplace(
                    occupierId,
                    targetId
                ).second;
            if (!valid && version >= 13) {
                errorMessage = "invalid v13 occupy intent authority";
                return false;
            }
        }
    } else {
        std::vector<int> behaviorIds;
        behaviorIds.reserve(lateCandidate.behaviors.size());
        for (const auto& [objectId, behavior] : lateCandidate.behaviors) {
            (void)behavior;
            behaviorIds.push_back(objectId);
        }
        std::sort(behaviorIds.begin(), behaviorIds.end());
        for (int occupierId : behaviorIds) {
            if (lateCandidate.pendingEngineerCaptures.contains(occupierId)
                || lateCandidate.pendingTransportTargets.contains(occupierId)
                || lateCandidate.pendingTimedBombIntents.contains(occupierId)) {
                continue;
            }
            auto behavior = lateCandidate.behaviors.find(occupierId);
            WorldObjectInstance* occupier = findMutableObjectById(
                lateCandidate,
                occupierId
            );
            if (behavior == lateCandidate.behaviors.end()
                || occupier == nullptr
                || behavior->second.kind != RuntimeBehaviorKind::Move
                || !runtimeMissionAllowsPendingOccupyIntent(*occupier)) {
                continue;
            }
            const int targetId =
                behavior->second.pendingAllowedOccupiedObjectId;
            WorldObjectInstance* target = findMutableObjectById(
                lateCandidate,
                targetId
            );
            if (target != nullptr
                && runtimeObjectCanOccupyTarget(
                    lateCandidate,
                    *occupier,
                    *target,
                    true,
                    occupierId
                )) {
                lateCandidate.pendingOccupyTargets.emplace(
                    occupierId,
                    targetId
                );
            }
        }
    }
    if (version >= 13) {
        for (const auto& [objectId, behavior] : lateCandidate.behaviors) {
            const WorldObjectInstance* object = findObjectById(
                lateCandidate,
                objectId
            );
            const WorldObjectInstance* spyTarget = behavior.targetObjectId >= 0
                ? findObjectById(lateCandidate, behavior.targetObjectId)
                : nullptr;
            const bool pendingSpyInfiltration = object != nullptr
                && spyTarget != nullptr
                && behavior.kind == RuntimeBehaviorKind::Move
                && behavior.pendingAllowedOccupiedObjectId
                    == behavior.targetObjectId
                && runtimeSpyCanInfiltrateTarget(
                    lateCandidate,
                    *object,
                    *spyTarget
                );
            if (object != nullptr
                && behavior.kind == RuntimeBehaviorKind::Move
                && runtimeMissionAllowsPendingOccupyIntent(*object)
                && !lateCandidate.pendingEngineerCaptures.contains(objectId)
                && !lateCandidate.pendingTransportTargets.contains(objectId)
                && !lateCandidate.pendingOccupyTargets.contains(objectId)
                && !lateCandidate.pendingTimedBombIntents.contains(objectId)
                && !pendingSpyInfiltration
                && !runtimeBehaviorIsPendingInfantryService(
                    lateCandidate,
                    objectId,
                    behavior
                )) {
                errorMessage = "v13 behavior has orphaned entry intent";
                return false;
            }
        }
    }

    RuntimeGrindingSystemState validatedGrinding;
    RuntimeGrindingReconcileReport grindingLoadReport;
    const bool hasGrindingRuntime =
        ini.getSection("GrindingRuntime") != nullptr;
    RuntimeGrindingContext grindingLoadContext = makeRuntimeGrindingContext(
        lateCandidate,
        &validatedGrinding,
        false
    );
    if (!validatedGrinding.load(
            ini,
            grindingLoadContext,
            &errorMessage,
            &grindingLoadReport
        )
        || (hasGrindingRuntime
            && (!grindingLoadReport.contextValid
                || grindingLoadReport.releasedRelations != 0U
                || grindingLoadReport.duplicateManagedObjects != 0U
                || !grindingLoadReport.actions.empty()))) {
        if (errorMessage.empty()) {
            errorMessage = "invalid grinding runtime candidate";
        }
        return false;
    }
    lateCandidate.grinding = std::move(validatedGrinding);
    if (!validateRuntimeGrindingHostProjection(
            lateCandidate,
            lateCandidate.grinding,
            savedFactoryDeliveryTick,
            errorMessage
        )) {
        return false;
    }

    if (!restoreRuntimeGattlingTimersFromSave(
            lateCandidate,
            ini,
            errorMessage,
            version
        )
        || !restoreRuntimeCratesFromSave(
            lateCandidate,
            ini,
            errorMessage
        )
        || !restoreRuntimePublicBuildingLocations(lateCandidate, ini, errorMessage)
        || !restoreRuntimeWrecks(lateCandidate, ini, errorMessage)
        || !loadRuntimeUnitDeaths(lateCandidate, ini, errorMessage, false)
        || !restoreRuntimeBuildingCollapses(lateCandidate, ini, errorMessage)
        || !restoreRuntimeGrindingAnimations(lateCandidate, ini, errorMessage)) {
        return false;
    }
    lateCandidate.specialMechanisms.reconcile(
        lateCandidate.world.accessObjects(),
        lateCandidate.tick,
        SpecialEventSink{}
    );
    if (lateCandidate.rulesLoaded) {
        lateCandidate.superWeapons.reset(lateCandidate.rules);
    } else {
        lateCandidate.superWeapons.reset(std::vector<SuperWeaponRule>{});
    }
    lateCandidate.superWeaponsInitialized = true;
    if (version >= 3
        && !lateCandidate.superWeapons.load(ini, &errorMessage)) {
        return false;
    }
    lateCandidate.superWeapons.reconcileSources(
        runtimeSuperWeaponContext(lateCandidate)
    );
    if (version >= 14
        && lateCandidate.superWeapons.lastTick()
            != savedFactoryDeliveryTick) {
        errorMessage = "v14 SuperWeapon clock does not match RuntimeSave Tick";
        return false;
    }
    const std::map<std::uint64_t, RuntimeActiveLightingToken>
        expectedActiveLightingTokens =
            runtimeActiveLightingTokensForSuperWeapons(lateCandidate);
    if (version == 14
        && expectedActiveLightingTokens
            != validatedActiveLightingTokens) {
        errorMessage =
            "v14 lighting tokens do not match active SuperWeapons";
        return false;
    }
    lateCandidate.worldAudio = std::move(validatedWorldAudio);
    lateCandidate.worldAudioInitialized = true;
    lateCandidate.lighting = std::move(validatedLighting);
    lateCandidate.lightingInitialized = true;
    lateCandidate.activeLightingTokens = version >= 14
        ? std::move(validatedActiveLightingTokens)
        : expectedActiveLightingTokens;
    if (version < 14 && !lateCandidate.activeLightingTokens.empty()) {
        const auto selected = std::max_element(
            lateCandidate.activeLightingTokens.begin(),
            lateCandidate.activeLightingTokens.end(),
            [](const auto& left, const auto& right) {
                return std::tie(left.second.startedTick, left.second.token)
                    < std::tie(
                        right.second.startedTick,
                        right.second.token
                    );
            }
        );
        if (selected->second.mode == RuntimeLightingMode::Ion) {
            (void)lateCandidate.lighting.activateIon(
                savedFactoryDeliveryTick
            );
        } else {
            (void)lateCandidate.lighting.activateDominator(
                savedFactoryDeliveryTick
            );
        }
    }
    lateCandidate.particles = std::move(validatedParticles);
    lateCandidate.particlesInitialized = true;
    lateCandidate.particleRenderSnapshots.clear();
    lateCandidate.particleBeamRenderSnapshots.clear();
    lateCandidate.particleLocalLightRenderSnapshots.clear();

    if (!loadRuntimeTransportTransferPresentation(lateCandidate, ini, errorMessage)) return false;
    const bool hasUnitTransportBoarding = std::any_of(lateCandidate.pendingTransportTargets.begin(),
        lateCandidate.pendingTransportTargets.end(), [&](const auto& intent) {
            const auto* host = findObjectById(lateCandidate, intent.second);
            return host && host->kind == WorldObjectKind::Unit;
        });
    if (hasUnitTransportBoarding) {
        // Restore strictly validated stopped host heights before infantry doorway levels.
        if (!loadRuntimeVehicleArrivals(lateCandidate, ini, errorMessage)) return false;
        if (!loadRuntimeInfantryWalks(lateCandidate, ini, errorMessage)) return false;
    } else {
        if (!loadRuntimeInfantryWalks(lateCandidate, ini, errorMessage)) return false;
        if (!loadRuntimeVehicleArrivals(lateCandidate, ini, errorMessage)) return false;
    }
    if (!validateRuntimeTransportTransferPresentation(lateCandidate, errorMessage)) return false;
    if (!loadRuntimeRepackMoveGroups(lateCandidate, ini, errorMessage)) return false;
    if (!loadRuntimeSpriteFirings(lateCandidate, ini, errorMessage)) return false;
    std::vector<RuntimeCarrierFlightPoseRecord> savedCarrierFlightPoses;
    if (!parseRuntimeCarrierFlightPoses(lateCandidate, ini, savedCarrierFlightPoses, errorMessage)) return false;

    // A worker at its own delivery point legitimately has no free
    // infantry subcell. Restore that relationship before reconciling cells.
    lateCandidate.slaveMinerTasks.clear();
    const int slaveMinerTaskCount = parseRuntimeIntOrDefault(
        ini.getValue("SlaveMinerTasks", "Count", "0"),
        0
    );
    for (int index = 0; index < slaveMinerTaskCount; ++index) {
        const std::vector<std::string> tokens =
            splitRuntimeSaveFields(
                ini.getValue("SlaveMinerTasks", std::to_string(index), "")
            );
        if (tokens.size() < 8) {
            continue;
        }
        const int objectId = parseRuntimeIntOrDefault(tokens[0], -1);
        if (findMutableRuntimeObjectByIdIncludingInactive(lateCandidate, objectId) == nullptr) {
            continue;
        }
        const std::string serializedTask =
            tokens[1] + "|"
            + tokens[2] + "|"
            + tokens[3] + "|"
            + tokens[4] + "|"
            + tokens[5] + "|"
            + tokens[6] + "|"
            + tokens[7]
            + (tokens.size() >= 9 ? "|" + tokens[8] : "")
            + (tokens.size() >= 10 ? "|" + tokens[9] : "");
        const std::optional<RuntimeSlaveMinerTask> task =
            parseRuntimeSlaveMinerTask(serializedTask);
        if (task.has_value()) {
            lateCandidate.slaveMinerTasks[objectId] = *task;
        }
    }

    if (!restoreRuntimeClonePendingDeliveries(lateCandidate, ini, errorMessage)) return false;
    if (!restoreRuntimeSlaveMinerMovePaths(lateCandidate, ini, errorMessage)) return false;
    if (!restoreRuntimeJumpjetMovePaths(lateCandidate, ini, errorMessage)) return false;
    if (!validateRuntimeStoredSlaveMinerState(lateCandidate, &errorMessage)) return false;
    const RuntimeSubcellReconcileReport stagedSubcellReconcile =
        lateCandidate.subcells.reconcile(
            lateCandidate.world.accessObjects(),
            lateCandidate.world.getTerrainObjects(),
            validatedGateSubcellContext
        );
    for (const auto& action : stagedSubcellReconcile.actions) {
        if (action.kind == RuntimeSubcellReconcileActionKind::DroppedReservation) continue;
        const auto entry = lateCandidate.pendingOccupyTargets.find(action.objectId);
        // The validated entry order deliberately has no ground subcell while
        // its last walking segment lies inside the host. Keep all unrelated
        // overlaps invalid; never make the host transparent to other units.
        if (action.kind == RuntimeSubcellReconcileActionKind::NeedsMoveOut
            && action.previousSubCell == -1
            && entry != lateCandidate.pendingOccupyTargets.end()) {
            const auto* occupier = findObjectById(lateCandidate, action.objectId);
            if (occupier != nullptr && runtimeCellIsAllowedOccupiedStructureFootprint(
                    lateCandidate, runtimeCellPointForObject(*occupier), entry->second)) continue;
        }
        if (action.kind == RuntimeSubcellReconcileActionKind::NeedsMoveOut
            && action.previousSubCell == -1) {
            const auto worker = lateCandidate.slaveMinerTasks.find(action.objectId);
            const auto* object = findObjectById(lateCandidate, action.objectId);
            const auto* parent = worker == lateCandidate.slaveMinerTasks.end() ? nullptr
                : findObjectById(lateCandidate, worker->second.refineryId);
            const auto* mobileType = parent && parent->kind == WorldObjectKind::Unit
                ? findRuntimeUnitType(lateCandidate.rules, *parent) : nullptr;
            const auto* building = parent ? findRuntimeBuildingTypeById(lateCandidate.rules,
                mobileType ? mobileType->deploysInto : parent->type) : nullptr;
            if (object && parent && object->owner == parent->owner && building
                && toUpperAsciiLocal(building->enslaves) == toUpperAsciiLocal(object->type)
                && runtimeCellIsAllowedOccupiedStructureFootprint(lateCandidate,
                    runtimeCellPointForObject(*object), parent->instanceId, object)) continue;
        }
        if (action.kind == RuntimeSubcellReconcileActionKind::NeedsMoveOut && action.previousSubCell == -1) {
            const auto* object = findObjectById(lateCandidate, action.objectId);
            if (object && std::any_of(lateCandidate.grinding.relations().begin(), lateCandidate.grinding.relations().end(),
                [&](const RuntimeGrindingRelation& relation) {
                    return relation.sourceId == action.objectId && relation.phase == RuntimeGrindingRelationPhase::Entering
                        && runtimeCellIsAllowedOccupiedStructureFootprint(lateCandidate,
                            runtimeCellPointForObject(*object), relation.hostId);
                })) continue;
        }
        errorMessage = "invalid committed subcell occupancy for #" + std::to_string(action.objectId);
        return false;
    }
    std::vector<RuntimeSubcellMoveRequest> stagedSubcellRequests;
    stagedSubcellRequests.reserve(savedSubcellReservations.size());
    for (const RuntimeSubcellReservation& reservation :
            savedSubcellReservations) {
        stagedSubcellRequests.push_back({
            reservation.moverObjectId,
            reservation.destination
        });
    }
    const std::vector<RuntimeSubcellReservationResult>
        stagedSubcellResults = lateCandidate.subcells.reserveBatch(
            lateCandidate.world.getObjects(),
            lateCandidate.world.getTerrainObjects(),
            stagedSubcellRequests,
            validatedGateSubcellContext
        );
    if (stagedSubcellResults.size() != savedSubcellReservations.size()) {
        errorMessage = "invalid subcell reservation allocation";
        return false;
    }
    for (std::size_t index = 0;
        index < stagedSubcellResults.size();
        ++index) {
        if (!stagedSubcellResults[index].reserved()
            || stagedSubcellResults[index].moverObjectId
                != savedSubcellReservations[index].moverObjectId
            || stagedSubcellResults[index].subCell
                != savedSubcellReservations[index].subCell) {
            errorMessage = "conflicting subcell reservation allocation";
            return false;
        }
    }
    lateCandidate.subcellsInitialized = true;

    if (state.campaignMode && version < 13) {
        const std::string savedMode = trimAsciiLocal(
            ini.getValue("Campaign", "Mode", "")
        );
        if (!savedMode.empty()
            && toUpperAsciiLocal(savedMode) != "CAMPAIGNMISSION") {
            errorMessage = "save is not a campaign mission";
            return false;
        }
        const std::string savedCampaign = trimAsciiLocal(
            ini.getValue("Campaign", "Campaign", "")
        );
        const std::string savedMission = trimAsciiLocal(
            ini.getValue("Campaign", "Mission", "")
        );
        if ((!savedCampaign.empty()
                && !state.config.campaignId.empty()
                && toLowerAsciiLocal(savedCampaign)
                    != toLowerAsciiLocal(state.config.campaignId))
            || (!savedMission.empty()
                && !state.config.campaignMissionId.empty()
                && toLowerAsciiLocal(savedMission)
                    != toLowerAsciiLocal(state.config.campaignMissionId))) {
            errorMessage = "save belongs to a different campaign mission";
            return false;
        }
    }

    state.tick = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "Tick", "0"),
        state.tick
    );
    const std::vector<std::string> cameraTokens =
        splitRuntimeSaveFields(ini.getValue("RuntimeSave", "Camera", ""));
    if (cameraTokens.size() >= 3) {
        state.camera.worldOffsetX =
            parseRuntimeIntOrDefault(
                cameraTokens[0],
                state.camera.worldOffsetX
            );
        state.camera.worldOffsetY =
            parseRuntimeIntOrDefault(
                cameraTokens[1],
                state.camera.worldOffsetY
            );
        state.camera.initialized = parseRuntimeBool(cameraTokens[2]);
    }
    state.activeBuildTab = runtimeBuildKindFromName(
        ini.getValue(
            "RuntimeSave",
            "ActiveBuildTab",
            runtimeBuildKindName(state.activeBuildTab)
        )
    );

    state.economy.owner =
        ini.getValue("Economy", "Owner", state.economy.owner);
    const int savedCredits = std::clamp(
        parseRuntimeIntOrDefault(
            ini.getValue(
                "Economy",
                "Credits",
                std::to_string(state.economy.credits)
            ),
            state.economy.credits
        ),
        0,
        runtimeMaximumCreditBalance
    );
    state.economy.credits = static_cast<int>(std::min<long long>(
        runtimeMaximumCreditBalance,
        static_cast<long long>(savedCredits)
            + std::max(0, savedProduction.legacyCreditRefund)
    ));
    state.economy.storedResourceUnits = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "StoredResourceUnits", "0"),
        0
    );
    state.economy.storedResourceValue = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "StoredResourceValue", "0"),
        0
    );
    state.economy.storedResourceGemUnits = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "StoredResourceGemUnits", "0"),
        0
    );
    state.economy.completedBuildings = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "CompletedBuildings", "0"),
        0
    );
    state.economy.completedUnits = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "CompletedUnits", "0"),
        0
    );
    state.economy.completedInfantry = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "CompletedInfantry", "0"),
        0
    );
    state.economy.resourceTrips = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "ResourceTrips", "0"),
        0
    );
    state.economy.slaveMinerLoads = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "SlaveMinerLoads", "0"),
        0
    );
    state.economy.incomeCooldownTicks = parseRuntimeIntOrDefault(
        ini.getValue("Economy", "IncomeCooldownTicks", "0"),
        0
    );
    state.economy.simplifiedHarvesterIncomeEnabled =
        parseRuntimeBool(
            ini.getValue("Economy", "SimplifiedHarvesterIncome", "no")
        );
    const std::vector<std::string> skirmishStartTokens =
        splitRuntimeSaveFields(ini.getValue("Economy", "SkirmishStart", ""));
    if (skirmishStartTokens.size() >= 5) {
        state.economy.skirmishStartApplied =
            parseRuntimeBool(skirmishStartTokens[0]);
        state.economy.skirmishStartObjectId =
            parseRuntimeIntOrDefault(skirmishStartTokens[1], -1);
        state.economy.skirmishStartCell.x =
            parseRuntimeIntOrDefault(skirmishStartTokens[2], -1);
        state.economy.skirmishStartCell.y =
            parseRuntimeIntOrDefault(skirmishStartTokens[3], -1);
        state.economy.skirmishStatus = skirmishStartTokens[4];
    }

    state.economy.localProductionLanes =
        std::move(savedProduction.lanes);
    state.economy.localProductionLanesInitialized = true;
    state.economy.selectedPlacementLane =
        savedProduction.selectedPlacementLane;
    state.economy.selectedPlacementSerial =
        savedProduction.selectedPlacementSerial;
    state.economy.placementValid = false;
    state.economy.placementCell = RuntimeCellPoint{};
    const std::vector<std::string> placementCellTokens =
        splitRuntimeSaveFields(ini.getValue("Economy", "PlacementCell", ""));
    if (placementCellTokens.size() >= 3) {
        state.economy.placementCell.x =
            parseRuntimeIntOrDefault(placementCellTokens[0], -1);
        state.economy.placementCell.y =
            parseRuntimeIntOrDefault(placementCellTokens[1], -1);
        state.economy.placementValid =
            parseRuntimeBool(placementCellTokens[2]);
    }
    clearRuntimeInvalidPlacementSelection(state);

    const std::string buildLimitHistoryCountText =
        ini.getValue("BuildLimitHistory", "Count", "");
    state.economy.limitedBuildHistoryByOwner.clear();
    if (!buildLimitHistoryCountText.empty()) {
        const int buildLimitHistoryCount = std::max(
            0,
            parseRuntimeIntOrDefault(buildLimitHistoryCountText, 0)
        );
        for (int index = 0; index < buildLimitHistoryCount; ++index) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue(
                    "BuildLimitHistory",
                    std::to_string(index),
                    ""
                )
            );
            if (tokens.size() < 3U
                || trimAsciiLocal(tokens[0]).empty()
                || trimAsciiLocal(tokens[1]).empty()) {
                continue;
            }
            const int count = std::max(
                0,
                parseRuntimeIntOrDefault(tokens[2], 0)
            );
            if (count > 0) {
                state.economy.limitedBuildHistoryByOwner[
                    runtimeBuildLimitKey(tokens[0])
                ][runtimeBuildLimitKey(tokens[1])] = count;
            }
        }
    }

    state.cooperativeSession = std::move(validatedCooperativeSession);
    state.computerMiningByOwner = std::move(validatedComputerMining);
    state.triggers.computerTacticsByOwner = std::move(validatedComputerTactics);
    state.computerAiByOwner.clear();
    const int computerAiCount = std::max(
        0,
        parseRuntimeIntOrDefault(
            ini.getValue("ComputerAI", "Count", "0"),
            0
        )
    );
    for (int index = 0; index < computerAiCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("ComputerAI", std::to_string(index), "")
        );
        if (tokens.size() < 28U || trimAsciiLocal(tokens[1]).empty()) {
            continue;
        }
        RuntimeComputerAiState ai;
        ai.initialized = parseRuntimeBool(tokens[0]);
        ai.owner = tokens[1];
        ai.country = tokens[2];
        ai.difficulty = tokens[3];
        ai.baseCenter.x = parseRuntimeIntOrDefault(tokens[4], 0);
        ai.baseCenter.y = parseRuntimeIntOrDefault(tokens[5], 0);
        ai.credits = std::clamp(
            parseRuntimeIntOrDefault(tokens[6], 0),
            0,
            runtimeMaximumCreditBalance
        );
        ai.nextThinkTick = std::max(
            0, parseRuntimeIntOrDefault(tokens[7], state.tick)
        );
        ai.nextIncomeTick = std::max(
            0, parseRuntimeIntOrDefault(tokens[8], state.tick)
        );
        ai.nextBuildTick = std::max(
            0, parseRuntimeIntOrDefault(tokens[9], state.tick)
        );
        ai.nextAttackTick = std::max(
            0, parseRuntimeIntOrDefault(tokens[10], state.tick)
        );
        ai.nextRepairTick = std::max(
            0, parseRuntimeIntOrDefault(tokens[11], state.tick)
        );
        ai.productionSerial = std::max(
            0, parseRuntimeIntOrDefault(tokens[12], 0)
        );
        ai.incomeEarned = std::max(
            0, parseRuntimeIntOrDefault(tokens[13], 0)
        );
        ai.creditsSpent = std::max(
            0, parseRuntimeIntOrDefault(tokens[14], 0)
        );
        ai.structuresBuilt = std::max(
            0, parseRuntimeIntOrDefault(tokens[15], 0)
        );
        ai.infantryBuilt = std::max(
            0, parseRuntimeIntOrDefault(tokens[16], 0)
        );
        ai.unitsBuilt = std::max(
            0, parseRuntimeIntOrDefault(tokens[17], 0)
        );
        ai.freeUnitsReceived = std::max(
            0, parseRuntimeIntOrDefault(tokens[18], 0)
        );
        ai.attackWaves = std::max(
            0, parseRuntimeIntOrDefault(tokens[19], 0)
        );
        ai.attacksOrdered = std::max(
            0, parseRuntimeIntOrDefault(tokens[20], 0)
        );
        ai.defenseOrders = std::max(
            0, parseRuntimeIntOrDefault(tokens[21], 0)
        );
        ai.tacticalOrders = std::max(
            0, parseRuntimeIntOrDefault(tokens[22], 0)
        );
        ai.repairsPerformed = std::max(
            0, parseRuntimeIntOrDefault(tokens[23], 0)
        );
        ai.stalledBuilds = std::max(
            0, parseRuntimeIntOrDefault(tokens[24], 0)
        );
        ai.lastTargetObjectId =
            parseRuntimeIntOrDefault(tokens[25], -1);
        ai.lastBuildType = tokens[26];
        ai.status = tokens[27];
        state.computerAiByOwner[
            toLowerAsciiLocal(trimAsciiLocal(ai.owner))
        ] = std::move(ai);
    }

    const int resourceOverlayCount = parseRuntimeIntOrDefault(
        ini.getValue("ResourceOverlays", "Count", "-1"),
        -1
    );
    const std::string initialOreSeededRaw =
        trimAsciiLocal(
            ini.getValue("RuntimeSave", "InitialOreFieldsSeeded", "")
        );
    state.initialOreFieldsSeeded =
        !initialOreSeededRaw.empty()
            ? parseRuntimeBool(initialOreSeededRaw)
            : resourceOverlayCount >= 0;
    state.initialOreCellsSeeded = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "InitialOreCellsSeeded", "0"),
        0
    );
    state.initialOreCellsDensified = parseRuntimeIntOrDefault(
        ini.getValue("RuntimeSave", "InitialOreCellsDensified", "0"),
        0
    );
    if (resourceOverlayCount >= 0) {
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            if (!terrain.overlayIsOre && !terrain.overlayIsGem) {
                continue;
            }
            terrain.hasOverlay = false;
            terrain.overlayIndex = -1;
            terrain.overlayFrame = 0;
            terrain.overlayType.clear();
            terrain.overlayImage.clear();
            terrain.overlayIsOre = false;
            terrain.overlayIsGem = false;
        }

        std::unordered_map<int, TerrainCell*> terrainByCell;
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            terrainByCell[runtimeCellKey(terrain.x, terrain.y)] = &terrain;
        }
        for (int index = 0; index < resourceOverlayCount; ++index) {
            const std::vector<std::string> tokens =
                splitRuntimeSaveFields(
                    ini.getValue("ResourceOverlays", std::to_string(index), "")
                );
            if (tokens.size() < 8) {
                continue;
            }
            const int x = parseRuntimeIntOrDefault(tokens[0], -1);
            const int y = parseRuntimeIntOrDefault(tokens[1], -1);
            const auto terrainIt = terrainByCell.find(runtimeCellKey(x, y));
            if (terrainIt == terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            TerrainCell& terrain = *terrainIt->second;
            terrain.hasOverlay = true;
            terrain.overlayIndex = parseRuntimeIntOrDefault(tokens[2], -1);
            terrain.overlayFrame = parseRuntimeIntOrDefault(tokens[3], 0);
            terrain.overlayType = tokens[4];
            terrain.overlayImage = tokens[5];
            terrain.overlayIsOre = parseRuntimeBool(tokens[6]);
            terrain.overlayIsGem = parseRuntimeBool(tokens[7]);
        }
    }

    const int bridgeOverlayCount = parseRuntimeIntOrDefault(
        ini.getValue("BridgeOverlays", "Count", "-1"),
        -1
    );
    if (bridgeOverlayCount >= 0) {
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            if (!terrain.hasOverlay || !runtimeOverlayLooksBridge(terrain)) {
                continue;
            }
            terrain.hasOverlay = false;
            terrain.overlayIndex = -1;
            terrain.overlayFrame = 0;
            terrain.overlayType.clear();
            terrain.overlayImage.clear();
            terrain.overlayIsOre = false;
            terrain.overlayIsGem = false;
        }

        std::unordered_map<int, TerrainCell*> terrainByCell;
        terrainByCell.reserve(state.world.accessTerrainCells().size());
        for (TerrainCell& terrain : state.world.accessTerrainCells()) {
            terrainByCell[runtimeCellKey(terrain.x, terrain.y)] = &terrain;
        }
        for (int index = 0; index < bridgeOverlayCount; ++index) {
            const std::vector<std::string> tokens =
                splitRuntimeSaveFields(
                    ini.getValue(
                        "BridgeOverlays",
                        std::to_string(index),
                        ""
                    )
                );
            if (tokens.size() < 8) {
                continue;
            }
            const int x = parseRuntimeIntOrDefault(tokens[0], -1);
            const int y = parseRuntimeIntOrDefault(tokens[1], -1);
            const auto terrainIt =
                terrainByCell.find(runtimeCellKey(x, y));
            if (terrainIt == terrainByCell.end()
                || terrainIt->second == nullptr) {
                continue;
            }
            TerrainCell& terrain = *terrainIt->second;
            terrain.hasOverlay = true;
            terrain.overlayIndex =
                parseRuntimeIntOrDefault(tokens[2], -1);
            terrain.overlayFrame =
                parseRuntimeIntOrDefault(tokens[3], 0);
            terrain.overlayType = tokens[4];
            terrain.overlayImage = tokens[5];
            terrain.overlayIsOre = parseRuntimeBool(tokens[6]);
            terrain.overlayIsGem = parseRuntimeBool(tokens[7]);
        }
    }

    state.bridgeHealthByCell.clear();
    const int bridgeHealthCount = parseRuntimeIntOrDefault(
        ini.getValue("BridgeHealth", "Count", "0"),
        0
    );
    for (int index = 0; index < bridgeHealthCount; ++index) {
        const std::vector<std::string> tokens = splitRuntimeSaveFields(
            ini.getValue("BridgeHealth", std::to_string(index), "")
        );
        if (tokens.size() < 2) {
            continue;
        }
        const int cellKey = parseRuntimeIntOrDefault(tokens[0], -1);
        if (cellKey < 0) {
            continue;
        }
        state.bridgeHealthByCell[cellKey] = std::max(
            0,
            parseRuntimeIntOrDefault(tokens[1], 0)
        );
    }

    // Version 1-6 saves without this optional section intentionally restore
    // terrain objects to the map-authored snapshot captured at startup.
    restoreRuntimeTerrainObjectsFromSave(state, ini);

    state.subcells.clearReservations();
    state.subcellsInitialized = false;
    state.gates.reset(runtimeGateConfigForState(state));
    state.gatesInitialized = false;
    state.gateIdsByCell.clear();
    state.gateTopologyFingerprint = 0;
    state.gateStrictOpenFingerprint = 0;
    std::vector<WorldObjectInstance>& objects = state.world.accessObjects();
    objects = std::move(validatedLateState->world.accessObjects());

    state.gates = validatedGateState;
    state.gatesInitialized = true;
    rebuildRuntimeGateFootprintIndex(state);
    state.gates.reconcile(runtimeGateContext(state));
    state.gateTopologyFingerprint = runtimeGateTopologyFingerprint(state);
    state.gateStrictOpenFingerprint =
        runtimeGateStrictOpenFingerprint(state);

    state.cloakObservedHealthByObject.clear();
    for (const WorldObjectInstance& object : objects) {
        if (object.alive) {
            state.cloakObservedHealthByObject[object.instanceId] =
                object.health;
        }
    }
    state.cloakStateInitialized = true;

    std::unordered_set<int> loadedPassengerIds;
    for (const WorldObjectInstance& transport : objects) {
        loadedPassengerIds.insert(
            transport.transportPassengerIds.begin(),
            transport.transportPassengerIds.end()
        );
    }

    // A legacy or modded save can contain a passenger that no longer fits
    // the restored transport. Never leave that object permanently hidden:
    // revive it deterministically near its saved position instead.
    for (WorldObjectInstance& passenger : objects) {
        if (passenger.alive
            || toLowerAsciiLocal(passenger.mission) != "insidetransport"
            || loadedPassengerIds.find(passenger.instanceId)
                != loadedPassengerIds.end()) {
            continue;
        }
        const std::optional<RuntimeCellPoint> exitCell =
            findRuntimePassableCellNear(
                state,
                passenger,
                runtimeCellPointForObject(passenger),
                4
            );
        if (exitCell.has_value()) {
            passenger.x = exitCell->x;
            passenger.y = exitCell->y;
            passenger.cell = runtimeCellKey(exitCell->x, exitCell->y);
            passenger.level = terrainLevelAtRuntimeCell(
                state,
                exitCell->x,
                exitCell->y
            );
            passenger.onBridge = runtimeCellIsBridge(
                state,
                exitCell->x,
                exitCell->y
            );
        }
        passenger.health = std::max(1, passenger.health);
        passenger.alive = true;
        passenger.mission = runtimeMissionName(RuntimeMissionIntent::Idle);
        passenger.hasRenderPosition = false;
        passenger.renderMoveActive = false;
        passenger.renderX = static_cast<double>(passenger.x);
        passenger.renderY = static_cast<double>(passenger.y);
        passenger.renderLevel = static_cast<double>(passenger.level);
        passenger.renderOnBridge = passenger.onBridge;
        assignRuntimeInfantrySubcellAtCurrentLocation(state, passenger);
    }

    state.garrisons = std::move(validatedLateState->garrisons);
    state.garrisonsInitialized = true;

    // Old or partially corrupt saves may contain an inactive legacy
    // occupant whose host relationship cannot be recovered. Never leave
    // that infantry permanently invisible: place it safely, or resolve it
    // as destroyed when no legal cell exists.
    for (WorldObjectInstance& occupant : objects) {
        const std::string mission = toLowerAsciiLocal(occupant.mission);
        if (occupant.alive
            || occupant.health <= 0
            || occupant.garrisonHostObjectId >= 0
            || (mission != "occupied" && mission != "garrisoned")) {
            continue;
        }
        const std::optional<RuntimeCellPoint> exitCell =
            findRuntimePassableCellNear(
                state,
                occupant,
                runtimeCellPointForObject(occupant),
                4
            );
        if (exitCell.has_value()) {
            occupant.x = exitCell->x;
            occupant.y = exitCell->y;
            occupant.cell = runtimeCellKey(exitCell->x, exitCell->y);
            occupant.level = terrainLevelAtRuntimeCell(
                state,
                exitCell->x,
                exitCell->y
            );
            occupant.onBridge = runtimeCellIsBridge(
                state,
                exitCell->x,
                exitCell->y
            );
            occupant.alive = true;
            occupant.mission = runtimeMissionName(RuntimeMissionIntent::Idle);
        } else {
            occupant.health = 0;
            occupant.alive = false;
            occupant.mission = "DestroyedWhileEvacuating";
        }
        clearRuntimeRenderPosition(occupant);
        if (occupant.alive) {
            assignRuntimeInfantrySubcellAtCurrentLocation(state, occupant);
        } else {
            occupant.subCell = -1;
        }
    }
    for (WorldObjectInstance& host : objects) {
        if (host.alive && !host.garrisonOccupantIds.empty()) {
            host.mission = runtimeMissionName(
                RuntimeMissionIntent::Occupying
            );
            state.garrisons.syncHostOccupants(objects, host.instanceId);
        }
    }
    for (WorldObjectInstance& occupant : objects) {
        if (occupant.garrisonHostObjectId >= 0 || !occupant.alive) {
            occupant.subCell = -1;
        }
    }

    state.harvesterTasks.clear();
    const int harvesterTaskCount = parseRuntimeIntOrDefault(
        ini.getValue("Harvesters", "Count", "0"),
        0
    );
    for (int index = 0; index < harvesterTaskCount; ++index) {
        const std::vector<std::string> tokens =
            splitRuntimeSaveFields(
                ini.getValue("Harvesters", std::to_string(index), "")
            );
        if (tokens.size() < 13) {
            continue;
        }
        const int objectId = parseRuntimeIntOrDefault(tokens[0], -1);
        WorldObjectInstance* harvesterObject =
            state.objectIndex.find(state.world.accessObjects(), objectId);
        if (harvesterObject == nullptr || harvesterObject->health <= 0
            || (!harvesterObject->alive && !loadedPassengerIds.contains(objectId))) {
            continue;
        }
        RuntimeHarvesterTask task;
        task.phase = runtimeHarvesterPhaseFromName(tokens[1]);
        task.targetOre.x = parseRuntimeIntOrDefault(tokens[2], -1);
        task.targetOre.y = parseRuntimeIntOrDefault(tokens[3], -1);
        task.dockCell.x = parseRuntimeIntOrDefault(tokens[4], -1);
        task.dockCell.y = parseRuntimeIntOrDefault(tokens[5], -1);
        const bool hasQueueCell = tokens.size() >= 15;
        int offset = 0;
        if (hasQueueCell) {
            task.queueCell.x = parseRuntimeIntOrDefault(tokens[6], -1);
            task.queueCell.y = parseRuntimeIntOrDefault(tokens[7], -1);
            offset = 2;
        }
        const bool hasGemUnits =
            tokens.size() >= static_cast<std::size_t>(14 + offset);
        task.targetRefineryId = parseRuntimeIntOrDefault(tokens[6 + offset], -1);
        task.cargoValue = parseRuntimeIntOrDefault(tokens[7 + offset], 0);
        task.cargoUnits = parseRuntimeIntOrDefault(tokens[8 + offset], 0);
        if (hasGemUnits) {
            task.cargoGemUnits =
                parseRuntimeIntOrDefault(tokens[9 + offset], 0);
            task.cargoCapacity =
                parseRuntimeIntOrDefault(tokens[10 + offset], 0);
            task.workTicksRemaining =
                parseRuntimeIntOrDefault(tokens[11 + offset], 0);
            task.cargoHasGems = parseRuntimeBool(tokens[12 + offset]);
            task.status = tokens[13 + offset];
        } else {
            task.cargoCapacity =
                parseRuntimeIntOrDefault(tokens[9 + offset], 0);
            task.workTicksRemaining =
                parseRuntimeIntOrDefault(tokens[10 + offset], 0);
            task.cargoHasGems = parseRuntimeBool(tokens[11 + offset]);
            task.status = tokens[12 + offset];
            task.cargoGemUnits =
                task.cargoHasGems ? std::min(task.cargoUnits, 1) : 0;
        }
        task.cargoGemUnits =
            std::clamp(task.cargoGemUnits, 0, std::max(0, task.cargoUnits));
        task.cargoHasGems = task.cargoHasGems || task.cargoGemUnits > 0;
        if (tokens.size() >= 17U
            && (!parseRuntimeInt(tokens[16], task.nextThinkTick) || task.nextThinkTick < 0)) {
            errorMessage = "invalid saved harvester next action tick"; return false;
        }
        normalizeRuntimeHarvesterTask(state, *harvesterObject, task);
        state.harvesterTasks[objectId] = std::move(task);
    }

    state.slaveMinerTasks = std::move(lateCandidate.slaveMinerTasks);

    state.slaveMinerRegenTicks.clear();
    const int slaveMinerRegenCount = parseRuntimeIntOrDefault(
        ini.getValue("SlaveMinerRegen", "Count", "0"),
        0
    );
    for (int index = 0; index < slaveMinerRegenCount; ++index) {
        const std::vector<std::string> tokens =
            splitRuntimeSaveFields(
                ini.getValue("SlaveMinerRegen", std::to_string(index), "")
            );
        if (tokens.size() < 2) {
            continue;
        }
        const int refineryId = parseRuntimeIntOrDefault(tokens[0], -1);
        const int ticks = std::max(0, parseRuntimeIntOrDefault(tokens[1], 0));
        if (refineryId >= 0 && ticks > 0) {
            state.slaveMinerRegenTicks[refineryId] = ticks;
        }
    }

    state.behaviors = std::move(validatedLateState->behaviors);
    state.pendingTimedBombIntents = std::move(
        validatedLateState->pendingTimedBombIntents
    );
    state.pendingEngineerCaptures = std::move(
        validatedLateState->pendingEngineerCaptures
    );
    state.pendingEngineerActions = std::move(
        validatedLateState->pendingEngineerActions
    );
    state.pendingTransportTargets = std::move(
        validatedLateState->pendingTransportTargets
    );
    state.pendingOccupyTargets = std::move(
        validatedLateState->pendingOccupyTargets
    );
    state.repairingStructureTicks.clear();
    const int repairCount = parseRuntimeIntOrDefault(
        ini.getValue("Repairs", "Count", "0"),
        0
    );
    for (int index = 0; index < repairCount; ++index) {
        const std::vector<std::string> tokens =
            splitRuntimeSaveFields(
                ini.getValue("Repairs", std::to_string(index), "")
            );
        if (tokens.size() < 2) {
            continue;
        }

        const int objectId = parseRuntimeIntOrDefault(tokens[0], -1);
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr
            || object->kind != WorldObjectKind::Structure
            || object->owner != state.economy.owner) {
            continue;
        }
        state.repairingStructureTicks[objectId] =
            std::max(0, parseRuntimeIntOrDefault(tokens[1], 0));
    }

    state.selectedObjectIds.clear();
    state.parasiteReselectAfterLimboObjectIds.clear();
    state.leftPointer.active = false;
    state.rightPointer.active = false;
    state.actionSummary = RuntimeActionSummary{};
    state.gattlingWeaponTimers = std::move(
        validatedLateState->gattlingWeaponTimers
    );
    state.skirmishCrates = std::move(validatedLateState->skirmishCrates);
    state.triggers.truckCrate = validatedLateState->triggers.truckCrate;
    state.triggers.trainCrate = validatedLateState->triggers.trainCrate;
    state.triggers.publicBuildingLocations = std::move(validatedLateState->triggers.publicBuildingLocations);
    state.wrecks = std::move(validatedLateState->wrecks);
    state.buildingCollapses = std::move(validatedLateState->buildingCollapses);
    state.sidebarPowerAnimation = RuntimeSidebarPowerAnimation{};
    state.specialMechanisms = std::move(validatedLateState->specialMechanisms);
    state.specialMechanismsInitialized = true;
    for (const auto& [id, missile] : state.specialMechanisms.spawnedMissiles())
        if (auto* object = findMutableObjectById(state, id)) applyRuntimeMissilePose(*object, missile);
    state.spies = std::move(validatedLateState->spies);
    state.spiesInitialized = true;
    state.grinding = std::move(validatedLateState->grinding);
    state.applyingGrindingActions = false;
    state.parasiteStunUntilTickByObject =
        std::move(validatedParasiteStuns);
    state.superWeapons = std::move(validatedLateState->superWeapons);
    state.superWeaponsInitialized = true;
    state.worldAudio = std::move(validatedLateState->worldAudio);
    state.worldAudioInitialized = true;
    state.lighting = std::move(validatedLateState->lighting);
    state.lightingInitialized = true;
    state.activeLightingTokens = std::move(
        validatedLateState->activeLightingTokens
    );
    state.lightingCrossingsThisTick.clear();
    state.particles = std::move(validatedLateState->particles);
    state.particlesInitialized = true;
    state.particleRenderSnapshots.clear();
    state.particleBeamRenderSnapshots.clear();
    state.particleLocalLightRenderSnapshots.clear();
    // Dock assignment is part of the power rebuild below.  Install the
    // detached, validated repair authority first so a saved repair claim wins
    // over automatic PadAircraft placement during load materialization.
    state.repairDepots = std::move(validatedRepairDepotState);
    state.repairDepotsInitialized = true;
    state.factoryDeliveries = std::move(validatedFactoryDeliveryState);
    state.pendingCloneDeliveries = std::move(validatedLateState->pendingCloneDeliveries);
    state.playerCommands = std::move(validatedPlayerCommands);
    state.aircraftServices = std::move(validatedAircraftServiceState);
    state.aircraftServicesInitialized = true;
    state.magBeams = std::move(validatedMagBeams);
    rebuildRuntimeEconomyPower(state);
    snapRuntimeSidebarPowerAnimation(state);
    snapRuntimeCreditAudio(state);
    initializeRuntimeNavigation(state);
    state.subcells = std::move(validatedLateState->subcells);
    state.subcellsInitialized = true;
    if (!savedSubcellReservations.empty()) {
        invalidateRuntimeSubcellDynamicPathState(state);
    }
    // Visibility follows subcell poses as well as logical cells. Restore
    // multiplayer poses before rebuilding it, so moving ships and infantry
    // do not briefly reveal a different region during a shared load.
    if ((version == 15 || exactOfflinePoses) && !restoreRuntimeMultiplayerObjectPoses(state, ini, errorMessage)) return false;
    refreshRuntimeRepairDepotDockPoses(state);
    if (state.visionEnabled) {
        state.vision.reset();
        updateRuntimeVisionState(state);
        const int visionOwnerCount = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue("Vision", "Count", "0"),
                0
            )
        );
        for (int ownerIndex = 0;
            ownerIndex < visionOwnerCount;
            ++ownerIndex) {
            const std::vector<std::string> tokens = splitRuntimeSaveFields(
                ini.getValue("Vision", std::to_string(ownerIndex), "")
            );
            if (tokens.empty() || tokens[0].empty()) {
                continue;
            }
            std::vector<int> exploredCells;
            if (tokens.size() >= 2 && !tokens[1].empty()) {
                std::stringstream cellStream(tokens[1]);
                std::string cellToken;
                while (std::getline(cellStream, cellToken, ',')) {
                    int cellIndex = -1;
                    if (parseRuntimeInt(cellToken, cellIndex)
                        && cellIndex >= 0
                        && cellIndex < 512 * 512) {
                        exploredCells.push_back(cellIndex);
                    }
                }
            }
            state.vision.restoreExploredCellIndices(
                tokens[0],
                exploredCells
            );
        }
        if (!readRuntimeFireReveals(state, ini, errorMessage)) return false;
    }
    if (state.campaignMode || state.cooperativeSession.active()) {
        const char* scenarioSaveSection = state.campaignMode ? "Campaign" : "CooperativeTriggers";
        RuntimeHostTriggerPersistenceOperations triggerPersistenceOperations(state);
        restoreRuntimeCampaignTriggerCollections(state.triggers, ini, state.tick,
            validatedLateState->triggers.campaignAlliedPairs, triggerPersistenceOperations);

        if (ini.getSection("CampaignScore") != nullptr) {
            const int scoreSchema = parseRuntimeIntOrDefault(
                ini.getValue("CampaignScore", "Schema", "-1"),
                -1
            );
            if (scoreSchema != 1 && scoreSchema != 2) {
                errorMessage = "unsupported campaign score schema";
                return false;
            }
            // Schema 1 did not persist authored [Ranking] metadata. Start
            // from the active map's initialized authority so old saves keep
            // the correct localized result after restoration.
            RuntimeCampaignScoreState loadedScore = state.campaignScore;
            loadedScore.missionStartTick = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "MissionStartTick", "0"),
                    0
                )
            );
            loadedScore.parTicks = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "ParTicks", "0"),
                    0
                )
            );
            if (scoreSchema >= 2) {
                loadedScore.parTimeField = trimAsciiLocal(
                    ini.getValue("CampaignScore", "ParTimeField", "")
                );
                loadedScore.parTimeText = trimAsciiLocal(
                    ini.getValue("CampaignScore", "ParTimeText", "")
                );
                loadedScore.underParTitleKey = trimAsciiLocal(
                    ini.getValue("CampaignScore", "UnderParTitle", "")
                );
                loadedScore.underParMessageKey = trimAsciiLocal(
                    ini.getValue("CampaignScore", "UnderParMessage", "")
                );
                loadedScore.overParTitleKey = trimAsciiLocal(
                    ini.getValue("CampaignScore", "OverParTitle", "")
                );
                loadedScore.overParMessageKey = trimAsciiLocal(
                    ini.getValue("CampaignScore", "OverParMessage", "")
                );
            }
            loadedScore.enemyUnitsDestroyed = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "EnemyUnitsDestroyed", "0"),
                    0
                )
            );
            loadedScore.enemyStructuresDestroyed = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue(
                        "CampaignScore",
                        "EnemyStructuresDestroyed",
                        "0"
                    ),
                    0
                )
            );
            loadedScore.friendlyUnitsLost = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "FriendlyUnitsLost", "0"),
                    0
                )
            );
            loadedScore.friendlyStructuresLost = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue(
                        "CampaignScore",
                        "FriendlyStructuresLost",
                        "0"
                    ),
                    0
                )
            );
            loadedScore.finishedTick = std::max(
                -1,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "FinishedTick", "-1"),
                    -1
                )
            );
            loadedScore.finalScore = std::max(
                0,
                parseRuntimeIntOrDefault(
                    ini.getValue("CampaignScore", "FinalScore", "0"),
                    0
                )
            );
            loadedScore.captured = parseRuntimeBool(
                ini.getValue("CampaignScore", "Captured", "no")
            );
            if (scoreSchema >= 2) {
                loadedScore.numericFormulaId = trimAsciiLocal(
                    ini.getValue(
                        "CampaignScore",
                        "NumericFormula",
                        "concord-approximate-v1"
                    )
                );
                loadedScore.finishedUnderPar = parseRuntimeBool(
                    ini.getValue("CampaignScore", "FinishedUnderPar", "no")
                );
            } else {
                loadedScore.finishedUnderPar = loadedScore.captured
                    && loadedScore.parTicks > 0
                    && runtimeCampaignScoreElapsedTicks(loadedScore)
                        <= loadedScore.parTicks;
            }
            state.campaignScore = loadedScore;
        }
        state.campaignProgressSaved = parseRuntimeBool(
            ini.getValue(scenarioSaveSection, "ProgressSaved", "no")
        );
        state.triggers.campaignMissionTimerStarted = parseRuntimeBool(
            ini.getValue(scenarioSaveSection, "MissionTimerStarted", "no")
        );
        state.triggers.campaignMissionTimerRunning = parseRuntimeBool(
            ini.getValue(scenarioSaveSection, "MissionTimerRunning", "no")
        );
        state.triggers.campaignMissionTimerRemainingTicks = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(
                    scenarioSaveSection,
                    "MissionTimerRemainingTicks",
                    "0"
                ),
                0
            )
        );
        state.triggers.campaignMissionTimerLabel = trimAsciiLocal(
            ini.getValue(scenarioSaveSection, "MissionTimerLabel", "")
        );
        state.triggers.campaignInputLocked = parseRuntimeBool(
            ini.getValue(scenarioSaveSection, "InputLocked", "no")
        );
        state.triggers.campaignRadarBlackoutUntilTick = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(scenarioSaveSection, "RadarBlackoutUntilTick", "0"),
                0
            )
        );
        state.triggers.campaignChronoScreenEffectUntilTick = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(
                    scenarioSaveSection,
                    "ChronoScreenEffectUntilTick",
                    "0"
                ),
                0
            )
        );
        state.triggers.campaignIonStormUntilTick = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(scenarioSaveSection, "IonStormUntilTick", "0"),
                0
            )
        );
        state.triggers.campaignNextIonStormStrikeTick = std::max(
            0,
            parseRuntimeIntOrDefault(
                ini.getValue(
                    scenarioSaveSection,
                    "NextIonStormStrikeTick",
                    "0"
                ),
                0
            )
        );
        const std::string savedViewLocalSize = trimAsciiLocal(
            ini.getValue(scenarioSaveSection, "ViewLocalSize", "")
        );
        if (!savedViewLocalSize.empty()) {
            state.report.map.localSize = savedViewLocalSize;
            state.radarProjectionCacheValid = false;
            state.radarProjectionCacheKey.clear();
            state.radarTerrainKey.clear();
        }
        state.triggers.firedTriggers = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "FiredTriggers",
                std::to_string(state.triggers.firedTriggers)
            ),
            state.triggers.firedTriggers
        );
        state.triggers.teamsCreated = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "TeamsCreated",
                std::to_string(state.triggers.teamsCreated)
            ),
            state.triggers.teamsCreated
        );
        state.triggers.unitsCreated = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "UnitsCreated",
                std::to_string(state.triggers.unitsCreated)
            ),
            state.triggers.unitsCreated
        );
        state.triggers.ordersIssued = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "OrdersIssued",
                std::to_string(state.triggers.ordersIssued)
            ),
            state.triggers.ordersIssued
        );
        state.triggers.scriptStepsIssued = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "ScriptStepsIssued",
                std::to_string(state.triggers.scriptStepsIssued)
            ),
            state.triggers.scriptStepsIssued
        );
        state.triggers.scriptStepsCompleted = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "ScriptStepsCompleted",
                std::to_string(state.triggers.scriptStepsCompleted)
            ),
            state.triggers.scriptStepsCompleted
        );
        state.triggers.unsupportedScriptSteps = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "UnsupportedScriptSteps",
                std::to_string(state.triggers.unsupportedScriptSteps)
            ),
            state.triggers.unsupportedScriptSteps
        );
        state.triggers.unsupportedEvents = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "UnsupportedEvents",
                std::to_string(state.triggers.unsupportedEvents)
            ),
            state.triggers.unsupportedEvents
        );
        state.triggers.unsupportedActions = parseRuntimeIntOrDefault(
            ini.getValue(
                scenarioSaveSection,
                "UnsupportedActions",
                std::to_string(state.triggers.unsupportedActions)
            ),
            state.triggers.unsupportedActions
        );
        const std::string savedResult = toUpperAsciiLocal(trimAsciiLocal(
            ini.getValue(scenarioSaveSection, "Result", "")
        ));
        if (savedResult == "VICTORY" || savedResult == "DEFEAT"
            || (!state.campaignMode && runtimeLocalObserver(state.config) && savedResult == "MATCH COMPLETE")) {
            state.skirmishMatchFinished = true;
            state.skirmishVictory = savedResult == "VICTORY";
            state.skirmishResult = savedResult;
            if (state.campaignMode) finalizeRuntimeCampaignScore(state, state.skirmishVictory);
        } else {
            state.skirmishMatchFinished = false;
            state.skirmishVictory = false;
            state.skirmishResult.clear();
            state.campaignScore.captured = false;
            state.campaignScore.finishedTick = -1;
            state.campaignScore.finalScore = 0;
            state.campaignScore.presentationStartedAtMs = 0;
        }
        snapshotRuntimeCampaignTriggerObservations(state);
        if (!readRuntimeCooperativeObservations(ini, state.triggers, errorMessage)) return false;
        synchronizeRuntimeCooperativeOutcome(state);
    }

    if (!readRuntimeJumpjetFlights(state, ini, errorMessage)) return false;
    if (!readRuntimeAircraftFlights(state, ini, errorMessage)) return false;
    if (!readRuntimeTriggerReinforcements(state, ini, errorMessage)) return false;
    RuntimeHostTriggerPersistenceOperations missionPersistenceOperations(state);
    restoreRuntimeTeamMissionState(state.triggers, ini, version,
        state.campaignMode, missionPersistenceOperations);
    state.attackApproachCache.clear();
    state.pathFailureCache.clear();
    state.renderCache.terrainLayer = RuntimeTerrainLayerCache{};
    resetRuntimeWaterWakeState(state.renderCache);
    state.radarTerrainKey.clear();
    state.radarProjectionCacheValid = false;
    if (!loadRuntimeMatchScore(state, ini, errorMessage)) return false;
    state.deathWeapons = std::move(validatedDeathWeapons);
    state.combatDetails = std::move(validatedCombatDetails);
    state.psychicStatuses = std::move(validatedPsychicStatuses);
    state.airstrikes = std::move(validatedAirstrikeHost.airstrikes);
    state.airstrikePlanesByToken = std::move(
        validatedAirstrikeHost.bindingsByToken
    );
    state.airstrikeRootAuthoritiesByActor = std::move(
        validatedAirstrikeHost.authoritiesByActor
    );
    for (const RuntimeMagBeamBindingState& binding :
            state.magBeams.bindings()) {
        WorldObjectInstance* target = findMutableObjectById(
            state,
            binding.targetObjectId
        );
        if (target != nullptr) {
            applyRuntimeMagBeamKinematicsToObject(
                state,
                *target,
                binding.kinematics,
                true
            );
        }
    }
    state.pendingObjectExits.clear();
    state.pendingObjectExitIds.clear();
    state.pendingBridgeCollapses.clear();
    state.deferredBridgeMutationCells.clear();
    state.drainingObjectExits = false;
    commitRuntimeProjectilesFromSave(state, std::move(savedProjectiles));
    if (!loadRuntimeVirusDeaths(state, ini, errorMessage)
        || !loadRuntimeUnitDeaths(state, ini, errorMessage)
        || !loadRuntimeSquidSplashes(state, ini, errorMessage)) return false;
    // Virus restoration replaces the impact list; unit deaths and splashes
    // then append. Preserve those effects while installing the validated FIFO.
    for (auto& cycle : validatedLateState->impactEffects)
        state.impactEffects.push_back(std::move(cycle));
    updateRuntimeAirstrikePlaneRoutes(state);
    // Navigation and support-route reconstruction are complete. Restore the
    // spawned aircraft's continuous pose, including returns without a behavior.
    restoreRuntimeCarrierFlightPoses(state, savedCarrierFlightPoses);
    invalidateRuntimeObjectProjectionCache(state);
    state.repaintRequested = true;
    refreshRuntimeActionSummary(state);
    // Legacy saves rebuild paths. Queue that work before the first reservation
    // pass, both for a saved segment and an accepted ordinary Move beginning
    // at its exact stopped ground pose. Exact snapshots retain their paths.
    if (version < 15 && !exactOfflinePoses) {
        std::vector<int> movingIds;
        for (const auto& [id, behavior] : state.behaviors) {
            const bool queuedStoppedMove = version == 14
                && behavior.state == RuntimeBehaviorState::Queued
                && behavior.moveCooldownTicks == 0
                && behavior.renderMoveTicksRemaining == 0 && behavior.renderMoveTicksTotal == 0
                && behavior.transportTransferKind == RuntimeTransportTransferKind::None
                && behavior.pendingAllowedOccupiedObjectId == -1
                && !state.pendingTransportTargets.contains(id)
                && behavior.moveTarget.x >= 0 && behavior.moveTarget.x < 512
                && behavior.moveTarget.y >= 0 && behavior.moveTarget.y < 512;
            if (behavior.kind == RuntimeBehaviorKind::Move
                && (behavior.moveCooldownTicks > 0 || queuedStoppedMove)
                && behavior.path.empty() && !behavior.pathRequestPending) movingIds.push_back(id);
        }
        std::sort(movingIds.begin(), movingIds.end());
        for (int id : movingIds) {
            auto* mover = findMutableObjectById(state, id);
            auto& behavior = state.behaviors.at(id);
            const bool restoredGroundInfantrySegment = mover
                && runtimeInfantryUsesGroundSubcells(*mover)
                && runtimeObjectMovementMode(state, *mover) != PathMovementMode::Air
                && mover->renderMoveActive && mover->hasRenderPosition
                && behavior.renderMoveTicksRemaining > 0;
            const bool restoredStoppedGroundMove = mover && mover->alive
                && behavior.moveCooldownTicks == 0
                && worldObjectHasStoppedGroundPose(*mover)
                && runtimeObjectMovementMode(state, *mover) != PathMovementMode::Air
                && (runtimeInfantryUsesGroundSubcells(*mover)
                    || runtimeObjectUsesTerrainMovementSpeed(state, *mover));
            if (mover && ((behavior.moveCooldownTicks > 0
                    && (runtimeObjectUsesTerrainMovementSpeed(state, *mover)
                        || restoredGroundInfantrySegment)) || restoredStoppedGroundMove)
                && runtimeCellPointForObject(*mover) != behavior.moveTarget) {
                submitRuntimeMovePathRequest(state, *mover, behavior, runtimeCellPointForObject(*mover),
                    behavior.moveTarget, behavior.pendingAllowedOccupiedObjectId);
            }
        }
    }
    state.economy.status = "loaded save";
    state.interactionStatus = "loaded save";
    return true;
}

bool materializeRuntimeLoadState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage) {
    IniFile ini;
    if (!loadRuntimeSaveDocument(savePath, ini)) {
        errorMessage = "failed to load save file";
        return false;
    }
    if (parseRuntimeIntOrDefault(
            ini.getValue("RuntimeSave", "Version", "0"),
            0
        ) == 15) {
        errorMessage =
            "multiplayer snapshot v15 requires the pure-memory stage API";
        return false;
    }
    return materializeRuntimeLoadStateFromIni(state, ini, errorMessage);
}

void seedRuntimeLoadScratch(
    RuntimeProductionHostState& scratch,
    const RuntimeProductionHostState& live,
    bool forceVisionEnabled) {
    // These fields form the immutable map/rules context used while parsing
    // and materializing a save.  All copies happen before the live state is
    // touched, so allocation failure leaves the running match unchanged.
    // Same borrowed host source for detached materialization; never committed.
    scratch.uiClock = live.uiClock;
    scratch.worldAudioClock = live.worldAudioClock;
    scratch.config = live.config;
    scratch.report = live.report;
    scratch.world = live.world;
    scratch.theater = live.theater;
    scratch.art = live.art;
    scratch.rules = live.rules;
    scratch.mapFacts = live.mapFacts;
    scratch.theaterLoaded = live.theaterLoaded;
    scratch.artLoaded = live.artLoaded;
    scratch.rulesLoaded = live.rulesLoaded;
    scratch.mapFactsLoaded = live.mapFactsLoaded;
    scratch.soundIni = live.soundIni;
    scratch.soundLoaded = live.soundLoaded;
    scratch.soundPath = live.soundPath;
    scratch.worldAudioCatalog = live.worldAudioCatalog;
    scratch.worldAudioCatalogLoaded = live.worldAudioCatalogLoaded;
    scratch.ownerRemapColors = live.ownerRemapColors;
    scratch.neutralRemapOwners = live.neutralRemapOwners;
    scratch.skirmishCountryByOwner = live.skirmishCountryByOwner;
    scratch.skirmishTeamByOwner = live.skirmishTeamByOwner;
    scratch.skirmishDifficultyByOwner = live.skirmishDifficultyByOwner;
    scratch.cooperativeSession = live.cooperativeSession;
    scratch.cooperativeSessionStatus = live.cooperativeSessionStatus;
    scratch.skirmishModeRules = live.skirmishModeRules;
    scratch.skirmishModeRulesLoaded = live.skirmishModeRulesLoaded;
    scratch.skirmishModeAlliesAllowed = live.skirmishModeAlliesAllowed;
    scratch.skirmishModeRulesPath = live.skirmishModeRulesPath;
    scratch.initialSkirmishCrates = live.initialSkirmishCrates;
    scratch.campaignMode = live.campaignMode;
    scratch.campaignProgressSaved = live.campaignProgressSaved;
    scratch.campaignScore = live.campaignScore;
    scratch.matchScores = live.matchScores;
    scratch.skirmishMatchFinished = live.skirmishMatchFinished;
    scratch.skirmishVictory = live.skirmishVictory;
    scratch.skirmishResult = live.skirmishResult;
    scratch.localRadarColor = live.localRadarColor;
    scratch.terrainInfoByCell = live.terrainInfoByCell;
    scratch.terrainObjectInitialStateCaptured =
        live.terrainObjectInitialStateCaptured;
    scratch.terrainObjectInitialLayoutSignature =
        live.terrainObjectInitialLayoutSignature;
    scratch.terrainObjectInitialStateByKey =
        live.terrainObjectInitialStateByKey;
    scratch.bridgeRepairSpansByHut = live.bridgeRepairSpansByHut;
    scratch.bridgeHealthByCell = live.bridgeHealthByCell;
    scratch.destroyableBridges = live.destroyableBridges;
    scratch.bridgeSpecialFlagsLoaded = live.bridgeSpecialFlagsLoaded;
    scratch.mapInitialVeteran = live.mapInitialVeteran;
    scratch.structureFootprints = live.structureFootprints;
    scratch.camera = live.camera;
    scratch.cameraClampViewport = live.cameraClampViewport;
    scratch.vision = live.vision;
    scratch.visionEnabled = forceVisionEnabled || live.visionEnabled;
    scratch.economy = live.economy;
    scratch.humanHouseAuthorities = live.humanHouseAuthorities;
    scratch.computerAiByOwner = live.computerAiByOwner;
    scratch.computerMiningByOwner = live.computerMiningByOwner;
    scratch.activeBuildTab = live.activeBuildTab;
    scratch.initialOreFieldsSeeded = live.initialOreFieldsSeeded;
    scratch.initialOreCellsSeeded = live.initialOreCellsSeeded;
    scratch.initialOreCellsDensified = live.initialOreCellsDensified;
    scratch.triggers = live.triggers;
    scratch.techTreeLoaded = live.techTreeLoaded;
    scratch.techTree = live.techTree;
    scratch.multiplayerDefeatedOwners = live.multiplayerDefeatedOwners;
    scratch.multiplayerWinningOwners = live.multiplayerWinningOwners;
    scratch.multiplayerWinningTeams = live.multiplayerWinningTeams;
    scratch.stableRuntimeResourceTracking =
        live.stableRuntimeResourceTracking;
    scratch.resourceStateRevision = live.resourceStateRevision;
    scratch.terrainByCell = buildRuntimeTerrainLookup(scratch.world);
}

void commitRuntimeLoadedState(
    RuntimeProductionHostState& live,
    RuntimeProductionHostState& loaded) noexcept {
#define RUNTIME_LOAD_SWAP(field) \
    swapRuntimeLoadedField(live.field, loaded.field)

    // World-owned storage moves first.  Pointer indexes and render caches are
    // swapped only afterwards, so each side remains paired with the vector
    // storage its pointers reference when the old state is destroyed.
    RUNTIME_LOAD_SWAP(config.noCooldown);
    invalidateRuntimeVisionTerrainCoordinateLeases(live.world);
    invalidateRuntimeVisionTerrainCoordinateLeases(loaded.world);
    RUNTIME_LOAD_SWAP(world);
    RUNTIME_LOAD_SWAP(report);
    RUNTIME_LOAD_SWAP(tick);
    RUNTIME_LOAD_SWAP(camera);
    RUNTIME_LOAD_SWAP(activeBuildTab);
    swapRuntimeLoadedEconomy(live.economy, loaded.economy);
    RUNTIME_LOAD_SWAP(humanHouseAuthorities);
    RUNTIME_LOAD_SWAP(computerAiByOwner);
    RUNTIME_LOAD_SWAP(computerMiningByOwner);
    RUNTIME_LOAD_SWAP(techTreeLoaded);
    RUNTIME_LOAD_SWAP(techTree);
    RUNTIME_LOAD_SWAP(initialOreFieldsSeeded);
    RUNTIME_LOAD_SWAP(initialOreCellsSeeded);
    RUNTIME_LOAD_SWAP(initialOreCellsDensified);

    RUNTIME_LOAD_SWAP(terrainByCell);
    RUNTIME_LOAD_SWAP(terrainInfoByCell);
    RUNTIME_LOAD_SWAP(terrainObjectInitialStateCaptured);
    RUNTIME_LOAD_SWAP(terrainObjectInitialLayoutSignature);
    RUNTIME_LOAD_SWAP(terrainObjectInitialStateByKey);
    RUNTIME_LOAD_SWAP(bridgeRepairSpansByHut);
    RUNTIME_LOAD_SWAP(bridgeHealthByCell);
    RUNTIME_LOAD_SWAP(destroyableBridges);
    RUNTIME_LOAD_SWAP(bridgeSpecialFlagsLoaded);
    RUNTIME_LOAD_SWAP(mapInitialVeteran);
    RUNTIME_LOAD_SWAP(structureFootprints);
    RUNTIME_LOAD_SWAP(mapFactsLoaded);
    RUNTIME_LOAD_SWAP(mapQueryStatus);
    RUNTIME_LOAD_SWAP(ownerRemapColors);
    RUNTIME_LOAD_SWAP(neutralRemapOwners);
    RUNTIME_LOAD_SWAP(navigationStats);
    RUNTIME_LOAD_SWAP(pathTerrainCache);
    RUNTIME_LOAD_SWAP(navigationRefreshPending);
    RUNTIME_LOAD_SWAP(powerRefreshPending);

    RUNTIME_LOAD_SWAP(gates);
    RUNTIME_LOAD_SWAP(gatesInitialized);
    RUNTIME_LOAD_SWAP(gateIdsByCell);
    RUNTIME_LOAD_SWAP(gateTopologyFingerprint);
    RUNTIME_LOAD_SWAP(gateStrictOpenFingerprint);
    RUNTIME_LOAD_SWAP(gateRenderSnapshots);
    RUNTIME_LOAD_SWAP(garrisons);
    RUNTIME_LOAD_SWAP(garrisonsInitialized);
    RUNTIME_LOAD_SWAP(subcells);
    RUNTIME_LOAD_SWAP(subcellsInitialized);
    RUNTIME_LOAD_SWAP(repairDepots);
    RUNTIME_LOAD_SWAP(repairDepotsInitialized);
    RUNTIME_LOAD_SWAP(factoryDeliveries);
    RUNTIME_LOAD_SWAP(pendingCloneDeliveries);
    RUNTIME_LOAD_SWAP(playerCommands);
    live.playerLocalCommands = {};
    live.playerLocalCommands.planning = live.playerCommands.waypoints.planningOwners.contains(
        runtimeCanonicalHumanHouseOwner(live.economy.owner));
    live.playerCommunication = {};
    RUNTIME_LOAD_SWAP(playerDefeatNotices);
    RUNTIME_LOAD_SWAP(grinding);
    RUNTIME_LOAD_SWAP(applyingGrindingActions);
    RUNTIME_LOAD_SWAP(aircraftServices);
    RUNTIME_LOAD_SWAP(aircraftServicesInitialized);
    RUNTIME_LOAD_SWAP(deathWeapons);
    RUNTIME_LOAD_SWAP(combatDetails);
    RUNTIME_LOAD_SWAP(psychicStatuses);
    RUNTIME_LOAD_SWAP(magBeams);
    RUNTIME_LOAD_SWAP(airstrikes);
    RUNTIME_LOAD_SWAP(airstrikePlanesByToken);
    RUNTIME_LOAD_SWAP(airstrikeRootAuthoritiesByActor);
    RUNTIME_LOAD_SWAP(specialMechanisms);
    RUNTIME_LOAD_SWAP(specialMechanismsInitialized);
    RUNTIME_LOAD_SWAP(spies);
    RUNTIME_LOAD_SWAP(spiesInitialized);
    RUNTIME_LOAD_SWAP(superWeapons);
    RUNTIME_LOAD_SWAP(superWeaponsInitialized);
    RUNTIME_LOAD_SWAP(superWeaponRenderSnapshots);
    RUNTIME_LOAD_SWAP(specialMechanismRenderSnapshots);
    RUNTIME_LOAD_SWAP(worldAudio);
    RUNTIME_LOAD_SWAP(worldAudioInitialized);
    RUNTIME_LOAD_SWAP(lighting);
    RUNTIME_LOAD_SWAP(lightingInitialized);
    RUNTIME_LOAD_SWAP(activeLightingTokens);
    RUNTIME_LOAD_SWAP(lightingCrossingsThisTick);
    RUNTIME_LOAD_SWAP(particles);
    RUNTIME_LOAD_SWAP(particlesInitialized);
    RUNTIME_LOAD_SWAP(particleRenderSnapshots);
    RUNTIME_LOAD_SWAP(particleBeamRenderSnapshots);
    RUNTIME_LOAD_SWAP(particleLocalLightRenderSnapshots);

    RUNTIME_LOAD_SWAP(cloakStateInitialized);
    RUNTIME_LOAD_SWAP(cloakObservedHealthByObject);
    RUNTIME_LOAD_SWAP(vision);
    RUNTIME_LOAD_SWAP(visionEnabled);
    RUNTIME_LOAD_SWAP(behaviors);
    RUNTIME_LOAD_SWAP(jumpjetFlights);
    RUNTIME_LOAD_SWAP(aircraftFlights);
    RUNTIME_LOAD_SWAP(pendingPathObjectIds);
    RUNTIME_LOAD_SWAP(pendingEngineerCaptures);
    RUNTIME_LOAD_SWAP(pendingEngineerActions);
    RUNTIME_LOAD_SWAP(pendingOccupyTargets);
    RUNTIME_LOAD_SWAP(pendingTransportTargets);
    RUNTIME_LOAD_SWAP(pendingTimedBombIntents);
    RUNTIME_LOAD_SWAP(parasiteStunUntilTickByObject);
    RUNTIME_LOAD_SWAP(harvesterTasks);
    RUNTIME_LOAD_SWAP(slaveMinerTasks);
    RUNTIME_LOAD_SWAP(slaveMinerRegenTicks);
    RUNTIME_LOAD_SWAP(gattlingWeaponTimers);
    RUNTIME_LOAD_SWAP(repairingStructureTicks);

    RUNTIME_LOAD_SWAP(attackApproachCache);
    RUNTIME_LOAD_SWAP(pathFailureCache);
    RUNTIME_LOAD_SWAP(tickOccupancyCacheTick);
    RUNTIME_LOAD_SWAP(tickOccupiedByCell);
    RUNTIME_LOAD_SWAP(tickGroundOccupantCountByCell);
    RUNTIME_LOAD_SWAP(tickCrushEmptyCellProofValid);
    RUNTIME_LOAD_SWAP(tickCrushEmptyCellObjectCount);
    RUNTIME_LOAD_SWAP(tickCrushUnindexedFoundationBounds);
    RUNTIME_LOAD_SWAP(nextProjectileActorId);
    RUNTIME_LOAD_SWAP(projectiles);
    RUNTIME_LOAD_SWAP(recentExpiredProjectiles);
    RUNTIME_LOAD_SWAP(impactEffects);
    RUNTIME_LOAD_SWAP(wrecks);
    RUNTIME_LOAD_SWAP(buildingCollapses);
    RUNTIME_LOAD_SWAP(nextReplayEventSequence);
    RUNTIME_LOAD_SWAP(recentReplayEvents);
    RUNTIME_LOAD_SWAP(snapshotCache);
    RUNTIME_LOAD_SWAP(actionSummary);

    swapRuntimeLoadedTriggers(live.triggers, loaded.triggers);
    RUNTIME_LOAD_SWAP(cooperativeSession);
    RUNTIME_LOAD_SWAP(cooperativeSessionStatus);
    RUNTIME_LOAD_SWAP(campaignProgressSaved);
    RUNTIME_LOAD_SWAP(campaignScore);
    RUNTIME_LOAD_SWAP(matchScores);
    RUNTIME_LOAD_SWAP(skirmishCrates);
    RUNTIME_LOAD_SWAP(skirmishMatchFinished);
    RUNTIME_LOAD_SWAP(skirmishVictory);
    RUNTIME_LOAD_SWAP(skirmishResult);
    // Presentation clocks/audio belong to this window, not the saved world.
    live.resultPresentation = {};
    RUNTIME_LOAD_SWAP(multiplayerDefeatedOwners);
    RUNTIME_LOAD_SWAP(multiplayerWinningOwners);
    RUNTIME_LOAD_SWAP(multiplayerWinningTeams);

    RUNTIME_LOAD_SWAP(pendingObjectExits);
    RUNTIME_LOAD_SWAP(pendingObjectExitIds);
    RUNTIME_LOAD_SWAP(drainingObjectExits);
    RUNTIME_LOAD_SWAP(selectedObjectIds);
    RUNTIME_LOAD_SWAP(parasiteReselectAfterLimboObjectIds);
    RUNTIME_LOAD_SWAP(leftPointer.active);
    RUNTIME_LOAD_SWAP(rightPointer.active);
    RUNTIME_LOAD_SWAP(sidebarPowerAnimation);
    swapRuntimeLoadedRenderWorldCaches(
        live.renderCache,
        loaded.renderCache
    );
    RUNTIME_LOAD_SWAP(radarTerrainKey);
    RUNTIME_LOAD_SWAP(radarProjectionCacheValid);
    RUNTIME_LOAD_SWAP(radarProjectionCacheKey);
    RUNTIME_LOAD_SWAP(stableRuntimeResourceTracking);
    RUNTIME_LOAD_SWAP(resourceStateRevision);
    RUNTIME_LOAD_SWAP(cachedRadarResourceRevision);
    RUNTIME_LOAD_SWAP(cachedRadarResourceHash);
    RUNTIME_LOAD_SWAP(runtimePowerStateObserved);
    RUNTIME_LOAD_SWAP(runtimeLastLowPowerState);
    RUNTIME_LOAD_SWAP(runtimeRadarStateObserved);
    RUNTIME_LOAD_SWAP(runtimeLastRadarAvailable);
    RUNTIME_LOAD_SWAP(runtimeRadarTransitionStartTick);
    RUNTIME_LOAD_SWAP(runtimeRadarTransitionDirection);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheValid);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheViewport);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheCameraX);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheCameraY);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheTileWidth);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheTileHeight);
    RUNTIME_LOAD_SWAP(projectedObjectsCacheSignature);
    RUNTIME_LOAD_SWAP(projectedObjectsCache);
    RUNTIME_LOAD_SWAP(placementChecksCacheTick);
    RUNTIME_LOAD_SWAP(placementChecksCacheCell);
    RUNTIME_LOAD_SWAP(placementChecksCacheKind);
    RUNTIME_LOAD_SWAP(placementChecksCacheTypeId);
    RUNTIME_LOAD_SWAP(placementChecksCacheVision);
    RUNTIME_LOAD_SWAP(placementChecksCache);
    RUNTIME_LOAD_SWAP(repaintRequested);
    RUNTIME_LOAD_SWAP(interactionStatus);

#undef RUNTIME_LOAD_SWAP
}

bool loadRuntimeStateWithVisionPolicy(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage,
    bool forceVisionEnabled) {
    using Candidate = RuntimeProductionHostState;
    class HostTransaction final : public RuntimeLoadTransaction {
    public:
        explicit HostTransaction(Candidate& live) noexcept : state(live) {}
        void seedCandidate(bool forceVision) override {
            scratch = std::make_unique<Candidate>(*state.constructionUiClock, *state.constructionAudioClock, state.geometry);
            seedRuntimeLoadScratch(*scratch, state, forceVision);
            injectRuntimeLoadFaultForSmoke(RuntimeLoadFaultPointForSmoke::AfterScratchSeed);
        }
        bool materializeCandidate(const std::string& path, std::string& error) override {
            return materializeRuntimeLoadState(*scratch, path, error);
        }
        void prepareCandidate() override {
            prepareRuntimePlayerDefeatNoticeLoadBaseline(*scratch);
            injectRuntimeLoadFaultForSmoke(RuntimeLoadFaultPointForSmoke::BeforeCommit);
            scratch->resourceStateRevision =
                state.resourceStateRevision == std::numeric_limits<std::uint64_t>::max()
                    ? 1U : state.resourceStateRevision + 1U;
            scratch->cachedRadarResourceRevision = scratch->resourceStateRevision ^ 1ULL;
            scratch->cachedRadarResourceHash = 0;
            rehydrateActions = scratch->worldAudio.rehydrateActions(
                runtimeWorldAudioContext(*scratch), scratch->tick);
        }
        void commitCandidate() noexcept override { commitRuntimeLoadedState(state, *scratch); }
        void presentCommitted() override {
            stopRuntimeWorldAudioBackend(state);
            applyRuntimeWorldAudioBackendActions(state, rehydrateActions);
        }
        void releaseCandidate() noexcept override {
            std::vector<RuntimeWorldAudioAction>().swap(rehydrateActions);
            scratch.reset();
        }
    private:
        Candidate& state;
        std::unique_ptr<Candidate> scratch;
        std::vector<RuntimeWorldAudioAction> rehydrateActions;
    };
    static_assert(std::is_nothrow_constructible_v<HostTransaction, Candidate&>);
    static_assert(std::is_nothrow_move_assignable_v<std::vector<RuntimeWorldAudioAction>>);
    HostTransaction transaction(state);
    return executeRuntimeLoadTransaction(transaction, savePath, errorMessage, forceVisionEnabled);
}

std::uint64_t runtimeMultiplayerAuthoritativeStateHash(
    const RuntimeGameSessionState& state, std::string& errorMessage,
    std::vector<std::pair<std::string, std::uint64_t>>* trace) {
    return runtimeWorldSimulationHash(state,errorMessage,true,trace);
}

std::uint64_t runtimeStandaloneSimulationStateHash(
    const RuntimeGameSessionState& state, std::string& errorMessage,
    std::vector<std::pair<std::string, std::uint64_t>>* trace) {
    return runtimeWorldSimulationHash(state,errorMessage,state.config.multiplayerActive,trace);
}

bool writeRuntimeMultiplayerSnapshotHouseSections(
    const RuntimeGameSessionState& state,
    std::ostream& output,
    std::string& errorMessage) {
    std::vector<const RuntimeHumanHouseAuthority*> authorities;
    authorities.reserve(state.humanHouseAuthorities.size());
    for (const RuntimeHumanHouseAuthority& authority :
            state.humanHouseAuthorities) {
        authorities.push_back(&authority);
    }
    std::sort(
        authorities.begin(),
        authorities.end(),
        [](const RuntimeHumanHouseAuthority* left,
           const RuntimeHumanHouseAuthority* right) {
            return std::tie(left->canonicalOwner, left->slotIndex)
                < std::tie(right->canonicalOwner, right->slotIndex);
        }
    );
    if (authorities.empty() || authorities.size() > 8U) {
        errorMessage = "invalid multiplayer human authority count";
        return false;
    }

    output << "\n[MultiplayerHouses]\nSchema=1\nCount="
           << authorities.size() << "\n";
    for (std::size_t index = 0; index < authorities.size(); ++index) {
        const RuntimeHumanHouseAuthority& authority = *authorities[index];
        const RuntimeEconomyState* economy = authority.localFacade
            ? &state.economy
            : authority.remoteEconomy.has_value()
                ? &*authority.remoteEconomy
                : nullptr;
        const TechTreeReport* techTree = authority.localFacade
            ? &state.techTree
            : authority.remoteTechTree.has_value()
                ? &*authority.remoteTechTree
                : nullptr;
        const bool techLoaded = authority.localFacade
            ? state.techTreeLoaded
            : authority.remoteTechTreeLoaded;
        if (economy == nullptr || techTree == nullptr
            || runtimeCanonicalHumanHouseOwner(economy->owner)
                != authority.canonicalOwner) {
            errorMessage = "incomplete multiplayer human authority";
            return false;
        }

        std::vector<std::string> catalog;
        catalog.reserve(economy->catalog.size());
        for (const RuntimeBuildCatalogItem& item : economy->catalog) {
            catalog.push_back(runtimeMultiplayerSnapshotCatalogRecord(item));
        }
        std::sort(catalog.begin(), catalog.end());

        std::vector<std::string> history;
        for (const auto& [owner, byType] :
                economy->limitedBuildHistoryByOwner) {
            for (const auto& [typeId, count] : byType) {
                history.push_back(
                    iniHexEncode(owner) + "|" + iniHexEncode(typeId)
                        + "|" + std::to_string(count)
                );
            }
        }
        std::sort(history.begin(), history.end());

        const std::string section =
            "MultiplayerHouse" + std::to_string(index);
        output << "\n[" << section << "]\n"
               << "Schema=1\n"
               << "SlotIndex=" << authority.slotIndex << "\n"
               << "OwnerHex=" << iniHexEncode(authority.owner) << "\n"
               << "CountryHex=" << iniHexEncode(authority.country) << "\n"
               << "Economy="
               << runtimeMultiplayerSnapshotEconomyScalars(*economy)
               << "\n"
               << "ProductionDataHex="
               << runtimeHexEncode(
                    runtimeSerializeProductionLanes(
                        economy->localProductionLanes
                    )
                  ) << "\n"
               << "ProductionStateHash="
               << runtimeProductionLanesStateHash(
                    economy->localProductionLanes
                  ) << "\n"
               << "ProductionSelection=";
        if (economy->selectedPlacementLane.has_value()) {
            output << runtimeProductionLaneName(
                *economy->selectedPlacementLane
            ) << '|' << economy->selectedPlacementSerial;
        }
        output << "\nTechLoaded=" << runtimeBoolText(techLoaded)
               << "\nTechStateHash="
               << runtimeMultiplayerSnapshotTechTreeHash(
                    *techTree,
                    techLoaded
                  )
               << "\nCatalogCount=" << catalog.size() << "\n";
        for (std::size_t itemIndex = 0;
            itemIndex < catalog.size(); ++itemIndex) {
            output << "Catalog" << itemIndex << '='
                   << catalog[itemIndex] << "\n";
        }
        output << "HistoryCount=" << history.size() << "\n";
        for (std::size_t historyIndex = 0;
            historyIndex < history.size(); ++historyIndex) {
            output << "History" << historyIndex << '='
                   << history[historyIndex] << "\n";
        }
    }
    errorMessage.clear();
    return true;
}

bool materializeRuntimeMultiplayerSnapshotHouses(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage) {
    struct SeededTechTree {
        TechTreeReport report;
        bool loaded = false;
    };
    std::unordered_map<std::string, SeededTechTree> seededTechByOwner;
    std::string localOwner;
    for (const RuntimeHumanHouseAuthority& authority :
            state.humanHouseAuthorities) {
        SeededTechTree seeded;
        if (authority.localFacade) {
            if (!localOwner.empty()) {
                errorMessage = "multiple seeded local multiplayer facades";
                return false;
            }
            localOwner = authority.canonicalOwner;
            seeded.report = state.techTree;
            seeded.loaded = state.techTreeLoaded;
        } else if (authority.remoteTechTree.has_value()) {
            seeded.report = *authority.remoteTechTree;
            seeded.loaded = authority.remoteTechTreeLoaded;
        } else {
            errorMessage = "seeded multiplayer tech authority is incomplete";
            return false;
        }
        if (!seededTechByOwner.emplace(
                authority.canonicalOwner,
                std::move(seeded)
            ).second) {
            errorMessage = "duplicate seeded multiplayer authority";
            return false;
        }
    }
    if (localOwner.empty()) {
        errorMessage = "seeded local multiplayer facade is missing";
        return false;
    }

    RuntimeSavedProductionByOwner productionByOwner;
    if (!parseRuntimeV15ProductionByOwner(
            ini,
            productionByOwner,
            errorMessage
        )) {
        return false;
    }
    const int count = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerHouses", "Count", "-1"),
        -1
    );
    std::vector<RuntimeHumanHouseAuthority> authorities;
    std::unordered_map<std::string, RuntimeEconomyState> economies;
    std::unordered_map<std::string, SeededTechTree> restoredTech;
    authorities.reserve(static_cast<std::size_t>(std::max(0, count)));
    for (int index = 0; index < count; ++index) {
        const std::string section =
            "MultiplayerHouse" + std::to_string(index);
        std::uint64_t slotValue = 0;
        std::string owner;
        std::string country;
        bool techLoaded = false;
        std::uint64_t techHash = 0;
        if (!parseRuntimeUint64Strict(
                ini.getValue(section, "SlotIndex", ""),
                slotValue
            )
            || slotValue >= state.config.playerSlots.size()
            || !iniHexDecode(
                ini.getValue(section, "OwnerHex", ""),
                owner,
                256U
            )
            || !iniHexDecode(
                ini.getValue(section, "CountryHex", ""),
                country,
                256U
            )
            || !parseRuntimeStrictBool(
                ini.getValue(section, "TechLoaded", ""),
                techLoaded
            )
            || !parseRuntimeUint64Strict(
                ini.getValue(section, "TechStateHash", ""),
                techHash
            )) {
            errorMessage = "invalid multiplayer house identity";
            return false;
        }
        const std::size_t slotIndex = static_cast<std::size_t>(slotValue);
        const RuntimeConfig::PlayerSlot& slot =
            state.config.playerSlots[slotIndex];
        const std::string ownerKey = runtimeCanonicalHumanHouseOwner(owner);
        if (!slot.enabled || !slot.human
            || ownerKey != runtimeCanonicalHumanHouseOwner(
                runtimeSkirmishOwnerForSlot(slotIndex)
            )) {
            errorMessage = "multiplayer house does not match setup slot";
            return false;
        }
        const auto seededTech = seededTechByOwner.find(ownerKey);
        if (seededTech == seededTechByOwner.end()
            || seededTech->second.loaded != techLoaded
            || runtimeMultiplayerSnapshotTechTreeHash(
                seededTech->second.report,
                seededTech->second.loaded
            ) != techHash) {
            errorMessage = "multiplayer tech tree differs from setup";
            return false;
        }

        RuntimeEconomyState economy;
        initializeRuntimeLocalProductionLanes(economy);
        economy.owner = owner;
        // The ordinary save body belongs to the canonical facade selected
        // before loading. Per-house scalars do not carry this saved text.
        if (ownerKey == runtimeCanonicalHumanHouseOwner(state.economy.owner)) {
            economy.skirmishStatus = state.economy.skirmishStatus;
        }
        if (!parseRuntimeMultiplayerSnapshotEconomyScalars(
                ini.getValue(section, "Economy", ""),
                economy
            )) {
            errorMessage = "invalid multiplayer house economy";
            return false;
        }
        const auto production = productionByOwner.find(ownerKey);
        if (production == productionByOwner.end()) {
            errorMessage = "missing multiplayer house production";
            return false;
        }
        economy.localProductionLanes = production->second.lanes;
        economy.localProductionLanesInitialized = true;
        economy.selectedPlacementLane =
            production->second.selectedPlacementLane;
        economy.selectedPlacementSerial =
            production->second.selectedPlacementSerial;

        const int catalogCount = parseRuntimeIntOrDefault(
            ini.getValue(section, "CatalogCount", "-1"),
            -1
        );
        if (catalogCount < 0 || catalogCount > 100000) {
            errorMessage = "invalid multiplayer catalog count";
            return false;
        }
        std::unordered_set<std::string> catalogKeys;
        for (int itemIndex = 0; itemIndex < catalogCount; ++itemIndex) {
            RuntimeBuildCatalogItem item;
            if (!parseRuntimeMultiplayerSnapshotCatalogRecord(
                    ini.getValue(
                        section,
                        "Catalog" + std::to_string(itemIndex),
                        ""
                    ),
                    item
                )
                || !catalogKeys.insert(
                    std::to_string(static_cast<int>(item.kind)) + ":"
                        + runtimeMultiplayerCanonicalIdentifier(item.typeId)
                ).second) {
                errorMessage = "invalid or duplicate multiplayer catalog item";
                return false;
            }
            economy.catalog.push_back(std::move(item));
        }

        const int historyCount = parseRuntimeIntOrDefault(
            ini.getValue(section, "HistoryCount", "-1"),
            -1
        );
        if (historyCount < 0 || historyCount > 100000) {
            errorMessage = "invalid multiplayer BuildLimit history count";
            return false;
        }
        for (int historyIndex = 0;
            historyIndex < historyCount; ++historyIndex) {
            const std::vector<std::string> fields = splitRuntimeSaveFields(
                ini.getValue(
                    section,
                    "History" + std::to_string(historyIndex),
                    ""
                )
            );
            std::string historyOwner;
            std::string typeId;
            int historyValue = 0;
            if (fields.size() != 3U
                || !iniHexDecode(fields[0], historyOwner, 256U)
                || !iniHexDecode(fields[1], typeId, 256U)
                || !parseRuntimeInt(fields[2], historyValue)
                || historyValue < 0
                || trimAsciiLocal(historyOwner).empty()
                || trimAsciiLocal(typeId).empty()) {
                errorMessage = "invalid multiplayer BuildLimit history";
                return false;
            }
            auto& byType =
                economy.limitedBuildHistoryByOwner[historyOwner];
            if (!byType.emplace(typeId, historyValue).second) {
                errorMessage = "duplicate multiplayer BuildLimit history";
                return false;
            }
        }
        economy.status = "multiplayer snapshot restored";

        RuntimeHumanHouseAuthority authority;
        authority.slotIndex = slotIndex;
        authority.owner = owner;
        authority.canonicalOwner = ownerKey;
        authority.country = country;
        authority.localFacade = ownerKey == localOwner;
        if (!economies.emplace(ownerKey, std::move(economy)).second
            || !restoredTech.emplace(
                ownerKey,
                std::move(seededTech->second)
            ).second) {
            errorMessage = "duplicate multiplayer restored owner";
            return false;
        }
        authorities.push_back(std::move(authority));
    }
    if (authorities.size() != seededTechByOwner.size()
        || authorities.size() != productionByOwner.size()) {
        errorMessage = "multiplayer human authority set is incomplete";
        return false;
    }
    std::sort(
        authorities.begin(),
        authorities.end(),
        [](const RuntimeHumanHouseAuthority& left,
           const RuntimeHumanHouseAuthority& right) {
            return std::tie(left.canonicalOwner, left.slotIndex)
                < std::tie(right.canonicalOwner, right.slotIndex);
        }
    );
    for (RuntimeHumanHouseAuthority& authority : authorities) {
        auto economy = economies.find(authority.canonicalOwner);
        auto tech = restoredTech.find(authority.canonicalOwner);
        if (economy == economies.end() || tech == restoredTech.end()) {
            errorMessage = "multiplayer authority assembly failed";
            return false;
        }
        if (authority.localFacade) {
            state.economy = std::move(economy->second);
            state.techTree = std::move(tech->second.report);
            state.techTreeLoaded = tech->second.loaded;
        } else {
            authority.remoteEconomy.emplace(std::move(economy->second));
            authority.remoteTechTree.emplace(std::move(tech->second.report));
            authority.remoteTechTreeLoaded = tech->second.loaded;
        }
    }
    state.humanHouseAuthorities = std::move(authorities);
    state.humanHouseScopeActive = false;
    state.remoteHumanHouseScopeActive = false;

    for (const RuntimeHumanHouseAuthority& authority :
            state.humanHouseAuthorities) {
        RuntimeHumanHouseScope scope(state, authority.owner);
        if (!scope) {
            errorMessage = "could not scope restored multiplayer house";
            return false;
        }
        RuntimeSavedProductionCandidate candidate;
        candidate.lanes = state.economy.localProductionLanes;
        candidate.selectedPlacementLane = state.economy.selectedPlacementLane;
        candidate.selectedPlacementSerial = state.economy.selectedPlacementSerial;
        if (!validateRuntimeProductionCatalogState(
                state,
                candidate,
                errorMessage
            )) {
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

bool restoreRuntimeMultiplayerObjectPoses(RuntimeGameSessionState& state, const IniFile& ini, std::string& error) {
    if (ini.getSection("MultiplayerObjectPoses") == nullptr) return true; // Earlier snapshot images.
    int schema = 0, count = -1;
    const auto fail = [&]() { error = "invalid multiplayer object poses"; return false; };
    if (!parseRuntimeInt(ini.getValue("MultiplayerObjectPoses", "Schema", ""), schema) || schema != 1
        || !parseRuntimeInt(ini.getValue("MultiplayerObjectPoses", "Count", ""), count)
        || count < 0 || static_cast<std::size_t>(count) != state.world.getObjects().size()) return fail();
    std::unordered_map<int, WorldObjectInstance*> remaining;
    for (auto& object : state.world.accessObjects()) remaining.emplace(object.instanceId, &object);
    for (int index = 0; index < count; ++index) {
        const auto fields = splitRuntimeSaveFields(ini.getValue("MultiplayerObjectPoses", std::to_string(index), ""));
        int id = -1; bool hasPose = false, moving = false, onBridge = false;
        double x = 0, y = 0, level = 0;
        if (fields.size() != 7 || !parseRuntimeInt(fields[0], id)
            || !parseRuntimeStrictBool(fields[1], hasPose)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[2], x)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[3], y)
            || !parseRuntimeStrictBool(fields[4], moving)
            || !runtimeMultiplayerSnapshotDoubleFromBits(fields[5], level)
            || !parseRuntimeStrictBool(fields[6], onBridge)
            || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(level)
            || (moving && !hasPose)) return fail();
        const auto found = remaining.find(id);
        if (found == remaining.end()) return fail();
        auto& object = *found->second;
        object.hasRenderPosition = hasPose; object.renderX = x; object.renderY = y;
        object.renderMoveActive = moving; object.renderLevel = level; object.renderOnBridge = onBridge;
        remaining.erase(found);
    }
    return remaining.empty();
}

bool writeRuntimeMultiplayerSnapshotExtension(
    const RuntimeGameSessionState& state,
    std::ostream& output,
    std::string& errorMessage) {
    std::vector<std::pair<int, const RuntimeObjectBehavior*>> behaviors;
    behaviors.reserve(state.behaviors.size());
    for (const auto& [objectId, behavior] : state.behaviors) {
        behaviors.emplace_back(objectId, &behavior);
    }
    std::sort(behaviors.begin(), behaviors.end(), [](const auto& left,
                                                      const auto& right) {
        return left.first < right.first;
    });
    writeRuntimeMultiplayerObjectPoses(state, output);
    writeRuntimeTransportTransferPresentation(state, output, true);
    writeRuntimeInfantryWalks(state, output, true);
    output << "\n[MultiplayerBehaviors]\nSchema=1\nCount="
           << behaviors.size() << "\n";
    for (std::size_t index = 0; index < behaviors.size(); ++index) {
        output << index << '=' << runtimeMultiplayerSnapshotBehaviorRecord(
            behaviors[index].first,
            *behaviors[index].second
        ) << "\n";
    }

    output << "\n[MultiplayerPathAuthority]\nSchema=1\nPending="
           << runtimeMultiplayerSnapshotIntsText(
                state.pendingPathObjectIds,
                false
              )
           << "\nFailureCount=" << state.pathFailureCache.size() << "\n";
    std::vector<std::pair<std::uint64_t, RuntimePathFailureCacheEntry>> failures(
        state.pathFailureCache.begin(), state.pathFailureCache.end()
    );
    std::sort(failures.begin(), failures.end(), [](const auto& left,
                                                   const auto& right) {
        return left.first < right.first;
    });
    for (std::size_t index = 0; index < failures.size(); ++index) {
        const auto& [key, entry] = failures[index];
        output << "Failure" << index << '=' << key << '|'
               << entry.firstTick << '|' << entry.lastTick << '|'
               << entry.nextAllowedTick << '|' << entry.failureCount << "\n";
    }
    std::vector<std::pair<std::uint64_t, RuntimeAttackApproachCacheEntry>> approaches(
        state.attackApproachCache.begin(), state.attackApproachCache.end()
    );
    std::sort(approaches.begin(), approaches.end(), [](const auto& left,
                                                       const auto& right) {
        return left.first < right.first;
    });
    output << "ApproachCount=" << approaches.size() << "\n";
    for (std::size_t index = 0; index < approaches.size(); ++index) {
        const auto& [key, entry] = approaches[index];
        output << "Approach" << index << '=' << key << '|'
               << entry.tick << '|' << entry.targetObjectId << '|'
               << entry.rangeBucket << '|'
               << runtimeBoolText(entry.mapQueryApproachAvailable) << '|'
               << entry.contextHash << '|'
               << iniHexEncode(
                    runtimeMultiplayerSnapshotCellsText(entry.candidates)
                  ) << '|'
               << iniHexEncode(runtimeMultiplayerSnapshotCellsText(
                    entry.mapQueryCandidates
                  )) << "\n";
    }

    output << "\n[MultiplayerTransientEffects]\nSchema=1\n"
           << "RecentExpiredCount="
           << state.recentExpiredProjectiles.size() << "\n";
    for (std::size_t index = 0;
        index < state.recentExpiredProjectiles.size(); ++index) {
        output << "RecentExpired" << index << '='
               << iniHexEncode(serializeRuntimeProjectileEffect(
                    state.recentExpiredProjectiles[index]
                  )) << "\n";
    }
    output << "ImpactCount=" << state.impactEffects.size() << "\n";
    for (std::size_t index = 0; index < state.impactEffects.size(); ++index) {
        output << "Impact" << index << '='
               << runtimeMultiplayerSnapshotImpactRecord(
                    state.impactEffects[index]
                  ) << "\n";
        // These fields also select the authored crater/scorch animation.
        // Keep them when the exact snapshot replaces ordinary-save effects.
        const auto& impact = state.impactEffects[index];
        output << "ImpactPresentation" << index << '=' << iniHexEncode(impact.animId) << '|'
               << iniHexEncode(impact.renderIntent) << '|' << impact.frameIndex << '|'
               << impact.facing << '|' << impact.radius << '|'
               << runtimeBoolText(impact.bright) << '|' << iniHexEncode(impact.projectileImageId) << "\n";
    }

    const RuntimeTriggerState& triggers = state.triggers;
    output << "\n[MultiplayerTriggerAuthority]\nSchema=1\nHeader="
           << runtimeBoolText(triggers.loaded) << '|'
           << runtimeBoolText(triggers.bootstrapComplete) << '|'
           << triggers.triggerCount << '|' << triggers.eventCount << '|'
           << triggers.actionCount << '|' << triggers.tagCount << '|'
           << triggers.teamTypeCount << '|' << triggers.taskForceCount << '|'
           << triggers.scriptTypeCount << '|'
           << triggers.createTeamActionCount << '|'
           << triggers.firedTriggers << '|' << triggers.teamsCreated << '|'
           << triggers.unitsCreated << '|' << triggers.ordersIssued << '|'
           << triggers.scriptStepsIssued << '|'
           << triggers.scriptStepsCompleted << '|'
           << triggers.unsupportedScriptSteps << '|'
           << triggers.unsupportedActions << '|'
           << triggers.unsupportedEvents << "\n";
    std::vector<const RuntimeTriggerDefinition*> triggerDefinitions;
    triggerDefinitions.reserve(triggers.triggers.size());
    for (const RuntimeTriggerDefinition& trigger : triggers.triggers) {
        triggerDefinitions.push_back(&trigger);
    }
    std::sort(
        triggerDefinitions.begin(),
        triggerDefinitions.end(),
        [](const RuntimeTriggerDefinition* left,
           const RuntimeTriggerDefinition* right) {
            return runtimeMultiplayerCanonicalIdentifier(left->id)
                < runtimeMultiplayerCanonicalIdentifier(right->id);
        }
    );
    output << "TriggerCount=" << triggerDefinitions.size() << "\n";
    for (std::size_t index = 0;
        index < triggerDefinitions.size(); ++index) {
        const RuntimeTriggerDefinition& trigger = *triggerDefinitions[index];
        output << "Trigger" << index << '=' << iniHexEncode(trigger.id) << '|'
               << runtimeBoolText(trigger.fired) << '|'
               << runtimeBoolText(trigger.enabled) << '|'
               << runtimeBoolText(trigger.repeating) << '|'
               << trigger.activationTick << '|' << trigger.lastFiredTick << '|'
               << trigger.nextEligibleTick << '|' << trigger.tagRepeatType << '|'
               << iniHexEncode(runtimeMultiplayerSnapshotIntsText(
                    trigger.remainingOnceAllObjectIds
                  )) << "\n";
   }
   output << "\n[MultiplayerSubsystemFlags]\nSchema=1\n"
          << "Cloak=" << runtimeBoolText(state.cloakStateInitialized)
          << "\nVision=" << runtimeBoolText(state.visionEnabled)
          << "\nGates=" << runtimeBoolText(state.gatesInitialized)
          << "\nGarrisons=" << runtimeBoolText(state.garrisonsInitialized)
          << "\nSubcells=" << runtimeBoolText(state.subcellsInitialized)
          << "\nRepairDepots="
          << runtimeBoolText(state.repairDepotsInitialized)
          << "\nAircraftServices="
          << runtimeBoolText(state.aircraftServicesInitialized)
          << "\nSpecialMechanisms="
          << runtimeBoolText(state.specialMechanismsInitialized)
          << "\nSpies=" << runtimeBoolText(state.spiesInitialized)
          << "\nParticles=" << runtimeBoolText(state.particlesInitialized)
          << "\nSuperWeapons="
          << runtimeBoolText(state.superWeaponsInitialized) << "\n";
   std::vector<std::pair<std::string, RuntimeTerrainObjectInitialState>>
       initialTerrain(
           state.terrainObjectInitialStateByKey.begin(),
           state.terrainObjectInitialStateByKey.end()
       );
   std::sort(initialTerrain.begin(), initialTerrain.end(), [](const auto& left,
                                                              const auto& right) {
       return left.first < right.first;
   });
   output << "\n[MultiplayerWorldMetadata]\nSchema=1\nInitialOre="
          << runtimeBoolText(state.initialOreFieldsSeeded) << '|'
          << state.initialOreCellsSeeded << '|'
          << state.initialOreCellsDensified << "\nTerrainInitial="
          << runtimeBoolText(state.terrainObjectInitialStateCaptured) << '|'
          << state.terrainObjectInitialLayoutSignature << '|'
          << initialTerrain.size() << "\nBridgeFlags="
          << runtimeBoolText(state.destroyableBridges) << '|'
          << runtimeBoolText(state.bridgeSpecialFlagsLoaded) << "\n";
   for (std::size_t index = 0; index < initialTerrain.size(); ++index) {
       output << "TerrainInitial" << index << '='
              << iniHexEncode(initialTerrain[index].first) << '|'
              << initialTerrain[index].second.health << '|'
              << runtimeBoolText(initialTerrain[index].second.alive) << "\n";
   }
   const auto writeOutcomeSet = [&output](
        const char* prefix,
        const std::set<std::string>& values,
        bool canonicalOwner
    ) {
        std::vector<std::string> sorted;
        sorted.reserve(values.size());
        for (const std::string& value : values) {
            sorted.push_back(
                canonicalOwner
                    ? runtimeCanonicalHumanHouseOwner(value)
                    : runtimeNormalizeSkirmishTeam(value)
            );
        }
        std::sort(sorted.begin(), sorted.end());
        output << prefix << "Count=" << sorted.size() << "\n";
        for (std::size_t index = 0; index < sorted.size(); ++index) {
            output << prefix << index << '=' << iniHexEncode(sorted[index])
                   << "\n";
        }
    };
    output << "\n[MultiplayerOutcome]\nSchema=1\nMatchFinished="
           << runtimeBoolText(state.skirmishMatchFinished) << "\n";
    writeOutcomeSet("Defeated", state.multiplayerDefeatedOwners, true);
    writeOutcomeSet("Winner", state.multiplayerWinningOwners, true);
    writeOutcomeSet("WinningTeam", state.multiplayerWinningTeams, false);
    errorMessage.clear();
    return true;
}

bool materializeRuntimeMultiplayerSnapshotExtension(
    RuntimeProductionHostState& state,
    const IniFile& ini,
    std::string& errorMessage) {
    const int behaviorCount = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerBehaviors", "Count", "-1"),
        -1
    );
    if (behaviorCount < 0 || behaviorCount > 100000) {
        errorMessage = "invalid multiplayer behavior count";
        return false;
    }
    std::unordered_map<int, RuntimeObjectBehavior> behaviors;
    for (int index = 0; index < behaviorCount; ++index) {
        int objectId = -1;
        RuntimeObjectBehavior behavior;
        if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(
                ini.getValue(
                    "MultiplayerBehaviors",
                    std::to_string(index),
                    ""
                ),
                objectId,
                behavior
            )) {
            errorMessage = "invalid multiplayer behavior record";
            return false;
        }
        if (!runtimeOverlayWallBehaviorTargetIsValid(state, behavior)) {
            errorMessage = "invalid multiplayer overlay wall target";
            return false;
        }
        const auto* object = findRuntimeObjectByIdIncludingInactive(state, objectId);
        const auto ordinary = state.behaviors.find(objectId);
        const bool hasClearanceSource = behavior.factoryExitClearanceHostId >= 0
            || (ordinary != state.behaviors.end() && ordinary->second.factoryExitClearanceHostId >= 0);
        if (!object
            || (behavior.factoryExitClearanceHostId >= 0
                && !runtimeFactoryClearanceBehaviorIsValid(state, *object, behavior))
            || (hasClearanceSource && (ordinary == state.behaviors.end()
                || ordinary->second.factoryExitClearanceHostId != behavior.factoryExitClearanceHostId))
            || !behaviors.emplace(objectId, std::move(behavior)).second) {
            errorMessage = "invalid multiplayer factory clearance behavior";
            return false;
        }
    }
    for (const auto& [objectId, ordinary] : state.behaviors) {
        if (ordinary.factoryExitClearanceHostId < 0) continue;
        const auto restored = behaviors.find(objectId);
        if (restored == behaviors.end()
            || restored->second.factoryExitClearanceHostId != ordinary.factoryExitClearanceHostId) {
            errorMessage = "multiplayer factory clearance source differs from ordinary behavior";
            return false;
        }
    }

    std::vector<int> pendingPaths;
    if (!parseRuntimeMultiplayerSnapshotIntsText(
            ini.getValue("MultiplayerPathAuthority", "Pending", ""),
            pendingPaths
        )) {
        errorMessage = "invalid multiplayer pending path queue";
        return false;
    }
    std::unordered_set<int> pendingPathIds;
    for (int objectId : pendingPaths) {
        if (behaviors.find(objectId) == behaviors.end()
            || !pendingPathIds.insert(objectId).second) {
            errorMessage = "invalid multiplayer pending path object";
            return false;
        }
    }

    const int failureCount = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerPathAuthority", "FailureCount", "-1"),
        -1
    );
    if (failureCount < 0 || failureCount > 100000) {
        errorMessage = "invalid multiplayer path failure count";
        return false;
    }
    std::unordered_map<std::uint64_t, RuntimePathFailureCacheEntry> failures;
    for (int index = 0; index < failureCount; ++index) {
        const std::vector<std::string> fields = splitRuntimeSaveFields(
            ini.getValue(
                "MultiplayerPathAuthority",
                "Failure" + std::to_string(index),
                ""
            )
        );
        std::uint64_t key = 0;
        RuntimePathFailureCacheEntry entry;
        if (fields.size() != 5U
            || !parseRuntimeUint64Strict(fields[0], key)
            || key == 0U
            || !parseRuntimeInt(fields[1], entry.firstTick)
            || !parseRuntimeInt(fields[2], entry.lastTick)
            || !parseRuntimeInt(fields[3], entry.nextAllowedTick)
            || !parseRuntimeInt(fields[4], entry.failureCount)
            || entry.failureCount <= 0
            || entry.firstTick < 0
            || entry.lastTick < entry.firstTick
            || entry.nextAllowedTick < entry.lastTick
            || !failures.emplace(key, entry).second) {
            errorMessage = "invalid multiplayer path failure record";
            return false;
        }
    }

    const int approachCount = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerPathAuthority", "ApproachCount", "-1"),
        -1
    );
    if (approachCount < 0 || approachCount > 100000) {
        errorMessage = "invalid multiplayer attack approach count";
        return false;
    }
    std::unordered_map<std::uint64_t, RuntimeAttackApproachCacheEntry>
        approaches;
    for (int index = 0; index < approachCount; ++index) {
        const std::vector<std::string> fields = splitRuntimeSaveFields(
            ini.getValue(
                "MultiplayerPathAuthority",
                "Approach" + std::to_string(index),
                ""
            )
        );
        std::uint64_t key = 0;
        RuntimeAttackApproachCacheEntry entry;
        std::string candidateText;
        std::string mapQueryText;
        if (fields.size() != 8U
            || !parseRuntimeUint64Strict(fields[0], key)
            || key == 0U
            || !parseRuntimeInt(fields[1], entry.tick)
            || !parseRuntimeInt(fields[2], entry.targetObjectId)
            || !parseRuntimeInt(fields[3], entry.rangeBucket)
            || !parseRuntimeStrictBool(
                fields[4],
                entry.mapQueryApproachAvailable
            )
            || !parseRuntimeUint64Strict(fields[5], entry.contextHash)
            || !iniHexDecode(fields[6], candidateText)
            || !iniHexDecode(fields[7], mapQueryText)
            || !parseRuntimeMultiplayerSnapshotCellsText(
                candidateText,
                entry.candidates
            )
            || !parseRuntimeMultiplayerSnapshotCellsText(
                mapQueryText,
                entry.mapQueryCandidates
            )
            || !approaches.emplace(key, std::move(entry)).second) {
            errorMessage = "invalid multiplayer attack approach record";
            return false;
        }
    }

    const int expiredCount = parseRuntimeIntOrDefault(
        ini.getValue(
            "MultiplayerTransientEffects",
            "RecentExpiredCount",
            "-1"
        ),
        -1
    );
    if (expiredCount < 0 || expiredCount > 32) {
        errorMessage = "invalid multiplayer expired projectile count";
        return false;
    }
    std::deque<RuntimeProjectileEffect> expiredProjectiles;
    std::unordered_set<int> projectileIds;
    for (const RuntimeProjectileEffect& projectile : state.projectiles) {
        projectileIds.insert(projectile.actorId);
    }
    for (int index = 0; index < expiredCount; ++index) {
        std::string serialized;
        RuntimeProjectileEffect projectile;
        if (!iniHexDecode(
                ini.getValue(
                    "MultiplayerTransientEffects",
                    "RecentExpired" + std::to_string(index),
                    ""
                ),
                serialized
            )
            || !parseRuntimeProjectileEffect(
                serialized,
                projectile,
                runtimeProjectileSaveSchemaV6,
                true
            )
            || projectile.lifecycle
                != RuntimeProjectileLifecycleState::Expired
            || !projectileIds.insert(projectile.actorId).second) {
            errorMessage = "invalid multiplayer expired projectile record";
            return false;
        }
        expiredProjectiles.push_back(std::move(projectile));
    }

    const int impactCount = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerTransientEffects", "ImpactCount", "-1"),
        -1
    );
    if (impactCount < 0 || impactCount > 100000) {
        errorMessage = "invalid multiplayer impact count";
        return false;
    }
    std::vector<RuntimeImpactEffect> impacts;
    impacts.reserve(static_cast<std::size_t>(impactCount));
    for (int index = 0; index < impactCount; ++index) {
        RuntimeImpactEffect impact;
        if (!parseRuntimeMultiplayerSnapshotImpactRecord(
                ini.getValue(
                    "MultiplayerTransientEffects",
                    "Impact" + std::to_string(index),
                    ""
                ),
                impact
            )) {
            errorMessage = "invalid multiplayer impact record";
            return false;
        }
        const auto presentation = ini.getValue("MultiplayerTransientEffects",
            "ImpactPresentation" + std::to_string(index), "");
        if (!presentation.empty()) {
            const auto fields = splitRuntimeSaveFields(presentation);
            if (fields.size() != 7U
                || !iniHexDecode(fields[0], impact.animId, 256U)
                || !iniHexDecode(fields[1], impact.renderIntent, 256U)
                || !parseRuntimeInt(fields[2], impact.frameIndex)
                || !parseRuntimeInt(fields[3], impact.facing)
                || !parseRuntimeInt(fields[4], impact.radius)
                || !parseRuntimeStrictBool(fields[5], impact.bright)
                || !iniHexDecode(fields[6], impact.projectileImageId, 256U)
                || impact.frameIndex < -1 || impact.frameIndex > 1000000
                || impact.facing < -1 || impact.facing > 255
                || impact.radius < 0 || impact.radius > 4096) {
                errorMessage = "invalid multiplayer impact presentation";
                return false;
            }
        }
        impacts.push_back(std::move(impact));
    }

    const std::vector<std::string> header = splitRuntimeSaveFields(
        ini.getValue("MultiplayerTriggerAuthority", "Header", "")
    );
    bool loaded = false;
    bool bootstrapComplete = false;
    std::array<int, 17U> triggerNumbers{};
    if (header.size() != 19U
        || !parseRuntimeStrictBool(header[0], loaded)
        || !parseRuntimeStrictBool(header[1], bootstrapComplete)) {
        errorMessage = "invalid multiplayer trigger header";
        return false;
    }
    for (std::size_t index = 0; index < triggerNumbers.size(); ++index) {
        if (!parseRuntimeInt(header[index + 2U], triggerNumbers[index])) {
            errorMessage = "invalid multiplayer trigger counter";
            return false;
        }
    }
    RuntimeTriggerState& triggers = state.triggers;
    if (triggerNumbers[0] != static_cast<int>(triggers.triggers.size())
        || triggerNumbers[1] != triggers.eventCount
        || triggerNumbers[2] != triggers.actionCount
        || triggerNumbers[3] != triggers.tagCount
        || triggerNumbers[4] != triggers.teamTypeCount
        || triggerNumbers[5] != triggers.taskForceCount
        || triggerNumbers[6] != triggers.scriptTypeCount
        || triggerNumbers[7] != triggers.createTeamActionCount) {
        errorMessage = "multiplayer trigger setup differs from snapshot";
        return false;
    }
    const int triggerStateCount = parseRuntimeIntOrDefault(
        ini.getValue("MultiplayerTriggerAuthority", "TriggerCount", "-1"),
        -1
    );
    if (triggerStateCount != static_cast<int>(triggers.triggers.size())) {
        errorMessage = "multiplayer trigger authority set is incomplete";
        return false;
    }
    std::unordered_set<std::string> restoredTriggerIds;
    for (int index = 0; index < triggerStateCount; ++index) {
        const std::vector<std::string> fields = splitRuntimeSaveFields(
            ini.getValue(
                "MultiplayerTriggerAuthority",
                "Trigger" + std::to_string(index),
                ""
            )
        );
        std::string triggerId;
        bool fired = false;
        bool enabled = false;
        bool repeating = false;
        int activationTick = 0;
        int lastFiredTick = -1;
        int nextEligibleTick = 0;
        int tagRepeatType = 0;
        std::string remainingText;
        std::vector<int> remaining;
        if (fields.size() != 9U
            || !iniHexDecode(fields[0], triggerId, 256U)
            || !parseRuntimeStrictBool(fields[1], fired)
            || !parseRuntimeStrictBool(fields[2], enabled)
            || !parseRuntimeStrictBool(fields[3], repeating)
            || !parseRuntimeInt(fields[4], activationTick)
            || !parseRuntimeInt(fields[5], lastFiredTick)
            || !parseRuntimeInt(fields[6], nextEligibleTick)
            || !parseRuntimeInt(fields[7], tagRepeatType)
            || !iniHexDecode(fields[8], remainingText)
            || !parseRuntimeMultiplayerSnapshotIntsText(
                remainingText,
                remaining
            )) {
            errorMessage = "invalid multiplayer trigger record";
            return false;
        }
        RuntimeTriggerDefinition* trigger = runtimeCampaignTriggerById(
            triggers,
            triggerId
        );
        const std::string triggerKey =
            runtimeMultiplayerCanonicalIdentifier(triggerId);
        if (trigger == nullptr
            || !restoredTriggerIds.insert(triggerKey).second
            || tagRepeatType != trigger->tagRepeatType) {
            errorMessage = "multiplayer trigger identity differs from setup";
            return false;
        }
        trigger->fired = fired;
        trigger->enabled = enabled;
        trigger->repeating = repeating;
        trigger->activationTick = activationTick;
        trigger->lastFiredTick = lastFiredTick;
        trigger->nextEligibleTick = nextEligibleTick;
        trigger->remainingOnceAllObjectIds.clear();
        for (int objectId : remaining) {
            if (objectId < 0
                || !trigger->remainingOnceAllObjectIds.insert(objectId).second) {
                errorMessage = "invalid multiplayer trigger target set";
                return false;
            }
        }
    }
    triggers.loaded = loaded;
    triggers.bootstrapComplete = bootstrapComplete;
    triggers.triggerCount = triggerNumbers[0];
    triggers.eventCount = triggerNumbers[1];
    triggers.actionCount = triggerNumbers[2];
    triggers.tagCount = triggerNumbers[3];
    triggers.teamTypeCount = triggerNumbers[4];
    triggers.taskForceCount = triggerNumbers[5];
    triggers.scriptTypeCount = triggerNumbers[6];
    triggers.createTeamActionCount = triggerNumbers[7];
    triggers.firedTriggers = triggerNumbers[8];
    triggers.teamsCreated = triggerNumbers[9];
    triggers.unitsCreated = triggerNumbers[10];
    triggers.ordersIssued = triggerNumbers[11];
    triggers.scriptStepsIssued = triggerNumbers[12];
    triggers.scriptStepsCompleted = triggerNumbers[13];
    triggers.unsupportedScriptSteps = triggerNumbers[14];
    triggers.unsupportedActions = triggerNumbers[15];
    triggers.unsupportedEvents = triggerNumbers[16];

    bool cloakInitialized = false;
    bool visionEnabled = false;
    bool gatesInitialized = false;
    bool garrisonsInitialized = false;
    bool subcellsInitialized = false;
    bool repairDepotsInitialized = false;
    bool aircraftServicesInitialized = false;
    bool specialMechanismsInitialized = false;
    bool spiesInitialized = false;
    bool particlesInitialized = false;
    bool superWeaponsInitialized = false;
    const auto readSubsystemFlag = [&ini](
        const char* key,
        bool& value
    ) {
        return parseRuntimeStrictBool(
            ini.getValue("MultiplayerSubsystemFlags", key, ""),
            value
        );
    };
    if (!readSubsystemFlag("Cloak", cloakInitialized)
        || !readSubsystemFlag("Vision", visionEnabled)
        || !readSubsystemFlag("Gates", gatesInitialized)
        || !readSubsystemFlag("Garrisons", garrisonsInitialized)
        || !readSubsystemFlag("Subcells", subcellsInitialized)
        || !readSubsystemFlag("RepairDepots", repairDepotsInitialized)
        || !readSubsystemFlag(
            "AircraftServices",
            aircraftServicesInitialized
        )
        || !readSubsystemFlag(
            "SpecialMechanisms",
            specialMechanismsInitialized
        )
        || !readSubsystemFlag("Spies", spiesInitialized)
        || !readSubsystemFlag("Particles", particlesInitialized)
        || !readSubsystemFlag("SuperWeapons", superWeaponsInitialized)) {
        errorMessage = "invalid multiplayer subsystem flags";
        return false;
    }

    const std::vector<std::string> initialOre = splitRuntimeSaveFields(
        ini.getValue("MultiplayerWorldMetadata", "InitialOre", "")
    );
    bool initialOreFieldsSeeded = false;
    int initialOreCellsSeeded = 0;
    int initialOreCellsDensified = 0;
    const std::vector<std::string> initialTerrainHeader =
        splitRuntimeSaveFields(
            ini.getValue(
                "MultiplayerWorldMetadata",
                "TerrainInitial",
                ""
            )
        );
    bool terrainInitialCaptured = false;
    std::uint64_t terrainInitialSignature = 0;
    int terrainInitialCount = -1;
    const std::vector<std::string> bridgeFlags = splitRuntimeSaveFields(
        ini.getValue("MultiplayerWorldMetadata", "BridgeFlags", "")
    );
    bool destroyableBridges = false;
    bool bridgeSpecialFlagsLoaded = false;
    if (initialOre.size() != 3U
        || !parseRuntimeStrictBool(initialOre[0], initialOreFieldsSeeded)
        || !parseRuntimeInt(initialOre[1], initialOreCellsSeeded)
        || !parseRuntimeInt(initialOre[2], initialOreCellsDensified)
        || initialOreCellsSeeded < 0 || initialOreCellsDensified < 0
        || initialTerrainHeader.size() != 3U
        || !parseRuntimeStrictBool(
            initialTerrainHeader[0],
            terrainInitialCaptured
        )
        || !parseRuntimeUint64Strict(
            initialTerrainHeader[1],
            terrainInitialSignature
        )
        || !parseRuntimeInt(initialTerrainHeader[2], terrainInitialCount)
        || terrainInitialCount < 0 || terrainInitialCount > 100000
        || bridgeFlags.size() != 2U
        || !parseRuntimeStrictBool(bridgeFlags[0], destroyableBridges)
        || !parseRuntimeStrictBool(
            bridgeFlags[1],
            bridgeSpecialFlagsLoaded
        )) {
        errorMessage = "invalid multiplayer world metadata";
        return false;
    }
    std::unordered_map<std::string, RuntimeTerrainObjectInitialState>
        initialTerrain;
    for (int index = 0; index < terrainInitialCount; ++index) {
        const std::vector<std::string> fields = splitRuntimeSaveFields(
            ini.getValue(
                "MultiplayerWorldMetadata",
                "TerrainInitial" + std::to_string(index),
                ""
            )
        );
        std::string key;
        RuntimeTerrainObjectInitialState initial;
        if (fields.size() != 3U
            || !iniHexDecode(fields[0], key, 4096U)
            || trimAsciiLocal(key).empty()
            || !parseRuntimeInt(fields[1], initial.health)
            || initial.health < 0
            || !parseRuntimeStrictBool(fields[2], initial.alive)
            || (initial.health == 0 && initial.alive)
            || !initialTerrain.emplace(key, initial).second) {
            errorMessage = "invalid multiplayer terrain baseline record";
            return false;
        }
    }

    bool matchFinished = false;
    if (!parseRuntimeStrictBool(
            ini.getValue("MultiplayerOutcome", "MatchFinished", ""),
            matchFinished
        )) {
        errorMessage = "invalid multiplayer outcome header";
        return false;
    }
    const auto parseOutcomeSet = [&ini, &errorMessage](
        const char* prefix,
        bool canonicalOwner,
        std::set<std::string>& values
    ) {
        const int count = parseRuntimeIntOrDefault(
            ini.getValue(
                "MultiplayerOutcome",
                std::string(prefix) + "Count",
                "-1"
            ),
            -1
        );
        if (count < 0 || count > 8) {
            errorMessage = "invalid multiplayer outcome count";
            return false;
        }
        std::set<std::string> parsed;
        for (int index = 0; index < count; ++index) {
            std::string value;
            if (!iniHexDecode(
                    ini.getValue(
                        "MultiplayerOutcome",
                        std::string(prefix) + std::to_string(index),
                        ""
                    ),
                    value,
                    256U
                )) {
                errorMessage = "invalid multiplayer outcome value";
                return false;
            }
            value = canonicalOwner
                ? runtimeCanonicalHumanHouseOwner(value)
                : runtimeNormalizeSkirmishTeam(value);
            if (value.empty() || !parsed.insert(value).second) {
                errorMessage = "duplicate multiplayer outcome value";
                return false;
            }
        }
        values = std::move(parsed);
        return true;
    };
    std::set<std::string> defeated;
    std::set<std::string> winners;
    std::set<std::string> winningTeams;
    if (!parseOutcomeSet("Defeated", true, defeated)
        || !parseOutcomeSet("Winner", true, winners)
        || !parseOutcomeSet("WinningTeam", false, winningTeams)) {
        return false;
    }

    state.behaviors = std::move(behaviors);
    state.pendingPathObjectIds.assign(
        pendingPaths.begin(),
        pendingPaths.end()
    );
    state.pathFailureCache = std::move(failures);
    state.attackApproachCache = std::move(approaches);
    state.recentExpiredProjectiles = std::move(expiredProjectiles);
    state.impactEffects = std::move(impacts);
    state.initialOreFieldsSeeded = initialOreFieldsSeeded;
    state.initialOreCellsSeeded = initialOreCellsSeeded;
    state.initialOreCellsDensified = initialOreCellsDensified;
    state.terrainObjectInitialStateCaptured = terrainInitialCaptured;
    state.terrainObjectInitialLayoutSignature = terrainInitialSignature;
    state.terrainObjectInitialStateByKey = std::move(initialTerrain);
    state.destroyableBridges = destroyableBridges;
    state.bridgeSpecialFlagsLoaded = bridgeSpecialFlagsLoaded;
    state.cloakStateInitialized = cloakInitialized;
    state.visionEnabled = visionEnabled;
    state.gatesInitialized = gatesInitialized;
    state.garrisonsInitialized = garrisonsInitialized;
    state.subcellsInitialized = subcellsInitialized;
    state.repairDepotsInitialized = repairDepotsInitialized;
    state.aircraftServicesInitialized = aircraftServicesInitialized;
    state.specialMechanismsInitialized = specialMechanismsInitialized;
    state.spiesInitialized = spiesInitialized;
    state.particlesInitialized = particlesInitialized;
    state.superWeaponsInitialized = superWeaponsInitialized;
    state.skirmishMatchFinished = matchFinished;
    state.multiplayerDefeatedOwners = std::move(defeated);
    state.multiplayerWinningOwners = std::move(winners);
    state.multiplayerWinningTeams = std::move(winningTeams);
    // The multiplayer behavior records replace the ordinary loaded behaviors.
    // Restore in-progress infantry and vehicle steps after that replacement, before
    // applying the exact snapshot pose used by targeting and vision.
    if (!loadRuntimeTransportTransferPresentation(state, ini, errorMessage, true)) return false;
    const bool hasUnitTransportBoarding = std::any_of(state.pendingTransportTargets.begin(),
        state.pendingTransportTargets.end(), [&](const auto& intent) {
            const auto* host = findObjectById(state, intent.second);
            return host && host->kind == WorldObjectKind::Unit;
        });
    if (hasUnitTransportBoarding) {
        if (!loadRuntimeVehicleArrivals(state, ini, errorMessage)) return false;
        if (!loadRuntimeInfantryWalks(state, ini, errorMessage,
                ini.getSection("MultiplayerMovementSteps") != nullptr)) return false;
    } else {
        if (!loadRuntimeInfantryWalks(state, ini, errorMessage,
                ini.getSection("MultiplayerMovementSteps") != nullptr)) return false;
        if (!loadRuntimeVehicleArrivals(state, ini, errorMessage)) return false;
    }
    if (!validateRuntimeTransportTransferPresentation(state, errorMessage)) return false;
    if (!loadRuntimeRepackMoveGroups(state, ini, errorMessage, true)) return false;
    if (!restoreRuntimeMultiplayerObjectPoses(state, ini, errorMessage)) return false;
    refreshRuntimeRepairDepotDockPoses(state);
    std::vector<RuntimeCarrierFlightPoseRecord> carrierPoses;
    if (!parseRuntimeCarrierFlightPoses(state, ini, carrierPoses, errorMessage)) return false;
    restoreRuntimeCarrierFlightSegments(state, carrierPoses);
    errorMessage.clear();
    return true;
}

bool writeRuntimeMultiplayerSnapshotCanonicalIni(
    const IniFile& ini,
    std::ostream& output,
    std::string& errorMessage,
    bool omitRuntimeSave) {
    std::vector<std::string> sections = ini.getSectionNames();
    std::sort(sections.begin(), sections.end(), [](const std::string& left,
                                                   const std::string& right) {
        return std::make_tuple(toUpperAsciiLocal(left), left)
            < std::make_tuple(toUpperAsciiLocal(right), right);
    });
    std::unordered_set<std::string> sectionKeys;
    for (const std::string& sectionName : sections) {
        if (omitRuntimeSave
            && toUpperAsciiLocal(trimAsciiLocal(sectionName))
                == "RUNTIMESAVE") {
            continue;
        }
        if (runtimeMultiplayerSnapshotSectionExcluded(sectionName)) {
            continue;
        }
        const std::string canonicalSection =
            toUpperAsciiLocal(trimAsciiLocal(sectionName));
        if (canonicalSection.empty()
            || !sectionKeys.insert(canonicalSection).second) {
            errorMessage = "duplicate canonical snapshot section";
            return false;
        }
        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) {
            errorMessage = "snapshot section disappeared during serialization";
            return false;
        }
        std::vector<IniFile::KeyValue> ordinary;
        std::vector<std::string> numericRecords;
        std::unordered_set<std::string> keySet;
        for (const auto& [key, value] : *section) {
            const std::string canonicalKey =
                toUpperAsciiLocal(trimAsciiLocal(key));
            if (canonicalKey.empty()
                || !keySet.insert(canonicalKey).second) {
                errorMessage = "duplicate canonical snapshot key";
                return false;
            }
            int numericIndex = -1;
            if (!runtimeMultiplayerSnapshotSequenceSection(sectionName)
                && runtimeMultiplayerSnapshotNumericRecordKey(
                    key,
                    numericIndex
                )) {
                numericRecords.push_back(value);
            } else {
                ordinary.emplace_back(key, value);
            }
        }
        std::sort(
            ordinary.begin(),
            ordinary.end(),
            [](const auto& left, const auto& right) {
                return std::make_tuple(
                        toUpperAsciiLocal(left.first),
                        left.first
                    ) < std::make_tuple(
                        toUpperAsciiLocal(right.first),
                        right.first
                    );
            }
        );
        std::sort(numericRecords.begin(), numericRecords.end());
        output << '[' << sectionName << "]\n";
        for (const auto& [key, value] : ordinary) {
            output << key << '=' << value << "\n";
        }
        for (std::size_t index = 0; index < numericRecords.size(); ++index) {
            output << index << '=' << numericRecords[index] << "\n";
        }
        output << "\n";
    }
    errorMessage.clear();
    return true;
}

bool exportRuntimeMultiplayerSnapshotV15(
    const RuntimeProductionHostState& live,
    const RuntimeMultiplayerSnapshotIdentity& identity,
    RuntimeMultiplayerSnapshotMemoryImage& image,
    std::string& errorMessage) {
    std::string hashError;
    const std::uint64_t authoritativeHash =
        runtimeMultiplayerAuthoritativeStateHash(live, hashError);
    if (authoritativeHash == 0U) {
        errorMessage = "snapshot authority is unstable: " + hashError;
        return false;
    }
    if (identity.sourceTimelineEpoch
            != live.config.multiplayerTimelineEpoch
        || identity.snapshotTick != live.tick
        || identity.gameplaySetupFingerprint
            != live.config.multiplayerSetupHash
        || identity.authoritativeStateHash != authoritativeHash) {
        errorMessage = "snapshot identity does not match live authority";
        return false;
    }

    try {
        auto detached = std::make_unique<RuntimeProductionHostState>(*live.constructionUiClock, *live.constructionAudioClock, live.geometry);
        seedRuntimeGrindingTransactionScratch(*detached, live);
        detached->tick = live.tick;
        detached->skirmishCrates = live.skirmishCrates;
        detached->config = live.config;
        detached->config.defaultMapPath = "multiplayer-map";
        detached->campaignMode = false;
        detached->reconcilingMagBeams = false;
        detached->applyingGrindingActions = false;
        detached->drainingObjectExits = false;
        detached->gates = live.gates;
        detached->gatesInitialized = live.gatesInitialized;
        detached->gateIdsByCell = live.gateIdsByCell;
        detached->gateTopologyFingerprint = live.gateTopologyFingerprint;
        detached->gateStrictOpenFingerprint =
            live.gateStrictOpenFingerprint;
        detached->worldAudio = live.worldAudio;
        detached->worldAudioInitialized = live.worldAudioInitialized;
        detached->lighting = live.lighting;
        detached->lightingInitialized = live.lightingInitialized;
        detached->activeLightingTokens = live.activeLightingTokens;
        detached->particles = live.particles;
        detached->particlesInitialized = live.particlesInitialized;
        detached->superWeapons = live.superWeapons;
        detached->superWeaponsInitialized = live.superWeaponsInitialized;
        detached->humanHouseAuthorities = live.humanHouseAuthorities;
        detached->techTree = live.techTree;
        detached->techTreeLoaded = live.techTreeLoaded;
        detached->multiplayerDefeatedOwners =
            live.multiplayerDefeatedOwners;
        detached->multiplayerWinningOwners =
            live.multiplayerWinningOwners;
        detached->multiplayerWinningTeams = live.multiplayerWinningTeams;

        std::vector<std::string> owners;
        owners.reserve(detached->humanHouseAuthorities.size());
        for (const RuntimeHumanHouseAuthority& authority :
                detached->humanHouseAuthorities) {
            owners.push_back(authority.canonicalOwner);
        }
        std::sort(owners.begin(), owners.end());
        if (owners.empty()
            || !runtimeMultiplayerSnapshotRebindHumanFacade(
                *detached,
                owners.front(),
                errorMessage
            )) {
            if (errorMessage.empty()) {
                errorMessage = "snapshot has no canonical human facade";
            }
            return false;
        }

        std::ostringstream legacy;
        if (!writeRuntimeStateToStream(*detached, legacy, errorMessage)) {
            return false;
        }
        IniFile legacyIni;
        if (!parseRuntimeSaveDocument(legacy.str(), legacyIni)) {
            errorMessage = "could not parse detached runtime save";
            return false;
        }

        std::ostringstream assembled;
        assembled << "[RuntimeSave]\nVersion=15\nTick="
                  << detached->tick << "\nInitialOreFieldsSeeded="
                  << runtimeBoolText(detached->initialOreFieldsSeeded)
                  << "\nInitialOreCellsSeeded="
                  << detached->initialOreCellsSeeded
                  << "\nInitialOreCellsDensified="
                  << detached->initialOreCellsDensified << "\n\n";
        assembled << "[MultiplayerSnapshot]\nSchema=15\nKind="
                  << runtimeMultiplayerSnapshotKindName(identity.kind)
                  << "\nProposalId=" << identity.proposalId
                  << "\nSnapshotTokenHex="
                  << iniHexEncode(identity.snapshotToken)
                  << "\nSourceTimelineEpoch="
                  << identity.sourceTimelineEpoch
                  << "\nBarrierTick=" << identity.barrierTick
                  << "\nSnapshotTick=" << identity.snapshotTick
                  << "\nGameplaySetupFingerprint="
                  << identity.gameplaySetupFingerprint
                  << "\nCanonicalMapIdHex="
                  << iniHexEncode(runtimeMultiplayerCanonicalIdentifier(
                        live.config.multiplayerCanonicalMapId
                     ))
                  << "\nMapContentFingerprint="
                  << live.config.multiplayerMapContentFingerprint
                  << "\nFixedTickHz=" << live.config.fixedTickHz
                  << "\nSimulationRandomSeed="
                  << live.config.simulationRandomSeed
                  << "\nAuthoritativeStateHash="
                  << authoritativeHash << "\n\n";
        if (!writeRuntimeMultiplayerSnapshotCanonicalIni(
                legacyIni,
                assembled,
                errorMessage,
                true
            )
            || !writeRuntimeMultiplayerSnapshotHouseSections(
                *detached,
                assembled,
                errorMessage
            )
            || !writeRuntimeMultiplayerSnapshotExtension(
                *detached,
                assembled,
                errorMessage
            )) {
            return false;
        }

        if (identity.kind == RuntimeMultiplayerSnapshotKind::Save && live.multiplayerBridge) {
            const auto& session = live.multiplayerBridge->lan().session();
            std::string metadata;
            if (!session.matchSetup() || !writeRuntimeMultiplayerResumeMetadata(
                *session.matchSetup(), session.config().compatibility, metadata, errorMessage)) {
                if (errorMessage.empty()) errorMessage = "live multiplayer room recipe is unavailable";
                return false;
            }
            assembled << metadata;
        }
        IniFile assembledIni;
        if (!parseRuntimeSaveDocument(assembled.str(), assembledIni)) {
            errorMessage = "could not parse assembled multiplayer snapshot";
            return false;
        }
        std::ostringstream canonical;
        if (!writeRuntimeMultiplayerSnapshotCanonicalIni(
                assembledIni,
                canonical,
                errorMessage
            )) {
            return false;
        }
        const std::string canonicalText = canonical.str();
        std::vector<std::uint8_t> bytes(
            canonicalText.begin(),
            canonicalText.end()
        );
        RuntimeMultiplayerSnapshotDescriptor descriptor;
        if (!makeRuntimeMultiplayerSnapshotDescriptor(
                identity,
                bytes,
                descriptor,
                errorMessage
            )) {
            return false;
        }
        RuntimeMultiplayerSnapshotMemoryImage candidate;
        candidate.bytes = std::move(bytes);
        candidate.descriptor = std::move(descriptor);
        image = std::move(candidate);
        errorMessage.clear();
        return true;
    } catch (const std::exception& exception) {
        errorMessage = exception.what();
        return false;
    }
}

bool validateRuntimeMultiplayerSnapshotEnvelope(
    const RuntimeGameSessionState& live,
    const IniFile& ini,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    std::string& errorMessage) {
    const RuntimeMultiplayerSnapshotIdentity& identity = descriptor.identity;
    std::string snapshotToken;
    std::string canonicalMapId;
    std::uint64_t proposalId = 0;
    std::uint64_t sourceEpoch = 0;
    std::uint64_t setupFingerprint = 0;
    std::uint64_t mapFingerprint = 0;
    std::uint64_t simulationSeed = 0;
    std::uint64_t authoritativeHash = 0;
    int barrierTick = -1;
    int snapshotTick = -1;
    int fixedTickHz = 0;
    const std::string expectedKind =
        runtimeMultiplayerSnapshotKindName(identity.kind);
    if (parseRuntimeIntOrDefault(
            ini.getValue("MultiplayerSnapshot", "Schema", "-1"),
            -1
        ) != 15
        || trimAsciiLocal(
            ini.getValue("MultiplayerSnapshot", "Kind", "")
        ) != expectedKind
        || !parseRuntimeUint64Strict(
            ini.getValue("MultiplayerSnapshot", "ProposalId", ""),
            proposalId
        )
        || !iniHexDecode(
            ini.getValue("MultiplayerSnapshot", "SnapshotTokenHex", ""),
            snapshotToken,
            runtimeMultiplayerSnapshotMaximumTokenBytes
        )
        || !parseRuntimeUint64Strict(
            ini.getValue(
                "MultiplayerSnapshot",
                "SourceTimelineEpoch",
                ""
            ),
            sourceEpoch
        )
        || !parseRuntimeInt(
            ini.getValue("MultiplayerSnapshot", "BarrierTick", ""),
            barrierTick
        )
        || !parseRuntimeInt(
            ini.getValue("MultiplayerSnapshot", "SnapshotTick", ""),
            snapshotTick
        )
        || !parseRuntimeUint64Strict(
            ini.getValue(
                "MultiplayerSnapshot",
                "GameplaySetupFingerprint",
                ""
            ),
            setupFingerprint
        )
        || !iniHexDecode(
            ini.getValue("MultiplayerSnapshot", "CanonicalMapIdHex", ""),
            canonicalMapId,
            1024U
        )
        || !parseRuntimeUint64Strict(
            ini.getValue(
                "MultiplayerSnapshot",
                "MapContentFingerprint",
                ""
            ),
            mapFingerprint
        )
        || !parseRuntimeInt(
            ini.getValue("MultiplayerSnapshot", "FixedTickHz", ""),
            fixedTickHz
        )
        || !parseRuntimeUint64Strict(
            ini.getValue(
                "MultiplayerSnapshot",
                "SimulationRandomSeed",
                ""
            ),
            simulationSeed
        )
        || !parseRuntimeUint64Strict(
            ini.getValue(
                "MultiplayerSnapshot",
                "AuthoritativeStateHash",
                ""
            ),
            authoritativeHash
        )) {
        errorMessage = "invalid multiplayer snapshot envelope";
        return false;
    }
    if (proposalId != identity.proposalId
        || snapshotToken != identity.snapshotToken
        || sourceEpoch != identity.sourceTimelineEpoch
        || barrierTick != identity.barrierTick
        || snapshotTick != identity.snapshotTick
        || setupFingerprint != identity.gameplaySetupFingerprint
        || authoritativeHash != identity.authoritativeStateHash
        || fixedTickHz != live.config.fixedTickHz
        || simulationSeed == 0U
        || simulationSeed != live.config.simulationRandomSeed
        || mapFingerprint == 0U
        || mapFingerprint
            != live.config.multiplayerMapContentFingerprint
        || runtimeMultiplayerCanonicalIdentifier(canonicalMapId)
            != runtimeMultiplayerCanonicalIdentifier(
                live.config.multiplayerCanonicalMapId
            )) {
        errorMessage = "multiplayer snapshot envelope identity mismatch";
        return false;
    }
    errorMessage.clear();
    return true;
}

bool stageRuntimeMultiplayerSnapshot(
    const RuntimeProductionHostState& live,
    std::span<const std::uint8_t> bytes,
    const RuntimeMultiplayerSnapshotDescriptor& descriptor,
    RuntimeMultiplayerSnapshotStagedState& staged,
    std::string& errorMessage) {
    if (!validateRuntimeMultiplayerSnapshotDescriptor(
            descriptor,
            errorMessage
        )) {
        return false;
    }
    if (bytes.size() != descriptor.byteCount
        || runtimeMultiplayerSnapshotFingerprint(bytes)
            != descriptor.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes) != descriptor.sha256) {
        errorMessage = "multiplayer snapshot byte digest mismatch";
        return false;
    }
    const RuntimeMultiplayerSnapshotIdentity& identity = descriptor.identity;
    if (!live.config.multiplayerActive
        || identity.sourceTimelineEpoch
            != live.config.multiplayerTimelineEpoch
        || identity.gameplaySetupFingerprint
            != live.config.multiplayerSetupHash
        || identity.barrierTick != live.tick
        || live.config.simulationRandomSeed == 0U) {
        errorMessage = "snapshot descriptor does not match live timeline";
        return false;
    }

    try {
        const std::string text(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size()
        );
        IniFile ini;
        if (!parseRuntimeSaveDocument(text, ini)) {
            errorMessage = "multiplayer snapshot INI is invalid";
            return false;
        }
        const int version = parseRuntimeIntOrDefault(
            ini.getValue("RuntimeSave", "Version", "0"),
            0
        );
        if (version == 15
            && !validateRuntimeMultiplayerSnapshotEnvelope(
                live,
                ini,
                descriptor,
                errorMessage
            )) {
            return false;
        }
        if (version < 1 || version > 15) {
            errorMessage = "unsupported multiplayer snapshot schema";
            return false;
        }
        if (version < 15) {
            if (identity.kind != RuntimeMultiplayerSnapshotKind::Load) {
                errorMessage = "legacy multiplayer snapshot must be a load";
                return false;
            }
            const std::size_t humanCount = std::count_if(
                live.humanHouseAuthorities.begin(),
                live.humanHouseAuthorities.end(),
                [](const RuntimeHumanHouseAuthority&) { return true; }
            );
            if (humanCount != 1U) {
                errorMessage =
                    "legacy save requires exactly one multiplayer human";
                return false;
            }
        }

        auto scratch = std::make_unique<RuntimeProductionHostState>(*live.constructionUiClock, *live.constructionAudioClock, live.geometry);
        seedRuntimeLoadScratch(*scratch, live, live.visionEnabled);
        std::string processLocalOwner;
        for (const RuntimeHumanHouseAuthority& authority :
                scratch->humanHouseAuthorities) {
            if (authority.localFacade) {
                processLocalOwner = authority.canonicalOwner;
                break;
            }
        }
        if (version == 15) {
            std::string canonicalFacade;
            if (!iniHexDecode(
                    ini.getValue(
                        "MultiplayerHouse0",
                        "OwnerHex",
                        ""
                    ),
                    canonicalFacade,
                    256U
                )
                || !runtimeMultiplayerSnapshotRebindHumanFacade(
                    *scratch,
                    canonicalFacade,
                    errorMessage
                )) {
                if (errorMessage.empty()) {
                    errorMessage = "invalid canonical snapshot facade";
                }
                return false;
            }
        }
        if (!materializeRuntimeLoadStateFromIni(
                *scratch,
                ini,
                errorMessage
            )) {
            return false;
        }
        if (version == 15
            && (!materializeRuntimeMultiplayerSnapshotHouses(
                    *scratch,
                    ini,
                    errorMessage
                )
                || !materializeRuntimeMultiplayerSnapshotExtension(
                    *scratch,
                    ini,
                    errorMessage
                )
                || !runtimeMultiplayerSnapshotRebindHumanFacade(
                    *scratch,
                    processLocalOwner,
                    errorMessage
                ))) {
            return false;
        }
        std::string hashError;
        const std::uint64_t restoredHash =
            runtimeMultiplayerAuthoritativeStateHash(*scratch, hashError);
        if (restoredHash == 0U
            || restoredHash != identity.authoritativeStateHash
            || scratch->tick != identity.snapshotTick) {
            recordRuntimeCooperativeSnapshotDifference(live, *scratch);
            errorMessage = restoredHash == 0U
                ? "staged snapshot authority is invalid: " + hashError
                : "staged snapshot authoritative hash mismatch domains="
                    + runtimeMultiplayerSnapshotAuthorityDifferenceSummary(
                        live,
                        *scratch
                    );
            return false;
        }
        prepareRuntimePlayerDefeatNoticeLoadBaseline(*scratch);
        RuntimeMultiplayerSnapshotStagedState candidate;
        candidate.scratch = std::move(scratch);
        candidate.descriptor = descriptor;
        staged = std::move(candidate);
        errorMessage.clear();
        return true;
    } catch (const std::exception& exception) {
        errorMessage = exception.what();
        return false;
    }
}

void commitRuntimeMultiplayerSnapshotAuthority(
    RuntimeProductionHostState& live,
    RuntimeMultiplayerSnapshotStagedState&& staged) noexcept {
    if (!staged.scratch
        || staged.descriptor.identity.kind
            != RuntimeMultiplayerSnapshotKind::Load) {
        return;
    }
    RuntimeProductionHostState& loaded = *staged.scratch;
#define RUNTIME_MP_SNAPSHOT_SWAP(field) \
    swapRuntimeLoadedField(live.field, loaded.field)

    invalidateRuntimeVisionTerrainCoordinateLeases(live.world);
    invalidateRuntimeVisionTerrainCoordinateLeases(loaded.world);
    RUNTIME_MP_SNAPSHOT_SWAP(world);
    RUNTIME_MP_SNAPSHOT_SWAP(tick);
    swapRuntimeLoadedEconomy(live.economy, loaded.economy);
    RUNTIME_MP_SNAPSHOT_SWAP(humanHouseAuthorities);
    RUNTIME_MP_SNAPSHOT_SWAP(computerAiByOwner);
    RUNTIME_MP_SNAPSHOT_SWAP(computerMiningByOwner);
    RUNTIME_MP_SNAPSHOT_SWAP(cooperativeSession);
    RUNTIME_MP_SNAPSHOT_SWAP(cooperativeSessionStatus);
    RUNTIME_MP_SNAPSHOT_SWAP(techTreeLoaded);
    RUNTIME_MP_SNAPSHOT_SWAP(techTree);
    RUNTIME_MP_SNAPSHOT_SWAP(initialOreFieldsSeeded);
    RUNTIME_MP_SNAPSHOT_SWAP(initialOreCellsSeeded);
    RUNTIME_MP_SNAPSHOT_SWAP(initialOreCellsDensified);

    RUNTIME_MP_SNAPSHOT_SWAP(terrainByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(terrainInfoByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(terrainObjectInitialStateCaptured);
    RUNTIME_MP_SNAPSHOT_SWAP(terrainObjectInitialLayoutSignature);
    RUNTIME_MP_SNAPSHOT_SWAP(terrainObjectInitialStateByKey);
    RUNTIME_MP_SNAPSHOT_SWAP(bridgeRepairSpansByHut);
    RUNTIME_MP_SNAPSHOT_SWAP(bridgeHealthByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(destroyableBridges);
    RUNTIME_MP_SNAPSHOT_SWAP(bridgeSpecialFlagsLoaded);
    RUNTIME_MP_SNAPSHOT_SWAP(mapInitialVeteran);
    RUNTIME_MP_SNAPSHOT_SWAP(structureFootprints);
    RUNTIME_MP_SNAPSHOT_SWAP(navigationRefreshPending);
    RUNTIME_MP_SNAPSHOT_SWAP(powerRefreshPending);

    RUNTIME_MP_SNAPSHOT_SWAP(gates);
    RUNTIME_MP_SNAPSHOT_SWAP(gatesInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(gateIdsByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(gateTopologyFingerprint);
    RUNTIME_MP_SNAPSHOT_SWAP(gateStrictOpenFingerprint);
    RUNTIME_MP_SNAPSHOT_SWAP(garrisons);
    RUNTIME_MP_SNAPSHOT_SWAP(garrisonsInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(subcells);
    RUNTIME_MP_SNAPSHOT_SWAP(subcellsInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(repairDepots);
    RUNTIME_MP_SNAPSHOT_SWAP(repairDepotsInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(factoryDeliveries);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingCloneDeliveries);
    RUNTIME_MP_SNAPSHOT_SWAP(playerCommands);
    live.playerLocalCommands = {};
    live.playerLocalCommands.planning = live.playerCommands.waypoints.planningOwners.contains(
        runtimeCanonicalHumanHouseOwner(live.economy.owner));
    live.playerCommunication = {};
    RUNTIME_MP_SNAPSHOT_SWAP(playerDefeatNotices);
    RUNTIME_MP_SNAPSHOT_SWAP(grinding);
    RUNTIME_MP_SNAPSHOT_SWAP(aircraftServices);
    RUNTIME_MP_SNAPSHOT_SWAP(aircraftServicesInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(deathWeapons);
    RUNTIME_MP_SNAPSHOT_SWAP(combatDetails);
    RUNTIME_MP_SNAPSHOT_SWAP(psychicStatuses);
    RUNTIME_MP_SNAPSHOT_SWAP(magBeams);
    RUNTIME_MP_SNAPSHOT_SWAP(airstrikes);
    RUNTIME_MP_SNAPSHOT_SWAP(airstrikePlanesByToken);
    RUNTIME_MP_SNAPSHOT_SWAP(airstrikeRootAuthoritiesByActor);
    RUNTIME_MP_SNAPSHOT_SWAP(specialMechanisms);
    RUNTIME_MP_SNAPSHOT_SWAP(specialMechanismsInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(spies);
    RUNTIME_MP_SNAPSHOT_SWAP(spiesInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(superWeapons);
    RUNTIME_MP_SNAPSHOT_SWAP(superWeaponsInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(particles);
    RUNTIME_MP_SNAPSHOT_SWAP(particlesInitialized);

    RUNTIME_MP_SNAPSHOT_SWAP(cloakStateInitialized);
    RUNTIME_MP_SNAPSHOT_SWAP(cloakObservedHealthByObject);
    RUNTIME_MP_SNAPSHOT_SWAP(vision);
    RUNTIME_MP_SNAPSHOT_SWAP(visionEnabled);
    RUNTIME_MP_SNAPSHOT_SWAP(behaviors);
    RUNTIME_MP_SNAPSHOT_SWAP(jumpjetFlights);
    RUNTIME_MP_SNAPSHOT_SWAP(aircraftFlights);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingPathObjectIds);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingEngineerCaptures);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingEngineerActions);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingOccupyTargets);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingTransportTargets);
    RUNTIME_MP_SNAPSHOT_SWAP(pendingTimedBombIntents);
    RUNTIME_MP_SNAPSHOT_SWAP(parasiteStunUntilTickByObject);
    RUNTIME_MP_SNAPSHOT_SWAP(harvesterTasks);
    RUNTIME_MP_SNAPSHOT_SWAP(slaveMinerTasks);
    RUNTIME_MP_SNAPSHOT_SWAP(slaveMinerRegenTicks);
    RUNTIME_MP_SNAPSHOT_SWAP(gattlingWeaponTimers);
    RUNTIME_MP_SNAPSHOT_SWAP(repairingStructureTicks);
    RUNTIME_MP_SNAPSHOT_SWAP(attackApproachCache);
    RUNTIME_MP_SNAPSHOT_SWAP(pathFailureCache);
    RUNTIME_MP_SNAPSHOT_SWAP(tickOccupancyCacheTick);
    RUNTIME_MP_SNAPSHOT_SWAP(tickOccupiedByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(tickGroundOccupantCountByCell);
    RUNTIME_MP_SNAPSHOT_SWAP(tickCrushEmptyCellProofValid);
    RUNTIME_MP_SNAPSHOT_SWAP(tickCrushEmptyCellObjectCount);
    RUNTIME_MP_SNAPSHOT_SWAP(tickCrushUnindexedFoundationBounds);
    RUNTIME_MP_SNAPSHOT_SWAP(nextProjectileActorId);
    RUNTIME_MP_SNAPSHOT_SWAP(projectiles);
    RUNTIME_MP_SNAPSHOT_SWAP(recentExpiredProjectiles);
    RUNTIME_MP_SNAPSHOT_SWAP(impactEffects);
    RUNTIME_MP_SNAPSHOT_SWAP(wrecks);
    RUNTIME_MP_SNAPSHOT_SWAP(buildingCollapses);

    swapRuntimeLoadedTriggers(live.triggers, loaded.triggers);
    RUNTIME_MP_SNAPSHOT_SWAP(skirmishCrates);
    RUNTIME_MP_SNAPSHOT_SWAP(skirmishMatchFinished);
    RUNTIME_MP_SNAPSHOT_SWAP(matchScores);
    RUNTIME_MP_SNAPSHOT_SWAP(multiplayerDefeatedOwners);
    RUNTIME_MP_SNAPSHOT_SWAP(multiplayerWinningOwners);
    RUNTIME_MP_SNAPSHOT_SWAP(multiplayerWinningTeams);

    // The descriptor and payload never own process-local selection, camera,
    // sidebar, menu, audio, music, lighting, render caches, peer bindings, or
    // facade choice.  Those fields are intentionally absent from this swap.
    live.humanHouseScopeActive = false;
    live.remoteHumanHouseScopeActive = false;

#undef RUNTIME_MP_SNAPSHOT_SWAP
}

int loadRuntimeTeamTrucks(
    RuntimeProductionHostState& state,
    const std::vector<int>& liveIds) {
    const UnitType* loadedTruck =
        state.rules.findUnitType("UnitType", "TRUCKB");
    if (loadedTruck == nullptr) return 0;

    int converted = 0;
    for (int objectId : liveIds) {
        WorldObjectInstance* object = findMutableObjectById(state, objectId);
        if (object == nullptr || !object->alive
            || object->kind != WorldObjectKind::Unit
            || toUpperAsciiLocal(trimAsciiLocal(object->type)) != "TRUCKA") {
            continue;
        }
        const int oldMaximum = std::max(
            1,
            runtimeMaxHealthForObject(state, *object)
        );
        const double healthRatio = std::clamp(
            static_cast<double>(std::max(0, object->health))
                / static_cast<double>(oldMaximum),
            0.0,
            1.0
        );
        cancelRuntimeCombatFire(state, object->instanceId);
        object->type = loadedTruck->id;
        const int newMaximum = std::max(1, loadedTruck->strength);
        object->health = object->health <= 0
            ? 0
            : std::clamp(
                static_cast<int>(std::lround(
                    healthRatio * static_cast<double>(newMaximum)
                )),
                1,
                newMaximum
            );
        initializeRuntimeObjectAmmo(*object, loadedTruck);
        initializeRuntimeObjectCloakState(state, *object);
        object->submerged = loadedTruck->underwater;
        ++converted;
    }
    if (converted > 0) {
        refreshRuntimeObjectOccupancyAfterLocalChange(state);
    }
    return converted;
}

bool completeRuntimeWorldRefresh(RuntimeProductionHostState& state) {
    RuntimePerfTraceScope stageTrace("completeRuntimeWorldRefresh", state.tick);
    // Reconcile deaths, captures, selling and replaced orders before power is
    // rebuilt. Only the main simulation pass may perform a money transfer.
    bool changed = advanceRuntimeDiskDrains(state, false);
    if (state.navigationRefreshPending) {
        refreshRuntimeNavigationOccupancyStats(state);
        state.navigationRefreshPending = false;
        changed = true;
    }
    if (state.powerRefreshPending) {
        rebuildRuntimeEconomyPower(state);
        // A map action can give a factory to either human after the ordinary
        // economy pass. Both accounts must observe it before this tick ends.
        for (const auto& authority : state.humanHouseAuthorities) {
            RuntimeHumanHouseScope house(state, authority.owner);
            if (house) {
                refreshRuntimeEconomyMetricsForCurrentOwner(state);
                (void)refreshRuntimeFactoryPlantCatalog(state);
            }
        }
        state.powerRefreshPending = false;
        changed = true;
    }
    return changed;
}

bool loadRuntimeMenuCsfFile(
    const std::filesystem::path& path,
    std::unordered_map<std::string, std::wstring>& labels) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    if (bytes.size() < 24 || !runtimeMenuCsfFourCc(bytes, 0, " FSC")) {
        return false;
    }

    std::unordered_map<std::string, std::wstring> decodedLabels;
    const std::uint32_t labelCount = runtimeReadLe32(bytes.data() + 8);
    std::size_t cursor = 24;
    for (std::uint32_t labelIndex = 0;
        labelIndex < labelCount && cursor + 12 <= bytes.size();
        ++labelIndex) {
        if (!runtimeMenuCsfFourCc(bytes, cursor, " LBL")) {
            return false;
        }
        cursor += 4;
        const std::uint32_t stringCount =
            runtimeReadLe32(bytes.data() + cursor);
        cursor += 4;
        const std::uint32_t keyLength =
            runtimeReadLe32(bytes.data() + cursor);
        cursor += 4;
        if (keyLength > bytes.size() - cursor) {
            return false;
        }
        const std::string key(
            reinterpret_cast<const char*>(bytes.data() + cursor),
            keyLength
        );
        cursor += keyLength;

        std::wstring firstValue;
        for (std::uint32_t stringIndex = 0;
            stringIndex < stringCount;
            ++stringIndex) {
            if (cursor + 8 > bytes.size()) {
                return false;
            }
            const bool plain =
                runtimeMenuCsfFourCc(bytes, cursor, " RTS");
            const bool withExtra =
                runtimeMenuCsfFourCc(bytes, cursor, "WRTS");
            if (!plain && !withExtra) {
                return false;
            }
            cursor += 4;
            const std::uint32_t characterCount =
                runtimeReadLe32(bytes.data() + cursor);
            cursor += 4;
            const std::uint64_t encodedBytes =
                static_cast<std::uint64_t>(characterCount) * 2ULL;
            if (encodedBytes > bytes.size() - cursor) {
                return false;
            }

            std::wstring value;
            value.reserve(characterCount);
            for (std::uint32_t characterIndex = 0;
                characterIndex < characterCount;
                ++characterIndex) {
                const std::size_t at =
                    cursor + static_cast<std::size_t>(characterIndex) * 2;
                const std::uint16_t encoded =
                    static_cast<std::uint16_t>(bytes[at])
                    | (static_cast<std::uint16_t>(bytes[at + 1]) << 8);
                wchar_t character =
                    static_cast<wchar_t>(encoded ^ 0xffffU);
                if (character == L'\r' || character == L'\n') {
                    character = L' ';
                }
                value.push_back(character);
            }
            cursor += static_cast<std::size_t>(encodedBytes);

            if (withExtra) {
                if (cursor + 4 > bytes.size()) {
                    return false;
                }
                const std::uint32_t extraLength =
                    runtimeReadLe32(bytes.data() + cursor);
                cursor += 4;
                if (extraLength > bytes.size() - cursor) {
                    return false;
                }
                cursor += extraLength;
            }
            if (stringIndex == 0) {
                firstValue = std::move(value);
            }
        }
        if (!key.empty() && !firstValue.empty()) {
            decodedLabels[toLowerAsciiLocal(key)] = std::move(firstValue);
        }
    }

    if (decodedLabels.empty()) {
        return false;
    }
    labels = std::move(decodedLabels);
    return true;
}

std::uint64_t runtimeMatchReplayStateHash(const RuntimeGameSessionState& state,
    std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* trace) {
    return runtimeStandaloneSimulationStateHash(state,error,trace);
}

std::string serializeRuntimeMatchReplayConfig(const RuntimeConfig& config) {
    std::ostringstream out;out.imbue(std::locale::classic());
    out << "CRTS_CONFIG_2\n";
#define REPLAY_STRING(field) out << std::quoted(config.field) << '\n'
    REPLAY_STRING(defaultMapPath);REPLAY_STRING(modRootPath);REPLAY_STRING(gameMode);
    REPLAY_STRING(gameModeRulesOverride);REPLAY_STRING(campaignId);REPLAY_STRING(campaignMissionId);
    REPLAY_STRING(campaignSourceId);REPLAY_STRING(campaignDifficulty);REPLAY_STRING(campaignPlayerHouse);
    REPLAY_STRING(campaignNextMissionId);REPLAY_STRING(campaignBriefingKey);
    REPLAY_STRING(playerCountry);REPLAY_STRING(playerColor);REPLAY_STRING(multiplayerCanonicalMapId);
#undef REPLAY_STRING
    out << config.fixedTickHz << ' ' << config.multiplayerActive << ' '
        << config.multiplayerSetupHash << ' ' << config.multiplayerMapContentFingerprint << ' '
        << config.multiplayerCooperativeScenarioFingerprint << ' ' << config.multiplayerTimelineEpoch << ' '
        << config.simulationRandomSeed << '\n';
    out << config.startingCredits << ' ' << config.startingUnits << ' ' << config.techLevel << ' '
        << config.gameSpeed << ' ' << config.shortGame << ' ' << config.mcvRepacks << ' '
        << config.crates << ' ' << config.superweapons << ' ' << config.buildOffAlly << ' '
        << config.computerAiEnabled << ' ' << config.noCooldown << '\n' << config.playerSlots.size() << '\n';
    for (const auto& slot:config.playerSlots) {
        out << slot.enabled << ' ' << slot.human << ' ' << slot.local << ' ' << slot.startPosition << ' '
            << std::quoted(slot.peerId) << ' ' << std::quoted(slot.name) << ' '
            << std::quoted(slot.country) << ' ' << std::quoted(slot.color) << ' '
            << std::quoted(slot.team) << ' ' << std::quoted(slot.difficulty) << '\n';
    }
    return out.str();
}

bool deserializeRuntimeMatchReplayConfig(const std::string& text,
    RuntimeConfig& config, std::string& error) {
    auto candidate=config;
    std::istringstream in(text);in.imbue(std::locale::classic());
    std::string version;in>>version;
    const auto string=[&](std::string& value) {
        in>>std::ws;
        if(in.peek()!='"' || !(in>>std::quoted(value)) || value.size()>4096
            || value.find('\0')!=std::string::npos) in.setstate(std::ios::failbit);
    };
    const auto number=[&](std::uint64_t& value) {
        std::string token;
        if(!(in>>token) || !parseRuntimeUint64Strict(token,value)) in.setstate(std::ios::failbit);
    };
#define REPLAY_STRING(field) string(candidate.field)
    REPLAY_STRING(defaultMapPath);REPLAY_STRING(modRootPath);REPLAY_STRING(gameMode);
    REPLAY_STRING(gameModeRulesOverride);REPLAY_STRING(campaignId);REPLAY_STRING(campaignMissionId);
    REPLAY_STRING(campaignSourceId);REPLAY_STRING(campaignDifficulty);REPLAY_STRING(campaignPlayerHouse);
    REPLAY_STRING(campaignNextMissionId);REPLAY_STRING(campaignBriefingKey);
    REPLAY_STRING(playerCountry);REPLAY_STRING(playerColor);REPLAY_STRING(multiplayerCanonicalMapId);
#undef REPLAY_STRING
    in>>candidate.fixedTickHz>>candidate.multiplayerActive;
    number(candidate.multiplayerSetupHash);number(candidate.multiplayerMapContentFingerprint);
    number(candidate.multiplayerCooperativeScenarioFingerprint);number(candidate.multiplayerTimelineEpoch);
    number(candidate.simulationRandomSeed);
    in>>candidate.startingCredits>>candidate.startingUnits>>candidate.techLevel>>candidate.gameSpeed
        >>candidate.shortGame>>candidate.mcvRepacks>>candidate.crates>>candidate.superweapons
        >>candidate.buildOffAlly>>candidate.computerAiEnabled>>candidate.noCooldown;
    int count=-1;in>>count;
    if(version!="CRTS_CONFIG_2" || !in || count<0 || count>8 || candidate.fixedTickHz<1
        || candidate.fixedTickHz>240 || candidate.startingCredits<0
        || candidate.startingCredits>runtimeMaximumCreditBalance || candidate.startingUnits<0
        || candidate.startingUnits>10000 || candidate.techLevel<1 || candidate.techLevel>10
        || candidate.gameSpeed<0 || candidate.gameSpeed>6 || candidate.defaultMapPath.empty()) {
        error="invalid replay startup configuration";return false;
    }
    candidate.playerSlots.clear();
    for(int index=0;index<count;++index) {
        RuntimeConfig::PlayerSlot slot;
        in>>slot.enabled>>slot.human>>slot.local>>slot.startPosition;
        string(slot.peerId);string(slot.name);string(slot.country);string(slot.color);
        string(slot.team);string(slot.difficulty);
        if(!in || slot.startPosition<0 || slot.startPosition>8) {
            error="invalid replay player slot";return false;
        }
        candidate.playerSlots.push_back(std::move(slot));
    }
    in>>std::ws;
    if(!in.eof()) {error="trailing replay configuration";return false;}
    // Recorded configuration never supplies a destination for saves, progress,
    // preferences or scripts. Resource roots come from the local installation.
    candidate.campaignLoadSavePath.clear();candidate.skirmishLoadSavePath.clear();
    candidate.campaignSavePath.clear();candidate.campaignIntroVideoPath.clear();
    candidate.campaignPostScoreVideoPath.clear();
    config=std::move(candidate);error.clear();return true;
}

bool writeRuntimeMatchReplayWorld(const RuntimeProductionHostState& state, std::string& data, std::string& error) {
    std::ostringstream out;out.imbue(std::locale::classic());
    if(!writeRuntimeStateToStream(state,out,error)
        || (!state.humanHouseAuthorities.empty() && !writeRuntimeMultiplayerSnapshotHouseSections(state,out,error))
        || !writeRuntimeMultiplayerSnapshotExtension(state,out,error)) return false;
    if(state.campaignMode) writeRuntimeCooperativeObservations(out,state,true);
    // Ordinary saves prepare/reconcile support skills for an immediate game
    // load. Replay startup must retain the exact pre-initialization state too.
    std::ostringstream superWeapons;state.superWeapons.save(superWeapons);
    out << "\n[ReplaySuperWeapons]\nSchema=1\nStateHex=" << iniHexEncode(superWeapons.str()) << '\n';
    if(!out) {error="replay state serialization failed";return false;}
    data=out.str();return true;
}

bool restoreRuntimeMatchReplayWorld(RuntimeProductionHostState& state, const std::string& configText,
    const std::string& data, int tick, std::uint64_t expectedHash, std::string& error,
    std::vector<std::pair<std::string,std::uint64_t>>* failureTrace,
    std::string* failureState) {
    if(tick<0 || expectedHash==0) {
        error="invalid replay restore checkpoint";return false;
    }
    auto config=state.config;
    if(!deserializeRuntimeMatchReplayConfig(configText,config,error)) return false;
    // A load/reset belongs to this same map and rules session.
    if(config.defaultMapPath!=state.config.defaultMapPath || config.modRootPath!=state.config.modRootPath
        || config.gameMode!=state.config.gameMode || config.campaignSourceId!=state.config.campaignSourceId) {
        error="replay reset changes map or rules context";return false;
    }
    IniFile ini;if(!parseRuntimeSaveDocument(data,ini)) {error="invalid replay world state";return false;}
    auto scratch=std::make_unique<RuntimeProductionHostState>(*state.constructionUiClock, *state.constructionAudioClock, state.geometry);seedRuntimeLoadScratch(*scratch,state,false);
    scratch->config=std::move(config);
    // Unlike an in-session game load, a replay must not inherit whether the
    // previous viewer had vision initialized. Rebuild it at exact saved poses.
    if(!parseRuntimeStrictBool(ini.getValue("MultiplayerSubsystemFlags","Vision",""),scratch->visionEnabled)) {
        error="replay vision initialization flag is missing or invalid";return false;
    }
    if(!materializeRuntimeLoadStateFromIni(*scratch,ini,error,true)) return false;
    initializeRuntimeNavigation(*scratch);completeRuntimeWorldRefresh(*scratch);
    if(ini.getSection("MultiplayerHouses") && !materializeRuntimeMultiplayerSnapshotHouses(*scratch,ini,error)) return false;
    if(!materializeRuntimeMultiplayerSnapshotExtension(*scratch,ini,error)) return false;
    // Ordinary campaign loads reconstruct previous-frame observations. A
    // replay must preserve discovery, damage and selection edges exactly.
    if(scratch->campaignMode && !readRuntimeCooperativeObservations(ini,scratch->triggers,error,true)) return false;
    std::string superWeapons;IniFile superWeaponIni;
    if(ini.getValue("ReplaySuperWeapons","Schema","")!="1"
        || !iniHexDecode(ini.getValue("ReplaySuperWeapons","StateHex",""),superWeapons,runtimeMatchReplayMaximumRecordBytes/2)
        || !superWeaponIni.loadFromString(superWeapons) || !superWeaponIni.getSection("SuperWeapons")) {
        error="missing or invalid exact replay support-skill state";return false;
    }
    if(!scratch->superWeapons.load(superWeaponIni,&error)) return false;
    const auto restoredHash=runtimeMatchReplayStateHash(*scratch,error);
    if(scratch->tick!=tick || restoredHash==0 || restoredHash!=expectedHash) {
        if(failureTrace)runtimeMatchReplayStateHash(*scratch,error,failureTrace);
        if(failureState) {std::string diagnosticError;writeRuntimeMatchReplayWorld(*scratch,*failureState,diagnosticError);}
        error="replay initial/reset world checksum differs at tick "+std::to_string(tick)+": "+error;return false;
    }
    auto restoredConfig=std::move(scratch->config);
    static_assert(std::is_nothrow_move_assignable_v<RuntimeConfig>);
    commitRuntimeLoadedState(state,*scratch);
    state.config=std::move(restoredConfig);
    state.repaintRequested=true;
    return true;
}

bool rebindRuntimeMultiplayerSaveAsLoad(
    const RuntimeProductionHostState& state,
    std::span<const std::uint8_t> sourceBytes,
    std::uint64_t proposalId,
    std::uint64_t sourceEpoch,
    int barrierTick,
    RuntimeMultiplayerSnapshotMemoryImage& image,
    std::string& errorMessage
) {
    const std::string sourceText(
        reinterpret_cast<const char*>(sourceBytes.data()),
        sourceBytes.size()
    );
    IniFile sourceIni;
    if (!parseRuntimeSaveDocument(sourceText, sourceIni)
        || parseRuntimeIntOrDefault(
            sourceIni.getValue("RuntimeSave", "Version", "0"),
            0
        ) != 15
        || trimAsciiLocal(sourceIni.getValue(
            "MultiplayerSnapshot",
            "Kind",
            ""
        )) != "Save") {
        errorMessage = "multiplayer load requires a v15 Save snapshot";
        return false;
    }
    std::ostringstream canonicalStream;
    if (!writeRuntimeMultiplayerSnapshotCanonicalIni(
            sourceIni,
            canonicalStream,
            errorMessage
        )) {
        return false;
    }
    const std::string canonicalText = canonicalStream.str();
    std::vector<std::uint8_t> canonicalBytes(
        canonicalText.begin(),
        canonicalText.end()
    );
    IniFile canonicalIni;
    if (!parseRuntimeSaveDocument(canonicalText, canonicalIni)) {
        errorMessage = "multiplayer save canonicalization failed";
        return false;
    }

    RuntimeMultiplayerSnapshotIdentity savedIdentity;
    savedIdentity.kind = RuntimeMultiplayerSnapshotKind::Save;
    int savedBarrierTick = -1;
    if (!parseRuntimeUint64Strict(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "ProposalId",
                ""
            ),
            savedIdentity.proposalId
        )
        || !parseRuntimeUint64Strict(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "SourceTimelineEpoch",
                ""
            ),
            savedIdentity.sourceTimelineEpoch
        )
        || !parseRuntimeInt(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "BarrierTick",
                ""
            ),
            savedBarrierTick
        )
        || !parseRuntimeInt(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "SnapshotTick",
                ""
            ),
            savedIdentity.snapshotTick
        )
        || !iniHexDecode(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "SnapshotTokenHex",
                ""
            ),
            savedIdentity.snapshotToken,
            runtimeMultiplayerSnapshotMaximumTokenBytes
        )
        || !parseRuntimeUint64Strict(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "AuthoritativeStateHash",
                ""
            ),
            savedIdentity.authoritativeStateHash
        )
        || !parseRuntimeUint64Strict(
            canonicalIni.getValue(
                "MultiplayerSnapshot",
                "GameplaySetupFingerprint",
                ""
            ),
            savedIdentity.gameplaySetupFingerprint
        )) {
        errorMessage = "invalid multiplayer Save envelope";
        return false;
    }
    savedIdentity.barrierTick = savedBarrierTick;
    if (savedBarrierTick != savedIdentity.snapshotTick
        || savedIdentity.snapshotTick < 0
        || savedIdentity.snapshotTick > state.tick
        || !runtimeMultiplayerSnapshotTokenIsSafe(
            savedIdentity.snapshotToken
        )
        || savedIdentity.authoritativeStateHash == 0U
        || savedIdentity.gameplaySetupFingerprint
            != state.config.multiplayerSetupHash) {
        errorMessage = "multiplayer Save identity is incompatible";
        return false;
    }
    RuntimeMultiplayerSnapshotDescriptor savedDescriptor;
    if (!makeRuntimeMultiplayerSnapshotDescriptor(
            savedIdentity,
            canonicalBytes,
            savedDescriptor,
            errorMessage
        )
        || !validateRuntimeMultiplayerSnapshotEnvelope(
            state,
            canonicalIni,
            savedDescriptor,
            errorMessage
        )) {
        return false;
    }

    if (!replaceRuntimeMultiplayerSnapshotIniValue(
            canonicalBytes,
            "MultiplayerSnapshot",
            "Kind",
            "Load"
        )
        || !replaceRuntimeMultiplayerSnapshotIniValue(
            canonicalBytes,
            "MultiplayerSnapshot",
            "ProposalId",
            std::to_string(proposalId)
        )
        || !replaceRuntimeMultiplayerSnapshotIniValue(
            canonicalBytes,
            "MultiplayerSnapshot",
            "SourceTimelineEpoch",
            std::to_string(sourceEpoch)
        )
        || !replaceRuntimeMultiplayerSnapshotIniValue(
            canonicalBytes,
            "MultiplayerSnapshot",
            "BarrierTick",
            std::to_string(barrierTick)
        )) {
        errorMessage = "could not rebind multiplayer Save envelope";
        return false;
    }

    RuntimeMultiplayerSnapshotIdentity loadIdentity = savedIdentity;
    loadIdentity.proposalId = proposalId;
    loadIdentity.kind = RuntimeMultiplayerSnapshotKind::Load;
    loadIdentity.sourceTimelineEpoch = sourceEpoch;
    loadIdentity.barrierTick = barrierTick;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    if (!makeRuntimeMultiplayerSnapshotDescriptor(
            loadIdentity,
            canonicalBytes,
            descriptor,
            errorMessage
        )) {
        return false;
    }
    RuntimeMultiplayerSnapshotStagedState preflight;
    if (!stageRuntimeMultiplayerSnapshot(
            state,
            canonicalBytes,
            descriptor,
            preflight,
            errorMessage
        )) {
        return false;
    }
    image.bytes = std::move(canonicalBytes);
    image.descriptor = std::move(descriptor);
    errorMessage.clear();
    return true;
}

}

namespace concord::runtime::session_detail {

bool loadRuntimeState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage
) {
    return loadRuntimeStateWithVisionPolicy(
        state,
        savePath,
        errorMessage,
        false
    );
}

bool loadRuntimeCampaignState(
    RuntimeProductionHostState& state,
    const std::string& savePath,
    std::string& errorMessage
) {
    return loadRuntimeStateWithVisionPolicy(
        state,
        savePath,
        errorMessage,
        true
    );
}

}
