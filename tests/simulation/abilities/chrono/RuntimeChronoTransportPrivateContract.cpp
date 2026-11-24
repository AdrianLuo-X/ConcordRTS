// Private contract assembly: the standard model is compiled on the original
// registered SpecialMechanisms target; real host qualification stays with
// the existing recovery host and its original diagnostics.
#include "RuntimeChronoTransportPrivateContract.h"
#include "RuntimeChronoTransportAdmissionContract.inl"
#include "RuntimeChronoLocomotionContract.inl"

bool runRuntimeChronoTransportAdmissionContract() {
    return chrono_transport_model_detail::runContract();
}

bool runRuntimeChronoLocomotionContract() {
    return chrono_locomotion_model_detail::runContract();
}

#include "RuntimeChronoStateContract.inl"
#include "RuntimeChronoStateFacadeContract.inl"

bool runRuntimeChronoStateContract() {
    return chrono_state_model_detail::runContract()
        && chrono_state_facade_detail::runContract();
}

#include "RuntimeChronoSpatialContract.inl"

bool runRuntimeChronoSpatialContract() {
    return chrono_spatial_model_detail::runContract();
}

#include "RuntimeChronoWarpContract.inl"

bool runRuntimeChronoWarpContract() {
    return chrono_warp_model_detail::runContract();
}

#include "RuntimeTemporalStateContract.inl"

bool runRuntimeTemporalStateContract() {
    return temporal_state_model_detail::runContract();
}

#include "RuntimeTemporalAttackPolicyContract.inl"

bool runRuntimeTemporalAttackPolicyContract() {
    return temporal_attack_model_detail::runContract();
}

#include "RuntimeTemporalFeedbackPolicyContract.inl"
bool runRuntimeTemporalFeedbackPolicyContract() {
    return temporal_feedback_model_detail::runContract();
}
