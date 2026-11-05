#pragma once

#include "simulation/combat/targeting/RuntimeWeaponFireFlh.h"

#include <span>

struct WorldObjectInstance;
struct ArtType;

// The result borrows the original live world element. It stays valid until
// that world's object storage is structurally changed or destroyed.
struct RuntimeOpenTransportMuzzle {
    const WorldObjectInstance* host = nullptr;
    RuntimeWeaponFireFlh flh;
};

// Synchronous queries only. The world view and ArtType are borrowed for this
// invocation; the resolver retains neither the provider nor the art pointer.
class RuntimeOpenTransportMuzzleQueries {
public:
    virtual ~RuntimeOpenTransportMuzzleQueries() = default;
    virtual std::span<const WorldObjectInstance> objects() const = 0;
    virtual const ArtType* artTypeForObject(const WorldObjectInstance&) const = 0;
};

std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const RuntimeOpenTransportMuzzleQueries& queries, const WorldObjectInstance& passenger,
    bool openTransport);
