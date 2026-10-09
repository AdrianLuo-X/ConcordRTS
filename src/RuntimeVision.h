#pragma once

#include "GameWorld.h"
#include "RulesDatabase.h"

#include <cstdint>
#include <array>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class RuntimeCellVisibility : std::uint8_t {
    Shrouded = 0,
    Fogged = 1,
    Visible = 2
};

struct RuntimeVisionObserverDefinition {
    std::string owner;
    std::vector<std::string> sharedVisionOwners;
};

struct RuntimeVisionObserverStats {
    int validCells = 0;
    int visibleCells = 0;
    int exploredCells = 0;
    int visionSources = 0;
    int spySatSources = 0;
    int gapGeneratorSources = 0;
    int gapShroudedCells = 0;
    int lastUpdateTick = -1;
    std::uint64_t revision = 0;
};

// Immutable, short-lived access to one observer's packed visibility grids.
// Rendering takes this view once per frame instead of normalizing the owner
// name and probing an unordered_map for every terrain cell and TMP pixel.
// The view remains valid until the RuntimeVisionSystem is modified.
struct RuntimeVisionGridView {
    const std::uint8_t* validCells = nullptr;
    const std::uint8_t* visibleCells = nullptr;
    const std::uint8_t* exploredCells = nullptr;
    const std::uint8_t* gapShroudedCells = nullptr;
    std::size_t cellCount = 0;
    std::uint64_t revision = 0;
    std::uint64_t layoutEpoch = 0;

    bool available() const noexcept {
        return validCells != nullptr
            && visibleCells != nullptr
            && exploredCells != nullptr
            && cellCount > 0;
    }

    RuntimeCellVisibility visibilityAtIndex(int index) const noexcept {
        if (!available()) {
            return RuntimeCellVisibility::Visible;
        }
        if (index < 0
            || static_cast<std::size_t>(index) >= cellCount
            || validCells[static_cast<std::size_t>(index)] == 0) {
            return RuntimeCellVisibility::Shrouded;
        }
        const std::size_t offset = static_cast<std::size_t>(index);
        if (gapShroudedCells != nullptr && gapShroudedCells[offset] != 0) {
            return RuntimeCellVisibility::Shrouded;
        }
        if (visibleCells[offset] != 0) {
            return RuntimeCellVisibility::Visible;
        }
        return exploredCells[offset] != 0
            ? RuntimeCellVisibility::Fogged
            : RuntimeCellVisibility::Shrouded;
    }
};

// Maintains a current-visibility mask and a permanent exploration mask for
// every active house. Explored cells keep revealing live objects until an
// explicit shroud reset or enemy Gap Generator covers them. Current sight is
// tracked separately to explore new cells and pierce Gap Generator shroud.
class RuntimeVisionSystem {
public:
    void swap(RuntimeVisionSystem& other) noexcept;

    void reset();

    bool update(
        const GameWorld& world,
        const RulesDatabase* rules,
        const std::vector<RuntimeVisionObserverDefinition>& observers,
        int tick
    );

    // Independent reference path for exact state comparisons. This derived
    // memo does not affect rules, saved visibility or the simulation clock.
    void setSourceMemoEnabledForDiagnostics(bool enabled) noexcept {
        sourceMemoEnabled_ = enabled;
    }
    std::uint64_t sourceMemoHitsForDiagnostics() const noexcept {
        return sourceMemoHits_;
    }

    bool hasObserver(const std::string& observerOwner) const;
    RuntimeCellVisibility visibilityFor(
        const std::string& observerOwner,
        int cellX,
        int cellY
    ) const;
    bool gridViewFor(
        const std::string& observerOwner,
        RuntimeVisionGridView& view
    ) const;
    // Borrow a validated grid only while this vision system remains unchanged.
    // Cell bounds/indexing stay identical to the scalar owner query.
    static RuntimeCellVisibility visibilityForGridView(
        const RuntimeVisionGridView& view, int cellX, int cellY);
    bool isCellVisibleTo(
        const std::string& observerOwner,
        int cellX,
        int cellY
    ) const;
    bool isCellExploredBy(
        const std::string& observerOwner,
        int cellX,
        int cellY
    ) const;
    bool isOwnerSharedWithObserver(
        const std::string& observerOwner,
        const std::string& subjectOwner
    ) const;
    bool isObjectVisibleTo(
        const std::string& observerOwner,
        const WorldObjectInstance& object
    ) const;
    // Presentation-only location memory. This intentionally ignores the
    // object's live visibility/stealth state and must not be used for target
    // acquisition. It also lets static rubble remain drawn on explored cells
    // after the original structure is no longer alive.
    bool isObjectLocationExploredBy(
        const std::string& observerOwner,
        const WorldObjectInstance& object
    ) const;

