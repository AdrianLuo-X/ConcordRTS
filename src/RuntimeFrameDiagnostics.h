#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// Timings describe one completed-paint interval. simulationMs includes every
// simulation step since the preceding completed paint; renderMs is the whole
// current paint. Move command wall time can run inside or outside simulation;
// it must not be added to simulationMs. Remaining stages are nested detail.
enum RuntimeFrameStage : std::uint32_t {
    RuntimeFrameSimulation = 1u << 0,
    RuntimeFrameRender = 1u << 1,
    RuntimeFrameMap = 1u << 2,
    RuntimeFrameFog = 1u << 3,
    RuntimeFrameOverlay = 1u << 4,
    RuntimeFrameResource = 1u << 5,
    RuntimeFramePathfinding = 1u << 6,
    RuntimeFrameSimVision = 1u << 7,
    RuntimeFrameSimMove = 1u << 8,
    RuntimeFrameSimAttack = 1u << 9,
    RuntimeFrameSimEconomy = 1u << 10,
    RuntimeFrameSimEffects = 1u << 11,
    RuntimeFrameSimGuard = 1u << 12,
    RuntimeFrameTerrain = 1u << 13,
    RuntimeFrameWorldObjects = 1u << 14,
    RuntimeFrameLighting = 1u << 15,
    RuntimeFrameSidebar = 1u << 16,
    RuntimeFrameMoveCommand = 1u << 17,
    RuntimeFrameRadar = 1u << 18,
    RuntimeFrameRadarRulesColor = 1u << 19,
    RuntimeFrameMoveComponent = 1u << 20,
};

struct RuntimeFrameCacheCounters {
    bool measured = false;
    std::uint64_t totalMisses = 0;
    std::uint64_t voxelMisses = 0;
    std::uint64_t spriteMisses = 0;
    std::uint64_t paletteMisses = 0;
};

struct RuntimeFrameSample {
    double frameGapMs = 0.0;
    double simulationMs = 0.0;
    double renderMs = 0.0;
    double mapMs = 0.0;
    double fogMs = 0.0;
    double overlayMs = 0.0;
    double resourceMs = 0.0;
    double pathfindingMs = 0.0;
    double simVisionMs = 0.0;
    double simMoveMs = 0.0;
    double simAttackMs = 0.0;
    double simEconomyMs = 0.0;
    double simEffectsMs = 0.0;
    double simGuardMs = 0.0;
    double terrainMs = 0.0;
    double worldObjectsMs = 0.0;
    double lightingMs = 0.0;
    double sidebarMs = 0.0;
    double moveCommandMs = 0.0;
    double radarMs = 0.0;
    double radarRulesColorMs = 0.0;
    double moveComponentMs = 0.0;
    std::uint32_t measuredStages = 0;
    std::uint64_t frameNumber = 0;
    std::uint64_t tick = 0;
    std::uint64_t simTicks = 0;
    std::uint64_t totalObjects = 0;
    std::uint64_t aliveObjects = 0;
    std::uint64_t visibleObjects = 0;
    int gameSpeed = 0;
    int width = 0;
    int height = 0;
    bool hiddenWindow = false;
    RuntimeFrameCacheCounters cacheCounters;
    // Compatibility name: this stores a resource kind (for example SHP/VXL),
    // not an identified unit type or proof that the resource caused the stall.
    std::array<char, 48> suspectUnitType{};
    std::array<char, 80> suspectResource{};

    // Retain a bounded resource leaf/kind only, excluding paths and control
    // characters even if a caller supplies a full resource path accidentally.
    void setSuspectResource(std::string_view kind, std::string_view resource) noexcept {
        setLabel(suspectUnitType, kind);
        setLabel(suspectResource, resource);
    }

