#include "runtime/persistence/RuntimeLoadTransaction.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <utility>

// This records transaction phases only. Full real-world save/restore is checked
// by the unchanged production recovery fixture, never by this policy fixture.
enum class Phase { Seed, Materialize, Prepare, Commit, Present };
enum class Failure { None, Standard, Unknown, Reject };
struct RecordingTransaction final : RuntimeLoadTransaction {
    Phase failAt = Phase::Seed;
    Failure failure = Failure::None;
    std::array<Phase, 5> order{};
    std::size_t count = 0;
    bool vision = false, committed = false;
    std::string path;
    int releaseCalls = 0;
    std::string* diagnostic = nullptr;
    bool releasedBeforeExceptionDiagnostic = false;
    void record(Phase phase) noexcept { order[count++] = phase; }
    void inject(Phase phase) const {
        if (phase != failAt) return;
        if (failure == Failure::Standard) throw std::runtime_error("planned");
        if (failure == Failure::Unknown) throw 17;
    }
    void seedCandidate(bool forceVision) override {
        record(Phase::Seed); vision = forceVision; inject(Phase::Seed);
    }
    bool materializeCandidate(const std::string& savePath, std::string& error) override {
        record(Phase::Materialize); path = savePath; inject(Phase::Materialize);
        if (failAt == Phase::Materialize && failure == Failure::Reject) {
            error = "invalid save"; return false;
        }
        return true;
    }
    void prepareCandidate() override { record(Phase::Prepare); inject(Phase::Prepare); }
    void commitCandidate() noexcept override { record(Phase::Commit); committed = true; }
    void presentCommitted() override { record(Phase::Present); inject(Phase::Present); }
    void releaseCandidate() noexcept override {
        ++releaseCalls;
        releasedBeforeExceptionDiagnostic = diagnostic && *diagnostic == "sentinel";
    }
};
static_assert(noexcept(std::declval<RuntimeLoadTransaction&>().commitCandidate()));
static_assert(noexcept(std::declval<RuntimeLoadTransaction&>().releaseCandidate()));

int main() {
    int checks = 0, failures = 0, cases = 0;
    const auto need = [&](bool result, const char* label) {
        ++checks; if (!result) { ++failures; std::cerr << label << '\n'; }
    };
    constexpr std::array sequence{Phase::Seed, Phase::Materialize, Phase::Prepare, Phase::Commit, Phase::Present};
    for (bool vision : {false, true}) {
        for (Phase stage : {Phase::Seed, Phase::Materialize, Phase::Prepare, Phase::Present}) {
            for (Failure failure : {Failure::Standard, Failure::Unknown}) {
                ++cases; RecordingTransaction transaction;
                transaction.failAt = stage; transaction.failure = failure;
                std::string error = "sentinel";
                transaction.diagnostic = &error;
                const bool loaded = executeRuntimeLoadTransaction(transaction, "owned/save", error, vision);
                const bool afterCommit = stage == Phase::Present;
                const std::size_t expected = static_cast<std::size_t>(stage) + 1;
                need(loaded == afterCommit, "return reflects authority commit");
                need(transaction.committed == afterCommit, "failure before commit preserves live authority");
                need(transaction.count == expected, "failure stops later phases");
                for (std::size_t j = 0; j < transaction.count; ++j) need(transaction.order[j] == sequence[j], "exact phase order");
                need(transaction.vision == vision, "vision policy preserved");
                if (stage != Phase::Seed) need(transaction.path == "owned/save", "path borrowed unchanged");
                const std::string expectedError = afterCommit ? "sentinel" : failure == Failure::Standard
                    ? "load materialization failed: planned" : "load materialization failed";
                need(error == expectedError, "original exception text and post-commit best effort");
                need(transaction.releaseCalls == 1, "candidate lifetime ends once on every path");
                need(transaction.releasedBeforeExceptionDiagnostic, "candidate released before catch diagnostics");
            }
        }
        ++cases; RecordingTransaction rejected;
        rejected.failAt = Phase::Materialize; rejected.failure = Failure::Reject;
        std::string error = "sentinel";
        rejected.diagnostic = &error;
        need(!executeRuntimeLoadTransaction(rejected, "owned/save", error, vision), "materializer false preserved");
        need(!rejected.committed && rejected.count == 2 && error == "invalid save", "domain error preserved without commit/presentation");
        need(rejected.releaseCalls == 1, "candidate released on rejected materialization");
        ++cases; RecordingTransaction success; error = "sentinel";
        success.diagnostic = &error;
        need(executeRuntimeLoadTransaction(success, "owned/save", error, vision), "successful commit");
        need(success.count == 5 && success.committed && success.vision == vision, "all five phases exactly once");
        need(error == "sentinel", "success preserves existing error value");
        need(success.releaseCalls == 1 && success.releasedBeforeExceptionDiagnostic, "candidate released on success before return");
        for (std::size_t j = 0; j < sequence.size(); ++j) need(success.order[j] == sequence[j], "success order");
    }
    std::cout << "load transaction policy: cases=" << cases << " checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
