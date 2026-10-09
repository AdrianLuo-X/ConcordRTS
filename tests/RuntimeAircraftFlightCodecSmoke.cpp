#include "RuntimeAircraftMotion.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
int checks = 0;

void require(bool ok, const std::string& reason) {
    ++checks;
    if (!ok) {
        std::cerr << "Aircraft flight codec: " << reason << '\n';
        std::exit(1);
    }
}

using Row = std::array<std::string, 12>;

std::string record(const Row& fields) {
    std::string text;
    for (std::size_t n = 0; n < fields.size(); ++n) {
        if (n) text += ' ';
        text += fields[n];
    }
    return text + '\n';
}

std::string document(const Row& fields) {
    return "AircraftFlights 3 1\n" + record(fields);
}
}

int main() {
    constexpr auto q = runtimeAircraftCellQ16;
    constexpr auto xyLimit = 512 * q;
    constexpr auto zLimit = 4096 * q;
    RuntimeAircraftFlights flights;

    require(deserializeRuntimeAircraftFlights("AircraftFlights 1 1\n7 65537 131074 -1 1\n", flights),
        "legacy schema 1 rejected");
    const auto& old1 = flights.at(7);
    require(old1.x == 65537 && old1.y == 131074 && old1.lastTick == -1 && old1.moving
        && old1.heading == -1 && old1.z == -1 && old1.dockFlightStartZ == -1
        && old1.dockFlightTargetZ == -1 && old1.landingHostId == -1
        && old1.landingDockIndex == -1 && !old1.landingSoundPlayed,
        "schema 1 did not retain coordinates and import new defaults");
    require(deserializeRuntimeAircraftFlights("AircraftFlights 2 1\n9 0 33554431 45 0 65535\n", flights),
        "legacy schema 2 rejected");
    const auto& old2 = flights.at(9);
    require(old2.heading == 65535 && !old2.moving && old2.z == -1
        && old2.dockFlightStartZ == -1 && old2.dockFlightTargetZ == -1
        && old2.landingHostId == -1 && old2.landingDockIndex == -1 && !old2.landingSoundPlayed,
        "schema 2 heading or new defaults changed");

    RuntimeAircraftFlights expected;
    RuntimeAircraftFlight imported;
    imported.x = xyLimit - 1; imported.y = 0;
    expected.emplace(0, imported);
    RuntimeAircraftFlight cruise;
    cruise.x = q + 17; cruise.y = 2 * q + 33; cruise.lastTick = 81;
    cruise.moving = true; cruise.heading = 13579; cruise.z = zLimit;
    expected.emplace(10, cruise);
    RuntimeAircraftFlight descending;
    descending.x = 3 * q + q / 2; descending.y = 7 * q + q / 2;
    descending.lastTick = 100; descending.heading = 65535;
    descending.z = 3 * q + 173; descending.dockFlightStartZ = 6 * q;
    descending.dockFlightTargetZ = 2 * q; descending.landingHostId = 42;
    descending.landingDockIndex = 3; descending.landingSoundPlayed = true;
    expected.emplace(999, descending);
    RuntimeAircraftFlight cancelled = descending;
    cancelled.z = cancelled.dockFlightTargetZ; cancelled.heading = 0;
    cancelled.landingHostId = cancelled.landingDockIndex = -1;
    expected.emplace(1000, cancelled);
    const auto encoded = serializeRuntimeAircraftFlights(expected);
    require(encoded.rfind("AircraftFlights 3 4\n", 0) == 0, "writer did not emit schema 3");
    require(deserializeRuntimeAircraftFlights(encoded, flights), "schema 3 roundtrip rejected");
    require(serializeRuntimeAircraftFlights(flights) == encoded,
        "schema 3 changed continuous height, heading, envelope or sound latch");
    require(deserializeRuntimeAircraftFlights("AircraftFlights 3 0\n \t\r\n", flights)
        && flights.empty(), "empty flight table or trailing whitespace rejected");

    const Row ordinary{"5", "1", "2", "-1", "0", "-1", "-1", "-1", "-1", "-1", "-1", "0"};
    const Row landing{"5", "1", "2", "9", "0", "512", "100", "200", "0", "42", "3", "1"};
    const auto accept = [&](const Row& fields, const std::string& reason) {
        RuntimeAircraftFlights parsed;
        require(deserializeRuntimeAircraftFlights(document(fields), parsed) && parsed.size() == 1, reason);
    };
    const auto reject = [&](const std::string& text, const std::string& reason) {
        RuntimeAircraftFlights unchanged = expected;
        require(!deserializeRuntimeAircraftFlights(text, unchanged), reason + " accepted");
        require(serializeRuntimeAircraftFlights(unchanged) == encoded, reason + " mutated destination on failure");
    };
    const auto rejectField = [&](const Row& base, std::size_t index, const std::string& value) {
        auto changed = base; changed[index] = value;
        reject(document(changed), "field " + std::to_string(index) + " = " + value);
    };
    accept(ordinary, "import state rejected");
    accept(landing, "active landing rejected");
    auto edge = ordinary;
    edge[0] = std::to_string(std::numeric_limits<int>::max());
    edge[1] = edge[2] = std::to_string(xyLimit - 1);
    edge[3] = std::to_string(std::numeric_limits<int>::max());
    edge[4] = "1"; edge[5] = "65535"; edge[6] = std::to_string(zLimit);
    accept(edge, "inclusive field bounds rejected");
    edge = landing; edge[6] = edge[7] = std::to_string(zLimit); edge[8] = "0";
    edge[9] = std::to_string(std::numeric_limits<int>::max()); edge[10] = "100000";
    accept(edge, "landing upper bounds rejected");
    edge = landing; edge[6] = edge[8]; edge[9] = edge[10] = "-1";
    accept(edge, "cancelled landing at its lower height rejected");

    for (const auto index : {std::size_t{1}, std::size_t{2}}) {
        rejectField(ordinary, index, "-1");
        rejectField(ordinary, index, std::to_string(xyLimit));
        rejectField(ordinary, index, "9223372036854775808");
    }
    rejectField(ordinary, 0, "-1");
    rejectField(ordinary, 0, "2147483648");
    rejectField(ordinary, 3, "-2");
    rejectField(ordinary, 3, "2147483648");
    for (const auto index : {std::size_t{4}, std::size_t{11}})
        for (const auto* value : {"-1", "2"}) rejectField(ordinary, index, value);
    rejectField(ordinary, 5, "-2"); rejectField(ordinary, 5, "65536");
    for (const auto index : {std::size_t{6}, std::size_t{7}, std::size_t{8}}) {
        rejectField(landing, index, "-2");
        rejectField(landing, index, std::to_string(zLimit + 1));
        rejectField(landing, index, "9223372036854775808");
    }
    rejectField(ordinary, 9, "-2"); rejectField(ordinary, 9, "2147483648");
    rejectField(ordinary, 10, "-2"); rejectField(ordinary, 10, "100001");
    for (std::size_t index = 0; index < ordinary.size(); ++index) {
        rejectField(ordinary, index, "word");
        rejectField(ordinary, index, "1.5");
    }

    rejectField(ordinary, 7, "200"); // Half an envelope.
    rejectField(ordinary, 8, "0");
    rejectField(ordinary, 9, "42"); // Half a dock relationship.
    rejectField(ordinary, 10, "3");
    rejectField(ordinary, 11, "1"); // No landing envelope for the latch.
    rejectField(landing, 6, "-1");
    rejectField(landing, 6, "201");
    rejectField(landing, 6, "-1");
    rejectField(landing, 7, "0");
    rejectField(landing, 8, "200");
    rejectField(landing, 8, "101");
    rejectField(landing, 9, "-1");
    rejectField(landing, 10, "-1");
    edge = ordinary; edge[9] = "42"; edge[10] = "3";
    reject(document(edge), "dock relationship without height envelope");

    for (const auto* text : {"", "AircraftFlight 3 0", "AircraftFlights 0 0",
            "AircraftFlights 4 0", "AircraftFlights 3 -1", "AircraftFlights 3 100001",
            "AircraftFlights 3 1", "AircraftFlights 1 1\n5 1 2 -1 2",
            "AircraftFlights 2 1\n5 1 2 -1 0 65536"})
        reject(text, "invalid header, count or legacy record");
    reject("AircraftFlights 3 2\n" + record(ordinary) + record(ordinary), "duplicate aircraft id");
    auto second = ordinary; second[0] = "6"; second[11] = "2";
    reject("AircraftFlights 3 2\n" + record(ordinary) + record(second), "invalid second record after valid first");
    reject("AircraftFlights 3 1\n" + record(ordinary) + record(second), "record beyond declared count");
    reject(document(landing) + "junk", "trailing data");
    for (std::size_t n = 0; n < landing.size(); ++n) {
        std::string truncated = "AircraftFlights 3 1\n";
        for (std::size_t part = 0; part < n; ++part) truncated += landing[part] + ' ';
        reject(truncated, "truncated record at field " + std::to_string(n));
    }
    reject(std::string(16U * 1024U * 1024U + 1U, ' '), "oversized payload");
    std::cout << "Aircraft flight codec: legacy 1/2, schema 3 continuous landing, bounds, strict parse and atomic failure passed checks="
        << checks << '\n';
    return 0;
}