    template <std::size_t N>
    static void setLabel(std::array<char, N>& output, std::string_view text) noexcept {
        output.fill('\0');
        const auto slash = text.find_last_of("/\\");
        if (slash != std::string_view::npos) text.remove_prefix(slash + 1);
        std::size_t written = 0;
        for (char character : text) {
            if (written + 1 >= N) break;
            if ((character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '.' || character == '_' || character == '-') {
                output[written++] = character;
            }
        }
    }
};

// One fixed-size accumulator per paint interval. A valid interval with no
// commands is measured zero; an invalid timer makes that interval unmeasured.
// Generation prevents a command that straddles a paint/menu/video reset from
// attaching its full duration to a different interval.
class RuntimeFrameMoveCommandInterval {
public:
    struct ScopeToken {
        std::uint64_t generation = 0;
        bool ownsTimer = false;
    };
    void reset() noexcept {
        milliseconds_ = 0.0;
        measured_ = true;
        depth_ = 0;
        ++generation_;
    }
    ScopeToken enter() noexcept { return {generation_, depth_++ == 0}; }
    void leave(ScopeToken token, double milliseconds, bool timerValid = true) noexcept {
        if (token.generation != generation_) return;
        if (depth_ != 0) --depth_;
        // A recursive command is already inside its outer command's wall time.
        if (!token.ownsTimer) return;
        if (timerValid) add(milliseconds);
        else {
            milliseconds_ = 0.0;
            measured_ = false;
        }
    }
    void add(double milliseconds) noexcept {
        if (!measured_) return;
        const double total = milliseconds_ + milliseconds;
        if (!std::isfinite(milliseconds) || milliseconds < 0.0
            || !std::isfinite(total)) {
            milliseconds_ = 0.0;
            measured_ = false;
            return;
        }
        milliseconds_ = total;
    }
    void writeTo(RuntimeFrameSample& sample) const noexcept {
        sample.moveCommandMs = measured_ ? milliseconds_ : 0.0;
        sample.measuredStages &= ~RuntimeFrameMoveCommand;
        if (measured_) sample.measuredStages |= RuntimeFrameMoveCommand;
    }
    std::uint64_t generation() const noexcept { return generation_; }
private:
    double milliseconds_ = 0.0;
    std::uint64_t generation_ = 0;
    std::uint64_t depth_ = 0;
    bool measured_ = true;
};

// Three nested diagnostic stages, with fixed storage and no clock/IO. A cache
// hit or an interval without that command is measured zero. Reset invalidates
// timers spanning a menu/video/paint boundary; same-stage recursion is timed
// once by its outer scope. Different stages overlap their existing parents.
class RuntimeFrameNestedStageInterval {
public:
    struct ScopeToken {
        std::uint64_t generation = 0;
        std::uint32_t stage = 0;
        bool ownsTimer = false;
    };
    static constexpr std::uint32_t stages = RuntimeFrameRadar
        | RuntimeFrameRadarRulesColor | RuntimeFrameMoveComponent;
    void reset() noexcept {
        milliseconds_ = {}; depth_ = {}; measured_ = stages; ++generation_;
    }
    ScopeToken enter(std::uint32_t stage) noexcept {
        const std::size_t index = stageIndex(stage);
        return {generation_, stage, index < 3 && depth_[index]++ == 0};
    }
    void leave(ScopeToken token, double milliseconds, bool timerValid = true) noexcept {
        const std::size_t index = stageIndex(token.stage);
        if (token.generation != generation_ || index >= 3) return;
        if (depth_[index] != 0) --depth_[index];
        if (!token.ownsTimer) return;
        if (!timerValid || !std::isfinite(milliseconds) || milliseconds < 0.0
            || !std::isfinite(milliseconds_[index] + milliseconds)) {
            milliseconds_[index] = 0.0; measured_ &= ~token.stage;
        } else if (measured_ & token.stage) {
            milliseconds_[index] += milliseconds;
        }
    }
    void writeTo(RuntimeFrameSample& sample) const noexcept {
        sample.measuredStages = (sample.measuredStages & ~stages) | measured_;
        sample.radarMs = measured_ & RuntimeFrameRadar ? milliseconds_[0] : 0.0;
        sample.radarRulesColorMs = measured_ & RuntimeFrameRadarRulesColor ? milliseconds_[1] : 0.0;
        sample.moveComponentMs = measured_ & RuntimeFrameMoveComponent ? milliseconds_[2] : 0.0;
    }
    std::uint64_t generation() const noexcept { return generation_; }
private:
    static std::size_t stageIndex(std::uint32_t stage) noexcept {
        return stage == RuntimeFrameRadar ? 0 : stage == RuntimeFrameRadarRulesColor ? 1
            : stage == RuntimeFrameMoveComponent ? 2 : 3;
    }
    std::array<double, 3> milliseconds_{};
    std::array<std::uint64_t, 3> depth_{};
    std::uint64_t generation_ = 0;
    std::uint32_t measured_ = stages;
};

// Written once after the session loop. This is the EXIT snapshot; none of its
// map or selection fields identify the objects in a retained slow frame.
struct RuntimeFrameSessionMetadata {
    struct SelectionType {
        std::array<char, 48> type{};
        std::array<char, 24> kind{};
        std::uint64_t count = 0, aliveCount = 0;
    };
    std::array<char, 96> finalMapName{}, finalMapFile{};
    std::array<char, 32> finalMapSize{}, finalMapLocalSize{}, capturedUtc{};
    std::uint64_t finalTick = 0, terrainCells = 0;
    std::uint64_t finalSelected = 0, selectedOther = 0, selectedUnresolved = 0;
    std::array<SelectionType, 8> selectionTypes{};
    std::size_t selectionTypeCount = 0;

