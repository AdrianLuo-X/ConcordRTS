#include "simulation/vision/RuntimeVision.h"

#include "core/io/IniFile.h"
#include "simulation/buildings/RuntimeSpecialBuildingSystem.h"
#include "simulation/vision/RuntimeVisionTerrainCoordinateLease.h"
#include "simulation/vision/RuntimeVisionLiveSources.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace {

constexpr std::uint64_t fnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

void mixHash(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value;
    hash *= fnvPrime;
}

bool isInsideTransport(const WorldObjectInstance& object) {
    std::string mission = object.mission;
    std::transform(
        mission.begin(),
        mission.end(),
        mission.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return mission == "insidetransport";
}

} // namespace

void RuntimeVisionSystem::swap(RuntimeVisionSystem& other) noexcept {
    static_assert(std::is_nothrow_swappable_v<
        std::vector<std::uint8_t>
    >);
    static_assert(std::is_nothrow_swappable_v<std::vector<int>>);
    static_assert(std::is_nothrow_swappable_v<decltype(observers_)>);
    validCells_.swap(other.validCells_);
    validCellIndices_.swap(other.validCellIndices_);
    observers_.swap(other.observers_);
    fireReveals_.swap(other.fireReveals_);
    using std::swap;
    swap(terrainSignature_, other.terrainSignature_);
    swap(layoutEpoch_, other.layoutEpoch_);
    swap(sourceMemoEnabled_, other.sourceMemoEnabled_);
    swap(sourceMemoHits_, other.sourceMemoHits_);
}

void swap(RuntimeVisionSystem& left, RuntimeVisionSystem& right) noexcept {
    left.swap(right);
}

void RuntimeVisionSystem::reset() {
    validCells_.clear();
    validCellIndices_.clear();
    observers_.clear();
    terrainSignature_ = 0;
    fireReveals_.clear();
    sourceMemoHits_ = 0;
    ++layoutEpoch_;
}

std::string RuntimeVisionSystem::canonicalOwner(std::string owner) {
    const auto notSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    owner.erase(owner.begin(), std::find_if(owner.begin(), owner.end(), notSpace));
    owner.erase(
        std::find_if(owner.rbegin(), owner.rend(), notSpace).base(),
        owner.end()
    );
    std::transform(
        owner.begin(),
        owner.end(),
        owner.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return owner;
}

int RuntimeVisionSystem::cellIndex(int x, int y) {
    if (x < 0 || y < 0 || x >= mapCellStride_ || y >= mapCellStride_) {
        return -1;
    }
    return (y * mapCellStride_) + x;
}

int RuntimeVisionSystem::sightForObject(
    const WorldObjectInstance& object,
    const RulesDatabase* rules
) {
    int sight = 0;
    const VeterancyProfile* veterancy = nullptr;
    if (rules != nullptr) {
        if (object.kind == WorldObjectKind::Structure) {
            const BuildingType* building = rules->findBuildingType(object.type);
            if (building != nullptr) {
                sight = building->sight;
                veterancy = &building->veterancy;
            }
        } else if (object.kind == WorldObjectKind::Infantry) {
            const UnitType* infantry = rules->findUnitType(
                "InfantryType",
                object.type
            );
            if (infantry != nullptr) {
                sight = infantry->sight;
                veterancy = &infantry->veterancy;
            }
        } else {
            const UnitType* unit = rules->findUnitType("UnitType", object.type);
            if (unit == nullptr) {
                unit = rules->findUnitType("AircraftType", object.type);
            }
            if (unit != nullptr) {
                sight = unit->sight;
                veterancy = &unit->veterancy;
            }
        }

        const std::vector<VeteranAbility>* activeAbilities = nullptr;
        if (veterancy != nullptr && object.veterancy >= 2) {
            activeAbilities = &veterancy->eliteAbilities;
        } else if (veterancy != nullptr && object.veterancy >= 1) {
            activeAbilities = &veterancy->veteranAbilities;
        }
        if (activeAbilities != nullptr
            && std::find(
                activeAbilities->begin(),
                activeAbilities->end(),
                VeteranAbility::Sight
            ) != activeAbilities->end()) {
            // EliteAbilities already includes the veteran set. Applying the
            // global multiplier once matches Westwood/huangkaoya semantics;
            // reaching elite does not square VeteranSight.
            const int baseSight = sight > 0 ? sight : 5;
            sight = static_cast<int>(
                static_cast<double>(baseSight)
                * rules->getVeterancyRules().veteranSight
            );
        }
    }

    // A missing/modded Sight value must not turn a live object into a blind
    // spot. Five cells matches the common Westwood baseline while preserving
    // explicitly authored larger ranges.
    return std::clamp(sight > 0 ? sight : 5, 1, 64);
}

bool RuntimeVisionSystem::refreshTerrainLayout(const GameWorld& world) {
    const std::uint64_t signature = runtimeVisionTerrainCoordinateSignature(world,[&] {
        // Keep the exact original ordered count/cellIndex/FNV computation.
        std::uint64_t value = fnvOffset;
        mixHash(value, world.getTerrainCells().size());
        for (const TerrainCell& cell : world.getTerrainCells()) {
            mixHash(value, static_cast<std::uint64_t>(cellIndex(cell.x, cell.y) + 1));
        }
        return value;
    });
    if (signature == terrainSignature_
        && validCells_.size() == static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }

    terrainSignature_ = signature;
    ++layoutEpoch_;
    validCells_.assign(mapCellCount_, 0);
    validCellIndices_.clear();
    validCellIndices_.reserve(world.getTerrainCells().size());
    for (const TerrainCell& cell : world.getTerrainCells()) {
        const int index = cellIndex(cell.x, cell.y);
        if (index < 0 || validCells_[static_cast<std::size_t>(index)] != 0) {
            continue;
        }
        validCells_[static_cast<std::size_t>(index)] = 1;
        validCellIndices_.push_back(index);
    }
    observers_.clear();
    fireReveals_.clear();
    return true;
}

RuntimeVisionSystem::ObserverGrid& RuntimeVisionSystem::ensureObserver(
    const std::string& canonicalOwnerId
) {
    auto [found, inserted] = observers_.try_emplace(canonicalOwnerId);
    ObserverGrid& grid = found->second;
    if (inserted
        || grid.visible.size() != static_cast<std::size_t>(mapCellCount_)) {
        grid.owner = canonicalOwnerId;
        grid.visible.assign(mapCellCount_, 0);
        grid.explored.assign(mapCellCount_, 0);
        grid.gapShrouded.assign(mapCellCount_, 0);
        grid.visibleIndices.clear();
        grid.gapShroudedIndices.clear();
        grid.stats = RuntimeVisionObserverStats{};
        grid.stats.validCells = static_cast<int>(validCellIndices_.size());
    }
    return grid;
}

bool RuntimeVisionSystem::revealCell(ObserverGrid& grid, int x, int y) {
    const int index = cellIndex(x, y);
    if (index < 0
        || validCells_.empty()
        || validCells_[static_cast<std::size_t>(index)] == 0) {
        return false;
    }

    if (grid.visible[static_cast<std::size_t>(index)] == 0) {
        grid.visible[static_cast<std::size_t>(index)] = 1;
        grid.visibleIndices.push_back(index);
    }
    bool explorationChanged = false;
    if (grid.explored[static_cast<std::size_t>(index)] == 0) {
        grid.explored[static_cast<std::size_t>(index)] = 1;
        ++grid.stats.exploredCells;
        explorationChanged = true;
    }
    return explorationChanged;
}

bool RuntimeVisionSystem::revealCircle(
    ObserverGrid& grid,
    int centerX,
    int centerY,
    int radius
) {
    radius = std::clamp(radius, 1, mapCellStride_);
    const int radiusSquared = radius * radius;
    bool changed = false;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if ((dx * dx) + (dy * dy) > radiusSquared) {
                continue;
            }
            changed = revealCell(grid, centerX + dx, centerY + dy) || changed;
        }
    }
    return changed;
}

