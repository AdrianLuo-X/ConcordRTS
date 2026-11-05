// A verified entry owns a slotless destination, while neighboring bodies
// remain ordinary collision obstacles. Unit hosts use the existing strict
// slave-worker/owner/task/phase/refinery rule; structures keep their entry marker.



// Accepted convoys can interpolate at different speeds or stop independently.
// Keep each accepted cell/timing, but wait before a visible body closes inside
// another moving/stopped body's spacing. An overlap may leave by separating.


// Reuse only the tick cache's sparse candidate inventory, never cached pose
// coordinates. Newly accepted segments append their envelopes during this tick.
// Dead/off-ground/changed records are checked at every read.


// The occupied destination of an Omni approach has not committed yet, but its
// accepted segment still reserves the same corridor against new foundations
// and other committing ground movers.


// A new foundation cannot appear under an already accepted ground segment.
// Use saved segment endpoints, including a stopped body's resumed first leg.
// Diagonal side cells were required to be clear when that segment committed, too.


// x/y is committed at the start of a step. Hold its departure cell until the
// authoritative cooldown finishes for mixed traffic. Infantry departures also
// retain capacity against the three-person limit. Planning keeps transient
// traffic; actual commits wait, without discarding the path or changing its goal.


class RuntimeGroundDepartureBatchReferenceScope {
    int previous_;
public:
    explicit RuntimeGroundDepartureBatchReferenceScope(bool reference)
        : previous_(runtimeGroundDepartureBatchReferenceOverride) {
        runtimeGroundDepartureBatchReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeGroundDepartureBatchReferenceScope() { runtimeGroundDepartureBatchReferenceOverride = previous_; }
    RuntimeGroundDepartureBatchReferenceScope(const RuntimeGroundDepartureBatchReferenceScope&) = delete;
    RuntimeGroundDepartureBatchReferenceScope& operator=(const RuntimeGroundDepartureBatchReferenceScope&) = delete;
};




// Only the current mover's read-only neighbor-candidate loop may own this scope.
// Destroy/invalidate before facing, damage, reservations or commits mutate any
// world/behavior state. No snapshot survives the loop, unit or fixed tick.




// Shared infantry capacity does not reserve an entire tile. Serialize only
// strictly crossing body paths, before a second accepted segment can deadlock
// both bodies at their spacing limit. Parallel paths and shared endpoints keep
// the existing three-subcell admission rules.


// A dedicated factory segment keeps its committed linear clock. Wait before
// committing the cell when its complete body route cannot clear outside traffic.


// Match reserveBatch's actual current-or-first-available slot without mutating leases.


// Reject a body route before its logical cell/subcell commits. The late guard
// remains a floor, but cannot reroute an already committed segment through an idle resident.

// Virtual-cell searches have already selected the current-or-first-available slot. Do not reuse
// a real object's old reservation as if it survived an earlier virtual step.



