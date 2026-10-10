RuntimeHvaTransform runtimeTiltVoxelSection(
    const RuntimeHvaTransform* original, const RuntimeVoxelBodyRotation& rotation,
    double facingCos, double facingSin, double pivotX, double pivotY
) {
    const RuntimeHvaTransform base = original == nullptr ? RuntimeHvaTransform{} : *original;
    std::array<std::array<double, 3>, 3> axes{};
    // All parts receive the same world-axis tilt, even when the turret points
    // elsewhere. Convert it back into each part's own yaw coordinates.
    for (int column = 0; column < 3; ++column) {
        const double x = column == 0 ? facingCos : column == 1 ? -facingSin : 0.0;
        const double y = column == 0 ? facingSin : column == 1 ? facingCos : 0.0;
        const auto v = rotation.rotate(x, y, column == 2 ? 1.0 : 0.0);
        axes[column] = {v[0] * facingCos + v[1] * facingSin,
            -v[0] * facingSin + v[1] * facingCos, v[2]};
    }
    RuntimeHvaTransform result;
    result.valid = true;
    const std::array<double, 3> pivot{pivotX, pivotY, rotation.floorZ};
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            result.m[row * 4 + column] = 0.0;
            for (int k = 0; k < 3; ++k)
                result.m[row * 4 + column] += axes[k][row] * base.m[k * 4 + column];
        }
        result.m[row * 4 + 3] += pivot[row];
        for (int k = 0; k < 3; ++k) result.m[row * 4 + 3] -= axes[k][row] * pivot[k];
    }
    return result;
}