    std::uint64_t revisionFor(const std::string& observerOwner) const;
    const RuntimeVisionObserverStats* statsFor(
        const std::string& observerOwner
    ) const;

    std::vector<std::string> observerOwners() const;
    std::vector<int> exploredCellIndices(
        const std::string& observerOwner
    ) const;
    bool restoreExploredCellIndices(
        const std::string& observerOwner,
        const std::vector<int>& cellIndices
    );

    // Scripted reconnaissance and campaign trigger actions use the same
    // permanent exploration mask as ordinary unit sight. Revealed terrain is
    // never covered again by ordinary movement; Fogged records the absence of
    // nearby vision sources without hiding live enemy information.
    bool revealArea(
        const std::string& observerOwner,
        int centerX,
        int centerY,
        int radius
    );
    bool revealMap(const std::string& observerOwner);
    // Public building locations reveal terrain without becoming a sight source
    // or piercing a Gap Generator's reversible shroud layer.
    bool exploreArea(const std::string& observerOwner, int centerX, int centerY, int radius);
    // A shot exposes its fixed firing location to the target's house for five
    // simulated seconds. It neither explores terrain nor detects cloaked units.
    bool notifyWeaponFired(const WorldObjectInstance& attacker,
        const WorldObjectInstance& target, const WeaponType& weapon, int tick, int fixedTickHz);
    std::string serializeFireReveals(int tick) const;
    bool restoreFireReveals(const std::string& text, int tick, int fixedTickHz);
    bool resetShroud(const std::string& observerOwner);
    bool resetShroudArea(
        const std::string& observerOwner,
        int centerX,
        int centerY,
        int radius
    );

private:
    static constexpr int mapCellStride_ = 512;
    static constexpr int mapCellCount_ = mapCellStride_ * mapCellStride_;

    struct ObserverGrid {
        std::string owner;
        std::unordered_set<std::string> sharedOwners;
        std::vector<std::uint8_t> visible;
        std::vector<std::uint8_t> explored;
        // Gap generators add a reversible shroud layer.  Exploration is kept
        // intact underneath it so destroying or depowering the generator
        // immediately restores the previously explored terrain.
        std::vector<std::uint8_t> gapShrouded;
        std::vector<int> visibleIndices;
        std::vector<int> gapShroudedIndices;
        RuntimeVisionObserverStats stats;
        // Exact projected source inputs, rather than a collision-prone hash.
        // A revision check also catches scripted reveals/resets between ticks.
        std::vector<std::array<int, 4>> sourceMemo;
        std::uint64_t sourceMemoRevision = 0;
        bool sourceMemoValid = false;
    };

    std::vector<std::uint8_t> validCells_;
    std::vector<int> validCellIndices_;
    std::unordered_map<std::string, ObserverGrid> observers_;
    using FireRevealKey = std::tuple<std::string, int, int>;
    std::map<FireRevealKey, int> fireReveals_;
    std::uint64_t terrainSignature_ = 0;
    std::uint64_t layoutEpoch_ = 0;
    bool sourceMemoEnabled_ = true;
    std::uint64_t sourceMemoHits_ = 0;

    static std::string canonicalOwner(std::string owner);
    static int cellIndex(int x, int y);
    static int sightForObject(
        const WorldObjectInstance& object,
        const RulesDatabase* rules
    );
    RuntimeCellVisibility visibilityForGrid(
        const ObserverGrid& grid,
        int cellX,
        int cellY
    ) const;
    bool refreshTerrainLayout(const GameWorld& world);
    ObserverGrid& ensureObserver(const std::string& canonicalOwnerId);
    bool revealCell(ObserverGrid& grid, int x, int y);
    bool revealCircle(ObserverGrid& grid, int centerX, int centerY, int radius);
    bool revealAll(ObserverGrid& grid);
    bool applyFireReveals(ObserverGrid& grid, int tick);
    bool shroudGapCircle(
        ObserverGrid& grid,
        int centerX,
        int centerY,
        int radius
    );
    bool revealThroughGapCircle(
        ObserverGrid& grid,
        int centerX,
        int centerY,
        int radius
    );
};

void swap(RuntimeVisionSystem& left, RuntimeVisionSystem& right) noexcept;

bool runRuntimeVisionContractCheck(std::string& errorMessage);
