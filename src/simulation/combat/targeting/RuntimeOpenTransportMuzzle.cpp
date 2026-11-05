#include "simulation/combat/targeting/RuntimeOpenTransportMuzzle.h"

#include "core/io/RuntimeSaveTextFields.h"
#include "data/art/ArtDatabase.h"
#include "world/model/GameWorld.h"

#include <algorithm>
#include <vector>

using runtime_save_text::parseRuntimeInt;
using runtime_save_text::splitRuntimeTokenList;
using runtime_save_text::toLowerAsciiLocal;

std::optional<RuntimeWeaponFireFlh> parseRuntimeWeaponFireFlh(
    const std::string& rawFlh
) {
    const std::vector<std::string> components =
        splitRuntimeTokenList(rawFlh);
    if (components.size() < 3U) {
        return std::nullopt;
    }

    RuntimeWeaponFireFlh flh;
    if (!parseRuntimeInt(components[0], flh.forwardLeptons)
        || !parseRuntimeInt(components[1], flh.lateralLeptons)
        || !parseRuntimeInt(components[2], flh.heightLeptons)) {
        return std::nullopt;
    }
    flh.authored = true;
    return flh;
}

std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const RuntimeOpenTransportMuzzleQueries& queries, const WorldObjectInstance& passenger,
    bool openTransport) {
    if (!openTransport || toLowerAsciiLocal(passenger.mission) != "insidetransport")
        return std::nullopt;
    for (const auto& host : queries.objects()) {
        if (!host.alive || host.health <= 0) continue;
        const auto seat = std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(), passenger.instanceId);
        if (seat == host.transportPassengerIds.end()) continue;
        const auto* art = queries.artTypeForObject(host);
        if (!art) return std::nullopt;
        const auto index = static_cast<std::size_t>(seat - host.transportPassengerIds.begin());
        if (index >= art->alternateFireFLHs.size()) return std::nullopt;
        const auto flh = parseRuntimeWeaponFireFlh(art->alternateFireFLHs[index]);
        if (flh) return RuntimeOpenTransportMuzzle{&host, *flh};
        return std::nullopt;
    }
    return std::nullopt;
}
