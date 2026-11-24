#include "runtime/session/detail/RuntimeSessionCommands.h"
#include "runtime/session/detail/RuntimeCamouflageStationaryHostAdapter.h"
#include "runtime/session/detail/RuntimeDominatorControlHostAdapter.h"
#include <typeinfo>
#include <tuple>
#include <bit>
#include <type_traits>
#include "runtime/session/detail/RuntimeSuperWeaponAiHostAdapter.h"
#include "../assets/ShpCodecCorpus.h"
#include "../simulation/ai/tactic_reservations/RuntimeComputerTacticReservationsPrivateContract.inl"

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
#include "../simulation/ai/tactic_recruitment/RuntimeComputerTacticRecruitmentPrivateContract.inl"
#include "../simulation/ai/base_plan/RuntimeComputerBasePlanPrivateContract.inl"
#include "../simulation/ai/tactic_production/RuntimeComputerTacticProductionPrivateContract.inl"
#include "../simulation/superweapons/nuclear_death/RuntimeNuclearDeathDetonationPrivateContract.inl"
#include "../simulation/psionic/overload_effects/RuntimeMindControlOverloadEffectsPrivateContract.inl"
#include "../simulation/psionic/dominator_control/RuntimeDominatorControlCompletionPrivateContract.inl"
#include "../simulation/superweapons/animation_timing/RuntimeAnimationTimingPrivateContract.inl"
#include "../simulation/superweapons/lifecycle/RuntimeSuperWeaponLifecyclePrivateContract.inl"
#include "../simulation/abilities/camouflage/RuntimeCamouflageStationaryPrivateContract.inl"
#include "../simulation/abilities/chrono/RuntimeChronoTransportPrivateContract.inl"
#include "../simulation/abilities/chrono/RuntimeChronoLocomotionPrivateContract.inl"
#include "../simulation/abilities/chrono/RuntimeChronoWarpPrivateContract.inl"
#include "../simulation/abilities/chrono/RuntimeTemporalAttackPrivateContract.inl"
#include "../simulation/abilities/chrono/RuntimeTemporalFeedbackPrivateContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteStunPrivateContract.inl"
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
    if(argc==3 && std::string_view(argv[1])=="--parasite-stun") return h02505_private_qualification::run(argv[2]);
    if(argc==4 && std::string_view(argv[1])=="--temporal-feedback") return h02403_private_qualification::run(argv[2],argv[3]);
    if(argc==3 && std::string_view(argv[1])=="--temporal-attack") return h02402_private_qualification::run(argv[2]);
    if(argc==4 && std::string_view(argv[1])=="--chrono-warp") {
        return h02305_private_qualification::run(argv[2],argv[3]);
    }
    if(argc==3 && std::string_view(argv[1])=="--chrono-locomotion") {
        return h02302_private_qualification::run(argv[2]);
    }
    if(argc==3 && std::string_view(argv[1])=="--chrono-transport") {
        return h023_private_qualification::run(argv[2]);
    }
    if(argc==3 && std::string_view(argv[1])=="--camouflage-stationary") {
        return h022_private_qualification::run(argv[2]);
    }
    if(argc==3 && std::string_view(argv[1])=="--superweapon-lifecycle") {
        return h020_private_qualification::run(argv[2]);
    }
    if(argc==4 && std::string_view(argv[1])=="--animation-timing") {
        return h017_private_qualification::run(argv[2],argv[3]);
    }
    if(argc==3 && std::string_view(argv[1])=="--dominator-control") {
        return h015_private_qualification::run(argv[2]);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--mind-overload") {
        return h021_private_qualification::run(argv[2]);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--nuclear-death") {
        return h012_private_qualification::run(argv[2]);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--tactic-production") {
        return h009_private_qualification::run();
    }
    if (argc == 2 && std::string_view(argv[1]) == "--base-plan") {
        return h008_private_qualification::run();
    }
    if (argc == 2 && std::string_view(argv[1]) == "--tactic-recruitment") {
        return h007_private_qualification::run();
    }
    if (argc == 3 && std::string_view(argv[1]) == "--tactic-reservations") {
        return concord_reservation_private_qualification::run(argv[2]);
    }
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
