RuntimeMouseCursorFrame runtimePanMouseCursorFrame(const RuntimeWindowState& state) {
    // Retail MOUSE.SHP 385 is the neutral pan cross. Frames 386..393
    // highlight N, NE, E, SE, S, SW, W, NW, with the native center hotspot.
    if (!state.rightPointer.moved) return {385, 27, 21};
    const POINT delta = state.runtimeScrollCoastingEnabled
        ? POINT{state.rightPointer.last.x - state.rightPointer.start.x,
                state.rightPointer.last.y - state.rightPointer.start.y}
        : POINT{state.rightPointer.cursorDelta.x, state.rightPointer.cursorDelta.y};
    const double dx = delta.x, dy = delta.y;
    if (dx * dx + dy * dy <= 16.0) return {385, 27, 21};
    constexpr double pi = 3.14159265358979323846;
    const int direction = (static_cast<int>(std::floor(
        std::atan2(dx, -dy) * 4.0 / pi + 0.5)) + 8) % 8;
    return {386 + direction, 27, 21};
}
