#pragma once

#include <array>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

inline const char* runtimeRendererRawEnvironmentValue(const char* name) {
    return std::getenv(name);
}

// One draw owns both names and values. Outside the scope (and during atlas
// diagnostics, which temporarily change settings), reads use getenv directly.
class RuntimeRendererEnvironmentScope {
public:
    using Lookup = const char* (*)(const char*);

    explicit RuntimeRendererEnvironmentScope(
        Lookup lookup = runtimeRendererRawEnvironmentValue
    ) : previous_(active_), lookup_(lookup) {
        static constexpr std::array<const char*, 5> diagnosticSettings{
            "CONCORD_VXL_DUMP_ATLAS",
            "CONCORD_VXL_DUMP_SWEEPS",
            "CONCORD_VXL_DUMP_SWEEP_GROUP",
            "CONCORD_VXL_DUMP_ATLAS_OBJECTS",
            "CONCORD_VXL_DUMP_OWNER_COLORS"
        };
        bool enabled = true;
        for (const char* name : diagnosticSettings) {
            const char* value = lookup_(name);
            if (value != nullptr && value[0] != '\0') {
                enabled = false;
                break;
            }
        }
        active_ = enabled ? this : nullptr;
    }

    ~RuntimeRendererEnvironmentScope() {
        active_ = previous_;
    }

    RuntimeRendererEnvironmentScope(const RuntimeRendererEnvironmentScope&) = delete;
    RuntimeRendererEnvironmentScope& operator=(const RuntimeRendererEnvironmentScope&) = delete;

    static const char* read(const char* name) {
        if (active_ == nullptr || name == nullptr) {
            return runtimeRendererRawEnvironmentValue(name);
        }
        auto& scope = *active_;
        const auto cached = scope.values_.find(std::string_view{name});
        if (cached != scope.values_.end()) {
            return cached->second ? cached->second->c_str() : nullptr;
        }
        const char* value = scope.lookup_(name);
        std::optional<std::string> ownedValue;
        if (value != nullptr) {
            ownedValue.emplace(value);
        }
        const auto inserted = scope.values_.emplace(name, std::move(ownedValue));
        return inserted.first->second ? inserted.first->second->c_str() : nullptr;
    }

private:
    struct NameHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const noexcept {
            return std::hash<std::string_view>{}(name);
        }
    };
    inline static thread_local RuntimeRendererEnvironmentScope* active_ = nullptr;
    RuntimeRendererEnvironmentScope* previous_;
    Lookup lookup_;
    std::unordered_map<std::string, std::optional<std::string>, NameHash, std::equal_to<>> values_;
};

inline const char* runtimeRendererEnvironmentValue(const char* name) {
    return RuntimeRendererEnvironmentScope::read(name);
}