bool RuntimeVisionSystem::revealAll(ObserverGrid& grid) {
    bool explorationChanged = false;
    for (int index : validCellIndices_) {
        const std::size_t offset = static_cast<std::size_t>(index);
        if (grid.visible[offset] == 0) {
            grid.visible[offset] = 1;
            grid.visibleIndices.push_back(index);
        }
        if (grid.explored[offset] == 0) {
            grid.explored[offset] = 1;
            ++grid.stats.exploredCells;
            explorationChanged = true;
        }
    }
    return explorationChanged;
}

bool RuntimeVisionSystem::shroudGapCircle(
    ObserverGrid& grid,
    int centerX,
    int centerY,
    int radius
) {
    radius = std::clamp(radius, 1, mapCellStride_);
    const int radiusSquared = radius * radius;
    bool changed = false;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if ((dx * dx) + (dy * dy) > radiusSquared) {
                continue;
            }
            const int index = cellIndex(centerX + dx, centerY + dy);
            if (index < 0
                || validCells_.empty()
                || validCells_[static_cast<std::size_t>(index)] == 0) {
                continue;
            }
            const std::size_t offset = static_cast<std::size_t>(index);
            if (grid.gapShrouded[offset] == 0) {
                grid.gapShrouded[offset] = 1;
                grid.gapShroudedIndices.push_back(index);
                changed = true;
            }
        }
    }
    return changed;
}

bool RuntimeVisionSystem::revealThroughGapCircle(
    ObserverGrid& grid,
    int centerX,
    int centerY,
    int radius
) {
    radius = std::clamp(radius, 1, 64);
    const int radiusSquared = radius * radius;
    bool changed = false;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if ((dx * dx) + (dy * dy) > radiusSquared) {
                continue;
            }
            const int index = cellIndex(centerX + dx, centerY + dy);
            if (index < 0 || grid.gapShrouded.empty()) {
                continue;
            }
            const std::size_t offset = static_cast<std::size_t>(index);
            if (grid.gapShrouded[offset] != 0) {
                grid.gapShrouded[offset] = 0;
                changed = true;
            }
        }
    }
    return changed;
}

