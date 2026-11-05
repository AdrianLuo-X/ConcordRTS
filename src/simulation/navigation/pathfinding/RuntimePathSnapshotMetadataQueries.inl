// Include after RuntimeWindowState/RuntimeTerrainInfo, before the dynamic
// buildRuntimePathTerrainSnapshot body. A builder borrows its owner's table.
#pragma once

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>





class RuntimePathSnapshotMetadataQueryReferenceScope {
public:
    explicit RuntimePathSnapshotMetadataQueryReferenceScope(bool reference)
        : previous_(runtimePathSnapshotMetadataQueryReferenceOverride) {
        runtimePathSnapshotMetadataQueryReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimePathSnapshotMetadataQueryReferenceScope() {
        runtimePathSnapshotMetadataQueryReferenceOverride = previous_;
    }
    RuntimePathSnapshotMetadataQueryReferenceScope(const RuntimePathSnapshotMetadataQueryReferenceScope&) = delete;
    RuntimePathSnapshotMetadataQueryReferenceScope& operator=(const RuntimePathSnapshotMetadataQueryReferenceScope&) = delete;
private:
    int previous_;
};

// One read-only dynamic snapshot loop borrows a container-owned table. Preserve
// the ORIGINAL integer-key identity, including legacy raw-coordinate aliases.
// Missing metadata means nullptr/skip, with NO runtimeTerrainInfoForCell or
// runtimeCachedTerrainInfoForCell call. An out-of-domain key uses the old find.
//
// The metadata container invalidates the table before every structural edit,
// copy, move and swap. In-place value edits retain their live node addresses.
// The structural generation also detects same-size replacements.
// A view is acquired once immediately before the loop and must not outlive that
// synchronous read-only loop, its owner or a reference-scope change.

