#include "simulation/combat/projectiles/RuntimeMissileAcceleration.h"

// Retail gamemd.exe 0x5B20F0: ROT missiles keep a direction vector, climb
// toward a terrain-relative cruise height, then steer toward the target.
// World Z is in terrain levels; XY and the normalized direction use cells.
// Fractional rule frames adapt the native 15 Hz steering to the fixed clock.


// Startup acceleration uses the same eligibility for ordinary ranged and
// radial launches. Special projectile paths keep their existing cruising step.


// Keep the stock Aegis check-mark launch close to the ship. The immutable
// launch identity survives source destruction and ordinary/canonical restores.







