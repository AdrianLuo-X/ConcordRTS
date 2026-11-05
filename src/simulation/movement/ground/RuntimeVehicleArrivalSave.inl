#include "simulation/movement/ground/RuntimeStoppedMovePoseSave.inl"

// Startup distance controls the next accepted segment, so retain it even at
// a segment boundary where no interpolation record is active. Earlier saves
// already had a cruise clock; importing that active clock must not restart
// acceleration on the following cell.


// The original Omni approach can retain navigation ownership of FROM until
// the occupied destination becomes available.




// Optional visual state. Movement cooldowns remain in the ordinary behavior
// records and determine gameplay timing, including after a legacy save loads.