bool RuntimeVisionSystem::update(
    const GameWorld& world,
    const RulesDatabase* rules,
    const std::vector<RuntimeVisionObserverDefinition>& observerDefinitions,
    int tick
) {
    bool changed = refreshTerrainLayout(world);
    static const bool sourceMemoReference = [] {
        const char* value = std::getenv("CONCORD_VISION_SOURCE_REFERENCE");
        return value != nullptr && std::strcmp(value, "1") == 0;
    }();
    const bool useSourceMemo = sourceMemoEnabled_ && !sourceMemoReference;

    RuntimeVisionLiveSources sourceObjects(world,tick);
    std::unordered_set<int> transportedObjectIds;
    for (const WorldObjectInstance& object : world.getObjects()) {
        sourceObjects.appendDuringCargoScan(object);
        transportedObjectIds.insert(
            object.transportPassengerIds.begin(),
            object.transportPassengerIds.end()
        );
    }

    // Generator activity is observer-independent. Resolve the same world
    // snapshot once, then apply each observer's alliance filter below.
    std::vector<RuntimeSpecialBuildingGap> activeGaps;
    if (rules != nullptr) {
        RuntimeSpecialBuildingContext specialContext;
        specialContext.rules = rules;
        specialContext.objects = &world.getObjects();
        specialContext.canonicalOwnerId = [](const std::string& value) {
            return canonicalOwner(value);
        };
        specialContext.ownerIsNeutral = [](const std::string& value) {
            const std::string key = canonicalOwner(value);
            return key.empty()
                || key == "neutral"
                || key == "civilian"
                || key == "special";
        };
        activeGaps = RuntimeSpecialBuildingSystem::activeGapGenerators(specialContext);
    }

    std::unordered_set<std::string> activeObservers;
    std::vector<std::array<int, 4>> projectedSources;
    if (useSourceMemo) projectedSources.reserve(sourceObjects.sourceCount() + activeGaps.size());
    for (const RuntimeVisionObserverDefinition& definition :
            observerDefinitions) {
        const std::string owner = canonicalOwner(definition.owner);
        if (owner.empty() || !activeObservers.insert(owner).second) {
            continue;
        }

        ObserverGrid& grid = ensureObserver(owner);
        std::unordered_set<std::string> sharedOwners;
        sharedOwners.insert(owner);
        for (const std::string& sharedOwner : definition.sharedVisionOwners) {
            const std::string canonical = canonicalOwner(sharedOwner);
            if (!canonical.empty()) {
                sharedOwners.insert(canonical);
            }
        }
        const bool allianceChanged = grid.sharedOwners != sharedOwners;
        grid.sharedOwners = std::move(sharedOwners);

        if (useSourceMemo) {
            projectedSources.clear();
            for (const WorldObjectInstance& object : sourceObjects.pass()) {
                if (!object.alive
                    || !grid.sharedOwners.contains(canonicalOwner(object.owner))) continue;
                // SpySat eligibility intentionally does not depend on the
                // transport filter used for ordinary sight sources.
                if (rules != nullptr && object.kind == WorldObjectKind::Structure) {
                    const BuildingType* building = rules->findBuildingType(object.type);
                    if (building != nullptr && building->spySat && object.poweredOperational)
                        projectedSources.push_back({1, 0, 0, 0});
                }
                if (isInsideTransport(object) || transportedObjectIds.contains(object.instanceId)) continue;
                const double sourceX = object.hasRenderPosition ? object.renderX : object.x;
                const double sourceY = object.hasRenderPosition ? object.renderY : object.y;
                projectedSources.push_back({0, static_cast<int>(std::llround(sourceX)),
                    static_cast<int>(std::llround(sourceY)), sightForObject(object, rules)});
            }
            for (const RuntimeSpecialBuildingGap& gap : activeGaps) {
                if (!grid.sharedOwners.contains(canonicalOwner(gap.owner)))
                    projectedSources.push_back({2, gap.x, gap.y, gap.radius});
            }
            auto reveal = fireReveals_.lower_bound(FireRevealKey{grid.owner, -1, -1});
            for (; reveal != fireReveals_.end() && std::get<0>(reveal->first) == grid.owner; ++reveal) {
                if (reveal->second > tick)
                    projectedSources.push_back({3, std::get<1>(reveal->first), std::get<2>(reveal->first), 3});
            }
            if (!allianceChanged && grid.sourceMemoValid
                && grid.sourceMemoRevision == grid.stats.revision
                && projectedSources == grid.sourceMemo) {
                grid.stats.lastUpdateTick = tick;
                ++sourceMemoHits_;
                continue;
            }
        }

        std::vector<int> previousVisible = grid.visibleIndices;
        std::vector<int> previousGapShrouded = grid.gapShroudedIndices;
        for (int index : previousVisible) {
            grid.visible[static_cast<std::size_t>(index)] = 0;
        }
        for (int index : previousGapShrouded) {
            grid.gapShrouded[static_cast<std::size_t>(index)] = 0;
        }
        grid.visibleIndices.clear();
        grid.gapShroudedIndices.clear();
        grid.stats.visionSources = 0;
        grid.stats.spySatSources = 0;
        grid.stats.gapGeneratorSources = 0;
        grid.stats.gapShroudedCells = 0;
        grid.stats.lastUpdateTick = tick;

        bool explorationChanged = false;
        bool fullMapReveal = false;
        if (rules != nullptr) {
            for (const WorldObjectInstance& object : sourceObjects.pass()) {
                if (!object.alive
                    || object.kind != WorldObjectKind::Structure
                    || grid.sharedOwners.find(canonicalOwner(object.owner))
                        == grid.sharedOwners.end()) {
                    continue;
                }
                const BuildingType* building =
                    rules->findBuildingType(object.type);
                if (building != nullptr
                    && building->spySat
                    && object.poweredOperational) {
                    fullMapReveal = true;
                    ++grid.stats.spySatSources;
                }
            }
        }

        if (fullMapReveal) {
            explorationChanged = revealAll(grid);
        } else {
            for (const WorldObjectInstance& object : sourceObjects.pass()) {
                if (!object.alive
                    || isInsideTransport(object)
                    || transportedObjectIds.find(object.instanceId)
                        != transportedObjectIds.end()
                    || grid.sharedOwners.find(canonicalOwner(object.owner))
                        == grid.sharedOwners.end()) {
                    continue;
                }
                const double sourceX = object.hasRenderPosition
                    ? object.renderX
                    : static_cast<double>(object.x);
                const double sourceY = object.hasRenderPosition
                    ? object.renderY
                    : static_cast<double>(object.y);
                explorationChanged = revealCircle(
                    grid,
                    static_cast<int>(std::llround(sourceX)),
                    static_cast<int>(std::llround(sourceY)),
                    sightForObject(object, rules)
                ) || explorationChanged;
                ++grid.stats.visionSources;
            }
        }

        // Generated shroud is a reversible layer over ordinary exploration
        // and SpySat reveal.  Direct sight from the observer or a vision-
        // sharing ally is applied after this layer and therefore pierces it,
        // matching Westwood's visible-count precedence.
        if (rules != nullptr) {
            for (const RuntimeSpecialBuildingGap& gap : activeGaps) {
                if (grid.sharedOwners.contains(canonicalOwner(gap.owner))) {
                    continue;
                }
                (void)shroudGapCircle(grid, gap.x, gap.y, gap.radius);
                ++grid.stats.gapGeneratorSources;
            }

            if (!grid.gapShroudedIndices.empty()) {
                for (const WorldObjectInstance& object : sourceObjects.pass()) {
                    if (!object.alive
                        || isInsideTransport(object)
                        || transportedObjectIds.contains(object.instanceId)
                        || !grid.sharedOwners.contains(
                            canonicalOwner(object.owner)
                        )) {
                        continue;
                    }
                    const double sourceX = object.hasRenderPosition
                        ? object.renderX
                        : static_cast<double>(object.x);
                    const double sourceY = object.hasRenderPosition
                        ? object.renderY
                        : static_cast<double>(object.y);
                    (void)revealThroughGapCircle(
                        grid,
                        static_cast<int>(std::llround(sourceX)),
                        static_cast<int>(std::llround(sourceY)),
                        sightForObject(object, rules)
                    );
                }
                grid.gapShroudedIndices.erase(
                    std::remove_if(
                        grid.gapShroudedIndices.begin(),
                        grid.gapShroudedIndices.end(),
                        [&grid](int index) {
                            return grid.gapShrouded[
                                static_cast<std::size_t>(index)
                            ] == 0;
                        }
                    ),
                    grid.gapShroudedIndices.end()
                );
            }
        }

        (void)applyFireReveals(grid, tick);

        // Indices are unique: reveal/shroud append only when a mask changes
        // from zero to one. Equal cardinality plus membership therefore
        // compares these sets exactly without sorting every observer's map.
        const bool visibilityChanged = previousVisible.size() != grid.visibleIndices.size()
            || std::any_of(previousVisible.begin(), previousVisible.end(),
                [&grid](int index) { return grid.visible[index] == 0; });
        const bool gapChanged = previousGapShrouded.size() != grid.gapShroudedIndices.size()
            || std::any_of(previousGapShrouded.begin(), previousGapShrouded.end(),
                [&grid](int index) { return grid.gapShrouded[index] == 0; });
        if (allianceChanged
            || visibilityChanged
            || explorationChanged
            || gapChanged) {
            ++grid.stats.revision;
            changed = true;
        }
        grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
        grid.stats.gapShroudedCells = static_cast<int>(
            grid.gapShroudedIndices.size()
        );
        if (useSourceMemo) {
            grid.sourceMemo.assign(projectedSources.begin(), projectedSources.end());
            grid.sourceMemoRevision = grid.stats.revision;
            grid.sourceMemoValid = true;
        } else {
            grid.sourceMemoValid = false;
        }
    }

    std::erase_if(fireReveals_, [tick](const auto& entry) { return entry.second <= tick; });
    for (auto& [owner, grid] : observers_) {
        if (activeObservers.find(owner) != activeObservers.end()
            || (grid.visibleIndices.empty()
                && grid.gapShroudedIndices.empty())) {
            continue;
        }
        for (int index : grid.visibleIndices) {
            grid.visible[static_cast<std::size_t>(index)] = 0;
        }
        grid.visibleIndices.clear();
        for (int index : grid.gapShroudedIndices) {
            grid.gapShrouded[static_cast<std::size_t>(index)] = 0;
        }
        grid.gapShroudedIndices.clear();
        grid.stats.visibleCells = 0;
        grid.stats.gapGeneratorSources = 0;
        grid.stats.gapShroudedCells = 0;
        grid.stats.lastUpdateTick = tick;
        ++grid.stats.revision;
        changed = true;
    }
    return changed;
}

