std::vector<std::uint8_t> encodeProbeWorld(const RuntimeProbeWorld& world) {
    std::ostringstream out;
    out << world.tick << ' ' << world.applied.size() << '\n';
    for (const auto& command : world.applied) {
        std::vector<std::uint8_t> payload;
        std::string error;
        if (!encodeRuntimeMultiplayerGameplayCommand(command.command, payload, error)) return {};
        out << command.tick << ' ' << command.frameSequence << ' ' << command.commandIndex << ' '
            << std::quoted(command.peerId) << ' ' << payload.size();
        for (const auto byte : payload) out << ' ' << static_cast<unsigned>(byte);
        out << '\n';
    }
    const auto text = out.str();
    return {text.begin(), text.end()};
}

bool decodeProbeWorld(std::span<const std::uint8_t> bytes, RuntimeProbeWorld& world, std::string& error) {
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    std::size_t count = 0;
    if (!(in >> world.tick >> count) || count > 10000U) { error = "invalid probe snapshot";return false; }
    world.applied.clear();
    for (std::size_t index = 0; index < count; ++index) {
        RuntimeMultiplayerLockstepApplyCommand command;
        std::size_t size = 0;
        if (!(in >> command.tick >> command.frameSequence >> command.commandIndex >> std::quoted(command.peerId) >> size)
            || size > 100000U) { error = "invalid probe command";return false; }
        std::vector<std::uint8_t> payload(size);
        for (auto& byte : payload) {
            unsigned value = 0;
            if (!(in >> value) || value > 255U) { error = "invalid probe command bytes";return false; }
            byte = static_cast<std::uint8_t>(value);
        }
        if (!decodeRuntimeMultiplayerGameplayCommand(payload, command.command, error)) return false;
        world.applied.push_back(std::move(command));
    }
    in >> std::ws;
    if (!in.eof()) { error = "probe snapshot trailing bytes";return false; }
    return true;
}
