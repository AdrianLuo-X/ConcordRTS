#include "simulation/combat/projectiles/RuntimeSonicWaveGeometry.inl"

// A sonic shot has a conventional end hit plus an independently advancing wave.
// Reuse the saved projectile timeline and geometric fields: progress is the
// front, currentXYZ is the trailing point, speed is the front's advance/cycle.