bool RuntimeVisionSystem::hasObserver(
    const std::string& observerOwner
) const {
    return observers_.find(canonicalOwner(observerOwner)) != observers_.end();
}

RuntimeCellVisibility RuntimeVisionSystem::visibilityFor(
    const std::string& observerOwner,
    int cellX,
    int cellY
) const {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()) {
        // Vision is optional for renderer probes and legacy saves. Failing
        // open keeps those paths visually identical until an observer exists.
        return RuntimeCellVisibility::Visible;
    }
    return visibilityForGrid(found->second, cellX, cellY);
}

RuntimeCellVisibility RuntimeVisionSystem::visibilityForGrid(
    const ObserverGrid& grid,
    int cellX,
    int cellY
) const {
    const int index = cellIndex(cellX, cellY);
    if (index < 0
        || validCells_.empty()
        || validCells_[static_cast<std::size_t>(index)] == 0) {
        return RuntimeCellVisibility::Shrouded;
    }
    if (!grid.gapShrouded.empty()
        && grid.gapShrouded[static_cast<std::size_t>(index)] != 0) {
        return RuntimeCellVisibility::Shrouded;
    }
    if (grid.visible[static_cast<std::size_t>(index)] != 0) {
        return RuntimeCellVisibility::Visible;
    }
    return grid.explored[static_cast<std::size_t>(index)] != 0
        ? RuntimeCellVisibility::Fogged
        : RuntimeCellVisibility::Shrouded;
}

bool RuntimeVisionSystem::gridViewFor(
    const std::string& observerOwner,
    RuntimeVisionGridView& view
) const {
    view = RuntimeVisionGridView{};
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()
        || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }

    const ObserverGrid& grid = found->second;
    if (grid.visible.size() != validCells_.size()
        || grid.explored.size() != validCells_.size()
        || (!grid.gapShrouded.empty()
            && grid.gapShrouded.size() != validCells_.size())) {
        return false;
    }

    view.validCells = validCells_.data();
    view.visibleCells = grid.visible.data();
    view.exploredCells = grid.explored.data();
    view.gapShroudedCells = grid.gapShrouded.empty()
        ? nullptr
        : grid.gapShrouded.data();
    view.cellCount = validCells_.size();
    view.revision = grid.stats.revision;
    view.layoutEpoch = layoutEpoch_;
    return true;
}

RuntimeCellVisibility RuntimeVisionSystem::visibilityForGridView(
    const RuntimeVisionGridView& view, int cellX, int cellY) {
    return view.visibilityAtIndex(cellIndex(cellX, cellY));
}

bool RuntimeVisionSystem::isCellVisibleTo(
    const std::string& observerOwner,
    int cellX,
    int cellY
) const {
    return visibilityFor(observerOwner, cellX, cellY)
        == RuntimeCellVisibility::Visible;
}

bool RuntimeVisionSystem::isCellExploredBy(
    const std::string& observerOwner,
    int cellX,
    int cellY
) const {
    return visibilityFor(observerOwner, cellX, cellY)
        != RuntimeCellVisibility::Shrouded;
}

bool RuntimeVisionSystem::isOwnerSharedWithObserver(
    const std::string& observerOwner,
    const std::string& subjectOwner
) const {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()) {
        return false;
    }
    return found->second.sharedOwners.find(canonicalOwner(subjectOwner))
        != found->second.sharedOwners.end();
}

