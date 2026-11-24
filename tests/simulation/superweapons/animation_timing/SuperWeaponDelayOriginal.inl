    const int hz = std::max(1, fixedTickHz);
    // artmd Rate is frames per game minute (900 original ticks), not ticks
    // in this host's 30/60 Hz simulation.
    return art && art->rate > 0
        ? std::max(1, std::max(1, 900 / art->rate) * hz / 15)
        : std::max(1, hz / 15); // AnimTypeClass default delay is one native frame.
