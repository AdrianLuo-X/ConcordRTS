#pragma once
#include "render/core/MapDrawCommands.h"
#include "core/tasks/BoundedTaskExecutor.h"
#include <memory>

namespace concord::render {
// Only owned presentation values cross the worker boundary. No pixels, resource
// spans, World, HDC, mutable cache, depth scope or device/context are retained.
struct MapTileStamp {
    std::uint64_t generation=0,resourceRevision=0,commandRevision=0;
    std::uint64_t sequence=0,tick=0,deviceGeneration=0;
    std::int32_t width=0,height=0;
    bool operator==(const MapTileStamp&) const = default;
};
struct MapTileRect {
    std::int32_t left=0,top=0,right=0,bottom=0; // Half-open tile coordinates.
    std::uint32_t command=0; // Original command ordinal, never completion order.
};
struct MapTileSnapshot {
    MapTileStamp stamp;
    std::vector<MapTileRect> rectangles;
    std::size_t words=0;
};
MapTileSnapshot snapshotMapTiles(const MapDrawFrame&,std::uint64_t deviceGeneration);
// Independent original command-major/vector-bin reference, kept as default 0.
std::vector<std::uint32_t> referenceMapTiles(const MapTileSnapshot&);
struct MapTilePreparationMetrics {
    double snapshotMs=0,dispatchMs=0,waitMs=0,mergeMs=0,totalMs=0;
    std::size_t snapshotBytes=0,ownedPeakBytes=0,jobs=0,peakRunning=0;
};
struct PreparedMapTiles {
    MapTileStamp stamp;
    std::vector<std::uint32_t> indices;
    MapTilePreparationMetrics metrics;
    bool accepted=false;
};
// Called by one frame owner. At most one frame and four jobs are admitted.
// start() consumes an owned snapshot; finish() joins that exact complete set.
// A changed stamp discards presentation only. Business events never enter it.
class MapTilePreparer {
public:
    explicit MapTilePreparer(unsigned workers=0);
    ~MapTilePreparer();
    MapTilePreparer(const MapTilePreparer&)=delete;
    MapTilePreparer& operator=(const MapTilePreparer&)=delete;
    void start(MapTileSnapshot);
    PreparedMapTiles finish(const MapTileStamp& current);
    void cancel(); // Running jobs keep their owned input until actual return.
    concord::tasks::ExecutorStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace concord::render