bool RuntimeVisionSystem::isObjectVisibleTo(
    const std::string& observerOwner,
    const WorldObjectInstance& object
) const {
    if (!object.alive) {
        return false;
    }
    const auto observer = observers_.find(canonicalOwner(observerOwner));
    if (observer == observers_.end()) {
        return true;
    }
    const ObserverGrid& grid = observer->second;
    if (grid.sharedOwners.find(canonicalOwner(object.owner))
        != grid.sharedOwners.end()) {
        return true;
    }
    if (object.cloaked) {
        return false;
    }

    const int objectX = static_cast<int>(std::llround(
        object.hasRenderPosition ? object.renderX : static_cast<double>(object.x)
    ));
    const int objectY = static_cast<int>(std::llround(
        object.hasRenderPosition ? object.renderY : static_cast<double>(object.y)
    ));
    const int edgeAllowance = object.kind == WorldObjectKind::Structure ? 1 : 0;
    for (int dy = -edgeAllowance; dy <= edgeAllowance; ++dy) {
        for (int dx = -edgeAllowance; dx <= edgeAllowance; ++dx) {
            if (visibilityForGrid(grid, objectX + dx, objectY + dy)
                != RuntimeCellVisibility::Shrouded) {
                return true;
            }
        }
    }
    return false;
}

bool RuntimeVisionSystem::isObjectLocationExploredBy(
    const std::string& observerOwner,
    const WorldObjectInstance& object
) const {
    const auto observer = observers_.find(canonicalOwner(observerOwner));
    if (observer == observers_.end()) {
        return true;
    }
    const ObserverGrid& grid = observer->second;

    const int objectX = static_cast<int>(std::llround(
        object.hasRenderPosition ? object.renderX : static_cast<double>(object.x)
    ));
    const int objectY = static_cast<int>(std::llround(
        object.hasRenderPosition ? object.renderY : static_cast<double>(object.y)
    ));
    const int edgeAllowance = object.kind == WorldObjectKind::Structure ? 1 : 0;
    for (int dy = -edgeAllowance; dy <= edgeAllowance; ++dy) {
        for (int dx = -edgeAllowance; dx <= edgeAllowance; ++dx) {
            if (visibilityForGrid(grid, objectX + dx, objectY + dy)
                != RuntimeCellVisibility::Shrouded) {
                return true;
            }
        }
    }
    return false;
}

std::uint64_t RuntimeVisionSystem::revisionFor(
    const std::string& observerOwner
) const {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    return found == observers_.end() ? 0 : found->second.stats.revision;
}

const RuntimeVisionObserverStats* RuntimeVisionSystem::statsFor(
    const std::string& observerOwner
) const {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    return found == observers_.end() ? nullptr : &found->second.stats;
}

std::vector<std::string> RuntimeVisionSystem::observerOwners() const {
    std::vector<std::string> owners;
    owners.reserve(observers_.size());
    for (const auto& [owner, grid] : observers_) {
        (void)grid;
        owners.push_back(owner);
    }
    std::sort(owners.begin(), owners.end());
    return owners;
}

std::vector<int> RuntimeVisionSystem::exploredCellIndices(
    const std::string& observerOwner
) const {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()) {
        return {};
    }
    std::vector<int> result;
    result.reserve(static_cast<std::size_t>(found->second.stats.exploredCells));
    for (int index : validCellIndices_) {
        if (found->second.explored[static_cast<std::size_t>(index)] != 0) {
            result.push_back(index);
        }
    }
    return result;
}

bool RuntimeVisionSystem::restoreExploredCellIndices(
    const std::string& observerOwner,
    const std::vector<int>& cellIndices
) {
    const std::string owner = canonicalOwner(observerOwner);
    if (owner.empty()
        || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }
    ObserverGrid& grid = ensureObserver(owner);
    grid.sharedOwners.insert(owner);
    bool changed = false;
    for (int index : cellIndices) {
        if (index < 0
            || index >= mapCellCount_
            || validCells_[static_cast<std::size_t>(index)] == 0
            || grid.explored[static_cast<std::size_t>(index)] != 0) {
            continue;
        }
        grid.explored[static_cast<std::size_t>(index)] = 1;
        ++grid.stats.exploredCells;
        changed = true;
    }
    if (changed) {
        ++grid.stats.revision;
    }
    return changed;
}

bool RuntimeVisionSystem::revealArea(
    const std::string& observerOwner,
    int centerX,
    int centerY,
    int radius
) {
    const std::string owner = canonicalOwner(observerOwner);
    if (owner.empty()
        || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }
    ObserverGrid& grid = ensureObserver(owner);
    grid.sharedOwners.insert(owner);
    const std::size_t visibleBefore = grid.visibleIndices.size();
    const bool explorationChanged = revealCircle(
        grid,
        centerX,
        centerY,
        radius
    );
    const bool visibilityChanged = grid.visibleIndices.size() != visibleBefore;
    if (explorationChanged || visibilityChanged) {
        grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
        ++grid.stats.revision;
    }
    return explorationChanged || visibilityChanged;
}

bool RuntimeVisionSystem::exploreArea(
    const std::string& observerOwner, int centerX, int centerY, int radius
) {
    const auto owner = canonicalOwner(observerOwner);
    if (owner.empty() || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) return false;
    auto& grid = ensureObserver(owner);
    radius = std::clamp(radius, 1, mapCellStride_);
    bool changed = false;
    for (int y = std::max(0, centerY - radius); y <= std::min(mapCellStride_ - 1, centerY + radius); ++y)
        for (int x = std::max(0, centerX - radius); x <= std::min(mapCellStride_ - 1, centerX + radius); ++x) {
            const int dx = x - centerX, dy = y - centerY;
            if (dx * dx + dy * dy > radius * radius) continue;
            const int index = cellIndex(x, y);
            if (index < 0 || !validCells_[index] || grid.explored[index]) continue;
            grid.explored[index] = 1;
            ++grid.stats.exploredCells;
            changed = true;
        }
    if (changed) ++grid.stats.revision;
    return changed;
}

bool RuntimeVisionSystem::revealMap(
    const std::string& observerOwner
) {
    const std::string owner = canonicalOwner(observerOwner);
    if (owner.empty()
        || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }
    ObserverGrid& grid = ensureObserver(owner);
    grid.sharedOwners.insert(owner);
    const std::size_t visibleBefore = grid.visibleIndices.size();
    const bool explorationChanged = revealAll(grid);
    const bool visibilityChanged = grid.visibleIndices.size() != visibleBefore;
    if (explorationChanged || visibilityChanged) {
        grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
        ++grid.stats.revision;
    }
    return explorationChanged || visibilityChanged;
}

