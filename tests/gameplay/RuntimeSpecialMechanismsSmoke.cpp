#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"

#include <array>
#include <iostream>
#include <limits>

#include <cstdlib>
#include <string_view>
#include "../simulation/abilities/camouflage/RuntimeCamouflageStationaryContract.inl"
#include "../simulation/abilities/chrono/RuntimeChronoTransportPrivateContract.h"
#include "../simulation/abilities/parasites/RuntimeParasiteStateContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteActionPolicyContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteHostDeathPolicyContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteStunStateContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteAtomicPolicyContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteAdmissionPolicyContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteLaunchPolicyContract.inl"
#include "../simulation/abilities/parasites/RuntimeParasiteImpactPolicyContract.inl"

#include "../simulation/lifecycle/RuntimeLifecycleValuesContract.inl"

#include "../simulation/abilities/parasites/RuntimeParasiteHostEventPolicyContract.inl"

int main() {
    std::cout.setf(std::ios::unitbuf);
    const int result = runRuntimeSpecialMechanismsSmokeTest();
    if (result != 0) return result;

    // Terrain targets use existing ground/bridge save IDs. Check the shared
    // boundary where the two kinds meet, plus IDs that must never launch.
    struct Case { int id; int x; int y; bool bridge; };
    constexpr std::array<Case, 6> valid{{
        {-2959999, 999, 999, false}, {-1960000, 0, 0, false},
        {-1959999, 999, 999, true}, {-960000, 0, 0, true},
        {-2035075, 75, 75, false}, {-1035075, 75, 75, true}
    }};
    for (const auto& test : valid) {
        SpecialCell cell;
        bool bridge = false;
        if (!decodeRuntimeCarrierCellTarget(test.id, cell, bridge)
            || cell.x != test.x || cell.y != test.y || bridge != test.bridge) {
            std::cerr << "Carrier terrain target boundary rejected: " << test.id << '\n';
            return 1;
        }
    }
    constexpr std::array<int, 8> invalid{{
        -2960000, -959999, -2, -1, 0, 1,
        std::numeric_limits<int>::min(), std::numeric_limits<int>::max()
    }};
    for (int id : invalid) {
        SpecialCell cell;
        bool bridge = false;
        if (decodeRuntimeCarrierCellTarget(id, cell, bridge)) {
            std::cerr << "Carrier terrain target accepted invalid ID: " << id << '\n';
            return 1;
        }
    }
    std::cout << "Carrier terrain target boundaries: 6 valid, 8 invalid passed\n";
    const char* mechanism = std::getenv("CONCORD_SPECIAL_MECHANISM");
    if ((mechanism == nullptr || std::string_view(mechanism) == "camouflage")
        && !camouflage_stationary_model_detail::runContract()) return 2;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeChronoTransportAdmissionContract()) return 3;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeChronoLocomotionContract()) return 4;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeChronoStateContract()) return 5;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeChronoSpatialContract()) return 6;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeChronoWarpContract()) return 7;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeTemporalStateContract()) return 8;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeTemporalAttackPolicyContract()) return 9;
    if ((mechanism == nullptr || std::string_view(mechanism) == "chrono-temporal")
        && !runRuntimeTemporalFeedbackPolicyContract()) return 10;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && !parasite_state_model_detail::runContract()) return 11;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && !parasite_action_model_detail::runContract()) return 12;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && !parasite_host_death_model_detail::runContract()) return 13;
    if (!lifecycle_values_model_detail::runContract()) return 14;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && !parasite_stun_model_detail::runContract()) return 15;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && parasite_atomic_model::run() != 0) return 16;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && parasite_admission_model::run() != 0) return 17;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && parasite_launch_model::runLaunch() != 0) return 18;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && parasite_impact_model::runImpact() != 0) return 19;
    if ((mechanism == nullptr || std::string_view(mechanism) == "parasite")
        && parasite_host_event_model::runEvents() != 0) return 20;
    return 0;
}
