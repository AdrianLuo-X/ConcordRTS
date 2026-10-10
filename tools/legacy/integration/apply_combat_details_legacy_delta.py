"""Integrate the reviewed combat compatibility delta; --check never writes."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
target = root / "src" / "runtime" / "host" / "RuntimeShell.cpp"
edits = [
    ("migrate and validate old in-flight aggregate actors", '''    RuntimeCombatDetailsState validatedCombatDetails;
    if (!validatedCombatDetails.load(ini, errorMessage)) return false;
''', '''    RuntimeCombatDetailsState validatedCombatDetails;
    if (!validatedCombatDetails.load(ini, errorMessage)
        || !validatedCombatDetails.reconcileLegacyProjectiles(
            ini, state.rules, savedProjectiles.projectiles, errorMessage)) return false;
'''),
    ("retain virtual shrapnel weapon damage contract", '''        const int damage = runtimeWeaponDamageAgainst(
            state,
            *target,
            *childWeapon,
            0.0,
            parent.veteranDamageMultiplier
        );''', '''        const int damage = runtimeAuthoredDamageAgainst(
            state,
            *target,
            static_cast<long long>(childWeapon->damage) * std::max(1, childWeapon->burst),
            childWeapon->warhead,
            0.0,
            parent.veteranDamageMultiplier
        );'''),
    ("bound existing aggregate airburst payload", '''    const int burst = childWeapon->burst <= 0 ? 1 : childWeapon->burst;
    const int damage = static_cast<int>(std::lround(
        static_cast<double>(childWeapon->damage)
            * static_cast<double>(burst)
            * parent.veteranDamageMultiplier
    ));''', '''    const long long authoredDamage = static_cast<long long>(childWeapon->damage)
        * std::max(1, childWeapon->burst);
    const double scaledDamage = static_cast<double>(authoredDamage)
        * parent.veteranDamageMultiplier;
    const int damage = static_cast<int>(std::lround(std::clamp(scaledDamage,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max()))));'''),
    ("terrain damage receives combat actor state", '''int runtimeProjectileTerrainDamageAtDistance(
    const RuntimeProjectileEffect& projectile,''', '''int runtimeProjectileTerrainDamageAtDistance(
    const RuntimeWindowState& state,
    const RuntimeProjectileEffect& projectile,'''),
    ("terrain uses single ordinary rounds and explicit aggregates", '''        baseDamage = static_cast<long long>(weapon->damage)
            * static_cast<long long>(std::max(1, weapon->burst));''', '''        baseDamage = runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon);'''),
    ("forward terrain state", '''        const int damage = runtimeProjectileTerrainDamageAtDistance(
            projectile,''', '''        const int damage = runtimeProjectileTerrainDamageAtDistance(
            state,
            projectile,'''),
    ("clamp terrain magnitude before integer rounding", '''    return static_cast<int>(std::clamp<long double>(
        std::llround(scaled),
        0,
        std::numeric_limits<int>::max()
    ));''', '''    if (!std::isfinite(scaled) || scaled >= std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();
    return std::max(0, static_cast<int>(std::llround(scaled)));'''),
    ("object impact uses single ordinary rounds and explicit aggregates", '''                : runtimeWeaponDamageAgainst(
                state,
                candidate,
                *weapon,
                distance,
                projectile.veteranDamageMultiplier
            ));''', '''                : runtimeAuthoredDamageAgainst(
                state,
                candidate,
                runtimeCombatProjectileAuthoredDamage(state, projectile, *weapon),
                weapon->warhead,
                distance,
                projectile.veteranDamageMultiplier
            ));'''),
    ("cancel pre-transformation burst identity", '''        object->type = loadedTruck->id;
''', '''        cancelRuntimeCombatFire(state, object->instanceId);
        object->type = loadedTruck->id;
'''),
]

data = target.read_bytes()
had_bom = data.startswith(b"\xef\xbb\xbf")
newline = "\r\n" if b"\r\n" in data else "\n"
text = data.decode("utf-8-sig").replace("\r\n", "\n")
for name, old, new in edits:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{name}: expected 1 match, found {count}; no files written")
    text = text.replace(old, new)
if "--check" in sys.argv:
    print(f"{len(edits)} exact combat compatibility edits verified; no files written")
else:
    if target.read_bytes() != data:
        raise SystemExit("RuntimeShell changed during integration; no files written")
    encoded = text.replace("\n", newline).encode("utf-8")
    target.write_bytes((b"\xef\xbb\xbf" if had_bom else b"") + encoded)
    print(f"{len(edits)} combat compatibility edits applied")
