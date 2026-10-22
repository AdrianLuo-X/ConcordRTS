// Literal original bodies, mechanically renamed from the frozen pre-S16 shell.
struct S16OldStorageState {
    struct Economy {
        std::string owner = "AI";
        int credits = 137, harvesters = 2, refineries = 1;
        int storedResourceUnits = 0, storedResourceValue = 0, storedResourceGemUnits = 0;
        int resourceTrips = 3, harvesterTeleports = 4, resourceStorageCapacity = 0;
        int lastResourceStorageCapacity = 0, lastResourceStoredUnits = 0, lastResourceStoredValue = 0;
        int lastResourceStoredGemUnits = 0, lastResourceStorageOverflowUnits = 0;
    } economy;
    struct Rules { double bonus = 0.25; double getPurifierBonus() const { return bonus; } } rules;
    int capacity = 0, ownedPurifiers = 0, virtualPurifiers = 0;
};
int s16OldruntimeOwnedResourceStorageCapacity(const S16OldStorageState& state) { return state.capacity; }
int s16OldruntimeOwnedOrePurifierCount(const S16OldStorageState& state) { return state.ownedPurifiers; }
int s16OldruntimeComputerVirtualPurifierCount(const S16OldStorageState& state) { return state.virtualPurifiers; }
int s16OldruntimeScaleResourceValue(
    int value,
    int acceptedUnits,
    int totalUnits
) {
    if (value <= 0 || acceptedUnits <= 0 || totalUnits <= 0) {
        return 0;
    }
    if (acceptedUnits >= totalUnits) {
        return value;
    }
    const long long scaled =
        (static_cast<long long>(value) * acceptedUnits + totalUnits - 1)
        / totalUnits;
    return static_cast<int>(std::max<long long>(0, scaled));
}

void s16OldrefreshRuntimeResourceStorageCapacity(S16OldStorageState& state) {
    const int capacity = s16OldruntimeOwnedResourceStorageCapacity(state);
    state.economy.resourceStorageCapacity = capacity;
    state.economy.storedResourceUnits =
        std::max(0, state.economy.storedResourceUnits);
    state.economy.storedResourceValue =
        std::max(0, state.economy.storedResourceValue);
    state.economy.storedResourceGemUnits =
        std::clamp(
            state.economy.storedResourceGemUnits,
            0,
            state.economy.storedResourceUnits
        );
    if (capacity <= 0) {
        return;
    }
    if (state.economy.storedResourceUnits <= capacity) {
        return;
    }

    const int previousUnits = state.economy.storedResourceUnits;
    state.economy.storedResourceUnits = capacity;
    state.economy.storedResourceValue =
        s16OldruntimeScaleResourceValue(
            state.economy.storedResourceValue,
            capacity,
            previousUnits
        );
    state.economy.storedResourceGemUnits =
        s16OldruntimeScaleResourceValue(
            state.economy.storedResourceGemUnits,
            capacity,
            previousUnits
        );
}

void s16OldrecordRuntimeResourceDeliveryToStorage(
    S16OldStorageState& state,
    int cargoUnits,
    int cargoGemUnits,
    int deliveredValue
) {
    s16OldrefreshRuntimeResourceStorageCapacity(state);

    const int units = std::max(0, cargoUnits);
    const int gemUnits =
        std::clamp(cargoGemUnits, 0, units);
    const int value = std::max(0, deliveredValue);
    state.economy.lastResourceStorageCapacity =
        state.economy.resourceStorageCapacity;
    state.economy.lastResourceStoredUnits = 0;
    state.economy.lastResourceStoredValue = 0;
    state.economy.lastResourceStoredGemUnits = 0;
    state.economy.lastResourceStorageOverflowUnits = 0;
    if (units <= 0 && value <= 0) {
        return;
    }

    int acceptedUnits = units;
    if (state.economy.resourceStorageCapacity > 0) {
        const int availableUnits =
            std::max(
                0,
                state.economy.resourceStorageCapacity
                    - state.economy.storedResourceUnits
            );
        acceptedUnits = std::min(units, availableUnits);
        state.economy.lastResourceStorageOverflowUnits =
            std::max(0, units - acceptedUnits);
    }

    const int acceptedValue =
        s16OldruntimeScaleResourceValue(value, acceptedUnits, units);
    const int acceptedGemUnits =
        s16OldruntimeScaleResourceValue(gemUnits, acceptedUnits, units);
    state.economy.storedResourceUnits += acceptedUnits;
    state.economy.storedResourceValue += acceptedValue;
    state.economy.storedResourceGemUnits =
        std::clamp(
            state.economy.storedResourceGemUnits + acceptedGemUnits,
            0,
            state.economy.storedResourceUnits
        );
    state.economy.lastResourceStoredUnits = acceptedUnits;
    state.economy.lastResourceStoredValue = acceptedValue;
    state.economy.lastResourceStoredGemUnits = acceptedGemUnits;
}

int s16OldruntimeOrePurifierBonusValue(
    const S16OldStorageState& state,
    int baseValue,
    int* purifierCountOutput = nullptr
) {
    const int purifierCount = s16OldruntimeOwnedOrePurifierCount(state)
        + s16OldruntimeComputerVirtualPurifierCount(state);
    if (purifierCountOutput != nullptr) {
        *purifierCountOutput = purifierCount;
    }
    if (baseValue <= 0 || purifierCount <= 0) {
        return 0;
    }

    const double purifierBonus =
        std::max(0.0, state.rules.getPurifierBonus());
    if (purifierBonus <= 0.0) {
        return 0;
    }

    const int bonusPerPurifier = std::max(
        0,
        static_cast<int>(
            std::floor(static_cast<double>(baseValue) * purifierBonus)
        )
    );
    return purifierCount * bonusPerPurifier;
}

