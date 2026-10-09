    // Real rules and host event delivery: identical victims in the same
    // blast distinguish alliance filtering from armor/range differences.
    for (const int hz : {30,60}) for (const std::string power : {
        "NukeSpecial","LightningStormSpecial","PsychicDominatorSpecial",
        "IronCurtainSpecial","ChronoSphereSpecial","GeneticConverterSpecial"}) {
        auto storage=fixture("Americans",hz); auto& s=*storage;
        s.config.gameMode="BATTLE"; s.skirmishModeRulesLoaded=true; s.skirmishModeAlliesAllowed=true;
        s.skirmishTeamByOwner={{"commander","1"},{"ally","1"},{"enemy","2"}};
        const bool dominator=power=="PsychicDominatorSpecial";
        const bool organic=power=="IronCurtainSpecial" || power=="ChronoSphereSpecial"
            || power=="GeneticConverterSpecial";
        std::vector<int> ids, tanks;
        for (const std::string owner : {"Commander","Ally","Enemy"}) {
            ids.push_back(addUnit(s,dominator?"NAHAND":"E1",dominator?WorldObjectKind::Structure:WorldObjectKind::Infantry,
                90,90,owner,organic?125:10000));
            if (dominator) tanks.push_back(addUnit(s,"HTNK",WorldObjectKind::Unit,91,90,owner));
        }
        if (!runtimeOwnersAreAllied(s,"Commander","Ally") || runtimeOwnersAreAllied(s,"Commander","Enemy"))
            return fail("friendly damage alliance fixture");
        initializeRuntimeNavigation(s);
        const int outside=addUnit(s,"E1",WorldObjectKind::Infantry,111,110,"Ally",125);
        if (!fire(s,power,{{90,90},organic?ids.back():ids.front()},power=="ChronoSphereSpecial"
                ?std::optional<SuperWeaponTarget>({{104,100},-1}):std::nullopt))
            return fail(power+" rejected same-side damage target: "+error);
        if (organic) {
            step(s,2);
            for (const int id:ids) if (findObjectById(s,id)) return fail(power+" spared an organic target by alliance");
            if (power=="GeneticConverterSpecial" && std::count_if(s.world.getObjects().begin(),s.world.getObjects().end(),
                    [](const auto& o){return o.alive && o.type=="BRUTE" && o.owner=="Commander";})!=3)
                return fail("mutator did not transform all three sides");
        } else {
            for (int tick=0;tick<hz*30;++tick) {
                const auto* enemy=findObjectById(s,ids.back());
                if (enemy && enemy->health<10000) break;
                step(s);
            }
            const auto* enemy=findObjectById(s,ids.back());
            if (!enemy || enemy->health>=10000) return fail(power+" did not apply its real damage");
            for (const int id:ids) {
                const auto* victim=findObjectById(s,id);
                if (!victim || victim->health!=enemy->health)
                    return fail(power+" produced different same-position damage by alliance");
            }
            if (dominator) {
                for (std::size_t i=0;i<tanks.size();++i) {
                    const auto* tank=findObjectById(s,tanks[i]);
                    const std::string expectedOwner="Commander";
                    if (!tank || tank->owner!=expectedOwner
                        || tank->health!=s.rules.findUnitType("UnitType","HTNK")->strength)
                        return fail("dominator mixed independent building damage with tank control");
                }
            } else if (power=="NukeSpecial") {
                const int afterBlast=enemy->health;
                step(s,hz);
                for (const int id:ids) {
                    const auto* victim=findObjectById(s,id);
                    if (!victim || victim->health>=afterBlast
                        || victim->health!=findObjectById(s,ids.back())->health)
                        return fail("nuclear residual radiation filtered by alliance");
                }
            }
        }
        const auto* unaffected=findObjectById(s,outside);
        if (!unaffected || unaffected->health!=125) return fail(power+" damaged outside its area");
        std::cout<<"Support friendly damage: "<<power<<" "<<hz
            <<" Hz, self / ally / enemy and outside-area control PASS\n";
    }
    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPPORT_DAMAGE_ONLY") != nullptr) return true;
