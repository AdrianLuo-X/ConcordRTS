#include "SuperWeaponSystem.h"

int main() {
    const int result = SuperWeaponSystem::runSmokeTest();
    return result == 0 ? SuperWeaponSystem::runOneShotSmokeTest() : result;
}
