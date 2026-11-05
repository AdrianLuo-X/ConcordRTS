// Air traffic has its own occupancy domain. Ground foundations/subcells do
// not obstruct flight, but another flyer (including its unfinished segment)
// does. This is derived from authoritative objects/behaviors, not a second
// reservation state that could go stale after orders, deaths or save/load.


















// Find a free destination that can actually be reached. Testing only the
// nearest few empty cells repeatedly can trap a dense formation around an
// enclosed pocket, especially when its flyers now reserve cells for longer.


// Keep the already committed flight segment when a move order interrupts
// another order. Snapping to its endpoint both looks wrong and releases the
// departure cell before the visible flyer has actually left it.

