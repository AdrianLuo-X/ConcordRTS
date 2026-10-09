// Included after the renderer's original draw-record/foundation helpers.
// This is an explicit immutable collect lease, never a persistent world cache.
static thread_local std::optional<bool> runtimeRepairRecordsReferenceOverride;
static bool runtimeRepairRecordsMemoReference() {
    if (runtimeRepairRecordsReferenceOverride) return *runtimeRepairRecordsReferenceOverride;
    const char* setting = runtimeRendererEnvironmentValue("CONCORD_REPAIR_DEPTH_RECORDS_REFERENCE");
    return setting && setting[0] == '1' && setting[1] == '\0';
}
struct RuntimeRepairRecordsReferenceScope {
    std::optional<bool> previous = runtimeRepairRecordsReferenceOverride;
    explicit RuntimeRepairRecordsReferenceScope(bool reference) { runtimeRepairRecordsReferenceOverride = reference; }
    RuntimeRepairRecordsReferenceScope(const RuntimeRepairRecordsReferenceScope&) = delete;
    RuntimeRepairRecordsReferenceScope& operator=(const RuntimeRepairRecordsReferenceScope&) = delete;
    ~RuntimeRepairRecordsReferenceScope() { runtimeRepairRecordsReferenceOverride = previous; }
};
struct RuntimeRepairRecordsObservation {
    std::uint64_t scalarScans = 0, scannedObjects = 0, publications = 0, hits = 0, fallbacks = 0;
};
static thread_local RuntimeRepairRecordsObservation* runtimeRepairRecordsObservation = nullptr;
struct RuntimeRepairRecordsObservationScope {
    RuntimeRepairRecordsObservation* previous = runtimeRepairRecordsObservation;
    explicit RuntimeRepairRecordsObservationScope(RuntimeRepairRecordsObservation& observation) {
        runtimeRepairRecordsObservation = &observation;
    }
    ~RuntimeRepairRecordsObservationScope() { runtimeRepairRecordsObservation = previous; }
};

static void runtimeAppendOriginalGroundVehicleRepairRecords(const GameWorld& world,
    const RuntimeMapRenderContext& context, std::vector<RuntimeWorldObjectDrawRecord>& records) {
    if (auto* observation = runtimeRepairRecordsObservation) {
        ++observation->scalarScans; observation->scannedObjects += world.getObjects().size();
    }
    // The original predicates, metadata calls and authored insertion order.
    for (const auto& candidate : world.getObjects()) {
        if (candidate.kind != WorldObjectKind::Structure
            || !runtimeWorldObjectBodyShouldRender(candidate)) continue;
        const auto* building = context.rules->findBuildingType(candidate.type);
        if (!building || !building->unitRepair || building->naval || building->waterBound) continue;
        const auto footprint = runtimeObjectFoundationDimensions(candidate, context);
        RuntimeWorldObjectDrawRecord record;
        record.object = &candidate;
        record.instanceId = candidate.instanceId;
        record.footprintWidth = footprint.width;
        record.footprintHeight = footprint.height;
        records.push_back(record);
    }
}

class RuntimeGroundVehicleRepairRecordsMemo {
public:
    // The caller guarantees no world/rules/art/geometry-context mutation until
    // destruction. data/count checks are defensive, not the immutability proof.
    RuntimeGroundVehicleRepairRecordsMemo(const GameWorld& world,
        const RuntimeMapRenderContext& context, bool enabled)
        : world_(&world), context_(&context), data_(world.getObjects().data()),
          count_(world.getObjects().size()), rules_(context.rules), art_(context.art), enabled_(enabled) {}
    RuntimeGroundVehicleRepairRecordsMemo(const RuntimeGroundVehicleRepairRecordsMemo&) = delete;
    RuntimeGroundVehicleRepairRecordsMemo& operator=(const RuntimeGroundVehicleRepairRecordsMemo&) = delete;
    void invalidate() { enabled_ = false; ready_ = false; records_.clear(); }
    bool append(const WorldObjectInstance& object, const GameWorld& world,
        const RuntimeMapRenderContext& context, std::vector<RuntimeWorldObjectDrawRecord>& destination) {
        if (!matches(object, world, context) || !ready_) {
            if (auto* observation = runtimeRepairRecordsObservation) ++observation->fallbacks;
            return false;
        }
        destination.insert(destination.end(), records_.begin(), records_.end());
        if (auto* observation = runtimeRepairRecordsObservation) ++observation->hits;
        return true;
    }
    void remember(const WorldObjectInstance& object, const GameWorld& world,
        const RuntimeMapRenderContext& context, const std::vector<RuntimeWorldObjectDrawRecord>& original) {
        if (ready_ || !matches(object, world, context) || original.empty()) return;
        constexpr std::size_t maximumRecords = 1024; // <=1024 POD-like draw records, no owned resource data.
        if (original.size() - 1 > maximumRecords) { invalidate(); return; }
        try {
            std::vector<RuntimeWorldObjectDrawRecord> complete(original.begin() + 1, original.end());
            records_.swap(complete); ready_ = true; // Publish only a complete list.
        } catch (const std::bad_alloc&) { invalidate(); return; }
        if (auto* observation = runtimeRepairRecordsObservation) ++observation->publications;
    }
private:
    bool matches(const WorldObjectInstance& object, const GameWorld& world,
        const RuntimeMapRenderContext& context) {
        if (!enabled_) return false;
        if (&world != world_ || &context != context_ || context.rules != rules_ || context.art != art_) return false;
        if (world.getObjects().data() != data_ || world.getObjects().size() != count_) { invalidate(); return false; }
        // Observer projections and temporary probes do not borrow original-world records.
        if (!data_ || !count_) return false;
        const auto start = reinterpret_cast<std::uintptr_t>(data_);
        const auto value = reinterpret_cast<std::uintptr_t>(&object);
        if (value < start) return false;
        const auto distance = value - start;
        return distance % sizeof(WorldObjectInstance) == 0 && distance / sizeof(WorldObjectInstance) < count_;
    }
    const GameWorld* world_; const RuntimeMapRenderContext* context_;
    const WorldObjectInstance* data_; std::size_t count_;
    const RulesDatabase* rules_; const ArtDatabase* art_;
    bool enabled_ = false, ready_ = false;
    std::vector<RuntimeWorldObjectDrawRecord> records_;
};
