bool validParticlePresentation(const RuntimeParticleAirbornePresentation& presentation) {
    const auto coordinate = [](double value, double scale) {
        return std::isfinite(value)
            && std::abs(value) <= scale * kMaximumRenderCoordinate / kRawUnitsPerCell;
    };
    if (!presentation.enabled) return presentation == RuntimeParticleAirbornePresentation{};
    if (!coordinate(presentation.sourceCellSum, 2.0)
        || !coordinate(presentation.sourceLevel, 1.0)
        || presentation.factoryLiftCancellation < -1
        || presentation.factoryLiftCancellation > 34
        || !std::isfinite(presentation.clearanceFraction)
        || presentation.clearanceFraction < 0.0
        || presentation.clearanceFraction > 1.0
        || !std::isfinite(presentation.resolvedClearanceLevel)
        || presentation.resolvedClearanceLevel < -1.0
        || (presentation.resolvedClearanceLevel < 0.0 && presentation.resolvedClearanceLevel != -1.0)
        || presentation.resolvedClearanceLevel > 64.0
        || presentation.roofs.size() > runtimeParticleMaximumClearanceRoofs) return false;
    return std::all_of(presentation.roofs.begin(), presentation.roofs.end(), [&](const auto& roof) {
        return coordinate(roof.cellSum, 2.0) && coordinate(roof.level, 1.0)
            && roof.authoredHeight >= 0 && roof.authoredHeight <= 65536
            && roof.nativeSpriteHeight >= 0 && roof.nativeSpriteHeight <= 65536;
    });
}

bool validParticleAttachment(const RuntimeParticleSpriteAttachment& attachment) {
    const auto bounded = [](int value) { return value >= -4096 && value <= 4096; };
    return bounded(attachment.screenOffsetX) && bounded(attachment.screenOffsetY)
        && bounded(attachment.nativeFixedX) && bounded(attachment.nativeFixedY);
}

void hashParticlePresentation(std::uint64_t& hash,
    const RuntimeParticleAirbornePresentation& presentation,
    const RuntimeParticleSpriteAttachment& attachment) {
    hashSnapshotValue(hash, presentation.enabled);
    hashSnapshotDouble(hash, presentation.sourceCellSum);
    hashSnapshotDouble(hash, presentation.sourceLevel);
    hashSnapshotValue(hash, static_cast<std::uint64_t>(presentation.factoryLiftCancellation));
    hashSnapshotValue(hash, presentation.roofs.size());
    for (const auto& roof : presentation.roofs) {
        hashSnapshotDouble(hash, roof.cellSum);
        hashSnapshotDouble(hash, roof.level);
        hashSnapshotValue(hash, roof.authoredHeight);
        hashSnapshotValue(hash, roof.nativeSpriteHeight);
        hashSnapshotValue(hash, roof.rubble);
    }
    // This revision describes local drawing; retain the legacy revision for
    // the default flight pose and include the frozen descent fade otherwise.
    if (presentation.clearanceFraction != 1.0) {
        hashSnapshotDouble(hash, presentation.clearanceFraction);
    }
    if (presentation.resolvedClearanceLevel >= 0.0) {
        hashSnapshotDouble(hash, presentation.resolvedClearanceLevel);
    }
    hashSnapshotValue(hash, static_cast<std::uint64_t>(attachment.screenOffsetX));
    hashSnapshotValue(hash, static_cast<std::uint64_t>(attachment.screenOffsetY));
    hashSnapshotValue(hash, static_cast<std::uint64_t>(attachment.nativeFixedX));
    hashSnapshotValue(hash, static_cast<std::uint64_t>(attachment.nativeFixedY));
    hashSnapshotValue(hash, attachment.voxelAnchor);
    hashSnapshotValue(hash, attachment.airborne);
}

