#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>

inline thread_local int runtimePathDynamicResourceFusionReferenceOverride=-1;
inline bool runtimePathDynamicResourceFusionReference() {
    if(runtimePathDynamicResourceFusionReferenceOverride>=0)
        return runtimePathDynamicResourceFusionReferenceOverride!=0;
    static const bool reference=[] {
        const char* value=std::getenv("CONCORD_PATH_DYNAMIC_RESOURCE_FUSION_REFERENCE");
        return value && value[0]=='1' && value[1]=='\0';
    }();
    return reference;
}
class RuntimePathDynamicResourceFusionReferenceScope {
    int previous_;
public:
    explicit RuntimePathDynamicResourceFusionReferenceScope(bool reference) noexcept
        :previous_(runtimePathDynamicResourceFusionReferenceOverride) {
        runtimePathDynamicResourceFusionReferenceOverride=reference?1:0;
    }
    ~RuntimePathDynamicResourceFusionReferenceScope() {
        runtimePathDynamicResourceFusionReferenceOverride=previous_;
    }
    RuntimePathDynamicResourceFusionReferenceScope(const RuntimePathDynamicResourceFusionReferenceScope&)=delete;
    RuntimePathDynamicResourceFusionReferenceScope& operator=(const RuntimePathDynamicResourceFusionReferenceScope&)=delete;
};

// Explicit actual-builder opt-in. All old/default generic constructors retain
// std::false_type and the original candidate predicate, without reading this
// independent reference flag or collecting resource rows.
struct RuntimePathDynamicResourceFusionPolicy {
    static constexpr bool value=true;
    double percent=100.0;
};
struct RuntimePathDynamicResourceFusionStats {
    bool requested=false,admitted=false;
    std::size_t entries=0,examined=0,bodyCandidates=0,resourceRows=0;
    std::size_t resourceVisits=0,bodyVisits=0,retainedResourceBytes=0;
};
using RuntimePathDynamicResourceFusionObserver=void(*)(void*,const void*,
    const RuntimePathDynamicResourceFusionStats&);
inline thread_local RuntimePathDynamicResourceFusionObserver runtimePathDynamicResourceFusionObserver=nullptr;
inline thread_local void* runtimePathDynamicResourceFusionObserverContext=nullptr;
class RuntimePathDynamicResourceFusionObserverScope {
    RuntimePathDynamicResourceFusionObserver previous_;
    void* previousContext_;
public:
    RuntimePathDynamicResourceFusionObserverScope(RuntimePathDynamicResourceFusionObserver observer,void* context) noexcept
        :previous_(runtimePathDynamicResourceFusionObserver),previousContext_(runtimePathDynamicResourceFusionObserverContext) {
        runtimePathDynamicResourceFusionObserver=observer;
        runtimePathDynamicResourceFusionObserverContext=context;
    }
    ~RuntimePathDynamicResourceFusionObserverScope() {
        runtimePathDynamicResourceFusionObserver=previous_;
        runtimePathDynamicResourceFusionObserverContext=previousContext_;
    }
    RuntimePathDynamicResourceFusionObserverScope(const RuntimePathDynamicResourceFusionObserverScope&)=delete;
    RuntimePathDynamicResourceFusionObserverScope& operator=(const RuntimePathDynamicResourceFusionObserverScope&)=delete;
};
inline void runtimeRecordPathDynamicResourceFusion(const RuntimePathDynamicResourceFusionStats& value) {
    static const bool trace=std::getenv("CONCORD_RUNTIME_PERF_TRACE")!=nullptr;
    if(!trace)return;
    struct Totals {
        std::uint64_t calls=0,requested=0,admitted=0,entries=0,examined=0;
        std::uint64_t bodyCandidates=0,resourceRows=0,resourceVisits=0,bodyVisits=0;
        ~Totals() {
            std::cerr<<"Runtime path resource fusion diagnostic: calls="<<calls
                <<" requested="<<requested<<" admitted="<<admitted<<" entries="<<entries
                <<" examined="<<examined<<" bodyCandidates="<<bodyCandidates
                <<" resourceRows="<<resourceRows<<" resourceVisits="<<resourceVisits
                <<" bodyVisits="<<bodyVisits<<'\n';
        }
    };
    static thread_local Totals totals;
    ++totals.calls;totals.requested+=value.requested;totals.admitted+=value.admitted;
    totals.entries+=value.entries;totals.examined+=value.examined;
    totals.bodyCandidates+=value.bodyCandidates;totals.resourceRows+=value.resourceRows;
    totals.resourceVisits+=value.resourceVisits;totals.bodyVisits+=value.bodyVisits;
}
