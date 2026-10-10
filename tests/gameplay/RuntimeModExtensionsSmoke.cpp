#include "simulation/mod_extensions/RuntimeModExtensions.h"
#include "simulation/superweapons/BuildingSuperWeapons.h"
#include "analysis/reports/ModCompatibilityScanner.h"
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto check = [](bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); };
        IniFile ini;
        check(ini.loadFromFile("tests/fixtures/mod_extensions.ini"), "fixture read");
        RulesDatabase rules; check(rules.loadFromIni(ini), "fixture rules");
        check(rules.findShieldType("basic") && rules.findShieldType("basic")->initialStrength == 100, "shield registry / default strength");
        check(buildingSuperWeapons(*rules.findBuildingType("HOST")) == std::vector<std::string>({"ONE","TWO","THREE","FOUR"}), "extra superweapon ordering/deduplication");
        for (int hz : {15,30,60}) {
            WorldObjectInstance object; object.type = "TARGET"; object.health = 1000;
            runtimeInitializeModExtensions(object, rules, hz);
            check(runtimeAbsorbShieldDamage(object,rules,rules.findWarheadType("NORMAL"),60,15,hz)==0 && object.modExtensions.shieldHealth==40, "shield absorbs separate armor damage");
            check(runtimeAbsorbShieldDamage(object,rules,rules.findWarheadType("NORMAL"),-20,-5,hz)==0 && object.modExtensions.shieldHealth==60, "shield healing");
            check(runtimeAbsorbShieldDamage(object,rules,rules.findWarheadType("PIERCEWH"),60,15,hz)==15 && object.modExtensions.shieldHealth==60, "penetrating warhead");
            check(runtimeAbsorbShieldDamage(object,rules,rules.findWarheadType("NORMAL"),120,30,hz)==15 && object.modExtensions.shieldHealth==0, "shield break passes body armor remainder");
            for (int tick=0;tick<hz*12-1;++tick) runtimeAdvanceModExtensions(object,rules,hz,false,false,true);
            check(object.modExtensions.shieldHealth==0, "respawn fired early");
            runtimeAdvanceModExtensions(object,rules,hz,false,false,true);
            check(object.modExtensions.shieldHealth==50, "respawn game-minute timing");
            for (int tick=0;tick<hz*6;++tick) runtimeAdvanceModExtensions(object,rules,hz,false,false,true);
            check(object.modExtensions.shieldHealth==60, "self-heal cadence");
            check(runtimeAttachEffect(object,rules,"BUFFWH",true,hz), "attach");
            check(runtimeAttachMultiplier(object,rules,&AttachEffectRule::firepower)==2, "live multiplier");
            for(int tick=0;tick<hz;++tick) runtimeAdvanceModExtensions(object,rules,hz,false,false,true);
            const auto paused=runtimeSerializeModExtensions(object.modExtensions);
            object.alive=false;object.mission="InsideTransport";
            for(int tick=0;tick<hz;++tick) runtimeAdvanceModExtensions(object,rules,hz,true,false,true);
            check(runtimeSerializeModExtensions(object.modExtensions)==paused, "transport advanced or discarded retained effects");
            object.alive=true;object.mission="Guard";
            runtimeAdvanceModExtensions(object,rules,hz,false,true,true);
            check(runtimeSerializeModExtensions(object.modExtensions)==paused, "temporal advanced timer");
            check(runtimeAttachEffect(object,rules,"BUFFWH",true,hz) && object.modExtensions.effects.size()==1, "noncumulative refresh");
            check(object.modExtensions.effects[0].elapsedTicks==hz && object.modExtensions.effects[0].remainingTicks==hz*2, "reapply phase or duration");
            for(int tick=0;tick<hz*2;++tick) runtimeAdvanceModExtensions(object,rules,hz,false,false,true);
            check(object.modExtensions.effects.empty() && runtimeAttachMultiplier(object,rules,&AttachEffectRule::firepower)==1, "effect expiry");
            runtimeAttachEffect(object,rules,"STACKWH",true,hz);runtimeAttachEffect(object,rules,"STACKWH",true,hz);
            check(runtimeAttachMultiplier(object,rules,&AttachEffectRule::firepower)==2.25, "cumulative stacks");
            RuntimeModExtensionState decoded;
            const auto saved=runtimeSerializeModExtensions(object.modExtensions);
            check(runtimeParseModExtensions(saved,rules,decoded) && runtimeSerializeModExtensions(decoded)==saved, "full extension state codec");
            check(!runtimeParseModExtensions(saved+"00",rules,decoded) && runtimeSerializeModExtensions(decoded)==saved, "invalid state must not replace output");
            const int before=object.modExtensions.shieldHealth;
            object.type="PLAIN";runtimeInitializeModExtensions(object,rules,hz);
            check(!runtimeActiveShield(object,rules), "conversion to no shield");
            object.type="TARGET";runtimeInitializeModExtensions(object,rules,hz);
            check(object.modExtensions.shieldHealth==before, "reverse conversion lost shield");
            WorldObjectInstance innate;innate.type="BOOST";runtimeInitializeModExtensions(innate,rules,hz);
            for(int tick=0;tick<hz;++tick)runtimeAdvanceModExtensions(innate,rules,hz,false,false,true);
            check(innate.modExtensions.effects.empty() && innate.modExtensions.innateDelayTicks==hz, "innate duration / renewal delay");
            for(int tick=0;tick<hz;++tick)runtimeAdvanceModExtensions(innate,rules,hz,false,false,true);
            check(innate.modExtensions.effects.size()==1, "innate renewal");
        }
        ModCompatibilityScanner scanner;
        {
            IniFile options;
            check(options.loadFromString(
                "[VehicleTypes]\n0=U\n[U]\nStrength=100\nShield.Type=S\nAttachEffect.Duration=-1\n"
                "AttachEffect.InitialDelay=15\nAttachEffect.Cloakable=yes\n[ShieldTypes]\n0=S\n"
                "[S]\nStrength=100\nInitialStrength=40\nPowered=yes\nAbsorbOverDamage=yes\n"
                "SelfHealing=20\nSelfHealing.Rate=0.01\nSelfHealing.RestartInCombatDelay=15\n"
                "[Warheads]\n0=D\n1=F\n2=P\n[D]\nAttachEffect.Duration=-1\nAttachEffect.DiscardOnEntry=yes\n"
                "[F]\nAttachEffect.Duration=15\nAttachEffect.ForceDecloak=yes\nAttachEffect.AnimResetOnReapply=yes\n"
                "[P]\nAttachEffect.Duration=15\nAttachEffect.PenetratesIronCurtain=yes\n"), "option rules");
            RulesDatabase configured;check(configured.loadFromIni(options),"option parse");
            WorldObjectInstance object;object.type="U";runtimeInitializeModExtensions(object,configured,30);
            check(object.modExtensions.effects.empty() && object.modExtensions.shieldHealth==40,"initial delay / initial strength");
            for(int n=0;n<30;++n)runtimeAdvanceModExtensions(object,configured,30,false,false,false);
            check(object.modExtensions.shieldHealth==40 && !runtimeActiveShield(object,configured),"powered shield was active or healed offline");
            check(runtimeAttachFlag(object,configured,&AttachEffectRule::cloakable),"delayed permanent cloaking effect");
            runtimeAdvanceModExtensions(object,configured,30,false,false,true);
            check(runtimeActiveShield(object,configured),"power restoration");
            check(runtimeAbsorbShieldDamage(object,configured,nullptr,100,100,30)==0 && object.modExtensions.shieldHealth==0,"absorb overdamage");
            object.modExtensions.shieldHealth=80;
            runtimeAbsorbShieldDamage(object,configured,nullptr,10,10,30);
            check(object.modExtensions.shieldTimer==30,"combat restart delay in game frames");
            for(int n=0;n<29;++n)runtimeAdvanceModExtensions(object,configured,30,false,false,true);
            check(object.modExtensions.shieldHealth==70,"combat healing restarted early");
            runtimeAdvanceModExtensions(object,configured,30,false,false,true);
            check(object.modExtensions.shieldHealth==90,"fixed health recovery");
            check(!runtimeAttachEffect(object,configured,"F",true,30,true)
                && runtimeAttachEffect(object,configured,"P",true,30,true),"iron curtain penetration policy");
            runtimeAttachEffect(object,configured,"F",true,30);
            check(runtimeAttachFlag(object,configured,&AttachEffectRule::forceDecloak),"forced decloak flag");
            runtimeAdvanceModExtensions(object,configured,30,false,false,true);
            runtimeAttachEffect(object,configured,"F",true,30);
            for(const auto& effect:object.modExtensions.effects)if(effect.sourceType=="F")check(effect.elapsedTicks==0,"reapply resets animation");
            runtimeAttachEffect(object,configured,"D",true,30);
            object.alive=false;object.mission="InsideTransport";
            runtimeAdvanceModExtensions(object,configured,30,true,false,true);
            for(const auto& effect:object.modExtensions.effects)check(effect.sourceType!="D","discard-on-entry retained effect");
            check(!object.modExtensions.effects.empty(),"transport discarded every effect");
        }
        {
            IniFile stacks;
            check(stacks.loadFromString("[VehicleTypes]\n0=U\n[U]\nStrength=100\n"
                "[Warheads]\n0=UP\n1=DOWN\n[UP]\nAttachEffect.Duration=1\n"
                "AttachEffect.Cumulative=yes\nAttachEffect.FirepowerMultiplier=100\n"
                "[DOWN]\nAttachEffect.Duration=1\nAttachEffect.Cumulative=yes\n"
                "AttachEffect.FirepowerMultiplier=0.01\n"), "opposing stack rules");
            RulesDatabase balanced;check(balanced.loadFromIni(stacks), "opposing stack parse");
            WorldObjectInstance object;object.type="U";
            for(int n=0;n<128;++n)runtimeAttachEffect(object,balanced,"UP",true,30);
            for(int n=0;n<128;++n)runtimeAttachEffect(object,balanced,"DOWN",true,30);
            check(std::abs(runtimeAttachMultiplier(object,balanced,&AttachEffectRule::firepower)-1.0)<1e-10,
                "opposing stacks must cancel before the final bound");
            runtimeAdvanceModExtensions(object,balanced,30,false,false,true);
            check(runtimeAdvanceModExtensions(object,balanced,30,false,false,true)
                && object.modExtensions.effects.empty(), "final expiry must request a repaint");
            for(int n=0;n<256;++n)runtimeAttachEffect(object,balanced,"UP",true,30);
            check(runtimeAttachMultiplier(object,balanced,&AttachEffectRule::firepower)==100.0,
                "maximum stacks must remain finite and bounded");
            // A valid maximum-size effect set must also survive saving.
            const std::string longId(256, 'X');
            IniFile longNames;
            check(longNames.loadFromString("[VehicleTypes]\n0=U\n[U]\nStrength=100\n[Warheads]\n0="+longId
                +"\n["+longId+"]\nAttachEffect.Duration=-1\nAttachEffect.Cumulative=yes\n"), "long effect id fixture");
            RulesDatabase longRules;check(longRules.loadFromIni(longNames), "long effect id parse");
            WorldObjectInstance maximum;maximum.type="U";
            for(int n=0;n<256;++n)check(runtimeAttachEffect(maximum,longRules,longId,true,30), "valid maximum effect count");
            runtimeInitializeModExtensions(maximum,longRules,30);
            const auto maximumSave=runtimeSerializeModExtensions(maximum.modExtensions);
            RuntimeModExtensionState maximumRestored;
            check(runtimeParseModExtensions(maximumSave,longRules,maximumRestored)
                && runtimeSerializeModExtensions(maximumRestored)==maximumSave, "maximum valid effect state could not be restored");
        }
        auto report=scanner.scan(ini,rules,nullptr,nullptr,{});
        for(const auto& tag:report.tagHits)check(tag.implemented,"baseline field incorrectly unsupported");
        IniFile unsupported;check(unsupported.loadFromString("[TARGET]\nShield.Strength=400\nSuperWeapon3=ONE\nAttachEffect.Mystery=yes\n"),"unsupported fixture");
        report=scanner.scan(unsupported,rules,nullptr,nullptr,{});
        check(report.tagHits.size()==3,"unsupported fields missing from report");
        for(const auto& tag:report.tagHits)check(!tag.implemented,"unsupported field claimed implemented");
        std::cout << "Mod extension core passed: 15/30/60Hz, shield, effects, save, conversion and honest scanner\n";
        return 0;
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