bool RuntimeVisionSystem::resetShroud(
    const std::string& observerOwner
) {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()) {
        return false;
    }
    ObserverGrid& grid = found->second;
    std::erase_if(fireReveals_, [&grid](const auto& entry) { return std::get<0>(entry.first) == grid.owner; });
    const bool changed = !grid.visibleIndices.empty()
        || grid.stats.exploredCells > 0;
    if (!changed) {
        return false;
    }
    for (int index : grid.visibleIndices) {
        grid.visible[static_cast<std::size_t>(index)] = 0;
    }
    std::fill(grid.explored.begin(), grid.explored.end(), 0);
    grid.visibleIndices.clear();
    grid.stats.visibleCells = 0;
    grid.stats.exploredCells = 0;
    ++grid.stats.revision;
    return true;
}

bool RuntimeVisionSystem::resetShroudArea(
    const std::string& observerOwner,
    int centerX,
    int centerY,
    int radius
) {
    const auto found = observers_.find(canonicalOwner(observerOwner));
    if (found == observers_.end()
        || validCells_.size() != static_cast<std::size_t>(mapCellCount_)) {
        return false;
    }
    ObserverGrid& grid = found->second;
    const int clampedRadius = std::max(0, radius);
    std::erase_if(fireReveals_, [&](const auto& entry) {
        const auto& [owner, x, y] = entry.first;
        const double dx = static_cast<double>(x) - centerX, dy = static_cast<double>(y) - centerY;
        return owner == grid.owner && dx * dx + dy * dy <= std::pow(static_cast<double>(clampedRadius) + 3, 2);
    });
    const int radiusSquared = clampedRadius * clampedRadius;
    bool changed = false;
    for (int y = std::max(0, centerY - clampedRadius);
        y <= std::min(mapCellStride_ - 1, centerY + clampedRadius);
        ++y) {
        for (int x = std::max(0, centerX - clampedRadius);
            x <= std::min(mapCellStride_ - 1, centerX + clampedRadius);
            ++x) {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if (dx * dx + dy * dy > radiusSquared) {
                continue;
            }
            const int index = cellIndex(x, y);
            if (index < 0
                || validCells_[static_cast<std::size_t>(index)] == 0) {
                continue;
            }
            const std::size_t offset = static_cast<std::size_t>(index);
            changed = changed
                || grid.visible[offset] != 0
                || grid.explored[offset] != 0;
            grid.visible[offset] = 0;
            grid.explored[offset] = 0;
        }
    }
    if (!changed) {
        return false;
    }
    grid.visibleIndices.erase(
        std::remove_if(
            grid.visibleIndices.begin(),
            grid.visibleIndices.end(),
            [&grid](int index) {
                return index >= 0
                    && static_cast<std::size_t>(index) < grid.visible.size()
                    && grid.visible[static_cast<std::size_t>(index)] == 0;
            }
        ),
        grid.visibleIndices.end()
    );
    grid.stats.visibleCells = static_cast<int>(grid.visibleIndices.size());
    grid.stats.exploredCells = static_cast<int>(std::count(
        grid.explored.begin(),
        grid.explored.end(),
        static_cast<std::uint8_t>(1)
    ));
    ++grid.stats.revision;
    return true;
}

#include "simulation/combat/targeting/RuntimeFireReveal.inl"
#include "simulation/vision/diagnostics/RuntimeVisionSourceMemoContract.inl"
#include "simulation/vision/diagnostics/RuntimeVisionTerrainCoordinateContract.inl"