    // Bound text by complete UTF-8 code points, remove control characters, and
    // exclude path prefixes. Metadata can retain public Chinese map names;
    // the stricter resource-label sanitizer is deliberately not reused here.
    template<std::size_t N>
    static void setText(std::array<char, N>& output, std::string_view text) noexcept {
        output.fill('\0');
        const auto slash = text.find_last_of("/\\");
        if (slash != std::string_view::npos) text.remove_prefix(slash + 1);
        std::size_t written = 0;
        for (std::size_t i = 0; i < text.size();) {
            const unsigned char first = static_cast<unsigned char>(text[i]);
            if (first < 0x20 || first == 0x7f) { ++i; continue; }
            std::size_t bytes = first < 0x80 ? 1 : first >= 0xc2 && first <= 0xdf ? 2
                : first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
            bool valid = bytes != 0 && i + bytes <= text.size();
            for (std::size_t j = 1; valid && j < bytes; ++j)
                valid = (static_cast<unsigned char>(text[i + j]) & 0xc0U) == 0x80U;
            if (valid && bytes >= 3) {
                const unsigned char next = static_cast<unsigned char>(text[i + 1]);
                valid = !(first == 0xe0 && next < 0xa0) && !(first == 0xed && next >= 0xa0)
                    && !(first == 0xf0 && next < 0x90) && !(first == 0xf4 && next >= 0x90);
            }
            if (!valid) { ++i; continue; }
            if (written + bytes >= N) break;
            for (std::size_t j = 0; j < bytes; ++j) output[written++] = text[i + j];
            i += bytes;
        }
    }
    void addSelection(std::string_view type, std::string_view kind, bool alive) noexcept {
        selectionTypeCount = std::min(selectionTypeCount, selectionTypes.size());
        std::array<char, 48> label{}; std::array<char, 24> kindLabel{};
        RuntimeFrameSample::setLabel(label, type);
        RuntimeFrameSample::setLabel(kindLabel, kind);
        for (std::size_t i = 0; i < selectionTypeCount; ++i) {
            if (selectionTypes[i].type == label && selectionTypes[i].kind == kindLabel) {
                ++selectionTypes[i].count;
                if (alive) ++selectionTypes[i].aliveCount;
                return;
            }
        }
        if (selectionTypeCount == selectionTypes.size()) { ++selectedOther; return; }
        auto& entry = selectionTypes[selectionTypeCount++];
        entry.type = label; entry.kind = kindLabel; entry.count = 1; entry.aliveCount = alive ? 1 : 0;
    }
};

struct RuntimeFrameStatistics {
    std::size_t sampleCount = 0;
    double meanMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double worstMs = 0.0;
    double fps = 0.0;
    // Reciprocal of the arithmetic mean of the slowest ceil(N / 100) gaps.
    double onePercentLowFps = 0.0;
};

enum class RuntimeFrameSlowCause {
    MoveCommandWork,
    ResourceWork,
    PathfindingWork,
    VisionWork,
    MovementWork,
    CombatWork,
    EconomyWork,
    EffectsWork,
    GuardWork,
    TerrainWork,
    WorldObjectsWork,
    FogWork,
    LightingWork,
    InteractionWork,
    SidebarWork,
    Simulation,
    Rendering,
    MixedWork,
    Unaccounted,
    // Append to retain numeric values and behavior of existing causes.
    MoveComponentWork,
    RadarWork,
};

struct RuntimeFrameSlowEvent {
    RuntimeFrameSample sample;
    RuntimeFrameCacheCounters cacheGrowth;
    RuntimeFrameSlowCause cause = RuntimeFrameSlowCause::Unaccounted;
    double unaccountedMs = 0.0;
    double sessionMilliseconds = 0.0;
};

struct RuntimeFrameDiagnosticsSnapshot {
    std::uint64_t sessionFrames = 0;
    std::uint64_t rejectedFrames = 0;
    std::uint64_t slow50Frames = 0;
    std::uint64_t slow100Frames = 0;
    double sessionMilliseconds = 0.0;
    double sessionFps = 0.0;
    double worstGapMs = 0.0;
    RuntimeFrameCacheCounters cacheGrowth;
    RuntimeFrameStatistics rolling;
};

// UI-thread-owned and bounded. observe performs no I/O, sorting, heap allocation,
// clocks, world traversal or resource lookup. No destructor writes a report;
// the session owner explicitly writes after leaving its render/simulation loop.
class RuntimeFrameDiagnostics {
public:
    static constexpr std::size_t rollingCapacity = 600;
    static constexpr std::size_t slowEventCapacity = 32;

    explicit RuntimeFrameDiagnostics(bool enabled = true) noexcept : enabled_(enabled) {}

    bool enabled() const noexcept { return enabled_; }

    // Optional baseline captured immediately before entering the session loop.
    // Without it, the first measured counter sample becomes the baseline so
    // preload misses cannot be mistaken for work performed during that frame.
    void primeCacheCounters(const RuntimeFrameCacheCounters& counters) noexcept {
        previousCounters_ = counters;
    }

