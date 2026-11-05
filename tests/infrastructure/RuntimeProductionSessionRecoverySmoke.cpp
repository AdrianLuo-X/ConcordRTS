#include "runtime/session/detail/RuntimeSessionCommands.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
using namespace concord::runtime::session_detail;

class RecoveryPresentationClock final : public RuntimeUiClock {
public:
    RuntimeUiTimeMs nowMs() const noexcept override {
        return static_cast<RuntimeUiTimeMs>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

const RecoveryPresentationClock recoveryPresentationClock;

class IndependentRecoveryHost final : public RuntimeProductionHostState {
public:
    IndependentRecoveryHost()
        : RuntimeProductionHostState(recoveryPresentationClock, recoveryPresentationClock) {}
};

#include "runtime/persistence/diagnostics/RuntimeProductionRecoverySetup.inl"
#include "runtime/persistence/diagnostics/RuntimeRecoveryConsistencySmoke.inl"
#include "runtime/session/diagnostics/RuntimeProductionCommittedTickSmoke.inl"
#include "runtime/persistence/diagnostics/RuntimeTeamMissionDelimitedRecoverySmoke.inl"

std::uint32_t evidenceProcessId() noexcept {
#ifdef _WIN32
    return static_cast<std::uint32_t>(::_getpid());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}
}

int main(int argc, char** argv) {
    std::string error;
    if (argc == 2 && std::string_view(argv[1]) == "--committed-tick") {
        if (!runRuntimeProductionCommittedTickSmoke<IndependentRecoveryHost>(error)) {
            std::cerr << error << '\n';
            return 1;
        }
        return 0;
    }
    if (!runRuntimeRecoveryConsistencySmoke<IndependentRecoveryHost>(error, evidenceProcessId())
        || !runRuntimeTeamMissionDelimitedRecoverySmoke<IndependentRecoveryHost>(error)) {
        std::cerr << error << '\n';
        return 1;
    }
    return 0;
}
