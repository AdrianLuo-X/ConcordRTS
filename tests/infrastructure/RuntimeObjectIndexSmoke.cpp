#include "world/queries/RuntimeObjectIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Count object reads independently of timings: empty-target work must stay
// constant even after a long match has accumulated many historical objects.
struct CountedObjects {
    std::vector<WorldObjectInstance> values;
    std::size_t reads = 0;

    WorldObjectInstance* data() { return values.data(); }
    std::size_t size() const { return values.size(); }
    WorldObjectInstance& operator[](std::size_t offset) {
        ++reads;
        return values[offset];
    }
};

bool verifyEmptyTargetCost() {
    RuntimeObjectIndex index;
    CountedObjects objects;
    objects.values.resize(32768);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        objects.values[i].instanceId = static_cast<int>(i + 1);
    }
    const auto start = std::chrono::steady_clock::now();
    for (int query = 0; query < 4096; ++query) {
        if (index.find(objects, -1 - query % 3) != nullptr) return false;
    }
    std::cout << "Empty target queries=4096 historical_objects=" << objects.size()
              << " object_reads=" << objects.reads << " ms="
              << std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now() - start).count() << '\n';
    return objects.reads == 0;
}

} // namespace

int main() {
    const bool emptyTargetCostValid = verifyEmptyTargetCost();
    RuntimeObjectIndex index;
    std::vector<WorldObjectInstance> objects(4096);
    for (std::size_t i = 0; i < objects.size(); ++i) {
        objects[i].instanceId = 10000 + static_cast<int>(i * 3);
        objects[i].alive = true;
    }
    const auto verify = [&]() {
        const auto& readOnly = objects;
        for (const auto& object : readOnly) {
            if (index.find(readOnly, object.instanceId) != &object) return false;
        }
        return true;
    };
    if (!verify()) return 1;
    std::reverse(objects.begin(), objects.end());
    if (!verify()) return 2;
    const int removedId = objects[17].instanceId;
    objects[17].instanceId = 90000;
    if (index.find(objects, removedId) != nullptr
        || index.find(objects, 90000) != &objects[17]) return 3;
    objects[17].alive = false;
    if (index.find(objects, 90000)->alive) return 4;
    // Same-size transaction, append/reallocation, compaction and cache copy.
    auto replacement = objects;
    replacement[21].instanceId = removedId;
    objects.swap(replacement);
    if (!verify() || index.find(objects, removedId) != &objects[21]) return 5;
    objects.push_back(WorldObjectInstance{});
    objects.back().instanceId = 123456;
    objects.erase(objects.begin() + 5);
    if (!verify()) return 6;
    RuntimeObjectIndex copiedIndex = index;
    std::rotate(objects.begin(), objects.begin() + 1300, objects.end());
    for (auto& object : objects) {
        if (copiedIndex.find(objects, object.instanceId) != &object) return 7;
    }

    const auto measure = [&](bool indexed) {
        std::uint64_t checksum = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < 32; ++pass) {
            for (std::size_t i = 0; i < objects.size(); ++i) {
                const int id = objects[(i * 73) % objects.size()].instanceId;
                const WorldObjectInstance* found = nullptr;
                if (indexed) {
                    found = index.find(objects, id);
                } else {
                    for (const auto& candidate : objects) {
                        if (candidate.instanceId == id) {
                            found = &candidate;
                            break;
                        }
                    }
                }
                if (found) checksum += static_cast<std::uint64_t>(found->instanceId);
            }
        }
        return std::pair{std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count(), checksum};
    };
    const auto baseline = measure(false);
    const auto optimized = measure(true);
    if (baseline.second != optimized.second) return 8;
    std::cout << "Runtime object index: storage replacement/reorder/growth/death OK; "
              << "objects=" << objects.size() << " queries=" << objects.size() * 32
              << " linear_ms=" << baseline.first << " indexed_ms=" << optimized.first
              << " checksum=" << optimized.second << '\n';
    objects.clear();
    if (index.find(objects, 90000) != nullptr) return 9;
    return emptyTargetCostValid ? 0 : 10;
}