    bool observe(const RuntimeFrameSample& input) noexcept {
        if (!enabled_) return false;
        if (!(input.frameGapMs > 0.0) || !std::isfinite(input.frameGapMs)) {
            ++rejectedFrames_;
            return false;
        }
        RuntimeFrameSample sample = input;
        sanitizeStage(sample.simulationMs, RuntimeFrameSimulation, sample.measuredStages);
        sanitizeStage(sample.renderMs, RuntimeFrameRender, sample.measuredStages);
        sanitizeStage(sample.mapMs, RuntimeFrameMap, sample.measuredStages);
        sanitizeStage(sample.fogMs, RuntimeFrameFog, sample.measuredStages);
        sanitizeStage(sample.overlayMs, RuntimeFrameOverlay, sample.measuredStages);
        sanitizeStage(sample.resourceMs, RuntimeFrameResource, sample.measuredStages);
        sanitizeStage(sample.pathfindingMs, RuntimeFramePathfinding, sample.measuredStages);
        sanitizeStage(sample.simVisionMs, RuntimeFrameSimVision, sample.measuredStages);
        sanitizeStage(sample.simMoveMs, RuntimeFrameSimMove, sample.measuredStages);
        sanitizeStage(sample.simAttackMs, RuntimeFrameSimAttack, sample.measuredStages);
        sanitizeStage(sample.simEconomyMs, RuntimeFrameSimEconomy, sample.measuredStages);
        sanitizeStage(sample.simEffectsMs, RuntimeFrameSimEffects, sample.measuredStages);
        sanitizeStage(sample.simGuardMs, RuntimeFrameSimGuard, sample.measuredStages);
        sanitizeStage(sample.terrainMs, RuntimeFrameTerrain, sample.measuredStages);
        sanitizeStage(sample.worldObjectsMs, RuntimeFrameWorldObjects, sample.measuredStages);
        sanitizeStage(sample.lightingMs, RuntimeFrameLighting, sample.measuredStages);
        sanitizeStage(sample.sidebarMs, RuntimeFrameSidebar, sample.measuredStages);
        sanitizeStage(sample.moveCommandMs, RuntimeFrameMoveCommand, sample.measuredStages);
        sanitizeStage(sample.radarMs, RuntimeFrameRadar, sample.measuredStages);
        sanitizeStage(sample.radarRulesColorMs, RuntimeFrameRadarRulesColor, sample.measuredStages);
        sanitizeStage(sample.moveComponentMs, RuntimeFrameMoveComponent, sample.measuredStages);
        const auto growth = observeCounters(sample.cacheCounters);
        gaps_[nextGap_] = sample.frameGapMs;
        nextGap_ = (nextGap_ + 1) % rollingCapacity;
        gapCount_ = std::min(gapCount_ + 1, rollingCapacity);
        ++sessionFrames_;
        sessionMilliseconds_ += sample.frameGapMs;
        worstGapMs_ = std::max(worstGapMs_, sample.frameGapMs);
        if (sample.frameGapMs >= 50.0) {
            ++slow50Frames_;
            if (sample.frameGapMs >= 100.0) ++slow100Frames_;
            // Fixed-size copies and a maximum of 32 comparisons on slow frames.
            std::size_t slot = eventCount_;
            if (eventCount_ == slowEventCapacity) {
                slot = 0;
                for (std::size_t index = 1; index < eventCount_; ++index) {
                    if (events_[index].sample.frameGapMs < events_[slot].sample.frameGapMs)
                        slot = index;
                }
                if (sample.frameGapMs <= events_[slot].sample.frameGapMs) return true;
            } else {
                ++eventCount_;
            }
            sanitizeLabel(sample.suspectUnitType);
            sanitizeLabel(sample.suspectResource);
            events_[slot] = {sample, growth, classify(sample),
                // Their overlap is unknown. max is a conservative union lower
                // bound; adding both would count in-simulation commands twice.
                std::max(0.0, sample.frameGapMs - sample.renderMs
                    - std::max(sample.simulationMs, sample.moveCommandMs)),
                sessionMilliseconds_};
        }
        return true;
    }

    RuntimeFrameDiagnosticsSnapshot snapshot() const noexcept {
        RuntimeFrameDiagnosticsSnapshot result;
        result.sessionFrames = sessionFrames_;
        result.rejectedFrames = rejectedFrames_;
        result.slow50Frames = slow50Frames_;
        result.slow100Frames = slow100Frames_;
        result.sessionMilliseconds = sessionMilliseconds_;
        result.sessionFps = sessionMilliseconds_ > 0.0
            ? static_cast<double>(sessionFrames_) * 1000.0 / sessionMilliseconds_ : 0.0;
        result.worstGapMs = worstGapMs_;
        result.cacheGrowth = cacheGrowth_;
        result.rolling.sampleCount = gapCount_;
        if (gapCount_ == 0) return result;
        auto ordered = gaps_;
        std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(gapCount_));
        double sum = 0.0;
        for (std::size_t index = 0; index < gapCount_; ++index) sum += ordered[index];
        auto& rolling = result.rolling;
        rolling.meanMs = sum / static_cast<double>(gapCount_);
        rolling.fps = 1000.0 / rolling.meanMs;
        rolling.p95Ms = ordered[(gapCount_ * 95 + 99) / 100 - 1];
        rolling.p99Ms = ordered[(gapCount_ * 99 + 99) / 100 - 1];
        rolling.worstMs = ordered[gapCount_ - 1];
        const std::size_t lowCount = (gapCount_ + 99) / 100;
        double slowSum = 0.0;
        for (std::size_t index = gapCount_ - lowCount; index < gapCount_; ++index)
            slowSum += ordered[index];
        rolling.onePercentLowFps = static_cast<double>(lowCount) * 1000.0 / slowSum;
        return result;
    }

