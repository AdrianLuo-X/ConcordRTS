void recordRuntimeCooperativeSnapshotDifference(const RuntimeWindowState& live,
    const RuntimeWindowState& restored) {
    if (!std::getenv("CONCORD_COMMAND_SMOKE_NETWORK_COOPERATIVE_ONLY")
        && !std::getenv("CONCORD_COMMAND_SMOKE_NETWORK_COOPERATIVE_LAN_ONLY")) return;
    for (const auto& entry : {std::pair{"live", &live}, std::pair{"restored", &restored}}) {
        auto path = std::filesystem::path("build/smoke/gameplay/06-world-probe")
            / (std::string(entry.first) + "-snapshot-difference.ini");
        if (const char* directory = std::getenv("CONCORD_COOPERATIVE_LAN_DIRECTORY")) {
            const char* role = std::getenv("CONCORD_COOPERATIVE_LAN_ROLE");
            path = std::filesystem::path(directory)
                / (std::string(role ? role : "peer") + '-' + entry.first + "-snapshot-difference.ini");
        }
        std::ofstream output(path);
        std::string reason;
        (void)writeRuntimeStateToStream(*entry.second, output, reason);
        (void)writeRuntimeMultiplayerSnapshotExtension(*entry.second, output, reason);
        std::vector<std::pair<std::string, std::uint64_t>> trace;
        (void)runtimeMultiplayerAuthoritativeStateHash(*entry.second, reason, &trace);
        output << "\n[DiagnosticHashTrace]\n";
        for (const auto& [key, value] : trace) output << key << '=' << value << '\n';
        output << "\n[CooperativeDiagnostic]\nReason=" << reason << '\n';
        const auto& world = entry.second->world;
        output << "\n[DiagnosticObjects]\n" << std::setprecision(17);
        for (const auto& object : world.getObjects()) {
            output << object.instanceId << '=';
            output << object.instanceId;
            output << '|';
            output << static_cast<int>(object.kind);
            output << '|';
            output << iniHexEncode(object.mapId);
            output << '|';
            output << iniHexEncode(object.owner);
            output << '|';
            output << iniHexEncode(object.type);
            output << '|';
            output << object.health;
            output << '|';
            output << object.x;
            output << '|';
            output << object.y;
            output << '|';
            output << object.cell;
            output << '|';
            output << object.level;
            output << '|';
            output << object.facing;
            output << '|';
            output << object.turretFacing;
            output << '|';
            output << object.subCell;
            output << '|';
            output << iniHexEncode(object.mission);
            output << '|';
            output << iniHexEncode(object.tag);
            output << '|';
            output << object.veterancy;
            output << '|';
            output << object.veterancyExperience;
            output << '|';
            output << object.crateArmorMultiplier;
            output << '|';
            output << object.crateFirepowerMultiplier;
            output << '|';
            output << object.crateSpeedMultiplier;
            output << '|';
            output << object.crateCloakable;
            output << '|';
            output << object.harvestAutoSearchSuspended;
            output << '|';
            output << object.hasBeenCaptured;
            output << '|';
            output << object.onBridge;
            output << '|';
            output << object.authoredAircraft;
            output << '|';
            output << object.alive;
            output << '|';
            output << object.rubble;
            output << '|';
            output << object.submerged;
            output << '|';
            output << object.cloaked;
            output << '|';
            output << object.cloakCooldownTicks;
            output << '|';
            output << object.submergeCooldownTicks;
            output << '|';
            output << object.deployed;
            output << '|';
            output << object.deployStateTicksRemaining;
            output << '|';
            output << object.prone;
            output << '|';
            output << object.proneTicksRemaining;
            output << '|';
            output << object.weaponCooldownTicksRemaining;
            output << '|';
            output << object.radiationEruptionCooldownTicksRemaining;
            output << '|';
            output << object.hasRenderPosition;
            output << '|';
            output << object.renderX;
            output << '|';
            output << object.renderY;
            output << '|';
            output << object.renderMoveActive;
            output << '|';
            output << object.renderLevel;
            output << '|';
            output << object.renderOnBridge;
            output << '|';
            output << object.movementAnimationDistance;
            output << '|';
            output << object.buildAnimTicksTotal;
            output << '|';
            output << object.buildAnimTicksRemaining;
            output << '|';
            output << object.structureRecoilStartTick;
            output << '|';
            output << object.chargeTurretStartTick;
            output << '|';
            output << object.workAnimationActive;
            output << '|';
            output << object.poweredOperational;
            output << '|';
            output << object.powerAnimationPausedAtTick;
            output << '|';
            output << object.powerAnimationPausedTicks;
            output << '|';
            output << object.factoryExitAnimationStartTick;
            output << '|';
            output << object.factoryExitAnimationEndTick;
            output << '|';
            output << object.factoryExitAirborne;
            output << '|';
            output << object.factoryExitHostId;
            output << '|';
            output << object.specialCashNextTick;
            output << '|';
            output << object.specialCloneDeliveryObjectId;
            output << '|';
            for (int id : object.transportPassengerIds) output << id << ',';
            output << '|';
            output << object.activeGunnerMode;
            output << '|';
            output << object.activeGunnerWeaponIndex;
            output << '|';
            output << object.activeGunnerTurretIndex;
            output << '|';
            output << object.tankBunkerHostId;
            output << '|';
            output << object.tankBunkerOccupantId;
            output << '|';
            output << object.tankBunkerAnimationStartTick;
            output << '|';
            output << object.tankBunkerLowering;
            output << '|';
            output << object.dockedToObjectId;
            output << '|';
            output << object.dockIndex;
            output << '|';
            output << object.airborne;
            output << '|';
            output << object.flightLevel;
            output << '|';
            output << object.wallConnectionMask;
            output << '|';
            output << object.garrisonHostObjectId;
            output << '|';
            for (int id : object.garrisonOccupantIds) output << id << ',';
            output << '|';
            output << object.garrisonFireCursor;
            output << '|';
            output << object.currentAmmo;
            output << '\n';
        }
        output << "\n[DiagnosticTerrain]\n";
        for (const auto& t : world.getTerrainCells()) {
            output << t.cellIndex512 << '=' << t.x << '|' << t.y << '|' << t.tileIndex << '|'
                << t.subTileIndex << '|' << t.level << '|' << t.iceGrowth << '|' << t.hasOverlay << '|'
                << t.overlayIndex << '|' << t.overlayFrame << '|' << t.overlayType << '|' << t.overlayImage << '|'
                << t.overlayIsOre << '|' << t.overlayIsGem << '\n';
        }
        output << "\n[DiagnosticTerrainObjects]\n";
        for (const auto& t : world.getTerrainObjects()) {
            output << t.mapId << '=' << t.type << '|' << t.tag << '|' << t.x << '|' << t.y << '|'
                << t.cell << '|' << t.level << '|' << t.strength << '|' << t.health << '|' << t.immune << '|'
                << t.alive << '|' << t.occupationBits << '|' << t.footprintWidth << '|' << t.footprintHeight << '|';
            for (int subcell : t.occupiedSubCells) output << subcell << ',';
            output << '\n';
        }
        output << "\n[DiagnosticWaypoints]\n";
        for (const auto& w : world.getWaypoints())
            output << w.id << '=' << w.x << '|' << w.y << '|' << w.cell << '\n';
    }
}
