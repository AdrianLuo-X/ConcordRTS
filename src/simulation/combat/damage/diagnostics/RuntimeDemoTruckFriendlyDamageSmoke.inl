// Observe stock impacts without delaying radiation or changing combat flags.
bool runRuntimeDemoTruckFriendlyDamageSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Demo truck friendly damage " + label + " [" + stage + "]: " + why;
        return false;
    };
    const char* output = std::getenv("CONCORD_DEMO_TRUCK_FRIENDLY_DAMAGE_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue22_08_demo_truck_friendly_damage";
    std::filesystem::create_directories(root);
    const char* selected = std::getenv("CONCORD_DEMO_TRUCK_FRIENDLY_DAMAGE_CASE");
    const std::string filter = selected ? selected : "";
    std::ofstream csv(root / "damage.csv"), summary(root / "cases.csv");
    if (!csv || !summary) return fail("output unavailable");
    csv << std::setprecision(17)
        << "case,phase,tick,actor,death_request,type,relation,object,before,after,delta,x,y\n";
    summary << "case,hz,route,source,death_request,primary_impacts,death_impacts,radiation_sites,radiation_ticks,result\n";

    IniFile raw;
    if (!raw.loadFromFile(live.report.rulesPath)) return fail("original rules unavailable");
    const auto rawVerses = splitRuntimeTokenList(raw.getValue("DemobombWH", "Verses", ""));
    if (raw.getValue("MTNK", "Armor", "") != "heavy" || rawVerses.size() != 11
        || rawVerses[5] != "50%") return fail("stock primary-target heavy armor/Verses declaration changed");
    // Ordinary projectiles cache target-adjusted damage at launch. The
    // actual impact independently reapplies authored Damage=300 and Verses.
    // DeathWeapon actors retain their original authored base damage.
    const int originalPrimaryCache = std::stoi(raw.getValue("Demobomb", "Damage", "0"))
        * std::stoi(rawVerses[5]) / 100;
    const auto* nativeTruck = live.rules.findUnitType("vehicle", "DTRUCK");
    const auto* nativeBomb = live.rules.findWeaponType("Demobomb");
    const auto* nativeWarhead = live.rules.findWarheadType("DemobombWH");
    if (!nativeTruck || !nativeBomb || !nativeWarhead
        || raw.getValue("VehicleTypes", "44", "") != "DTRUCK"
        || raw.getValue("DTRUCK", "Primary", "") != "Demobomb"
        || raw.getValue("DTRUCK", "DeathWeapon", "") != "Demobomb"
        || std::stoi(raw.getValue("DTRUCK", "Strength", "0")) != 150
        || std::stoi(raw.getValue("Demobomb", "Damage", "0")) != 300
        || raw.getValue("Demobomb", "Projectile", "") != "InvisibleLow"
        || raw.getValue("Demobomb", "Warhead", "") != "DemobombWH"
        || std::stoi(raw.getValue("Demobomb", "RadLevel", "0")) != 100
        || !parseRuntimeBool(raw.getValue("Demobomb", "Suicide", "no"))
        || std::stod(raw.getValue("DemobombWH", "CellSpread", "0")) != 8.0
        || std::stod(raw.getValue("DemobombWH", "PercentAtMax", "0")) != 0.1
        || raw.getValue("DemobombWH", "AffectsAllies", "__absent__") != "__absent__"
        || raw.getValue("DemobombWH", "AnimList", "") != "DEMTEXP"
        || nativeTruck->primaryWeapon != "Demobomb" || nativeTruck->deathWeapon != "Demobomb"
        || nativeTruck->strength != 150 || nativeTruck->crewed
        || nativeBomb->damage != 300 || nativeBomb->radLevel != 100 || !nativeBomb->suicide
        || nativeBomb->projectile != "InvisibleLow" || nativeBomb->warhead != "DemobombWH"
        || nativeWarhead->cellSpread != 8.0 || nativeWarhead->percentAtMax != 0.1
        || !nativeWarhead->affectsAllies || !nativeWarhead->affectsEnemies)
        return fail("stock Demobomb declarations or implicit friendly blast policy changed");
    const std::uint64_t originalRulesHash = live.rules.getDetailRulesHash();
    struct Victim { int id = -1; int originalHealth = 0; std::string type, relation; int x = 0, y = 0; };
    int passed = 0, ordinarySaves = 0, canonicalSaves = 0, observedImpacts = 0, damageChecks = 0;
    const auto hp = [](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        return o ? o->health : -1;
    };
    const auto describe = [&](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        if (!o) return std::string("missing#") + std::to_string(id);
        return o->type + "#" + std::to_string(id) + " owner=" + o->owner + " hp=" + std::to_string(o->health)
            + " alive=" + std::to_string(o->alive) + " cell=" + std::to_string(o->x) + "," + std::to_string(o->y)
            + " mission=" + o->mission + " subcell=" + std::to_string(o->subCell);
    };
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.tick = 200; s.config.fixedTickHz = hz; s.config.multiplayerActive = false;
        s.config.computerAiEnabled = s.config.superweapons = s.config.crates = s.config.shortGame = false;
        s.config.noCooldown = false; s.config.gameMode = "Battle"; s.campaignMode = false;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.behaviors.clear(); s.projectiles.clear(); s.recentExpiredProjectiles.clear(); s.impactEffects.clear();
        s.deathWeapons.reset(); s.pendingObjectExits.clear(); s.pendingObjectExitIds.clear();
        s.selectedObjectIds.clear(); s.playerCommands = {};
        // This fixture uses the existing offline local facade for whichever
        // real house issues the UI order, rather than inheriting the map's
        // unrelated remote-house production authority.
        s.humanHouseAuthorities.clear();
        s.specialMechanismsInitialized = s.superWeaponsInitialized = s.subcellsInitialized = false;
        s.particles.reset(hz); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick);
        s.config.playerSlots.clear();
        for (int slotIndex = 0; slotIndex < 3; ++slotIndex) {
            RuntimeConfig::PlayerSlot slot; slot.enabled = slot.human = true; slot.local = slotIndex == 0;
            slot.country = slotIndex == 0 ? "Africans" : slotIndex == 1 ? "Confederation" : "Americans";
            slot.team = slotIndex == 2 ? "2" : "1"; s.config.playerSlots.push_back(std::move(slot));
        }
        initializeRuntimeSkirmishPlayerIdentities(s, "Africans");
        s.economy.owner = runtimeSkirmishOwnerForSlot(0); s.economy.skirmishStartApplied = true;
        s.skirmishModeAlliesAllowed = true; s.skirmishModeRulesLoaded = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        for (int y = 44; y <= 91; ++y) for (int x = 44; x <= 91; ++x) {
            TerrainCell c; c.x = x; c.y = y; c.cellIndex512 = runtimeCellKey(x, y); c.tileIndex = c.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeSuperWeaponsInitialized(s);
        ensureRuntimeLightingInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        return box;
    };
    const auto clone = [&](RuntimeWindowState& s, bool canonical) -> std::unique_ptr<RuntimeWindowState> {
        std::ostringstream payload, normalized; IniFile base, exact;
        if (!writeRuntimeStateToStream(s, payload, error) || !base.loadFromString(payload.str())) return nullptr;
        std::ofstream ordinary(root / (label + "-" + stage + "-ordinary.ini"), std::ios::binary);
        ordinary << payload.str();
        if (canonical) {
            if (!writeRuntimeMultiplayerSnapshotExtension(s, payload, error) || !exact.loadFromString(payload.str())
                || !writeRuntimeMultiplayerSnapshotCanonicalIni(exact, normalized, error)
                || !exact.loadFromString(normalized.str())) return nullptr;
            std::ofstream saved(root / (label + "-" + stage + "-canonical.ini"), std::ios::binary);
            saved << normalized.str();
        }
        auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false);
        RuntimeSnapshotActiveStateScope active(*copy);
        if (!materializeRuntimeLoadStateFromIni(*copy, base, error)
            || (canonical && !materializeRuntimeMultiplayerSnapshotExtension(*copy, exact, error))) return nullptr;
        const auto* restoredWarhead = copy->rules.findWarheadType("DemobombWH");
        if (copy->tick != s.tick || copy->rules.getDetailRulesHash() != originalRulesHash
            || !restoredWarhead || !restoredWarhead->affectsAllies || !restoredWarhead->affectsEnemies
            || copy->runtimeProjectileImpactObserver || copy->particles.fixedTickHz() != s.config.fixedTickHz)
            { error = "restored original rules/tick/particle clock or observation lifetime changed"; return nullptr; }
        copy->runtimeSoundsEnabled = copy->runtimePreferencePersistenceEnabled = copy->visionEnabled = false;
        initializeRuntimeOfflineGameplayRouting(*copy);
        canonical ? ++canonicalSaves : ++ordinarySaves;
        return copy;
    };
    for (const int hz : {30, 60}) for (const std::string route : {"Primary", "DeployToggle", "Lethal"}) {
        label = route + "-" + std::to_string(hz); stage = "stock fixture";
        if (!filter.empty() && filter != label) continue;
        auto box = fixture(hz);
        const std::array<std::string, 3> owners{{runtimeSkirmishOwnerForSlot(0), runtimeSkirmishOwnerForSlot(1), runtimeSkirmishOwnerForSlot(2)}};
        if (!box->particlesInitialized || !runtimeOwnersAreAllied(*box, owners[0], owners[1])
            || runtimeOwnersAreAllied(*box, owners[0], owners[2])) return fail("real player/team identities");
        const auto add = [&](const std::string& type, RuntimeBuildKind kind, RuntimeCellPoint cell, const std::string& owner) {
            auto* o = addRuntimeObjectInstanceForOwner(*box, owner, kind, type, cell);
            if (!o) return -1;
            o->mission = "Sleep"; o->buildAnimTicksRemaining = o->buildAnimTicksTotal = 0;
            clearRuntimeRenderPosition(*o); return o->instanceId;
        };
        const int source = add("DTRUCK", RuntimeBuildKind::Unit, {64, 64}, owners[0]);
        const int trigger = route == "Primary" ? add("MTNK", RuntimeBuildKind::Unit, {65, 64}, owners[2]) : -1;
        const int gunner = route == "Lethal" ? add("HTNK", RuntimeBuildKind::Unit, {60, 64}, owners[2]) : -1;
        if (source < 0 || (route == "Primary" && trigger < 0) || (route == "Lethal" && gunner < 0))
            return fail("original source/attacker types");
        std::vector<Victim> victims;
        for (int typeIndex = 0; typeIndex < 3; ++typeIndex) {
            const std::string type = typeIndex == 0 ? "MTNK" : typeIndex == 1 ? "E1" : "GAPOWR";
            const auto kind = typeIndex == 2 ? RuntimeBuildKind::Building : typeIndex == 1 ? RuntimeBuildKind::Infantry : RuntimeBuildKind::Unit;
            // Original E1 has HP125: radius 3--4 would not survive two stock
            // Primary + DeathWeapon blasts. This outer point remains in both
            // radius-8 blasts and preserves its original health declaration.
            const RuntimeCellPoint point = typeIndex == 0 ? RuntimeCellPoint{64, 68}
                : typeIndex == 1 ? RuntimeCellPoint{62, 71} : RuntimeCellPoint{69, 69};
            const int nativeHealth = std::stoi(raw.getValue(type, "Strength", "0"));
            for (int relation = 0; relation < 4; ++relation) {
                const RuntimeCellPoint cell = relation == 3 ? RuntimeCellPoint{82, 76 + typeIndex * 3} : point;
                const int id = add(type, kind, cell, owners[relation % 3]);
                if (id < 0 || hp(*box, id) != nativeHealth || nativeHealth <= 0) return fail("original full-health " + type);
                if (kind == RuntimeBuildKind::Infantry)
                    assignRuntimeInfantrySubcellAtCurrentLocation(*box, *findMutableObjectById(*box, id));
                victims.push_back({id, nativeHealth, type, relation == 0 ? "own" : relation == 1 ? "ally" : relation == 2 ? "enemy" : "outside", cell.x, cell.y});
            }
        }
        std::set<int> infantrySlots;
        for (const auto& v : victims) if (v.type == "E1" && v.relation != "outside") {
            const auto* o = findObjectById(*box, v.id);
            if (!o || !RuntimeSubcellSystemState::isActiveSubCell(o->subCell)
                || !infantrySlots.insert(o->subCell).second) return fail("original infantry subcell occupancy");
        }
        if (hp(*box, source) != 150 || box->rules.getDetailRulesHash() != originalRulesHash)
            return fail("fixture changed original source health or rules");
        stage = "before-save";
        if (route == "DeployToggle" && hz == 30) {
            auto ordinary = clone(*box, false); if (!ordinary) return fail("ordinary before save: " + error);
            auto exact = clone(*box, true); if (!exact) return fail("canonical before save: " + error);
            for (const auto& v : victims) if (hp(*ordinary, v.id) != v.originalHealth || hp(*exact, v.id) != v.originalHealth)
                return fail("saved original victim health changed");
            box = std::move(ordinary); // The real UI detonation also runs from an ordinary restored world.
        }
        auto& s = *box; RuntimeSnapshotActiveStateScope active(s);
        std::vector<int> direct(victims.size(), 0);
        std::map<int, std::vector<int>> impactBefore;
        std::set<int> impactActors;
        int primaryImpacts = 0, deathImpacts = 0;
        std::string observationError;
        struct ObserverScope {
            RuntimeWindowState& state;
            std::function<void(const RuntimeProjectileEffect&, bool)> previous;
            explicit ObserverScope(RuntimeWindowState& s) : state(s), previous(std::move(s.runtimeProjectileImpactObserver)) {}
            ~ObserverScope() { state.runtimeProjectileImpactObserver = std::move(previous); }
        } observer(s);
        s.runtimeProjectileImpactObserver = [&](const RuntimeProjectileEffect& p, bool after) {
            if (p.weaponId != "Demobomb" || p.attackerObjectId != source || !observationError.empty()) return;
            if (!after) {
                if (impactBefore.contains(p.actorId) || impactActors.contains(p.actorId))
                    { observationError = "duplicate stock Demobomb actor " + std::to_string(p.actorId); return; }
                const int expectedCache = p.deathWeaponRequestId ? 300 : originalPrimaryCache;
                if (p.damage != expectedCache || p.warheadId != "DemobombWH" || p.projectileId != "InvisibleLow"
                    || p.cellSpread != 8.0 || p.percentAtMax != 0.1 || !p.affectsAllies || !p.affectsEnemies)
                    {
                        std::ostringstream details;
                        details << "actual original Demobomb actor policy/cache changed actor=" << p.actorId
                            << " deathRequest=" << p.deathWeaponRequestId << " damage=" << p.damage
                            << " expectedCache=" << expectedCache << " warhead=" << p.warheadId
                            << " projectile=" << p.projectileId << " spread=" << p.cellSpread
                            << " percentAtMax=" << p.percentAtMax << " allies=" << p.affectsAllies
                            << " enemies=" << p.affectsEnemies;
                        observationError = details.str(); return;
                    }
                auto& before = impactBefore[p.actorId];
                for (const auto& v : victims) before.push_back(hp(s, v.id));
                return;
            }
            const auto found = impactBefore.find(p.actorId);
            if (found == impactBefore.end()) { observationError = "impact observer missed the native entry"; return; }
            for (std::size_t i = 0; i < victims.size(); ++i) {
                const auto& v = victims[i]; const int before = found->second[i], afterHealth = hp(s, v.id), delta = before - afterHealth;
                if (before <= 0 || afterHealth <= 0 || delta < 0 || (v.relation == "outside" ? delta != 0 : delta <= 0)) {
                    observationError = "pure blast " + v.type + "/" + v.relation + " actor=" + std::to_string(p.actorId)
                        + " before=" + std::to_string(before) + " after=" + std::to_string(afterHealth) + " " + describe(s, v.id);
                    return;
                }
                direct[i] += delta;
                csv << label << ",blast," << s.tick << ',' << p.actorId << ',' << p.deathWeaponRequestId << ','
                    << v.type << ',' << v.relation << ',' << v.id << ',' << before << ',' << afterHealth << ',' << delta << ',' << v.x << ',' << v.y << '\n';
            }
            for (std::size_t i = 0; i < victims.size(); i += 4) {
                const int delta = found->second[i] - hp(s, victims[i].id);
                for (std::size_t relation = 1; relation < 3; ++relation)
                    if (found->second[i + relation] - hp(s, victims[i + relation].id) != delta) {
                        observationError = "same-cell pure blast differs across ownership for " + victims[i].type; return;
                    }
            }
            impactActors.insert(p.actorId); impactBefore.erase(found);
            p.deathWeaponRequestId ? ++deathImpacts : ++primaryImpacts;
            ++observedImpacts;
        };
        stage = "real UI";
        bool dispatched = false;
        if (route == "DeployToggle") {
            s.selectedObjectIds = {source};
            RuntimeMultiplayerGameplayCommand command;
            command.body = RuntimeMultiplayerGameplaySecondaryActionCommand{
                RuntimeMultiplayerGameplaySecondaryActionKind::DeployToggle, {source}};
            std::string reason;
            if (!prevalidateRuntimeMultiplayerGameplayCommand(s, owners[0], command, reason)) return fail("DeployToggle authority: " + reason);
            dispatched = applyRuntimeMultiplayerGameplayCommand(s, owners[0], command);
        } else {
            const int attacker = route == "Primary" ? source : gunner;
            const int target = route == "Primary" ? trigger : source;
            if (route == "Lethal") s.economy.owner = owners[2];
            initializeRuntimeOfflineGameplayRouting(s); s.selectedObjectIds = {attacker};
            auto* a = findMutableObjectById(s, attacker); const auto* t = findObjectById(s, target);
            a->facing = a->turretFacing = runtimeFacingFromDelta(t->x - a->x, t->y - a->y);
            dispatched = dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {t->x, t->y}, t);
        }
        if (!dispatched) return fail("native command rejected: accepted=" + std::to_string(s.lastCommand.acceptedCount)
            + " rejected=" + std::to_string(s.lastCommand.rejectedCount) + " status=" + s.interactionStatus + " source=" + describe(s, source));
        stage = "native flight/impact";
        const int expectedPrimary = route == "Primary" ? 1 : 0;
        for (int tick = 0; tick < hz * 30 && (deathImpacts != 1 || primaryImpacts != expectedPrimary); ++tick) {
            ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
            if (!observationError.empty()) return fail(observationError);
        }
        if (deathImpacts != 1 || primaryImpacts != expectedPrimary || !impactBefore.empty())
            return fail("actual impact budget exhausted: primary=" + std::to_string(primaryImpacts)
                + " death=" + std::to_string(deathImpacts) + " source=" + describe(s, source) + " status=" + s.simulationStatus);
        const auto* corpse = findRuntimeObjectByIdIncludingInactive(s, source);
        if (!corpse || corpse->alive || corpse->health != 0 || !s.deathWeapons.hasTriggered(source)
            || selectionContains(s, source) || s.behaviors.contains(source)) return fail("source survived or retained death authority");
        std::uint64_t sourceRequest = 0;
        for (const auto& death : s.deathWeapons.triggeredObjects()) if (death.objectId == source) sourceRequest = death.emittedRequestId;
        if (sourceRequest == 0 || s.specialMechanisms.radiationAreas().empty()
            || std::none_of(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& fx) {
                return fx.weaponId == "Demobomb" && fx.animId == "DEMTEXP" && !fx.death;
            })) return fail("original death request/DEMTEXP/radiation absent");
        for (const auto& [id, area] : s.specialMechanisms.radiationAreas()) {
            if (area.sourceObjectId == source && (area.radius != 8 || area.maximumStrength < 100
                || area.warheadId != raw.getValue("Radiation", "RadSiteWarhead", "")))
                return fail("original RadLevel100 site declaration changed");
        }
        stage = "radiation";
        const auto radiationAmount = [&](std::size_t i) { return victims[i].originalHealth - hp(s, victims[i].id) - direct[i]; };
        int radiationTicks = 0;
        const auto allRadiated = [&]() {
            for (std::size_t i = 0; i < victims.size(); ++i) if (victims[i].relation != "outside"
                && victims[i].type != "GAPOWR" && radiationAmount(i) <= 0) return false;
            return true;
        };
        while (!allRadiated() && radiationTicks < hz * 3) {
            ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); ++radiationTicks;
            if (!observationError.empty()) return fail(observationError);
        }
        if (!allRadiated()) return fail("stock radiation failed to further damage all own/allied/enemy infantry and vehicles");
        for (std::size_t i = 0; i < victims.size(); i += 4) {
            for (std::size_t relation = 0; relation < 4; ++relation) {
                const auto& v = victims[i + relation]; const int finalHealth = hp(s, v.id);
                if (direct[i + relation] != (relation == 3 ? 0 : direct[i]) || radiationAmount(i + relation) < 0
                    || (relation == 3 && finalHealth != v.originalHealth))
                    return fail("original blast/radiation/outside comparison differs: " + describe(s, v.id));
                csv << label << ",radiation," << s.tick << ",-1," << sourceRequest << ',' << v.type << ',' << v.relation
                    << ',' << v.id << ',' << v.originalHealth - direct[i + relation] << ',' << finalHealth << ','
                    << radiationAmount(i + relation) << ',' << v.x << ',' << v.y << '\n';
                ++damageChecks;
            }
        }
        stage = "duplicate guard";
        const auto nextRequest = s.deathWeapons.nextRequestId(); const auto actorCount = impactActors.size();
        RuntimeObjectExitRequest repeat; repeat.objectId = source; repeat.kind = RuntimeObjectExitKind::Destroyed; repeat.tick = s.tick;
        if (queueRuntimeObjectExit(s, repeat) != RuntimeObjectExitQueueResult::AlreadyFinalized
            || dispatchRuntimeSecondaryActionFromUi(s, source) || executeRuntimeSecondaryAction(s, source)
            || s.deathWeapons.nextRequestId() != nextRequest || impactActors.size() != actorCount)
            return fail("duplicate self-destruction emitted another native blast");
        stage = "after-save";
        if (route == "DeployToggle" && hz == 30) for (const bool canonical : {false, true}) {
            auto saved = clone(s, canonical); if (!saved) return fail("strict post-blast save: " + error);
            if (!saved->deathWeapons.hasTriggered(source) || hp(*saved, source) != 0
                || saved->deathWeapons.nextRequestId() != nextRequest) return fail("saved source tombstone/request changed");
            for (const auto& v : victims) if (hp(*saved, v.id) != hp(s, v.id)) return fail("saved blast/radiation health changed");
        }
        summary << label << ',' << hz << ',' << route << ',' << source << ',' << sourceRequest << ',' << primaryImpacts << ','
            << deathImpacts << ',' << s.specialMechanisms.radiationAreas().size() << ',' << radiationTicks << ",PASS\n";
        summary.flush(); csv.flush(); ++passed;
        std::cout << "Demo truck friendly damage " << label << " PASS primaryImpacts=" << primaryImpacts << " deathImpacts="
            << deathImpacts << " originalHP=ok own/ally/enemyPureBlast=equal radiation=separate outside=unchanged\n";
    }
    if (passed == 0) return fail("case filter matched no native scenario");
    if (filter.empty() && (passed != 6 || observedImpacts != 8 || damageChecks != 72
        || ordinarySaves != 2 || canonicalSaves != 2)) return fail("incomplete native scene/impact/save coverage");
    std::cout << "Demo truck friendly damage passed: cases=" << passed << " directImpacts=" << observedImpacts
        << " comparisons=" << damageChecks << " ordinarySaves=" << ordinarySaves << " canonicalSaves=" << canonicalSaves
        << " originalRulesHash=" << originalRulesHash << '\n';
    error.clear(); return true;
}
