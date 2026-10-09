// Local recording/viewing state. Never serialized as world authority.
struct RuntimeMatchReplaySession {
    bool recording = false;
    bool viewing = false;
    bool paused = false;
    bool ended = false;
    bool failed = false;
    bool resetPending = false;
    int speedIndex = 2;
    int recordedTick = 0;
    bool viewerDragging = false;
    POINT viewerDragPoint{};
    std::string error;
    std::filesystem::path path;
    std::filesystem::path partialPath;
    std::ofstream output;
    std::ifstream input;
    RuntimeMatchReplayWriter writer;
    RuntimeMatchReplayReader reader;
    RuntimeMatchReplayHeader header;
    std::vector<std::int32_t> selection;
};

bool runtimeViewingMatchReplay(const RuntimeWindowState& state);
bool runtimeUsesGameplayIntentRouter(const RuntimeWindowState& state);
void markRuntimeMatchReplayReset(RuntimeWindowState& state) noexcept;
void recordRuntimeMatchReplaySelection(RuntimeWindowState& state);
void recordRuntimeMatchReplayCommand(RuntimeWindowState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayCommand& command, bool applied);
void recordRuntimeMatchReplayStep(RuntimeWindowState& state, bool lightweight);
bool captureRuntimeOfflineReplayCommand(RuntimeWindowState& state,
    const RuntimeMultiplayerGameplayCommand& command, std::string& error);
