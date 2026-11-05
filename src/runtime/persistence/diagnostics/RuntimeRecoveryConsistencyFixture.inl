template<class Session>
auto makeRuntimeRecoveryConsistencyFixture(std::string& error, bool withSupportPowerRule = false) {
        auto result = std::make_unique<Session>();
        auto& s = *result;
        if (!configureRuntimeDetailSnapshotSmokeWorld(s, false, error)) return std::unique_ptr<Session>{};
        IniFile rules;
        if (!rules.loadFromString(R"RECOVERY(
[General]
BuildSpeed=0.1
[CombatDamage]
C4Delay=0.01
C4Warhead=RECOVERY_C4_WH
[VehicleTypes]
0=SNAPSHOT_TANK
[InfantryTypes]
0=RECOVERY_C4
[BuildingTypes]
0=DETAIL_FACTORY
1=SNAPSHOT_LANDMARK
[SNAPSHOT_LANDMARK]
Strength=1000
DestroyAnim=SNAPSHOT_COLLAPSE
LeaveRubble=yes
[SNAPSHOT_TANK]
Strength=200
Cost=100
TechLevel=1
Speed=6
SpeedType=Track
MovementZone=Normal
[DETAIL_FACTORY]
Strength=1000
Cost=100
TechLevel=1
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
[RECOVERY_C4]
Strength=100
Cost=100
C4=yes
Speed=4
[RECOVERY_C4_WH]
Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%
)RECOVERY")) return std::unique_ptr<Session>{};
        if (withSupportPowerRule) {
            // Include a stock rule kind with no providers. The production tick
            // advances the support system even though there is no power to fire.
            IniFile support;
            if (!support.loadFromString(R"RECOVERY(
[SuperWeaponTypes]
0=RECOVERY_SHIELD
[RECOVERY_SHIELD]
Type=ForceShield
Action=ForceShield
Range=3.4
)RECOVERY")) return std::unique_ptr<Session>{};
            rules.appendFrom(support);
        }
        if (!s.rules.loadFromIni(rules)) return std::unique_ptr<Session>{};
        if (withSupportPowerRule) {
            s.superWeaponsInitialized = false;
            ensureRuntimeSuperWeaponsInitialized(s);
        }
        s.config.defaultMapPath = "recovery-contract.map";
        s.config.shortGame = false; s.config.crates = false; s.config.superweapons = false;
        s.config.computerAiEnabled = false;
        // One human is also the original legacy snapshot compatibility boundary.
        s.config.playerSlots[1].human = false;
        s.humanHouseAuthorities.resize(1);
        s.economy=makeRuntimeMultiplayerSnapshotSmokeEconomy(s,"SkirmishPlayer1",9000);
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.wrecks.clear(); s.buildingCollapses.clear();
        s.world.accessObjects().erase(std::remove_if(s.world.accessObjects().begin(),
            s.world.accessObjects().end(), [](const auto& o) { return !o.alive; }), s.world.accessObjects().end());
        s.computerMiningByOwner["skirmishplayer3"] = {12, 450, 3, 7, 2, 4};
        s.economy.storedResourceUnits = 8; s.economy.storedResourceValue = 300;
        s.economy.storedResourceGemUnits = 2; s.economy.resourceTrips = 5;
        s.economy.skirmishStartApplied = true;
        auto* source = addRuntimeObjectInstanceForOwner(s, "SkirmishPlayer1",
            RuntimeBuildKind::Infantry, "RECOVERY_C4", {7, 12});
        if (!source) return std::unique_ptr<Session>{};
        // Production/load use the tile centre for idle continuous coordinates.
        clearRuntimeRenderPosition(*source);
        // Local saves initialize this empty subsystem when materializing it.
        ensureRuntimeRepairDepotsInitialized(s);
        const int sourceId = source->instanceId;
        ensureRuntimeSpecialMechanismsInitialized(s);
        if (!s.specialMechanisms.tryPlantC4(s.world.getObjects(), s.rules, sourceId, 47,
                s.tick, s.config.fixedTickHz, SpecialEventSink{}, error)) return std::unique_ptr<Session>{};
        initializeRuntimeNavigation(s); completeRuntimeWorldRefresh(s);
        // Enter production through the actual stage; never invent partially
        // paid counters that the original load validator correctly rejects.
        ++s.tick; advanceRuntimeSimulation(s,false); completeRuntimeWorldRefresh(s);
        // The stock snapshot fixture supplies empty subsystem clocks. Advance
        // those to this checkpoint after warming the real production stage.
        // Keep the separately recorded original SuperWeapon restore failure.
        if ((withSupportPowerRule ? s.superWeapons.lastTick() != s.tick
                                  : !s.superWeapons.alignEmptyStateTick(s.tick))
            || !s.particles.alignEmptyStateTick(s.tick)) return std::unique_ptr<Session>{};
        return result;
    }