bool runRuntimeVisionContractCheck(std::string& errorMessage) {
    if (!RuntimeVisionTerrainCoordinateContractDetail::primitive(errorMessage)) return false;
    if (!runRuntimeVisionSourceMemoContractCheck(errorMessage)) return false;
    GameWorld world;
    for (int y = 0; y <= 20; ++y) {
        for (int x = 0; x <= 20; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = (y * 512) + x;
            world.accessTerrainCells().push_back(cell);
        }
    }

    WorldObjectInstance alpha;
    alpha.instanceId = 1;
    alpha.kind = WorldObjectKind::Infantry;
    alpha.owner = "Alpha";
    alpha.type = "SCOUT";
    alpha.x = 5;
    alpha.y = 5;
    alpha.alive = true;
    alpha.veterancy = 2;
    world.accessObjects().push_back(alpha);

    WorldObjectInstance beta = alpha;
    beta.instanceId = 2;
    beta.owner = "Beta";
    beta.x = 15;
    beta.y = 15;
    world.accessObjects().push_back(beta);

    WorldObjectInstance enemy = alpha;
    enemy.instanceId = 3;
    enemy.owner = "Enemy";
    enemy.x = 7;
    enemy.y = 5;
    world.accessObjects().push_back(enemy);

    WorldObjectInstance spySat;
    spySat.instanceId = 4;
    spySat.kind = WorldObjectKind::Structure;
    spySat.owner = "Alpha";
    spySat.type = "SPY";
    spySat.x = 10;
    spySat.y = 10;
    spySat.alive = false;
    spySat.poweredOperational = true;
    world.accessObjects().push_back(spySat);

    IniFile rulesIni;
    const std::string rulesText =
        "[InfantryTypes]\n"
        "0=SCOUT\n"
        "[BuildingTypes]\n"
        "0=SPY\n"
        "1=GAP\n"
        "[General]\n"
        "VeteranSight=2\n"
        "[SCOUT]\n"
        "Sight=3\n"
        "VeteranAbilities=SIGHT\n"
        "EliteAbilities=FIREPOWER\n"
        "Strength=100\n"
        "[SPY]\n"
        "Sight=1\n"
        "SpySat=yes\n"
        "Strength=500\n"
        "[GAP]\n"
        "Sight=1\n"
        "GapGenerator=yes\n"
        "GapRadiusInCells=3\n"
        "Powered=yes\n"
        "Strength=500\n";
    RulesDatabase rules;
    if (!rulesIni.loadFromString(rulesText) || !rules.loadFromIni(rulesIni)) {
        errorMessage = "synthetic sight rules did not load";
        return false;
    }

    const std::vector<RuntimeVisionObserverDefinition> observers{
        {"Alpha", {"Alpha", "Beta"}},
        {"Enemy", {"Enemy"}}
    };
    RuntimeVisionSystem vision;
    vision.update(world, &rules, observers, 1);
    RuntimeVisionGridView gridView;
    const bool gridViewValid = vision.gridViewFor(" ALPHA ", gridView)
        && gridView.available()
        && gridView.revision == vision.revisionFor("Alpha")
        && gridView.visibilityAtIndex((5 * 512) + 5)
            == RuntimeCellVisibility::Visible
        && gridView.visibilityAtIndex((0 * 512) + 20)
            == RuntimeCellVisibility::Shrouded;
    if (!vision.hasObserver("ALPHA")
        || !gridViewValid
        || !vision.isCellVisibleTo("Alpha", 5, 5)
        || !vision.isCellVisibleTo("Alpha", 15, 15)
        || !vision.isCellVisibleTo("Alpha", 11, 5)
        || vision.isCellVisibleTo("Alpha", 12, 5)
        || vision.visibilityFor("Alpha", 20, 0)
            != RuntimeCellVisibility::Shrouded
        || !vision.isObjectVisibleTo("Alpha", world.getObjects()[2])) {
        errorMessage =
            "local, allied, veteran, grid-view, shrouded, or enemy visibility failed";
        return false;
    }

    world.accessObjects()[0].cloaked = true;
    world.accessObjects()[1].cloaked = true;
    world.accessObjects()[2].cloaked = true;
    if (!vision.isObjectVisibleTo("Alpha", world.getObjects()[0])
        || !vision.isObjectVisibleTo("Alpha", world.getObjects()[1])
        || vision.isObjectVisibleTo("Alpha", world.getObjects()[2])) {
        errorMessage =
            "cloaked owner, shared ally, or visible enemy filtering failed";
        return false;
    }
    world.accessObjects()[0].cloaked = false;
    world.accessObjects()[1].cloaked = false;
    world.accessObjects()[2].cloaked = false;

    // Explored terrain continues to reveal live enemies after scouts leave,
    // including enemies that move within that terrain without a nearby scout.
    world.accessObjects()[0].veterancy = 0;
    world.accessObjects()[0].x = 1;
    world.accessObjects()[0].y = 1;
    world.accessObjects()[2].x = 10;
    world.accessObjects()[2].y = 5;
    vision.update(world, &rules, observers, 2);
    WorldObjectInstance rememberedStructure = world.getObjects()[2];
    rememberedStructure.kind = WorldObjectKind::Structure;
    rememberedStructure.x = 5;
    rememberedStructure.y = 5;
    WorldObjectInstance unexploredStructure = rememberedStructure;
    unexploredStructure.x = 20;
    unexploredStructure.y = 0;
    if (vision.visibilityFor("Alpha", 5, 5)
            != RuntimeCellVisibility::Fogged
        || !vision.isObjectVisibleTo("Alpha", world.getObjects()[2])
        || !vision.isObjectVisibleTo("Alpha", rememberedStructure)
        || !vision.isObjectLocationExploredBy("Alpha", rememberedStructure)
        || vision.isObjectVisibleTo("Alpha", unexploredStructure)
        || vision.isObjectLocationExploredBy("Alpha", unexploredStructure)) {
        errorMessage =
            "persistent explored-area visibility or shrouded enemy filtering failed";
        return false;
    }

    world.accessObjects()[2].cloaked = true;
    if (vision.isObjectVisibleTo("Alpha", world.getObjects()[2])) {
        errorMessage = "exploration revealed a cloaked enemy";
        return false;
    }
    world.accessObjects()[2].cloaked = false;
    world.accessObjects()[2].x = 20;
    world.accessObjects()[2].y = 0;
    if (vision.isObjectVisibleTo("Alpha", world.getObjects()[2])) {
        errorMessage = "enemy remained visible after moving into unexplored terrain";
        return false;
    }
    world.accessObjects()[2].x = 10;
    world.accessObjects()[2].y = 5;

    if (!vision.revealArea("Alpha", 20, 20, 2)
        || !vision.isCellVisibleTo("Alpha", 20, 20)) {
        errorMessage = "scripted area reveal did not become visible";
        return false;
    }
    vision.update(world, &rules, observers, 3);
    if (vision.visibilityFor("Alpha", 20, 20)
            != RuntimeCellVisibility::Fogged) {
        errorMessage = "scripted area reveal did not retain exploration";
        return false;
    }
    if (!vision.resetShroudArea("Alpha", 20, 20, 2)
        || vision.visibilityFor("Alpha", 20, 20)
            != RuntimeCellVisibility::Shrouded) {
        errorMessage = "explicit campaign area reshroud failed";
        return false;
    }

    world.accessObjects()[3].alive = true;
    vision.update(world, &rules, observers, 4);
    const RuntimeVisionObserverStats* spyStats = vision.statsFor("Alpha");
    if (!vision.isCellVisibleTo("Alpha", 20, 0)
        || spyStats == nullptr
        || spyStats->spySatSources != 1
        || spyStats->visibleCells != 441) {
        errorMessage = "powered SpySat did not reveal the valid map";
        return false;
    }

    const std::vector<int> explored = vision.exploredCellIndices("Alpha");
    world.accessObjects()[3].alive = false;
    vision.update(world, &rules, observers, 5);
    if (vision.visibilityFor("Alpha", 20, 0)
        != RuntimeCellVisibility::Fogged) {
        errorMessage = "SpySat removal did not restore fog while retaining exploration";
        return false;
    }

    RuntimeVisionSystem restored;
    restored.update(world, &rules, observers, 6);
    restored.restoreExploredCellIndices("Alpha", explored);
    if (restored.visibilityFor("Alpha", 20, 0)
            != RuntimeCellVisibility::Fogged
        || restored.exploredCellIndices("Alpha").size() != explored.size()
        || !restored.isObjectVisibleTo("Alpha", unexploredStructure)) {
        errorMessage = "explored-cell save/restore contract failed";
        return false;
    }

    const RuntimeVisionObserverStats* stats = vision.statsFor("alpha");
    if (stats == nullptr || stats->revision < 3 || stats->visionSources < 2) {
        errorMessage = "vision revisions or source statistics did not advance";
        return false;
    }

    // Source order must not become a visibility change. This also exercises
    // the unsorted list left behind by an explicit scripted reveal.
    GameWorld orderedWorld;
    orderedWorld.accessTerrainCells() = world.getTerrainCells();
    WorldObjectInstance firstSource = alpha;
    firstSource.veterancy = 0;
    firstSource.x = 3;
    firstSource.y = 3;
    WorldObjectInstance secondSource = firstSource;
    secondSource.instanceId = 2;
    secondSource.x = 8;
    orderedWorld.accessObjects() = {firstSource, secondSource};
    RuntimeVisionSystem orderedVision;
    orderedVision.update(orderedWorld, &rules, observers, 1);
    const std::uint64_t orderedRevision = orderedVision.revisionFor("Alpha");
    RuntimeVisionGridView orderedView;
    orderedVision.gridViewFor("Alpha", orderedView);
    const std::vector<std::uint8_t> orderedMask(
        orderedView.visibleCells, orderedView.visibleCells + orderedView.cellCount
    );
    std::reverse(orderedWorld.accessObjects().begin(), orderedWorld.accessObjects().end());
    if (orderedVision.update(orderedWorld, &rules, observers, 2)
        || orderedVision.revisionFor("Alpha") != orderedRevision
        || !orderedVision.gridViewFor("Alpha", orderedView)
        || !std::equal(orderedMask.begin(), orderedMask.end(), orderedView.visibleCells)) {
        errorMessage = "source reordering changed the visibility set or revision";
        return false;
    }
    const int orderedVisibleCount = orderedVision.statsFor("Alpha")->visibleCells;
    for (auto& source : orderedWorld.accessObjects()) {
        ++source.y;
    }
    if (!orderedVision.update(orderedWorld, &rules, observers, 3)
        || orderedVision.revisionFor("Alpha") != orderedRevision + 1
        || orderedVision.statsFor("Alpha")->visibleCells != orderedVisibleCount
        || orderedVision.visibilityFor("Alpha", 3, 0) != RuntimeCellVisibility::Fogged
        || orderedVision.visibilityFor("Alpha", 3, 7) != RuntimeCellVisibility::Visible) {
        errorMessage = "equal-size moving sight did not update mask membership";
        return false;
    }
    for (auto& source : orderedWorld.accessObjects()) {
        --source.y;
    }
    orderedVision.update(orderedWorld, &rules, observers, 3);
    orderedVision.revealArea("Alpha", 20, 20, 1);
    const std::uint64_t scriptedRevision = orderedVision.revisionFor("Alpha");
    if (!orderedVision.update(orderedWorld, &rules, observers, 3)
        || orderedVision.revisionFor("Alpha") != scriptedRevision + 1
        || orderedVision.visibilityFor("Alpha", 20, 20) != RuntimeCellVisibility::Fogged) {
        errorMessage = "scripted visibility removal did not change the exact set";
        return false;
    }

    // Compare every valid cell with the authored circles, including a gap
    // movement that preserves cardinality but changes membership.
    WorldObjectInstance testSpySat = spySat;
    testSpySat.alive = true;
    testSpySat.health = 500;
    testSpySat.x = 0;
    testSpySat.y = 0;
    orderedWorld.accessObjects().push_back(testSpySat);
    WorldObjectInstance gap = testSpySat;
    gap.instanceId = 5;
    gap.type = "GAP";
    gap.owner = "Enemy";
    gap.x = 15;
    gap.y = 15;
    orderedWorld.accessObjects().push_back(gap);
    const auto verifyGapMask = [&](int tick, int expectedSources) {
        const std::uint64_t before = orderedVision.revisionFor("Alpha");
        if (!orderedVision.update(orderedWorld, &rules, observers, tick)
            || orderedVision.revisionFor("Alpha") != before + 1) {
            return false;
        }
        int expectedShrouded = 0;
        for (int y = 0; y <= 20; ++y) {
            for (int x = 0; x <= 20; ++x) {
                bool shrouded = false;
                for (const auto& source : orderedWorld.getObjects()) {
                    if (source.type != "GAP" || !source.alive || !source.poweredOperational) {
                        continue;
                    }
                    const int dx = x - source.x;
                    const int dy = y - source.y;
                    shrouded = shrouded || dx * dx + dy * dy <= 9;
                }
                expectedShrouded += shrouded ? 1 : 0;
                const auto expected = shrouded
                    ? RuntimeCellVisibility::Shrouded : RuntimeCellVisibility::Visible;
                if (orderedVision.visibilityFor("Alpha", x, y) != expected) {
                    return false;
                }
            }
        }
        const auto* gapStats = orderedVision.statsFor("Alpha");
        return gapStats != nullptr
            && gapStats->gapGeneratorSources == expectedSources
            && gapStats->gapShroudedCells == expectedShrouded;
    };
    if (!verifyGapMask(4, 1)) {
        errorMessage = "new gap did not produce its exact authored mask";
        return false;
    }
    orderedWorld.accessObjects().back().y = 11;
    if (!verifyGapMask(5, 1)) {
        errorMessage = "equal-size moved gap did not update mask membership";
        return false;
    }
    gap.instanceId = 6;
    gap.x = 12;
    orderedWorld.accessObjects().push_back(gap);
    if (!verifyGapMask(6, 2)) {
        errorMessage = "new overlapping gap did not preserve existing coverage";
        return false;
    }
    orderedWorld.accessObjects()[3].alive = false;
    if (!verifyGapMask(7, 1)) {
        errorMessage = "destroyed gap did not release only its own coverage";
        return false;
    }
    orderedWorld.accessObjects().back().poweredOperational = false;
    if (!verifyGapMask(8, 0)) {
        errorMessage = "depowered gap did not restore the exact revealed mask";
        return false;
    }

    errorMessage.clear();
    return true;
}
