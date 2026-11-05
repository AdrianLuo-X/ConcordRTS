// Teleporting infantry retain the ordinary three-subcell capacity. The
// caller separately validates terrain and whole-cell blockers at both ends.


// Warp effects outlive the one-tick displacement. Recovery belongs to the
// special mechanism state, so loading or issuing another order cannot skip it.




// Shared by queued movement and an attack's approach path. A teleporter must
// never fall back to walking when the destination or locomotor is unavailable.