    std::span<const RuntimeFrameSlowEvent> slowEvents() const noexcept {
        return {events_.data(), eventCount_};
    }

    void writeReport(std::ostream& output, const RuntimeFrameSessionMetadata* metadata = nullptr) const {
        const auto state = snapshot();
        output << std::fixed << std::setprecision(3)
            << "ConcordRTS 游戏会话性能报告\n"
            << "口径：两次完成绘制之间的时间；包含模拟与 CPU/GDI 绘制。\n"
            << "这不是显示器/DWM 实际呈现帧率，也不是任意对局最低帧率保证。\n"
            << "仅保存汇总与最多 " << slowEventCapacity << " 个最慢样本；没有玩家姓名、完整路径或世界快照。\n\n"
            << "采样帧数 / 无效间隔：" << state.sessionFrames << " / " << state.rejectedFrames << '\n'
            << "采样时长（秒）：" << state.sessionMilliseconds / 1000.0 << '\n'
            << "全会话绘制更新率：" << state.sessionFps << " 次/秒\n"
            << "最长帧间隔：" << state.worstGapMs << " ms\n"
            << ">=50 ms / >=100 ms 帧数：" << state.slow50Frames << " / " << state.slow100Frames << '\n'
            << "末尾滚动窗口（最多 " << rollingCapacity << " 帧）：" << state.rolling.sampleCount << " 帧\n"
            << "窗口更新率 / P95 / P99：" << state.rolling.fps << " 次/秒 / "
            << state.rolling.p95Ms << " ms / " << state.rolling.p99Ms << " ms\n"
            << "窗口 1% low：" << state.rolling.onePercentLowFps << " 次/秒\n"
            << "1% low 为窗口最慢 ceil(帧数/100) 个间隔平均值的倒数；不足100帧时仅供参考。\n";
        writeCounters(output, "资源缓存未命中累计增长", state.cacheGrowth);
        output << "\n慢帧样本（最慢优先）：\n"
            << "各子阶段嵌套在模拟或绘制内，不可相加；未采样阶段写 n/a。\n"
            << "移动命令规划时间覆盖整个命令处理入口，可能与模拟总时间重叠，不能相加；墙钟耗时不能证明 CPU 占用或某个单位是根因。\n"
            << "moveComponentMs 嵌套在移动命令内；radarMs 为雷达重建，radarRulesColorMs 嵌套在雷达重建内；这些子项不再加到总时间。\n"
            << "未运行连通性规划或雷达重建（包括雷达热缓存）记已测 0；无效或未接入的计时记 n/a。\n"
            << "unaccountedMs 按 gap-render-max(simulation,moveCommand) 保守估算；命令可能在模拟内或外，不能视为精确未归因时间。\n"
            << "原因是已测阶段占比给出的线索；缓存未命中或资源名不等于已证明根因。\n"
            << "未归因时间可能含等待、消息处理、抢占与未覆盖阶段，不能直接解释成 GPU 瓶颈。\n";
        auto ordered = events_;
        std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(eventCount_),
            [](const auto& left, const auto& right) {
                return left.sample.frameGapMs > right.sample.frameGapMs;
            });
        for (std::size_t index = 0; index < eventCount_; ++index) {
            const auto& event = ordered[index];
            const auto& sample = event.sample;
            output << "\nframe=" << sample.frameNumber << " tick=" << sample.tick
                << " elapsedSeconds=" << event.sessionMilliseconds / 1000.0
                << " gapMs=" << sample.frameGapMs << " cause=" << causeName(event.cause)
                << " hidden=" << (sample.hiddenWindow ? "yes" : "no") << '\n';
            writeStage(output, "simulationMs", sample.simulationMs, RuntimeFrameSimulation, sample);
            writeStage(output, "renderMs", sample.renderMs, RuntimeFrameRender, sample);
            writeStage(output, "mapMs", sample.mapMs, RuntimeFrameMap, sample);
            writeStage(output, "fogMs", sample.fogMs, RuntimeFrameFog, sample);
            writeStage(output, "overlayMs", sample.overlayMs, RuntimeFrameOverlay, sample);
            writeStage(output, "resourceMs", sample.resourceMs, RuntimeFrameResource, sample);
            writeStage(output, "pathfindingMs", sample.pathfindingMs, RuntimeFramePathfinding, sample);
            writeStage(output, "moveCommandMs", sample.moveCommandMs, RuntimeFrameMoveCommand, sample);
            writeStage(output, "moveComponentMs", sample.moveComponentMs, RuntimeFrameMoveComponent, sample);
            output << "commandStage=" << ((sample.measuredStages & RuntimeFrameMoveCommand) ? "yes" : "no") << ' ';
            output << '\n';
            writeStage(output, "simVisionMs", sample.simVisionMs, RuntimeFrameSimVision, sample);
            writeStage(output, "simMoveMs", sample.simMoveMs, RuntimeFrameSimMove, sample);
            writeStage(output, "simAttackMs", sample.simAttackMs, RuntimeFrameSimAttack, sample);
            writeStage(output, "simEconomyMs", sample.simEconomyMs, RuntimeFrameSimEconomy, sample);
            writeStage(output, "simEffectsMs", sample.simEffectsMs, RuntimeFrameSimEffects, sample);
            writeStage(output, "simGuardMs", sample.simGuardMs, RuntimeFrameSimGuard, sample);
            output << '\n';
            writeStage(output, "terrainMs", sample.terrainMs, RuntimeFrameTerrain, sample);
            writeStage(output, "worldObjectsMs", sample.worldObjectsMs, RuntimeFrameWorldObjects, sample);
            writeStage(output, "lightingMs", sample.lightingMs, RuntimeFrameLighting, sample);
            writeStage(output, "sidebarMs", sample.sidebarMs, RuntimeFrameSidebar, sample);
            writeStage(output, "radarMs", sample.radarMs, RuntimeFrameRadar, sample);
            writeStage(output, "radarRulesColorMs", sample.radarRulesColorMs, RuntimeFrameRadarRulesColor, sample);
            output << '\n';
            output << "unaccountedMs=" << event.unaccountedMs << " simTicks=" << sample.simTicks
                << " objects=" << sample.totalObjects << " alive=" << sample.aliveObjects
                << " visible=" << sample.visibleObjects << " speed=" << sample.gameSpeed
                << " size=" << sample.width << 'x' << sample.height << '\n';
            writeCounters(output, "cacheGrowth", event.cacheGrowth);
            output << "resourceKind=" << (sample.suspectUnitType[0] ? sample.suspectUnitType.data() : "n/a")
                << " suspectResource=" << (sample.suspectResource[0] ? sample.suspectResource.data() : "n/a") << '\n';
        }
        if (metadata) writeMetadata(output, *metadata);
    }

    // Explicitly call once after the session loop with a unique local_data/
    // performance/*.txt path. No files are deleted, and failures do not escape.
    bool writeSessionReport(const std::filesystem::path& path, std::string& error,
        const RuntimeFrameSessionMetadata* metadata = nullptr) const noexcept {
        try {
            error.clear();
            if (!enabled_) return true;
            std::error_code filesystemError;
            if (!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path(), filesystemError);
            if (filesystemError) {
                error = "Could not create the performance report directory.";
                return false;
            }
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) {
                error = "Could not open the performance report.";
                return false;
            }
            writeReport(output, metadata);
            output.flush();
            output.close();
            if (!output) {
                error = "Could not finish writing the performance report.";
                return false;
            }
            return true;
        } catch (...) {
            try { error = "Performance report writing failed."; } catch (...) {}
            return false;
        }
    }

    static RuntimeFrameSlowCause classify(const RuntimeFrameSample& sample) noexcept {
        const double major = sample.frameGapMs * 0.5;
        if ((sample.measuredStages & RuntimeFrameMoveCommand)
            && std::isfinite(sample.moveCommandMs) && sample.moveCommandMs >= major) {
            if (nestedDominates(sample, RuntimeFrameMoveComponent, sample.moveComponentMs,
                    RuntimeFrameMoveCommand, sample.moveCommandMs, major))
                return RuntimeFrameSlowCause::MoveComponentWork;
            return RuntimeFrameSlowCause::MoveCommandWork;
        }
        if ((sample.measuredStages & RuntimeFrameResource) && sample.resourceMs >= major)
            return RuntimeFrameSlowCause::ResourceWork;
        if ((sample.measuredStages & RuntimeFramePathfinding) && sample.pathfindingMs >= major)
            return RuntimeFrameSlowCause::PathfindingWork;
        // These stages may overlap their simulation/render parents. Select
        // the largest measured detail, never sum nested timings to manufacture
        // a dominant phase. Equal durations keep this deterministic order.
        RuntimeFrameSlowCause specific = RuntimeFrameSlowCause::Unaccounted;
        double longestSpecific = 0.0;
        const auto consider = [&](std::uint32_t bit, double duration, RuntimeFrameSlowCause cause) {
            if ((sample.measuredStages & bit) && std::isfinite(duration)
                && duration >= major && duration > longestSpecific) {
                specific = cause;
                longestSpecific = duration;
            }
        };
        consider(RuntimeFrameSimVision, sample.simVisionMs, RuntimeFrameSlowCause::VisionWork);
        consider(RuntimeFrameSimMove, sample.simMoveMs, RuntimeFrameSlowCause::MovementWork);
        consider(RuntimeFrameSimAttack, sample.simAttackMs, RuntimeFrameSlowCause::CombatWork);
        consider(RuntimeFrameSimEconomy, sample.simEconomyMs, RuntimeFrameSlowCause::EconomyWork);
        consider(RuntimeFrameSimEffects, sample.simEffectsMs, RuntimeFrameSlowCause::EffectsWork);
        consider(RuntimeFrameSimGuard, sample.simGuardMs, RuntimeFrameSlowCause::GuardWork);
        consider(RuntimeFrameTerrain, sample.terrainMs, RuntimeFrameSlowCause::TerrainWork);
        consider(RuntimeFrameWorldObjects, sample.worldObjectsMs, RuntimeFrameSlowCause::WorldObjectsWork);
        consider(RuntimeFrameFog, sample.fogMs, RuntimeFrameSlowCause::FogWork);
        consider(RuntimeFrameLighting, sample.lightingMs, RuntimeFrameSlowCause::LightingWork);
        consider(RuntimeFrameOverlay, sample.overlayMs, RuntimeFrameSlowCause::InteractionWork);
        consider(RuntimeFrameSidebar, sample.sidebarMs,
            nestedDominates(sample, RuntimeFrameRadar, sample.radarMs,
                RuntimeFrameSidebar, sample.sidebarMs, major)
                ? RuntimeFrameSlowCause::RadarWork : RuntimeFrameSlowCause::SidebarWork);
        if (specific != RuntimeFrameSlowCause::Unaccounted) return specific;
        if ((sample.measuredStages & RuntimeFrameSimulation) && sample.simulationMs >= major
            && sample.simulationMs >= sample.renderMs) return RuntimeFrameSlowCause::Simulation;
        if ((sample.measuredStages & RuntimeFrameRender) && sample.renderMs >= major)
            return RuntimeFrameSlowCause::Rendering;
        if ((sample.measuredStages & RuntimeFrameSimulation)
            && (sample.measuredStages & RuntimeFrameRender)
            && sample.simulationMs + sample.renderMs >= sample.frameGapMs * 0.6)
            return RuntimeFrameSlowCause::MixedWork;
        return RuntimeFrameSlowCause::Unaccounted;
    }

