#pragma once

#include "IniFile.h"
#include "RuntimeCampaignDataSource.h"
#include "RuntimeMapRenderer.h"
#include "RuntimePerfTrace.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

using RuntimeRadarRulesColorTable = std::unordered_map<std::string, COLORREF>;

struct RuntimeRadarRulesColorsMemoSummary {
    std::uint64_t queries = 0, hits = 0, readCalls = 0, parseCalls = 0, buildCalls = 0;
    std::size_t sourceBytes = 0;
    bool valid = false;
};
using RuntimeRadarRulesColorsMemoObserver = void(*)(void*, const RuntimeRadarRulesColorsMemoSummary&);
inline thread_local RuntimeRadarRulesColorsMemoObserver runtimeRadarRulesColorsMemoObserver = nullptr;
inline thread_local void* runtimeRadarRulesColorsMemoObserverContext = nullptr;
class RuntimeRadarRulesColorsMemoObserverScope {
public:
    RuntimeRadarRulesColorsMemoObserverScope(RuntimeRadarRulesColorsMemoObserver observer, void* context)
        : previous_(runtimeRadarRulesColorsMemoObserver), previousContext_(runtimeRadarRulesColorsMemoObserverContext) {
        runtimeRadarRulesColorsMemoObserver = observer;
        runtimeRadarRulesColorsMemoObserverContext = context;
    }
    ~RuntimeRadarRulesColorsMemoObserverScope() {
        runtimeRadarRulesColorsMemoObserver = previous_;
        runtimeRadarRulesColorsMemoObserverContext = previousContext_;
    }
    RuntimeRadarRulesColorsMemoObserverScope(const RuntimeRadarRulesColorsMemoObserverScope&) = delete;
    RuntimeRadarRulesColorsMemoObserverScope& operator=(const RuntimeRadarRulesColorsMemoObserverScope&) = delete;
private:
    RuntimeRadarRulesColorsMemoObserver previous_;
    void* previousContext_;
};

