#pragma once

class RuntimeInternetTlsCertificateInterop;

// Borrowed service identity used only when opening a TLS listener. The
// existing certificate owner and its private key remain responsible for the
// resource. A successful listener open acquires its own certificate reference.
class RuntimeInternetTlsServerCertificate {
public:
    constexpr RuntimeInternetTlsServerCertificate() noexcept = default;
    constexpr bool empty() const noexcept { return impl_ == nullptr; }
    bool operator==(const RuntimeInternetTlsServerCertificate&) const = default;

private:
    struct Impl;
    const Impl* impl_ = nullptr;
    explicit constexpr RuntimeInternetTlsServerCertificate(const Impl* impl)
        noexcept : impl_(impl) {}
    friend class RuntimeInternetTlsCertificateInterop;
};

// Borrowed validation authority for an explicitly configured private PKI.
// Empty selects the platform's system trust. This never replaces hostname,
// revocation or additional certificate-pin restrictions. The owner must keep
// the trust resource alive until the client's handshake has completed.
class RuntimeInternetTlsPeerTrust {
public:
    constexpr RuntimeInternetTlsPeerTrust() noexcept = default;
    constexpr bool empty() const noexcept { return impl_ == nullptr; }
    bool operator==(const RuntimeInternetTlsPeerTrust&) const = default;

private:
    struct Impl;
    const Impl* impl_ = nullptr;
    explicit constexpr RuntimeInternetTlsPeerTrust(const Impl* impl)
        noexcept : impl_(impl) {}
    friend class RuntimeInternetTlsCertificateInterop;
};