private:
    static bool nestedDominates(const RuntimeFrameSample& sample,
        std::uint32_t childFlag, double child, std::uint32_t parentFlag,
        double parent, double major) noexcept {
        return (sample.measuredStages & childFlag) && (sample.measuredStages & parentFlag)
            && std::isfinite(child) && std::isfinite(parent) && child >= major
            && child >= parent * 0.5 && child <= parent;
    }

    template<std::size_t N>
    static void writeMetadataText(std::ostream& output, const std::array<char, N>& text) {
        const auto end = std::find(text.begin(), text.end(), '\0');
        std::array<char, N> clean{};
        RuntimeFrameSessionMetadata::setText(clean,
            std::string_view(text.data(), static_cast<std::size_t>(end - text.begin())));
        output << (clean[0] ? clean.data() : "n/a");
    }

    static void writeMetadata(std::ostream& output, const RuntimeFrameSessionMetadata& metadata) {
        output << "\n退出时状态（一次采集）：地图与选择不代表上述最慢帧的对象，也不能证明掉帧原因。\n"
            << "会话统计可能跨载入；finalMap 仅为退出时地图，不能套用于全部历史慢帧。\n"
            << "capturedUtc=";
        writeMetadataText(output, metadata.capturedUtc);
        output << " finalTick=" << metadata.finalTick << " finalMapName=";
        writeMetadataText(output, metadata.finalMapName);
        output << " finalMapFile="; writeMetadataText(output, metadata.finalMapFile);
        output << " finalMapSize="; writeMetadataText(output, metadata.finalMapSize);
        output << " finalMapLocalSize="; writeMetadataText(output, metadata.finalMapLocalSize);
        output << " terrainCells=" << metadata.terrainCells << '\n'
            << "finalSelected=" << metadata.finalSelected << " selectedOther=" << metadata.selectedOther
            << " selectedUnresolved=" << metadata.selectedUnresolved
            << " displayedTypes=" << std::min(metadata.selectionTypeCount, metadata.selectionTypes.size()) << '\n';
        for (std::size_t i = 0; i < std::min(metadata.selectionTypeCount, metadata.selectionTypes.size()); ++i) {
            const auto& entry = metadata.selectionTypes[i];
            output << "finalSelectionType="; writeMetadataText(output, entry.type);
            output << " kind="; writeMetadataText(output, entry.kind);
            output << " count=" << entry.count << " alive=" << entry.aliveCount << '\n';
        }
    }
    static void sanitizeStage(double& value, std::uint32_t bit, std::uint32_t& stages) noexcept {
        if (!(stages & bit) || !std::isfinite(value) || value < 0.0) {
            value = 0.0;
            stages &= ~bit;
        }
    }

    template <std::size_t N>
    static void sanitizeLabel(std::array<char, N>& label) noexcept {
        const auto end = std::find(label.begin(), label.end(), '\0');
        const auto original = label;
        RuntimeFrameSample::setLabel(label,
            std::string_view(original.data(), static_cast<std::size_t>(end - label.begin())));
    }

    RuntimeFrameCacheCounters observeCounters(const RuntimeFrameCacheCounters& current) noexcept {
        RuntimeFrameCacheCounters growth;
        growth.measured = current.measured;
        if (!current.measured) {
            // Avoid attributing misses from an unmeasured interval to the next frame.
            previousCounters_.measured = false;
            return growth;
        }
        if (previousCounters_.measured) {
            const auto delta = [](std::uint64_t after, std::uint64_t before) noexcept {
                return after >= before ? after - before : after;
            };
            growth.totalMisses = delta(current.totalMisses, previousCounters_.totalMisses);
            growth.voxelMisses = delta(current.voxelMisses, previousCounters_.voxelMisses);
            growth.spriteMisses = delta(current.spriteMisses, previousCounters_.spriteMisses);
            growth.paletteMisses = delta(current.paletteMisses, previousCounters_.paletteMisses);
            cacheGrowth_.totalMisses += growth.totalMisses;
            cacheGrowth_.voxelMisses += growth.voxelMisses;
            cacheGrowth_.spriteMisses += growth.spriteMisses;
            cacheGrowth_.paletteMisses += growth.paletteMisses;
        }
        cacheGrowth_.measured = true;
        previousCounters_ = current;
        return growth;
    }

    static const char* causeName(RuntimeFrameSlowCause cause) noexcept {
        switch (cause) {
        case RuntimeFrameSlowCause::MoveComponentWork: return "群体命令连通性规划占主要时间（已测阶段线索）";
        case RuntimeFrameSlowCause::RadarWork: return "雷达重建占主要时间（已测阶段线索）";
        case RuntimeFrameSlowCause::MoveCommandWork: return "移动命令规划占主要时间";
        case RuntimeFrameSlowCause::ResourceWork: return "资源读取/生成占主要时间";
        case RuntimeFrameSlowCause::PathfindingWork: return "寻路占主要时间";
        case RuntimeFrameSlowCause::VisionWork: return "视野占主要时间";
        case RuntimeFrameSlowCause::MovementWork: return "移动占主要时间";
        case RuntimeFrameSlowCause::CombatWork: return "战斗占主要时间";
        case RuntimeFrameSlowCause::EconomyWork: return "经济与生产占主要时间";
        case RuntimeFrameSlowCause::EffectsWork: return "效果更新占主要时间";
        case RuntimeFrameSlowCause::GuardWork: return "警戒与目标查询占主要时间";
        case RuntimeFrameSlowCause::TerrainWork: return "地形绘制占主要时间";
        case RuntimeFrameSlowCause::WorldObjectsWork: return "世界对象绘制占主要时间";
        case RuntimeFrameSlowCause::FogWork: return "黑幕与迷雾占主要时间";
        case RuntimeFrameSlowCause::LightingWork: return "光照占主要时间";
        case RuntimeFrameSlowCause::InteractionWork: return "交互覆盖层占主要时间";
        case RuntimeFrameSlowCause::SidebarWork: return "侧栏占主要时间";
        case RuntimeFrameSlowCause::Simulation: return "模拟占主要时间";
        case RuntimeFrameSlowCause::Rendering: return "绘制占主要时间";
        case RuntimeFrameSlowCause::MixedWork: return "模拟与绘制共同占用";
        default: return "等待或未覆盖阶段，尚未归因";
        }
    }

    static void writeStage(std::ostream& output, const char* name, double value,
        std::uint32_t stage, const RuntimeFrameSample& sample) {
        output << name << '=';
        if (sample.measuredStages & stage) output << value;
        else output << "n/a";
        output << ' ';
    }

    static void writeCounters(std::ostream& output, const char* name,
        const RuntimeFrameCacheCounters& counters) {
        output << name << '=';
        if (!counters.measured) output << "n/a\n";
        else output << counters.totalMisses << " voxel=" << counters.voxelMisses
            << " sprite=" << counters.spriteMisses << " palette=" << counters.paletteMisses << '\n';
    }

    bool enabled_ = true;
    std::array<double, rollingCapacity> gaps_{};
    std::size_t nextGap_ = 0;
    std::size_t gapCount_ = 0;
    std::array<RuntimeFrameSlowEvent, slowEventCapacity> events_{};
    std::size_t eventCount_ = 0;
    std::uint64_t sessionFrames_ = 0;
    std::uint64_t rejectedFrames_ = 0;
    std::uint64_t slow50Frames_ = 0;
    std::uint64_t slow100Frames_ = 0;
    double sessionMilliseconds_ = 0.0;
    double worstGapMs_ = 0.0;
    RuntimeFrameCacheCounters previousCounters_;
    RuntimeFrameCacheCounters cacheGrowth_;
};
