// Include in RuntimeShell after drawRuntimeRadarTerrainCache,
// buildRuntimeRemapColorTable and seedRuntimeLoadScratch. The unchanged
// loadRuntimeRemapColorTable is the production reference, including source IO.
#include "RuntimeRadarRulesColorsMemo.h"
#include "RuntimeRadarCellColors.h"
#include "MixArchive.h"

#include <fstream>

static bool runRuntimeRadarCellColorsContract(std::string& error) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar cell colors: ") + detail; return false;
    };
    RuntimeRadarCellColors actual(0, false), old(0, true);
    // Compare against independent original containers, not just our reference
    // branch. Include negative/alias/oversized keys and arbitrary COLORREF bits.
    std::unordered_map<int, COLORREF> colors;
    std::unordered_set<int> resources;
    const auto write = [&](int key, COLORREF color, bool resource) {
        colors[key] = color;
        actual.setColor(key, color); old.setColor(key, color);
        if (resource) {
            resources.insert(key); actual.markResource(key); old.markResource(key);
        }
    };
    for (int y = 0; y < 512; y += 17) for (int x = 0; x < 512; x += 13)
        write(runtimeCellKey(x, y), static_cast<COLORREF>((x << 16) ^ (y * 251)), (x + y) % 3 == 0);
    const std::array<int, 10> keys{{-1, -1000, 0, 511, 512, 999, 1000,
        511511, 512000, std::numeric_limits<int>::max()}};
    for (int key : keys) {
        write(key, static_cast<COLORREF>(0xffffffffUL), true);
        write(key, RGB(1, 2, 3), false); // Must retain prior resource membership.
        write(key, RGB(91, 71, 13), true);
    }
    // A noncanonical x=1000 aliases (x=0,y=1); all negative coordinates alias
    // -1. Storage must preserve the old KEY, rather than normalize raw x/y.
    if (runtimeCellKey(1000, 0) != runtimeCellKey(0, 1)
        || runtimeCellKey(-1, 5) != runtimeCellKey(7, -1))
        return fail("legacy alias fixture did not exercise original key identity");
    write(runtimeCellKey(1000, 0), RGB(251, 2, 3), true);
    write(runtimeCellKey(0, 1), RGB(1, 252, 3), false);
    write(runtimeCellKey(-1, 5), RGB(1, 2, 253), true);
    write(runtimeCellKey(7, -1), RGB(17, 19, 23), false);
    const auto compare = [&](int key) {
        COLORREF a = 0x6e512913UL, b = a;
        const auto found = colors.find(key);
        const bool expected = found != colors.end();
        if (actual.findColor(key, a) != expected || old.findColor(key, b) != expected
            || a != b || (expected && a != found->second)
            || (!expected && a != 0x6e512913UL)) return false;
        const bool resource = resources.find(key) != resources.end();
        return actual.isResource(key) == resource && old.isResource(key) == resource;
    };
    for (int y = 0; y < 514; ++y) for (int x = 0; x < 514; ++x)
        if (!compare(runtimeCellKey(x, y))) return fail("dense boundary color/resource/output preservation");
    for (int key : keys) if (!compare(key)) return fail("fallback key differs from original containers");
    // A resource mark can precede its color or have no color at all.
    actual.markResource(234567); old.markResource(234567); resources.insert(234567);
    if (!compare(234567) || !compare(511999)) return fail("resource-only entry or untouched fallback");
    if (actual.denseBytes() != 512U * 512U * (sizeof(COLORREF) + sizeof(unsigned char))
        || old.denseBytes() != 0 || actual.fallbackColorCount() != 6
        || actual.fallbackResourceCount() != 7)
        return fail("dense capacity/fallback contract");
    const bool previous = runtimeRadarCellColorsReference();
    { RuntimeRadarCellColorsReferenceScope outer(true);
      if (!runtimeRadarCellColorsReference()) return fail("reference enable");
      { RuntimeRadarCellColorsReferenceScope inner(false);
        if (runtimeRadarCellColorsReference()) return fail("nested reference disable"); }
      if (!runtimeRadarCellColorsReference()) return fail("nested reference restore"); }
    if (runtimeRadarCellColorsReference() != previous) return fail("outer reference restore");
    return true;
}

