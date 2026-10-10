#pragma once
#include <compare>
#include <cstdint>
#include <type_traits>

template<class Value> class RuntimeTerrainMetadataMap;

// One binding lives inside its owning metadata map. Nodes carry only this
// binding's address and their original integer key. No callback is exposed to
// application code; the owning map supplies its noexcept derived-cache writer.
struct RuntimeAuthoredLandWriteBinding {
    void* owner = nullptr;
    void (*write)(void*, int, std::int64_t) noexcept = nullptr;
};

template<class Enum>
class RuntimeTrackedAuthoredLand {
    static_assert(std::is_enum_v<Enum>);
    static_assert(sizeof(std::underlying_type_t<Enum>) <= sizeof(std::int32_t));
    using Underlying = std::underlying_type_t<Enum>;
    RuntimeAuthoredLandWriteBinding* binding_ = nullptr;
    Enum value_{};
    int key_ = 0;
    template<class> friend class RuntimeTerrainMetadataMap;
    void bind(RuntimeAuthoredLandWriteBinding* binding, int key) noexcept {
        binding_ = binding; key_ = key;
    }
    void assign(Enum value) noexcept {
        if (value_ == value) return;
        value_ = value;
        if (binding_ && binding_->write)
            binding_->write(binding_->owner, key_, ordinal());
    }
public:
    using TrackedAuthoredLandTag = void;
    RuntimeTrackedAuthoredLand() noexcept = default;
    RuntimeTrackedAuthoredLand(Enum value) noexcept : value_(value) {}
    // Values copied into snapshots, hash-sort scratch or a different map are
    // detached. Whole-Value assignment retains the DESTINATION node binding.
    RuntimeTrackedAuthoredLand(const RuntimeTrackedAuthoredLand& other) noexcept : value_(other.value_) {}
    RuntimeTrackedAuthoredLand(RuntimeTrackedAuthoredLand&& other) noexcept : value_(other.value_) {}
    RuntimeTrackedAuthoredLand& operator=(Enum value) noexcept { assign(value); return *this; }
    RuntimeTrackedAuthoredLand& operator=(const RuntimeTrackedAuthoredLand& other) noexcept {
        assign(other.value_); return *this;
    }
    RuntimeTrackedAuthoredLand& operator=(RuntimeTrackedAuthoredLand&& other) noexcept {
        assign(other.value_); return *this;
    }
    // Conversion is by VALUE only. A retained field reference can assign this
    // wrapper, but cannot obtain a mutable Enum reference bypassing the writer.
    operator Enum() const noexcept { return value_; }
    explicit operator int() const noexcept { return static_cast<int>(value_); }
    std::int64_t ordinal() const noexcept { return static_cast<std::int64_t>(static_cast<Underlying>(value_)); }
    friend bool operator==(const RuntimeTrackedAuthoredLand& a, const RuntimeTrackedAuthoredLand& b) noexcept {
        return a.value_ == b.value_;
    }
    friend auto operator<=>(const RuntimeTrackedAuthoredLand& a, const RuntimeTrackedAuthoredLand& b) noexcept {
        return a.ordinal() <=> b.ordinal();
    }
    friend bool operator==(const RuntimeTrackedAuthoredLand& a, Enum b) noexcept { return a.value_ == b; }
    friend bool operator==(Enum a, const RuntimeTrackedAuthoredLand& b) noexcept { return a == b.value_; }
};
