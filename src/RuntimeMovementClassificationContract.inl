// Independent copies of the original classifier expressions retain the string
// normalization and precedence, so this contract detects changes to either.
bool runRuntimeMovementClassificationContract(std::string& error) {
    const auto lower = [](std::string value) {
        for (char& character : value)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return value;
    };
    const auto trim = [](const std::string& value) {
        std::size_t first = 0;
        while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
        std::size_t last = value.size();
        while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
        return value.substr(first, last - first);
    };
    const auto jumpjet = [&](const UnitType& unit) {
        return unit.jumpJet || lower(trim(unit.locomotor)) == "{92612c46-f71f-11d1-ac9f-006008055bb5}";
    };
    const auto air = [&](const UnitType& unit) {
        const std::string speedType = lower(unit.speedType);
        const std::string movementZone = lower(unit.movementZone);
        const std::string locomotor = lower(unit.locomotor);
        return jumpjet(unit) || unit.consideredAircraft
            || movementZone.find("fly") != std::string::npos
            || speedType.find("winged") != std::string::npos
            || speedType.find("fly") != std::string::npos
            || locomotor.find("fly") != std::string::npos;
    };
    const auto movement = [&](const UnitType& unit) {
        const std::string speedType = lower(unit.speedType);
        const std::string movementZone = lower(unit.movementZone);
        if (air(unit)) return PathMovementMode::Air;
        if (speedType.find("amphib") != std::string::npos || speedType.find("hover") != std::string::npos
            || movementZone.find("amphib") != std::string::npos || movementZone.find("hover") != std::string::npos)
            return PathMovementMode::Amphibious;
        if (unit.naval || unit.waterBound || speedType.find("float") != std::string::npos
            || speedType.find("water") != std::string::npos || movementZone.find("water") != std::string::npos)
            return PathMovementMode::Naval;
        return PathMovementMode::Ground;
    };
    const auto land = [&](const LandType& type, const UnitType& unit, bool infantry) {
        const std::string speedType = lower(unit.speedType);
        const std::string movementZone = lower(unit.movementZone);
        if (speedType.find("hover") != std::string::npos || movementZone.find("hover") != std::string::npos
            || movementZone.find("fly") != std::string::npos) return type.hover;
        if (speedType.find("amphib") != std::string::npos || movementZone.find("amphib") != std::string::npos)
            return type.amphibious;
        if (speedType.find("floatbeach") != std::string::npos || movementZone.find("floatbeach") != std::string::npos)
            return type.floatBeach;
        if (speedType.find("float") != std::string::npos || speedType.find("water") != std::string::npos
            || movementZone.find("water") != std::string::npos || unit.naval || unit.waterBound) return type.floatSpeed;
        if (speedType.find("track") != std::string::npos) return type.track;
        if (speedType.find("wheel") != std::string::npos) return type.wheel;
        if (infantry || speedType.find("foot") != std::string::npos
            || movementZone.find("infantry") != std::string::npos) return type.foot;
        return unit.crusher ? type.track : type.wheel;
    };
    const auto fail = [&](const std::string& reason) { error = "movement classification: " + reason; return false; };
    LandType type;
    // Compare return values bitwise, including NaN and signed zero: profiles
    // must select the same rule field without arithmetic or normalization.
    type.foot = std::bit_cast<double>(0x7ff8000000000169ULL);
    type.track = -0.0;
    type.wheel = -17.25;
    type.floatSpeed = 75.5;
    type.floatBeach = std::numeric_limits<double>::infinity();
    type.hover = 0.0;
    type.amphibious = 123.75;
    std::size_t checks = 0;
    const auto check = [&](const UnitType& unit) {
        if (runtimeUnitJumpjetMovementProfile(unit) != jumpjet(unit)
            || runtimeUnitAirMovementProfile(unit) != air(unit)
            || runtimeUnitMovementModeProfile(unit) != movement(unit)) return false;
        for (bool infantry : {false, true}) {
            if (std::bit_cast<std::uint64_t>(runtimeUnitLandPercentProfile(type, unit, infantry))
                != std::bit_cast<std::uint64_t>(land(type, unit, infantry))) return false;
        }
        ++checks;
        return true;
    };
    const std::array<std::string, 20> tokens{"", "Track", "Wheel", "Foot", "Infantry", "Winged", "Fly",
        "Hover", "Amphibious", "Float", "FloatBeach", "Water", "DEFAULT", " winged ", "trackwheel",
        "floatbeach-hover", "FlyAmphibiousWater", "waterwheelFOOT", std::string("\x80\xffHover\0x", 9),
        std::string("x\0FLOATBEACH\xff", 13)};
    const std::array<std::string, 6> locomotors{"", "Drive", "FLY",
        "{92612c46-f71f-11d1-ac9f-006008055bb5}",
        "\t\r {92612C46-F71F-11D1-AC9F-006008055BB5}\n\v",
        "{92612c46-f71f-11d1-ac9f-006008055bb5}x"};
    UnitType unit;
    for (unsigned int flags = 0; flags < 32; ++flags) {
        unit.jumpJet = (flags & 1) != 0;
        unit.consideredAircraft = (flags & 2) != 0;
        unit.naval = (flags & 4) != 0;
        unit.waterBound = (flags & 8) != 0;
        unit.crusher = (flags & 16) != 0;
        for (const auto& speed : tokens) for (const auto& zone : tokens) for (const auto& locomotor : locomotors) {
            unit.speedType = speed; unit.movementZone = zone; unit.locomotor = locomotor;
            if (!check(unit)) return fail("rule precedence at case " + std::to_string(checks));
        }
    }
    std::uint32_t seed = 0x2309217U;
    const auto next = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
    for (int sample = 0; sample < 20000; ++sample) {
        for (std::string* field : {&unit.speedType, &unit.movementZone, &unit.locomotor}) {
            field->clear();
            const auto length = next() % 96;
            for (unsigned int index = 0; index < length; ++index) field->push_back(static_cast<char>(next() & 255));
            if (sample % 3 == 0) field->insert(field->size() / 2, tokens[next() % tokens.size()]);
        }
        unit.jumpJet = next() % 5 == 0;
        unit.consideredAircraft = next() % 5 == 0;
        unit.naval = next() % 5 == 0;
        unit.waterBound = next() % 5 == 0;
        unit.crusher = next() % 2 == 0;
        if (!check(unit)) return fail("random/dynamic rules at case " + std::to_string(checks));
    }
    unit.jumpJet = unit.consideredAircraft = unit.naval = unit.waterBound = unit.crusher = false;
    for (const std::size_t length : {127U, 128U, 129U, 1024U}) {
        for (const auto& token : tokens) for (const bool atEnd : {false, true}) {
            unit.speedType = std::string(length, 'x');
            unit.speedType.insert(atEnd ? unit.speedType.size() : 0, token);
            unit.movementZone = unit.speedType;
            unit.locomotor = std::string(length, ' ')
                + "{92612C46-F71F-11D1-AC9F-006008055BB5}" + std::string(length, '\t');
            if (!check(unit)) return fail("long-field fallback/trim at case " + std::to_string(checks));
            unit.locomotor = "Drive";
            if (!check(unit)) return fail("long-field movement at case " + std::to_string(checks));
        }
    }
    // Exhaust every possible char byte around needles and whitespace. Embedded
    // NUL remains part of a std::string rather than terminating the comparison.
    for (unsigned int byte = 0; byte < 256; ++byte) {
        for (const auto& token : tokens) {
            const std::string value = std::string(3, static_cast<char>(byte)) + token + std::string(3, static_cast<char>(byte));
            for (const std::string_view needle : {"", "fly", "winged", "amphib", "hover", "floatbeach", "track"}) {
                if (runtimeMovementContainsLowercase(value, needle) != (lower(value).find(needle) != std::string::npos)
                    || runtimeMovementTrimEqualsLowercase(value, needle) != (lower(trim(value)) == needle))
                    return fail("byte/trim at " + std::to_string(byte));
            }
        }
    }
    std::cout << "Movement classification: " << checks << " rule profiles, all flags/priorities, dynamic mutations, "
        "all 256 bytes/embedded NUL/trim and exact land-value bits match original expressions.\n";
    return true;
}