void saveParticlePresentation(std::ostream& out,
    const RuntimeParticleAirbornePresentation& presentation,
    const RuntimeParticleSpriteAttachment& attachment, bool authoritative) {
    // Absent optional fields retain the exact legacy representation.
    if (presentation.enabled) {
        out << "PresentationEnabled=yes\n"
            << "PresentationSourceCellSum=" << presentation.sourceCellSum << '\n'
            << "PresentationSourceLevel=" << presentation.sourceLevel << '\n'
            << "PresentationFactoryLiftCancellation=" << presentation.factoryLiftCancellation << '\n'
            << "PresentationRoofCount=" << presentation.roofs.size() << '\n';
        // The fade affects only presentation. Like native sprite dimensions,
        // it belongs in saves/render revisions, outside the gameplay hash.
        if (!authoritative && presentation.clearanceFraction != 1.0) {
            out << "PresentationClearanceFraction=" << presentation.clearanceFraction << '\n';
        }
        if (!authoritative && presentation.resolvedClearanceLevel >= 0.0) {
            out << "PresentationResolvedClearanceLevel=" << presentation.resolvedClearanceLevel << '\n';
        }
        for (std::size_t i = 0; i < presentation.roofs.size(); ++i) {
            const auto& roof = presentation.roofs[i];
            const std::string prefix = "PresentationRoof" + std::to_string(i);
            out << prefix << "CellSum=" << roof.cellSum << '\n'
                << prefix << "Level=" << roof.level << '\n'
                << prefix << "AuthoredHeight=" << roof.authoredHeight << '\n'
                // Local sprite dimensions affect only drawing. Keep them in
                // saves/revisions but not in the lockstep authority hash.
                << prefix << "NativeSpriteHeight=" << (authoritative ? 0 : roof.nativeSpriteHeight) << '\n'
                << prefix << "Rubble=" << (roof.rubble ? "yes" : "no") << '\n';
        }
    }
    if (attachment != RuntimeParticleSpriteAttachment{}) {
        out << "AttachmentScreenOffsetX=" << attachment.screenOffsetX << '\n'
            << "AttachmentScreenOffsetY=" << attachment.screenOffsetY << '\n'
            << "AttachmentNativeFixedX=" << attachment.nativeFixedX << '\n'
            << "AttachmentNativeFixedY=" << attachment.nativeFixedY << '\n'
            << "AttachmentVoxelAnchor=" << (attachment.voxelAnchor ? "yes" : "no") << '\n'
            << "AttachmentAirborne=" << (attachment.airborne ? "yes" : "no") << '\n';
    }
}

bool loadParticlePresentation(const IniFile& ini, const std::string& section,
    RuntimeParticleAirbornePresentation& presentation,
    RuntimeParticleSpriteAttachment& attachment) {
    const auto value = [&](const std::string& key, const std::string& fallback) {
        return ini.getValue(section, key, fallback);
    };
    bool hasAttachment = false;
    for (const char* key : {"AttachmentScreenOffsetX", "AttachmentScreenOffsetY",
            "AttachmentNativeFixedX", "AttachmentNativeFixedY", "AttachmentVoxelAnchor", "AttachmentAirborne"})
        hasAttachment = hasAttachment || value(key, "__absent__") != "__absent__";
    int count = 0;
    if (!parseBool(value("PresentationEnabled", "no"), presentation.enabled)
        || !parseDoubleStrict(value("PresentationSourceCellSum", presentation.enabled ? "" : "0"), presentation.sourceCellSum)
        || !parseDoubleStrict(value("PresentationSourceLevel", presentation.enabled ? "" : "0"), presentation.sourceLevel)
        || !parseIntStrict(value("PresentationFactoryLiftCancellation", presentation.enabled ? "" : "-1"), presentation.factoryLiftCancellation)
        || !parseDoubleStrict(value("PresentationClearanceFraction", "1"), presentation.clearanceFraction)
        || !parseDoubleStrict(value("PresentationResolvedClearanceLevel", "-1"), presentation.resolvedClearanceLevel)
        || !parseIntStrict(value("PresentationRoofCount", presentation.enabled ? "" : "0"), count)
        || count < 0 || static_cast<std::size_t>(count) > runtimeParticleMaximumClearanceRoofs
        || !parseIntStrict(value("AttachmentScreenOffsetX", hasAttachment ? "" : "0"), attachment.screenOffsetX)
        || !parseIntStrict(value("AttachmentScreenOffsetY", hasAttachment ? "" : "0"), attachment.screenOffsetY)
        || !parseIntStrict(value("AttachmentNativeFixedX", hasAttachment ? "" : "0"), attachment.nativeFixedX)
        || !parseIntStrict(value("AttachmentNativeFixedY", hasAttachment ? "" : "0"), attachment.nativeFixedY)
        || !parseBool(value("AttachmentVoxelAnchor", hasAttachment ? "" : "no"), attachment.voxelAnchor)
        || !parseBool(value("AttachmentAirborne", hasAttachment ? "" : "no"), attachment.airborne)) return false;
    presentation.roofs.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        RuntimeParticleClearanceRoof roof;
        const std::string prefix = "PresentationRoof" + std::to_string(i);
        // A declared roof requires every geometry field: missing data is not
        // a legacy save and cannot silently become a different attachment.
        if (!parseDoubleStrict(value(prefix + "CellSum", ""), roof.cellSum)
            || !parseDoubleStrict(value(prefix + "Level", ""), roof.level)
            || !parseIntStrict(value(prefix + "AuthoredHeight", ""), roof.authoredHeight)
            || !parseIntStrict(value(prefix + "NativeSpriteHeight", ""), roof.nativeSpriteHeight)
            || !parseBool(value(prefix + "Rubble", ""), roof.rubble)) return false;
        presentation.roofs.push_back(roof);
    }
    return validParticlePresentation(presentation) && validParticleAttachment(attachment);
}
