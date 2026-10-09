namespace {
bool smokeGeneticMutationFullHealth() {
    const auto fail = [](const std::string& message) {
        std::cerr << "mutation full health: " << message << '\n'; return false;
    };
    IniFile ini; RulesDatabase rules;
    if (!ini.loadFromString(
        "[InfantryTypes]\n0=HUMAN\n1=HEAVYHUMAN\n2=CUSTOMBRUTE\n3=ANIMAL\n4=INORGANIC\n"
        "[HUMAN]\nStrength=125\nOrganic=yes\nArmor=none\n"
        "[HEAVYHUMAN]\nStrength=500\nOrganic=yes\nArmor=flak\n"
        "[CUSTOMBRUTE]\nStrength=333\nOrganic=yes\nArmor=plate\n"
        "[ANIMAL]\nStrength=30\nOrganic=yes\nNotHuman=yes\nArmor=none\n"
        "[INORGANIC]\nStrength=125\nOrganic=no\nArmor=none\n"
        "[Warheads]\n0=TESTMUTATE\n[TESTMUTATE]\nInfDeath=9\n"
        "Verses=100%,100%,100%,0%,0%,0%,0%,0%,0%,0%,0%\n")
        || !rules.loadFromIni(ini)) return fail("independent typed fixture");
    const auto* replacement = rules.findUnitType("InfantryType", "CUSTOMBRUTE");
    if (!replacement || replacement->strength != 333) return fail("typed maximum");
    int cases = 0;
    for (const int hz : {30, 60}) for (const bool hostMaximum : {false, true}) {
        std::vector<WorldObjectInstance> objects, births;
        const auto add = [&](int id, const std::string& type, int hp, int x, int y,
                             const std::string& owner) {
            WorldObjectInstance object;
            object.instanceId=id; object.kind=WorldObjectKind::Infantry;
            object.type=type; object.owner=owner; object.health=hp; object.alive=true;
            object.x=x; object.y=y; object.cell=superWeaponObjectCellKey(x,y);
            objects.push_back(std::move(object));
        };
        add(1,"HUMAN",1,24,24,"Caster");
        add(2,"HUMAN",73,25,24,"Ally");
        add(3,"HUMAN",125,24,25,"Enemy");
        add(4,"HEAVYHUMAN",499,23,24,"Neutral");
        add(5,"ANIMAL",30,24,24,"Enemy");
        add(6,"INORGANIC",125,24,24,"Enemy");
        add(7,"HUMAN",125,24,24,"Enemy"); objects.back().airborne=true;
        add(8,"HUMAN",125,32,24,"Enemy");
        SuperWeaponRule rule;
        rule.id="MutationHealth"; rule.type=rule.action="GeneticConverter";
        rule.targetMode="Area"; rule.isPowered=false; rule.radius=3;
        rule.mutationType="CUSTOMBRUTE"; rule.mutateExplosion=true; rule.warhead="TESTMUTATE";
        SuperWeaponWorldContext context;
        context.fixedTickHz=hz; context.mapWidth=context.mapHeight=64;
        context.rules=&rules; context.objects=&objects;
        context.cellLegal=[](int,int,SuperWeaponTargetMode){return true;};
        context.areAllied=[](const auto& a,const auto& b){return a==b || a=="Ally" || b=="Ally";};
        int nextId=1000, maximumCalls=0; bool callbackIdentity=true;
        context.allocateObjectId=[&](){return ++nextId;};
        if (hostMaximum) context.geneticMutationMaxHealth=[&](const WorldObjectInstance& created) {
            ++maximumCalls;
            callbackIdentity=callbackIdentity && created.type=="CUSTOMBRUTE"
                && created.owner=="Caster" && created.instanceId>1000;
            return 666; // Independently declared host difficulty maximum.
        };
        const int expected=hostMaximum ? 666 : 333;
        bool fullAtBirth=true;
        context.onObjectCreated=[&](WorldObjectInstance& created) {
            fullAtBirth=fullAtBirth && created.health==expected;
            births.push_back(created);
        };
        SuperWeaponSystem system; system.reset(std::vector<SuperWeaponRule>{rule});
        if (!system.grant("Caster",rule.id,-1,&context)
            || !system.forceReady("Caster",rule.id,&context)
            || !system.beginTargetSelection("Caster",rule.id,context)
            || !system.submitTarget("Caster",rule.id,{{24,24},-1},context))
            return fail("real state-machine targeting");
        for (int tick=1; tick<=8; ++tick) {context.tick=tick;system.tick(context);}
        if (births.size()!=4 || !fullAtBirth || !callbackIdentity
            || maximumCalls!=(hostMaximum ? 4 : 0)) return fail("birth maximum/callback");
        std::set<int> ids;
        for (const auto& birth : births) {
            if (!ids.insert(birth.instanceId).second || birth.instanceId<=1000
                || !birth.alive || birth.type!="CUSTOMBRUTE" || birth.owner!="Caster"
                || birth.health!=expected) return fail("replacement identity/full health");
        }
        for (int id=1; id<=8; ++id) {
            const auto found=std::find_if(objects.begin(),objects.end(),
                [id](const auto& object){return object.instanceId==id;});
            if (found==objects.end() || (id<=5 ? found->alive || found->health!=0
                : !found->alive || found->health!=125 || found->owner!="Enemy"))
                return fail("original eligibility/removal controls");
        }
        ++cases;
    }
    std::cout << "superweapon mutation full health: cases=" << cases
              << " typedMax=333 hostMax=666 IDs/owner/eligibility PASS\n";
    return true;
}

bool smokeSuperWeaponDetails() {
    if (!smokeGeneticMutationFullHealth()) return false;
    SuperWeaponRule rule;
    rule.id = "DETAIL";
    rule.type = rule.action = "PsychicDominator";
    rule.targetMode = "Area";
    rule.firstAnim = "FIRST";
    rule.durationFrames = 80;
    rule.fireAtPercentage = 20;
    rule.isPowered = false;
    SuperWeaponWorldContext context;
    context.fixedTickHz = 15;
    context.mapWidth = context.mapHeight = 64;
    context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
        return x >= 0 && y >= 0 && x < 64 && y < 64;
    };
    context.ownerPowered = [](const std::string&) { return true; };
    context.animationDurationTicks = [](const std::string&) { return 50; };
    std::vector<SuperWeaponImpactEvent> impacts;
    context.onSuperWeaponImpact = [&](SuperWeaponImpactEvent event) { impacts.push_back(std::move(event)); };
    auto arm = [&](SuperWeaponSystem& system) {
        return system.grant("A", "DETAIL") && system.forceReady("A", "DETAIL")
            && system.beginTargetSelection("A", "DETAIL", context)
            && system.submitTarget("A", "DETAIL", {{24, 24}, -1}, context);
    };
    auto restore = [&](const SuperWeaponSystem& source, SuperWeaponSystem& target,
        const SuperWeaponRule& definition) {
        std::ostringstream text;
        source.save(text);
        IniFile ini;
        target.reset(std::vector<SuperWeaponRule>{definition});
        return ini.loadFromString(text.str()) && target.load(ini)
            && source.stateHash() == target.stateHash();
    };
    SuperWeaponSystem system;
    system.reset(std::vector<SuperWeaponRule>{rule});
    if (!arm(system)) return false;
    for (int tick = 1; tick <= 10; ++tick) {
        context.tick = tick;
        system.tick(context);
    }
    if (!impacts.empty()) return false;
    SuperWeaponSystem restored;
    if (!restore(system, restored, rule)) return false;
    context.tick = 11;
    restored.tick(context);
    if (impacts.size() != 1 || impacts.front().tick != 11) return false;
    if (!restore(restored, system, rule)) return false;
    context.tick = 12;
    system.tick(context);
    if (impacts.size() != 1) return false;

    rule.type = rule.action = "LightningStorm";
    rule.durationFrames = 100;
    rule.scatterDelayFrames = 1;
    rule.cellSpread = 5;
    rule.separation = 3;
    rule.cloudAnim = "CLOUD_A,CLOUD_B,CLOUD_C";
    rule.boltAnim = "BOLT_A,BOLT_B";
    context.animationDurationTicks = [](const std::string&) { return 6; };
    context.tick = 0;
    impacts.clear();
    system.reset(std::vector<SuperWeaponRule>{rule});
    if (!arm(system)) return false;
    for (int tick = 1; tick <= 14; ++tick) {
        context.tick = tick;
        system.tick(context);
    }
    if (impacts.empty() || !restore(system, restored, rule)) return false;
    std::set<std::string> cloudVariants;
    for (const auto& impact : impacts) if (impact.kind == "LightningCloud") cloudVariants.insert(impact.animationId);
    std::vector<SuperWeaponImpactEvent> uninterrupted;
    auto sourceContext = context;
    sourceContext.onSuperWeaponImpact = [&](SuperWeaponImpactEvent event) {
        uninterrupted.push_back(std::move(event));
    };
    for (int tick = 15; tick <= 50; ++tick) {
        sourceContext.tick = context.tick = tick;
        system.tick(sourceContext);
        restored.tick(context);
        if (system.stateHash() != restored.stateHash()) return false;
    }
    for (std::size_t i = 0; i < impacts.size(); ++i) {
        if (impacts[i].kind != "LightningCloud") continue;
        cloudVariants.insert(impacts[i].animationId);
        for (std::size_t j = 0; j < i; ++j) {
            if (impacts[j].kind != "LightningCloud") continue;
            if (impacts[i].tick - impacts[j].tick >= 6) continue;
            const auto dx = impacts[i].cell.x - impacts[j].cell.x;
            const auto dy = impacts[i].cell.y - impacts[j].cell.y;
            if (std::abs(dx) + std::abs(dy) < 3) return false;
        }
    }
    if (cloudVariants.size() != 3 || cloudVariants.count("") != 0) return false;
    std::vector<SuperWeaponImpactEvent> resumed;
    std::copy_if(impacts.begin(), impacts.end(), std::back_inserter(resumed),
        [](const auto& impact) { return impact.tick >= 15; });
    if (resumed.size() != uninterrupted.size()) return false;
    for (std::size_t index = 0; index < resumed.size(); ++index) {
        if (resumed[index].tick != uninterrupted[index].tick
            || resumed[index].cell != uninterrupted[index].cell
            || resumed[index].kind != uninterrupted[index].kind
            || resumed[index].animationId != uninterrupted[index].animationId
            || resumed[index].secondaryAnimationId != uninterrupted[index].secondaryAnimationId)
            return false;
    }
    // Retail executable 0x53A944..0x53AA92: globally phased clocks and
    // an inclusive square, even at corners outside a radius-five circle.
    rule.cellSpread=10; rule.separation=0; rule.hitDelayFrames=10;
    rule.scatterDelayFrames=5; rule.durationFrames=200;
    context.tick=0; impacts.clear(); system.reset(std::vector<SuperWeaponRule>{rule});
    if(!arm(system)) return false;
    for(int tick=1;tick<=240;++tick){context.tick=tick;system.tick(context);}
    bool squareCorner=false;
    for(const auto& impact:impacts) if(impact.kind=="LightningCloud") {
        const int dx=impact.cell.x-24,dy=impact.cell.y-24;
        if(std::abs(dx)>5 || std::abs(dy)>5 || impact.tick%5!=0)return false;
        squareCorner=squareCorner || dx*dx+dy*dy>25;
    }
    if(!squareCorner)return false;
    // An old active storm saved already-resolved bolts and a moving scatter
    // clock. Migrating it must not replay hits, warning sounds, or its timer.
    context.tick=0; impacts.clear(); system.reset(std::vector<SuperWeaponRule>{rule});
    if(!arm(system))return false;
    for(int tick=1;tick<=10;++tick){context.tick=tick;system.tick(context);}
    std::ostringstream current; system.save(current);
    std::istringstream lines(current.str()); std::ostringstream legacy; std::string line;
    while(std::getline(lines,line)) {
        if(line=="Version=8")line="Version=6";
        if(line.rfind("NextImpactTick=",0)==0)line="NextImpactTick=-1";
        if(line.rfind("NextEffectTick=",0)==0)line="NextEffectTick=15";
        if(line.rfind("ActiveUntilTick=",0)==0)line="ActiveUntilTick=201";
        if(line.rfind("StormCloud",0)==0 && line.rfind("StormCloudCount",0)!=0) {
            auto comma=line.find(',');
            for(int n=0;n<2 && comma!=std::string::npos;++n)comma=line.find(',',comma+1);
            if(comma!=std::string::npos)line.resize(comma);
        }
        legacy<<line<<'\n';
    }
    IniFile legacyIni; restored.reset(std::vector<SuperWeaponRule>{rule});
    if(!legacyIni.loadFromString(legacy.str()) || !restored.load(legacyIni))return false;
    impacts.clear();context.tick=11;restored.tick(context);
    if(!impacts.empty() || !restored.isEnemyStormActive("B",11,{}))return false;
    std::ostringstream migrated;restored.save(migrated);
    if(migrated.str().find("NextImpactTick=201")==std::string::npos)return false;
    std::cout << "superweapon detail timing/cloud separation/save-resume PASS\n";
    return true;
}
} // namespace