inline thread_local int runtimeRadarRulesColorReferenceOverride = -1;
inline bool runtimeRadarRulesColorReference() {
    if (runtimeRadarRulesColorReferenceOverride >= 0)
        return runtimeRadarRulesColorReferenceOverride != 0;
    static const bool reference = [] {
        const char* value = std::getenv("CONCORD_RADAR_RULES_COLOR_REFERENCE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return reference;
}

class RuntimeRadarRulesColorReferenceScope {
public:
    explicit RuntimeRadarRulesColorReferenceScope(bool reference)
        : previous_(runtimeRadarRulesColorReferenceOverride) {
        runtimeRadarRulesColorReferenceOverride = reference ? 1 : 0;
    }
    ~RuntimeRadarRulesColorReferenceScope() {
        runtimeRadarRulesColorReferenceOverride = previous_;
    }
    RuntimeRadarRulesColorReferenceScope(const RuntimeRadarRulesColorReferenceScope&) = delete;
    RuntimeRadarRulesColorReferenceScope& operator=(const RuntimeRadarRulesColorReferenceScope&) = delete;
private:
    int previous_;
};

// A single owned result, independent of object/history/terrain counts. Resolve
// the original source again BEFORE each load: new loose overrides, edition
// changes and archive entry changes must still follow the original priority.
// Read the original source on EVERY load, including hits: size/mtime alone
// cannot detect an in-place same-size hot edit or a new read failure.
// Only the full source descriptor and exact bytes bypass the original parser
// and the caller's unchanged defaults + [Colors] conversion. No filesystem
// metadata, archive, borrowed IniFile, or failed read is retained.
// Use the same pure builder on every load; it must depend only on its IniFile.
class RuntimeRadarRulesColorsMemo {
public:
    static constexpr std::size_t maximumSourceBytes = 16U * 1024U * 1024U;

    template<class Builder>
    const RuntimeRadarRulesColorTable& load(const RuntimeIniSource& source,
        Builder&& builder, int tick = 0, std::string* errorMessage = nullptr) {
        return loadWithReader(source, std::forward<Builder>(builder),
            [](const RuntimeIniSource& current, std::string& text, std::string* error) {
                return current.readText(text, error);
            }, tick, errorMessage);
    }

    // Injectable IO for contracts. Production load above always uses the
    // original RuntimeIniSource::readText and does not take a test-only branch.
    template<class Builder, class Reader>
    const RuntimeRadarRulesColorTable& loadWithReader(const RuntimeIniSource& source,
        Builder&& builder, Reader&& reader, int tick = 0,
        std::string* errorMessage = nullptr) {
        struct Observe {
            RuntimeRadarRulesColorsMemo& memo;
            RuntimeRadarRulesColorsMemoSummary before;
            ~Observe() {
                if (!runtimeRadarRulesColorsMemoObserver) return;
                const RuntimeRadarRulesColorsMemoSummary result{
                    memo.queries() - before.queries, memo.hits() - before.hits,
                    memo.readCalls() - before.readCalls, memo.parseCalls() - before.parseCalls,
                    memo.buildCalls() - before.buildCalls, memo.sourceBytes(), memo.valid()};
                runtimeRadarRulesColorsMemoObserver(runtimeRadarRulesColorsMemoObserverContext, result);
            }
        } observe{*this, {queries_, hits_, readCalls_, parseCalls_, buildCalls_, 0, false}};
        ++queries_;
        std::string text, error;
        bool read = false;
        if (source.available()) {
            RuntimePerfTraceScope readTrace("radar.rules.readText", tick);
            ++readCalls_;
            read = reader(source, text, &error);
        } else {
            error = "runtime INI source is unavailable";
        }
        if (!read) {
            // A failure must return current defaults, never a former good
            // table. Invalidate so a later recovery reparses its current data.
            invalidate();
            colors_ = builder(nullptr);
            if (errorMessage) *errorMessage = std::move(error);
            return colors_;
        }
        if (valid_ && sameSource(source_, source) && text_ == text) {
            ++hits_;
            if (errorMessage) errorMessage->clear();
            return colors_;
        }

        IniFile parsed;
        bool parsedOk = false;
        {
            RuntimePerfTraceScope parseTrace("radar.rules.parse", tick);
            ++parseCalls_;
            parsedOk = parsed.loadFromString(text);
        }
        if (!parsedOk) {
            invalidate();
            colors_ = builder(nullptr);
            if (errorMessage) *errorMessage = "runtime INI could not be parsed: " + source.displayPath;
            return colors_;
        }
        colors_ = builder(&parsed);
        ++buildCalls_;
        // Production readText caps at this bound. An oversized injected input
        // can still be evaluated exactly, but is never kept as a cache entry.
        if (text.size() <= maximumSourceBytes) {
            source_ = source;
            text_ = std::move(text);
            valid_ = true;
        } else {
            clearIdentity();
        }
        if (errorMessage) errorMessage->clear();
        return colors_;
    }

    void invalidate() {
        clearIdentity();
        colors_.clear();
    }
    bool valid() const { return valid_; }
    std::size_t sourceBytes() const { return valid_ ? text_.size() : 0; }
    std::uint64_t queries() const { return queries_; }
    std::uint64_t hits() const { return hits_; }
    std::uint64_t readCalls() const { return readCalls_; }
    std::uint64_t parseCalls() const { return parseCalls_; }
    std::uint64_t buildCalls() const { return buildCalls_; }

private:
    static bool sameSource(const RuntimeIniSource& a, const RuntimeIniSource& b) {
        return a.sourceId == b.sourceId && a.displayPath == b.displayPath
            && a.backingPath == b.backingPath && a.entryName == b.entryName
            && a.mixEntry == b.mixEntry;
    }
    void clearIdentity() {
        valid_ = false;
        source_ = {};
        // Release a previous large source instead of retaining its capacity
        // indefinitely after a load failure or a smaller replacement.
        std::string{}.swap(text_);
    }
    RuntimeIniSource source_;
    std::string text_;
    RuntimeRadarRulesColorTable colors_;
    bool valid_ = false;
    std::uint64_t queries_ = 0, hits_ = 0, readCalls_ = 0, parseCalls_ = 0, buildCalls_ = 0;
};
