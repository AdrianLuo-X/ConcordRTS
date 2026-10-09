struct RuntimeOpenTransportMuzzle {
    const WorldObjectInstance* host = nullptr;
    RuntimeWeaponFireFlh flh;
};

std::optional<RuntimeOpenTransportMuzzle> runtimeOpenTransportMuzzle(
    const RuntimeWindowState& state, const WorldObjectInstance& passenger,
    const RuntimeWeaponProfile& profile) {
    if (!profile.openTransport || toLowerAsciiLocal(passenger.mission) != "insidetransport")
        return std::nullopt;
    for (const auto& host : state.world.getObjects()) {
        if (!host.alive || host.health <= 0) continue;
        const auto seat = std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(), passenger.instanceId);
        if (seat == host.transportPassengerIds.end()) continue;
        const auto* art = runtimeArtTypeForObject(state, host);
        if (!art) return std::nullopt;
        const auto index = static_cast<std::size_t>(seat - host.transportPassengerIds.begin());
        if (index >= art->alternateFireFLHs.size()) return std::nullopt;
        const auto flh = parseRuntimeWeaponFireFlh(art->alternateFireFLHs[index]);
        if (flh) return RuntimeOpenTransportMuzzle{&host, *flh};
        return std::nullopt;
    }
    return std::nullopt;
}
