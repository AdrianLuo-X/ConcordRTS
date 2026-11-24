#pragma once
#include <unordered_map>
struct TerrainCell;
namespace concord::simulation::abilities {
using RuntimeChronoTerrainLookup = std::unordered_map<int, const TerrainCell*>;
class RuntimeChronoTerrainSource {
public:
    virtual ~RuntimeChronoTerrainSource() = default;
    virtual RuntimeChronoTerrainLookup buildLookup() const = 0;
};
class RuntimeChronoTerrainView {
public:
    RuntimeChronoTerrainView(const RuntimeChronoTerrainLookup& cached, bool reference,
        const RuntimeChronoTerrainSource& source);
    const RuntimeChronoTerrainLookup& get() const { return *terrain_; }
    bool borrowed() const { return terrain_ != &owned_; }
    bool generated() const { return generated_; }
    bool reference() const { return reference_; }
    RuntimeChronoTerrainView(const RuntimeChronoTerrainView&) = delete;
    RuntimeChronoTerrainView& operator=(const RuntimeChronoTerrainView&) = delete;
protected:
    // The private host adapter enters the original trace after member construction.
    RuntimeChronoTerrainView(const RuntimeChronoTerrainLookup& cached, bool reference);
    void selectTerrain(const RuntimeChronoTerrainSource& source);
private:
    RuntimeChronoTerrainLookup owned_;
    const RuntimeChronoTerrainLookup* terrain_;
    bool reference_, generated_ = false;
};
}
