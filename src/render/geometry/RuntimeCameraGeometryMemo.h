#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <optional>
#include <vector>

// This cache compares EVERY ordered x/y/level value on every lookup. It does
// not trust a terrain pointer, size, generation or a possibly colliding hash.
// Only these values, tile dimensions and the ORIGINAL returned radar projection
// determine camera envelopes. Resource/overlay/type changes do not enter the
// original integer envelope calculation. The projection includes its existing
// cache behavior: callers supply the actual result, not reconstructed metadata.
struct RuntimeCameraGeometryProjection {
    bool valid=false;
    int minX=0,minY=0,maxX=0,maxY=0,fullMapWidth=0;
    bool operator==(const RuntimeCameraGeometryProjection&) const = default;
};
struct RuntimeCameraGeometryBounds {
    int left=0,top=0,right=0,bottom=0;
    bool valid=false;
    bool operator==(const RuntimeCameraGeometryBounds&) const = default;
};
struct RuntimeCameraGeometryEnvelopes {
    RuntimeCameraGeometryBounds playable,full;
    bool operator==(const RuntimeCameraGeometryEnvelopes&) const = default;
};
inline thread_local int runtimeCameraGeometryMemoReferenceOverride=-1;
inline bool runtimeCameraGeometryMemoReference() noexcept {
    if(runtimeCameraGeometryMemoReferenceOverride>=0)
        return runtimeCameraGeometryMemoReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_CAMERA_GEOMETRY_MEMO_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimeCameraGeometryMemoReferenceScope {
    int previous_;
public:
    explicit RuntimeCameraGeometryMemoReferenceScope(bool reference) noexcept
        :previous_(runtimeCameraGeometryMemoReferenceOverride) {
        runtimeCameraGeometryMemoReferenceOverride=reference?1:0;
    }
    ~RuntimeCameraGeometryMemoReferenceScope() {
        runtimeCameraGeometryMemoReferenceOverride=previous_;
    }
    RuntimeCameraGeometryMemoReferenceScope(const RuntimeCameraGeometryMemoReferenceScope&)=delete;
    RuntimeCameraGeometryMemoReferenceScope& operator=(const RuntimeCameraGeometryMemoReferenceScope&)=delete;
};

// Owned derived rendering state, never a borrowed World address. Its values
// may survive calls because lookup validates all mathematical inputs exactly.
// Default copying owns another complete key sequence; no address escapes.
class RuntimeCameraGeometryMemo {
    struct Key {int x,y,level;};
    std::vector<Key> keys_;
    RuntimeCameraGeometryProjection projection_;
    RuntimeCameraGeometryEnvelopes envelopes_;
    int tileWidth_=0,tileHeight_=0;
    bool valid_=false;
    std::size_t hits_=0,stores_=0,comparedCells_=0;
public:
    static constexpr std::size_t minimumEntries=4096;
    static constexpr std::size_t maximumEntries=262144;
    static constexpr std::size_t maximumKeyBytes=maximumEntries*sizeof(Key);
    static_assert(sizeof(Key)==3*sizeof(int));

    template<class TerrainRange>
    std::optional<RuntimeCameraGeometryEnvelopes> find(const TerrainRange& cells,
        int tileWidth,int tileHeight,const RuntimeCameraGeometryProjection& projection) noexcept {
        if(runtimeCameraGeometryMemoReference() || !valid_
            || keys_.size()<minimumEntries || keys_.size()>maximumEntries
            || tileWidth!=tileWidth_ || tileHeight!=tileHeight_
            || projection!=projection_ || cells.size()!=keys_.size()) return std::nullopt;
        std::size_t compared=0;
        for(std::size_t index=0;index<keys_.size();++index) {
            ++compared;
            const auto& cell=cells[index];
            const auto& key=keys_[index];
            if(cell.x!=key.x || cell.y!=key.y || cell.level!=key.level) {
                comparedCells_+=compared;return std::nullopt;
            }
        }
        comparedCells_+=compared;
        ++hits_;
        return envelopes_;
    }
    template<class TerrainRange>
    bool store(const TerrainRange& cells,int tileWidth,int tileHeight,
        const RuntimeCameraGeometryProjection& projection,
        const RuntimeCameraGeometryEnvelopes& envelopes) {
        if(runtimeCameraGeometryMemoReference() || cells.size()<minimumEntries
            || cells.size()>maximumEntries) return false;
        valid_=false;
        // The additional allocation is bounded. Failure retains the original
        // freshly calculated result; it must not turn a camera clamp into a
        // new out-of-memory failure or publish partially captured inputs.
        try {
            if(cells.size()>keys_.capacity()) {
                std::vector<Key> replacement(cells.size());
                if(replacement.capacity()>maximumEntries) return false;
                keys_.swap(replacement);
            } else keys_.resize(cells.size());
        }
        catch(const std::bad_alloc&) { return false; }
        for(std::size_t index=0;index<keys_.size();++index) {
            const auto& cell=cells[index];
            keys_[index]={cell.x,cell.y,cell.level};
        }
        tileWidth_=tileWidth;tileHeight_=tileHeight;projection_=projection;
        envelopes_=envelopes;valid_=true;++stores_;
        return true;
    }
    void invalidate() noexcept {valid_=false;}
    std::size_t hits() const noexcept {return hits_;}
    std::size_t stores() const noexcept {return stores_;}
    std::size_t comparedCells() const noexcept {return comparedCells_;}
    std::size_t keyBytes() const noexcept {return keys_.size()*sizeof(Key);}
};