static bool runRuntimeRadarRulesColorsMemoContract(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const char* detail) {
        error = std::string("radar rules colors memo: ") + detail; return false;
    };
    if (!runRuntimeRadarCellColorsContract(error)) return false;

    // Exercise exact raw bytes and all source fields separately. The injected
    // reader always executes, even on a hit, and mirrors readText error output.
    RuntimeIniSource identity;
    identity.sourceId = "yr"; identity.displayPath = "fixture!rulesmd.ini";
    identity.backingPath = "fixture"; identity.entryName = "rulesmd.ini"; identity.mixEntry = true;
    std::string raw = "[Colors]\n Gold = 20,255,255 ;first\nGOLD=80,255,200\n"
        "NewShade=128,200,100\nInvalid=-1,5,5\n[colors]\nOrange=3,7,11\n[Other]\nGold=1,2,3\n";
    bool readSuccess = true;
    std::uint64_t reads = 0;
    const auto reader = [&](const RuntimeIniSource&, std::string& text, std::string* outError) {
        ++reads; text = raw;
        if (outError) *outError = readSuccess ? "" : "simulated source read failure";
        return readSuccess;
    };
    const auto expectedRaw = [&] {
        IniFile ini; ini.loadFromString(raw); return buildRuntimeRemapColorTable(&ini);
    };
    RuntimeRadarRulesColorsMemo memo;
    std::string diagnostic = "dirty";
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader, 0, &diagnostic) != expectedRaw()
        || !diagnostic.empty() || !memo.valid() || memo.hits() != 0 || memo.parseCalls() != 1)
        return fail("initial original INI/default/duplicate/case/comment semantics");
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader, 0, &diagnostic) != expectedRaw()
        || !diagnostic.empty() || reads != 2 || memo.hits() != 1 || memo.parseCalls() != 1)
        return fail("hit did not read exact source or did not avoid parsing");
    // Same-size edits must invalidate without trusting timestamps or hashes.
    raw[raw.find("80,255,200")] = '9';
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader) != expectedRaw()
        || memo.hits() != 1 || memo.parseCalls() != 2)
        return fail("same-size in-place source edit remained stale");
    // An edit outside [Colors] still reparses: complete bytes are the identity.
    raw += "OtherSetting=yes\n";
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader) != expectedRaw()
        || memo.parseCalls() != 3) return fail("noncolor source edit not detected");
    for (int field = 0; field < 5; ++field) {
        auto changed = identity;
        switch (field) {
        case 0: changed.sourceId = "ra2"; break;
        case 1: changed.displayPath += "changed"; break;
        case 2: changed.backingPath += "changed"; break;
        case 3: changed.entryName = "rules.ini"; break;
        case 4: changed.mixEntry = false; break;
        }
        const auto parses = memo.parseCalls();
        if (memo.loadWithReader(changed, buildRuntimeRemapColorTable, reader) != expectedRaw()
            || memo.parseCalls() != parses + 1) return fail("complete resolved source descriptor not keyed");
    }
    readSuccess = false;
    const auto defaults = buildRuntimeRemapColorTable(nullptr);
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader, 0, &diagnostic) != defaults
        || memo.valid() || memo.sourceBytes() != 0 || diagnostic != "simulated source read failure")
        return fail("failed source retained successful colors or error");
    readSuccess = true;
    const auto parsesBeforeRecovery = memo.parseCalls();
    if (memo.loadWithReader(identity, buildRuntimeRemapColorTable, reader, 0, &diagnostic) != expectedRaw()
        || !diagnostic.empty() || memo.parseCalls() != parsesBeforeRecovery + 1)
        return fail("source recovery did not reparse");
    const auto readsBeforeAbsent = reads;
    if (memo.loadWithReader(RuntimeIniSource{}, buildRuntimeRemapColorTable, reader, 0, &diagnostic) != defaults
        || reads != readsBeforeAbsent || memo.valid() || diagnostic != "runtime INI source is unavailable")
        return fail("unavailable source attempted IO or returned old colors");
    memo.invalidate();
    if (memo.valid() || memo.sourceBytes() != 0) return fail("explicit invalidate retained identity");
    const bool previousReference = runtimeRadarRulesColorReference();
    { RuntimeRadarRulesColorReferenceScope outer(true);
      if (!runtimeRadarRulesColorReference()) return fail("reference enable");
      { RuntimeRadarRulesColorReferenceScope inner(false);
        if (runtimeRadarRulesColorReference()) return fail("nested reference disable"); }
      if (!runtimeRadarRulesColorReference()) return fail("nested reference restore"); }
    if (runtimeRadarRulesColorReference() != previousReference) return fail("reference restore");

    // Actual loose and MIX readers/resolver, against unchanged production
    // loadRuntimeRemapColorTable. Keep isolated fixture files as test evidence;
    // never overwrite the player's rules/config or recursively delete files.
    namespace radarFs = std::filesystem;
    std::error_code ec;
    const radarFs::path root = radarFs::temp_directory_path(ec)
        / ("concord-radar-rules-" + std::to_string(GetCurrentProcessId())
            + "-" + std::to_string(GetTickCount64()));
    if (ec || !radarFs::create_directory(root, ec) || ec) return fail("isolated fixture directory");
    const auto write = [](const radarFs::path& path, const std::string& text) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(file);
    };
    const auto writeMix = [&](const radarFs::path& path, const std::string& name, const std::string& text) {
        std::string bytes;
        const auto u16 = [&](std::uint16_t value) {
            for (int i = 0; i < 2; ++i) bytes.push_back(static_cast<char>(value >> (i * 8)));
        };
        const auto u32 = [&](std::uint32_t value) {
            for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<char>(value >> (i * 8)));
        };
        u16(1); u32(static_cast<std::uint32_t>(text.size()));
        u32(mixFileIdFromName(name)); u32(0); u32(static_cast<std::uint32_t>(text.size()));
        bytes += text; return write(path, bytes);
    };
    RuntimeConfig config = live.config;
    config.resourceRootPath.clear(); config.localDataRootPath = root.string();
    config.modRootPath.clear(); config.campaignSourceId = "yr";
    RuntimeRadarRulesColorsMemo realMemo;
    int productionComparisons = 0;
    const auto compare = [&] {
        const auto old = loadRuntimeRemapColorTable(config);
        const auto source = resolveRuntimeIniSource(config, RuntimeCampaignIniKind::Rules);
        const auto current = realMemo.load(source, buildRuntimeRemapColorTable);
        ++productionComparisons;
        return old == current;
    };
    const std::string first = "[Colors]\nGold=25,255,255\nOrange=90,180,220\n[General]\nDummy=one\n";
    const std::string second = "[Colors]\nGold=95,255,255\nOrange=30,180,220\n[General]\nDummy=two\n";
    if (!compare() || !write(root / "rulesmd.ini", first) || !compare() || !compare()
        || realMemo.hits() != 1) return fail("missing/loose/hit production equivalence");
    const auto preservedWriteTime = radarFs::last_write_time(root / "rulesmd.ini", ec);
    if (ec || first.size() != second.size() || !write(root / "rulesmd.ini", second))
        return fail("same-size hot edit fixture");
    radarFs::last_write_time(root / "rulesmd.ini", preservedWriteTime, ec);
    if (ec || !compare() || realMemo.hits() != 1) return fail("same-size same-mtime hot edit production equivalence");
    if (!write(root / "rulesmd.ini", "") || !compare() || realMemo.valid()
        || !write(root / "rulesmd.ini", first) || !compare()) return fail("empty/unreadable-size source recovery");
    // Move one explicit fixture file to expose lower MIX priority, then let a
    // new loose file override that same archive while the memo is warm.
    radarFs::rename(root / "rulesmd.ini", root / "held-rulesmd.ini", ec);
    if (ec || !writeMix(root / "localmd.mix", "rulesmd.ini", first + std::string(3, '\0'))
        || !compare() || !compare()) return fail("MIX resolution/read/trailing-zero/hit equivalence");
    if (!writeMix(root / "localmd.mix", "rulesmd.ini", second) || !compare()
        || !write(root / "rulesmd.ini", first) || !compare()) return fail("changed MIX/new loose override equivalence");
    if (!write(root / "rules.ini", "[Colors]\nGold=150,210,220\n")) return fail("base-edition fixture");
    config.campaignSourceId = "ra2";
    if (!compare()) return fail("edition switch retained expansion colors");
    config.campaignSourceId.clear();
    if (!compare()) return fail("historical edition priority retained base colors");
    const radarFs::path mod = root / "mod";
    if (!radarFs::create_directory(mod, ec) || ec || !write(mod / "rulesmd.ini", second))
        return fail("mod override fixture");
    config.modRootPath = mod.string();
    if (!compare()) return fail("new higher-priority mod retained lower colors");
    config.modRootPath.clear();
    if (!compare()) return fail("removed mod override retained its colors");
    // Explicit source read errors are also exact, including a formerly good
    // source descriptor whose backing file has moved away.
    RuntimeIniSource lost = resolveRuntimeIniSource(config, RuntimeCampaignIniKind::Rules);
    radarFs::rename(root / "rulesmd.ini", root / "removed-rulesmd.ini", ec);
    if (ec) return fail("single-file missing-source fixture");
    std::string sourceError, memoError;
    IniFile originalIni;
    if (lost.load(originalIni, &sourceError)
        || realMemo.load(lost, buildRuntimeRemapColorTable, 0, &memoError) != defaults
        || sourceError != memoError || realMemo.valid()) return fail("original missing-file diagnostic/default equivalence");
    if (!compare()) return fail("removed loose correctly exposes MIX source");

    // Actual full radar DIB, both independent reference switches and all four
    // branch combinations. This also proves same-source cached parsing cannot
    // change rule colors, fog/resource masks, fallback TMP errors or counters.
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    seedRuntimeLoadScratch(probe, live, live.visionEnabled);
    probe.economy = live.economy; probe.vision = live.vision; probe.visionEnabled = live.visionEnabled;
    probe.resourceStateRevision = live.resourceStateRevision;
    if (probe.world.getTerrainCells().empty()) return fail("actual radar world is empty");
    // Keep actual palette/TMP assets while the isolated highest-priority mod
    // supplies only this fixture's Rules INI. No player's file is overwritten.
    if (!write(root / "rulesmd.ini", second)) return fail("actual radar mod-rule fixture");
    probe.config.modRootPath = root.string();
    probe.config.campaignSourceId = "yr";
    IniFile resourceRules;
    if (!resourceRules.loadFromString("[Tiberiums]\n0=RadarRulesOre\n1=RadarRulesGems\n"
        "[RadarRulesOre]\nImage=1\nColor=Gold\nValue=25\n"
        "[RadarRulesGems]\nImage=2\nColor=Orange\nValue=50\n")
        || !probe.rules.loadFromIni(resourceRules)) return fail("actual resource color rules fixture");
    const auto* oreRule = probe.rules.findTiberiumType("RadarRulesOre");
    const auto* gemRule = probe.rules.findTiberiumType("RadarRulesGems");
    if (!oreRule || !gemRule || oreRule->image != 1 || gemRule->image != 2
        || oreRule->color != "Gold" || gemRule->color != "Orange")
        return fail("actual ore/gem color rules readback");
    // Pick cells that the actual pixel loop samples, including two separated
    // resource cells and a third cell for shroud. This prevents a zero-pixel
    // fixture from making a stale rules-color cache look correct.
    const auto productionProjection = buildRuntimeRadarProjection(probe);
    const auto productionLayout = runtimeRadarLayout(RECT{0, 0, 221, 147}, productionProjection);
    std::array<std::size_t, 3> sampledIndices{};
    std::size_t sampledCount = 0;
    for (int y = productionLayout.rect.top; y < productionLayout.rect.bottom && sampledCount < 3; ++y) {
        for (int x = productionLayout.rect.left; x < productionLayout.rect.right && sampledCount < 3; ++x) {
            if (productionLayout.scaleX <= 0 || productionLayout.scaleY <= 0) continue;
            const RuntimeRadarDxyPoint dxy{
                productionProjection.minX + (static_cast<double>(x - productionLayout.rect.left) + 0.5)
                    / productionLayout.scaleX,
                productionProjection.minY + (static_cast<double>(y - productionLayout.rect.top) + 0.5)
                    / productionLayout.scaleY};
            if (!runtimeRadarProjectionContains(productionProjection, dxy)) continue;
            const int tx = static_cast<int>(std::floor((dxy.x + dxy.y + 2.0) / 2.0));
            const int ty = static_cast<int>(std::floor((dxy.y - dxy.x
                + 2.0 * productionProjection.fullMapWidth) / 2.0));
            if (tx < 0 || ty < 0 || tx >= 512 || ty >= 512) continue;
            const auto found = probe.terrainByCell.find(runtimeCellKey(tx, ty));
            if (found == probe.terrainByCell.end()) continue;
            const auto index = static_cast<std::size_t>(found->second - probe.world.getTerrainCells().data());
            bool separated = true;
            for (std::size_t i = 0; i < sampledCount; ++i) {
                const auto& prior = probe.world.getTerrainCells()[sampledIndices[i]];
                if (std::max(std::abs(prior.x - tx), std::abs(prior.y - ty)) < 8) separated = false;
            }
            if (separated) sampledIndices[sampledCount++] = index;
        }
    }
    if (sampledCount != 3) return fail("actual radar did not sample three separated physical cells");
    setRuntimeResourceTerrainBails(probe.world.accessTerrainCells()[sampledIndices[0]], "GOLD", 8);
    setRuntimeResourceTerrainBails(probe.world.accessTerrainCells()[sampledIndices[1]], "GEM", 8);
    setRuntimeResourceTerrainBails(probe.world.accessTerrainCells()[sampledIndices[2]], "GOLD", 0);
    ++probe.resourceStateRevision;
    probe.visionEnabled = false;
    HDC screen = GetDC(nullptr);
    RuntimeBackBuffer destination;
    struct Release {
        RuntimeWindowState& state; RuntimeBackBuffer& buffer; HDC screen;
        ~Release() {
            releaseRuntimeBackBuffer(buffer);
            releaseRuntimeBackBuffer(state.radarTerrainBuffer);
            if (screen) ReleaseDC(nullptr, screen);
        }
    } release{probe, destination, screen};
    if (!screen || !ensureRuntimeBackBuffer(destination, screen, 256, 192)) return fail("actual radar destination");
    struct Snapshot {
        std::vector<std::uint32_t> pixels;
        std::array<int, 6> counts;
        std::uint64_t hash;
        std::string key, tmpError;
        RuntimeRadarRulesColorsMemoSummary memo;
        std::size_t memoCalls = 0, cellStores = 0, denseBytes = 0;
        bool cellsReference = false;
    };
    const auto draw = [&](bool oldRules, bool oldCells, Snapshot& result) {
        RuntimeRadarRulesColorReferenceScope rulesMode(oldRules);
        RuntimeRadarCellColorsReferenceScope cellsMode(oldCells);
        RuntimeRadarRulesColorsMemoObserverScope memoObserved(
            [](void* out, const RuntimeRadarRulesColorsMemoSummary& summary) {
                auto& snapshot = *static_cast<Snapshot*>(out);
                snapshot.memo = summary; ++snapshot.memoCalls;
            }, &result);
        RuntimeRadarCellColorsObserverScope cellsObserved(
            [](void* out, bool reference, std::size_t bytes) {
                auto& snapshot = *static_cast<Snapshot*>(out);
                ++snapshot.cellStores; snapshot.cellsReference = reference; snapshot.denseBytes = bytes;
            }, &result);
        probe.radarTerrainKey.clear();
        auto* pixels = static_cast<std::uint32_t*>(destination.pixels);
        std::fill(pixels, pixels + 256 * 192, 0x005f7311U);
        const RECT rect{3, 5, 224, 152};
        if (!drawRuntimeRadarTerrainCache(destination.memoryDc, probe, rect, buildRuntimeRadarProjection(probe)))
            return false;
        GdiFlush(); result.pixels.assign(pixels, pixels + 256 * 192);
        result.counts = {probe.lastRadarResourceCells, probe.lastRadarResourceOreCells,
            probe.lastRadarResourceGemCells, probe.lastRadarResourcePixels,
            probe.lastRadarResourceRuleColors, probe.lastRadarResourceFallbackColors};
        result.hash = probe.lastRadarResourceHash; result.key = probe.radarTerrainKey;
        result.tmpError = probe.renderCache.lastTmpTileLoadError;
        return true;
    };
    const auto same = [](const Snapshot& a, const Snapshot& b) {
        return a.pixels == b.pixels && a.counts == b.counts && a.hash == b.hash
            && a.key == b.key && a.tmpError == b.tmpError;
    };
    Snapshot original, optimized, mixedA, mixedB, hotEdited;
    if (!draw(true, true, original) || !draw(false, false, optimized)
        || !draw(true, false, mixedA) || !draw(false, true, mixedB)
        || !same(original, optimized) || !same(original, mixedA) || !same(original, mixedB))
        return fail("actual full DIB/reference branch/resource/hash/TMP diagnostic mismatch");
    if (original.memoCalls != 0 || mixedA.memoCalls != 0 || optimized.memoCalls != 1
        || mixedB.memoCalls != 1 || optimized.memo.queries != 1 || optimized.memo.readCalls != 1
        || optimized.memo.parseCalls != 1 || optimized.memo.hits != 0 || mixedB.memo.hits != 1
        || mixedB.memo.readCalls != 1 || mixedB.memo.parseCalls != 0
        || original.cellStores != 1 || optimized.cellStores != 1 || mixedA.cellStores != 1 || mixedB.cellStores != 1
        || !original.cellsReference || optimized.cellsReference || mixedA.cellsReference || !mixedB.cellsReference
        || original.denseBytes != 0 || mixedB.denseBytes != 0 || optimized.denseBytes == 0 || mixedA.denseBytes == 0)
        return fail("actual original/memo/dense branches or warm raw-source parse bypass not exercised");
    if (original.counts[0] == 0 || original.counts[1] == 0 || original.counts[2] == 0
        || original.counts[3] == 0 || original.counts[4] == 0)
        return fail("actual ore/gem/rule-colored resource cells and sampled pixels are not nonzero");
    if (!write(root / "rulesmd.ini", first)) return fail("radar hot-edit source");
    Snapshot editedReference;
    if (!draw(true, true, editedReference) || !draw(false, false, hotEdited)
        || !same(editedReference, hotEdited) || editedReference.memoCalls != 0
        || hotEdited.memoCalls != 1 || hotEdited.memo.hits != 0 || hotEdited.memo.parseCalls != 1
        || hotEdited.pixels == original.pixels)
        return fail("hot-edited actual radar DIB stale or source parse not exercised");
    probe.config.playerSlots.clear();
    probe.config.playerCountry = "Americans";
    probe.economy.owner = "RadarRulesColorObserver";
    probe.vision.reset();
    probe.vision.update(probe.world, &probe.rules,
        std::vector<RuntimeVisionObserverDefinition>{{probe.economy.owner, {}}}, probe.tick);
    if (!probe.vision.revealMap(probe.economy.owner)) return fail("actual radar resource fog reveal");
    // revealMap marks cells Visible; a real subsequent vision update with no
    // sources owned by this observer turns the explored cells into Fogged.
    probe.vision.update(probe.world, &probe.rules,
        std::vector<RuntimeVisionObserverDefinition>{{probe.economy.owner, {}}}, probe.tick + 1);
    const auto oreCell = probe.world.getTerrainCells()[sampledIndices[0]];
    const auto gemCell = probe.world.getTerrainCells()[sampledIndices[1]];
    const auto hiddenCell = probe.world.getTerrainCells()[sampledIndices[2]];
    if (!probe.vision.resetShroudArea(probe.economy.owner, hiddenCell.x, hiddenCell.y, 1))
        return fail("actual radar sampled shroud did not change mask");
    probe.visionEnabled = true;
    if (!runtimeLocalVisionRestricted(probe)
        || probe.vision.visibilityFor(probe.economy.owner, oreCell.x, oreCell.y) != RuntimeCellVisibility::Fogged
        || probe.vision.visibilityFor(probe.economy.owner, gemCell.x, gemCell.y) != RuntimeCellVisibility::Fogged
        || probe.vision.visibilityFor(probe.economy.owner, hiddenCell.x, hiddenCell.y) != RuntimeCellVisibility::Shrouded)
        return fail("actual fogged ore/gems plus sampled shrouded terrain fixture");
    Snapshot fogReference, fogOptimized;
    if (!draw(true, true, fogReference) || !draw(false, false, fogOptimized)
        || !same(fogReference, fogOptimized) || fogOptimized.pixels == hotEdited.pixels
        || fogOptimized.counts[1] == 0 || fogOptimized.counts[2] == 0 || fogOptimized.counts[3] == 0
        || fogReference.memoCalls != 0 || fogOptimized.memo.hits != 1 || fogOptimized.memo.parseCalls != 0)
        return fail("actual fog/resource pixels/counters/reference or unchanged-byte hit mismatch");
    std::cout << "Radar rules/cell colors contract: exactByteHotEdits=pass completeSourceIdentity=pass"
        << " originalProductionLoadComparisons=" << productionComparisons
        << " looseMIXOverrideEditionMissingFailureRecovery=exact"
        << " fullDIBBranchCombinations=4 hotEditedDIB=exact legacyAliasesResourceOR=exact"
        << " nonzeroOreGemRuleColoredPixels=verified foggedResourcesAndSampledShroud=exact"
        << " sourceMemoMaxBytes=" << RuntimeRadarRulesColorsMemo::maximumSourceBytes << '\n';
    return true;
}
