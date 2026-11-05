#pragma once

#include "network/internet/RuntimeInternetServiceHostConfig.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#include <span>
#include <utility>

class RuntimeCertificateStoreWin32 {
public:
    RuntimeCertificateStoreWin32() noexcept = default;
    explicit RuntimeCertificateStoreWin32(HCERTSTORE store) noexcept
        : store_(store) {}
    ~RuntimeCertificateStoreWin32() { reset(); }
    RuntimeCertificateStoreWin32(const RuntimeCertificateStoreWin32&) = delete;
    RuntimeCertificateStoreWin32& operator=(const RuntimeCertificateStoreWin32&) = delete;
    RuntimeCertificateStoreWin32(RuntimeCertificateStoreWin32&& other) noexcept
        : store_(std::exchange(other.store_, nullptr)) {}
    RuntimeCertificateStoreWin32& operator=(RuntimeCertificateStoreWin32&& other) noexcept {
        if (this != &other) {
            reset();
            store_ = std::exchange(other.store_, nullptr);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return store_ != nullptr; }
    HCERTSTORE get() const noexcept { return store_; }
    void reset() noexcept {
        if (store_) CertCloseStore(std::exchange(store_, nullptr), 0U);
    }
private:
    HCERTSTORE store_ = nullptr;
};

// Consumes a real owned store, closes it after the original enumeration, and
// duplicates the unique match into the existing empty certificate owner.
// Store creation/open policy remains with the caller. The production caller
// uses the original read-only existing system store; native tests use memory.
class RuntimeInternetServiceCertificateFactory {
public:
    static bool selectFromOwnedStore(
        RuntimeCertificateStoreWin32 store,
        const std::array<std::uint8_t, 20U>& thumbprint,
        RuntimeInternetServiceCertificateContext& certificate,
        std::string& errorMessage
    );
};

// Private diagnostics call the same existing primitives. No copied crypto or
// substitute provider is implemented here; these declarations expose no raw
// credentials to the public deployment configuration or certificate contract.
class RuntimeInternetServiceCredentialPrimitives {
public:
    static bool hash(std::span<const std::uint8_t> credential,
        std::array<std::uint8_t, 32U>& digest) noexcept;
    static bool equal(std::span<const std::uint8_t> first,
        std::span<const std::uint8_t> second) noexcept;
    static void clear(std::array<std::uint8_t, 32U>& bytes) noexcept;
    static void clear(std::vector<std::uint8_t>& bytes) noexcept;
};
#endif
