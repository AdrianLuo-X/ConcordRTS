// Read-only geometry derived from an authored VXL gun barrel. Include after
// RuntimeHvaTransform. Call once per source/HVA frame and cache the result.
// No lighting, screen projection, owner remap, body pivot, or facing is applied.
struct RuntimeVoxelMuzzleEndpoint {
    std::array<double, 3> modelPoint{};
    std::array<double, 3> modelForward{};
    unsigned int sectionIndex = 0;
    int faceVoxelCount = 0;
};

bool resolveRuntimeVoxelMuzzleEndpoints(
    const std::vector<unsigned char>& bytes,
    const std::vector<RuntimeHvaTransform>& transforms,
    std::vector<RuntimeVoxelMuzzleEndpoint>& endpoints,
    std::string& error
) {
    endpoints.clear();
    error.clear();
    const auto fail = [&](const char* message) {
        endpoints.clear();
        error = message;
        return false;
    };
    if (bytes.size() < 802 || std::memcmp(bytes.data(), "Voxel Animation", 15) != 0)
        return fail("Muzzle model has an invalid VXL header.");
    const auto u32 = [&](std::size_t at) {
        std::uint32_t value = 0;
        std::memcpy(&value, bytes.data() + at, sizeof(value));
        return value;
    };
    const auto f32 = [&](std::size_t at) {
        float value = 0.0f;
        std::memcpy(&value, bytes.data() + at, sizeof(value));
        return static_cast<double>(value);
    };
    const unsigned int sections = u32(20);
    if (sections == 0 || sections > 1024 || u32(24) != sections)
        return fail("Muzzle model has invalid VXL sections.");
    const std::size_t bodyStart = 802 + static_cast<std::size_t>(sections) * 28;
    const std::size_t bodyBytes = u32(28);
    const std::size_t footerStart = bodyStart + bodyBytes;
    if (footerStart > bytes.size()
        || bytes.size() - footerStart < static_cast<std::size_t>(sections) * 92)
        return fail("Muzzle model has truncated VXL data.");
    for (unsigned int section = 0; section < sections; ++section) {
        const std::size_t footer = footerStart + static_cast<std::size_t>(section) * 92;
        const int nx = bytes[footer + 88], ny = bytes[footer + 89], nz = bytes[footer + 90];
        if (nx == 0 || ny == 0 || nz == 0)
            return fail("Muzzle model has empty VXL dimensions.");
        std::array<double, 3> minimum{}, scale{}, span{};
        for (int axis = 0; axis < 3; ++axis) {
            minimum[axis] = f32(footer + 64 + axis * 4);
            span[axis] = f32(footer + 76 + axis * 4) - minimum[axis];
            scale[axis] = span[axis] / (axis == 0 ? nx : axis == 1 ? ny : nz);
            if (!std::isfinite(minimum[axis]) || !std::isfinite(scale[axis]) || scale[axis] <= 0.0)
                return fail("Muzzle model has invalid VXL bounds.");
        }
        // All stock separate barrels and the two YAGGUN tube sections are
        // elongated along authored +X. Reject broad housings/body sections;
        // callers retain the configured FLH when geometry is ambiguous.
        if (span[0] < 1.75 * std::max(span[1], span[2]))
            continue;
        RuntimeHvaTransform matrix;
        matrix.valid = true;
        for (int i = 0; i < 12; ++i)
            matrix.m[static_cast<std::size_t>(i)] = f32(footer + 16 + i * 4);
        if (section < transforms.size() && transforms[section].valid) {
            matrix = transforms[section];
            double multiplier = f32(footer + 12);
            if (!std::isfinite(multiplier) || multiplier == 0.0)
                multiplier = 1.0;
            for (int axis = 0; axis < 3; ++axis)
                matrix.m[static_cast<std::size_t>(axis * 4 + 3)] *= multiplier * scale[axis];
        }
        for (double value : matrix.m)
            if (!std::isfinite(value))
                return fail("Muzzle model has an invalid HVA transform.");
        const std::size_t count = static_cast<std::size_t>(nx) * ny;
        const std::size_t startOffset = u32(footer);
        const std::size_t endOffset = u32(footer + 4);
        const std::size_t dataOffset = u32(footer + 8);
        if (startOffset > bodyBytes || count * 4 > bodyBytes - startOffset
            || endOffset > bodyBytes || count * 4 > bodyBytes - endOffset
            || dataOffset > bodyBytes)
            return fail("Muzzle model has invalid VXL span tables.");
        // Keep only the forwardmost occupied local-X face. Gaps are encoded by
        // spans; palette index zero is occupied just as in the renderer.
        std::vector<unsigned char> face(static_cast<std::size_t>(ny) * nz, 0);
        int frontX = -1;
        for (int x = nx - 1; x >= 0 && frontX < 0; --x) {
            for (int y = 0; y < ny; ++y) {
                const std::size_t column = static_cast<std::size_t>(y) * nx + x;
                const std::uint32_t start = u32(bodyStart + startOffset + column * 4);
                const std::uint32_t end = u32(bodyStart + endOffset + column * 4);
                if (start == 0xffffffffU) {
                    if (end != 0xffffffffU)
                        return fail("Muzzle model has mismatched empty VXL spans.");
                    continue;
                }
                if (end < start || start >= bodyBytes - dataOffset
                    || end >= bodyBytes - dataOffset)
                    return fail("Muzzle model has a VXL span outside its body.");
                std::size_t cursor = bodyStart + dataOffset + start;
                const std::size_t stop = bodyStart + dataOffset + end + 1;
                int z = 0;
                while (cursor < stop && z < nz) {
                    if (stop - cursor < 3)
                        return fail("Muzzle model has a truncated VXL span.");
                    const int skip = bytes[cursor++], run = bytes[cursor++];
                    z += skip;
                    if (z > nz || run > nz - z
                        || static_cast<std::size_t>(run) * 2 + 1 > stop - cursor)
                        return fail("Muzzle model has an invalid VXL run.");
                    for (int i = 0; i < run; ++i) {
                        face[static_cast<std::size_t>(y) * nz + z++] = 1;
                        cursor += 2;
                        frontX = x;
                    }
                    if (bytes[cursor++] != run)
                        return fail("Muzzle model has an inconsistent VXL run.");
                    if (skip == 0 && run == 0)
                        break;
                }
                if (cursor != stop)
                    return fail("Muzzle model has trailing VXL span data.");
            }
        }
        if (frontX < 0)
            continue;
        // Separate lateral runs preserve the two Apocalypse barrels. Do not
        // split on gaps in Z: circular muzzles and the rotating YAGGUN cluster
        // have disconnected decorative tips belonging to the same gun mouth.
        const auto columnOccupied = [&](int y) {
            for (int z = 0; z < nz; ++z)
                if (face[static_cast<std::size_t>(y) * nz + z] != 0)
                    return true;
            return false;
        };
        for (int y = 0; y < ny;) {
            if (!columnOccupied(y)) { ++y; continue; }
            double sumY = 0.0, sumZ = 0.0;
            int samples = 0;
            while (y < ny && columnOccupied(y)) {
                for (int z = 0; z < nz; ++z) {
                    if (face[static_cast<std::size_t>(y) * nz + z] == 0)
                        continue;
                    sumY += y + 0.5;
                    sumZ += z + 0.5;
                    ++samples;
                }
                ++y;
            }
            RuntimeVoxelMuzzleEndpoint endpoint;
            endpoint.sectionIndex = section;
            endpoint.faceVoxelCount = samples;
            const std::array<double, 3> local{
                minimum[0] + (frontX + 1.0) * scale[0],
                minimum[1] + sumY / samples * scale[1],
                minimum[2] + sumZ / samples * scale[2]
            };
            double forwardLength = 0.0;
            for (int axis = 0; axis < 3; ++axis) {
                endpoint.modelPoint[axis] = matrix.m[static_cast<std::size_t>(axis * 4 + 3)];
                for (int column = 0; column < 3; ++column)
                    endpoint.modelPoint[axis] += matrix.m[static_cast<std::size_t>(axis * 4 + column)] * local[column];
                endpoint.modelForward[axis] = matrix.m[static_cast<std::size_t>(axis * 4)];
                forwardLength += endpoint.modelForward[axis] * endpoint.modelForward[axis];
            }
            forwardLength = std::sqrt(forwardLength);
            if (!std::isfinite(forwardLength) || forwardLength < 1e-8)
                return fail("Muzzle model has a degenerate HVA forward axis.");
            for (double& component : endpoint.modelForward)
                component /= forwardLength;
            endpoints.push_back(endpoint);
        }
    }
    if (endpoints.empty())
        return fail("No unambiguous elongated gun section exists in the VXL model.");
    return true;
}
