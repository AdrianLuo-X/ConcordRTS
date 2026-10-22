// Private synchronous observations. Ordinary sessions keep current == nullptr.
// Only owned values are stored. No native handle, state root or callback escapes.
namespace RuntimeKeyboardParentDiagnostics {
struct Key { std::uint64_t raw = 0; std::int32_t code = 0; };
struct Event {
    std::string stage, consumer, peer, error;
    int observedTick = -1, frameTick = -1;
    std::uint64_t frameSequence = 0;
    std::size_t commandIndex = 0;
    bool accepted = false, hasCommand = false, encoded = false;
    RuntimeMultiplayerGameplayCommand command;
    std::vector<std::uint8_t> payload;
};
struct Sink {
    std::vector<Key> keys;
    std::vector<Event> events;
    std::vector<std::pair<std::string, RuntimeMultiplayerCommandPacket>> packets;
};
thread_local Sink* current = nullptr;
struct Binding {
    Sink* previous;
    explicit Binding(Sink& sink) : previous(current) { current = &sink; }
    ~Binding() { current = previous; }
};
void key(std::uint64_t raw, std::int32_t code) {
    if (current) current->keys.push_back({raw, code});
}
void command(const char* stage, int tick, const std::string& consumer,
    const RuntimeMultiplayerGameplayCommand& value, bool accepted,
    const std::string& error) {
    if (!current) return;
    Event event; event.stage = stage; event.observedTick = tick;
    event.consumer = consumer; event.command = value; event.accepted = accepted;
    event.hasCommand = true; event.error = error;
    std::string encodingError;
    event.encoded = encodeRuntimeMultiplayerGameplayCommand(value, event.payload, encodingError);
    if (!event.encoded) event.error += encodingError;
    current->events.push_back(std::move(event));
}
void batch(const char* stage, int tick, const std::string& consumer,
    std::span<const RuntimeMultiplayerLockstepApplyCommand> values,
    bool accepted, const std::string& error) {
    if (!current) return;
    for (const auto& item : values) {
        command(stage, tick, consumer, item.command, accepted, error);
        auto& event = current->events.back();
        event.frameTick = item.tick; event.peer = item.peerId;
        event.frameSequence = item.frameSequence; event.commandIndex = item.commandIndex;
    }
}
void packets(const std::string& consumer, const RuntimeMultiplayerRuntimeBridge* bridge) {
    if (!current || !bridge) return;
    // Copy actual admitted envelopes at the existing bridge fixture lifetime
    // boundary. Never reconstruct a wire packet from an intent list.
    for (const auto& packet : bridge->lan().session().commands())
        current->packets.emplace_back(consumer, packet);
}
void advance(int committedTick, int observedTick, const std::string& consumer) {
    if (!current) return;
    Event event; event.stage = "mp-advance"; event.frameTick = committedTick;
    event.observedTick = observedTick; event.consumer = consumer; event.accepted = true;
    current->events.push_back(std::move(event));
}
} // namespace RuntimeKeyboardParentDiagnostics
