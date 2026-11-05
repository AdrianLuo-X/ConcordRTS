#pragma once

#include "network/transport/RuntimeInternetTlsCredentials.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>

struct RuntimeInternetTlsServerCertificate::Impl {
    PCCERT_CONTEXT const* context = nullptr;
};

struct RuntimeInternetTlsPeerTrust::Impl {
    HCERTCHAINENGINE const* engine = nullptr;
};

// A stable private binding to the existing owner's resource slots. No native
// duplication, close or allocation occurs here. Views must not outlive this
// binding; the underlying resource keeps its original TLS lifetime contract.
class RuntimeInternetTlsCertificateInterop {
public:
    explicit RuntimeInternetTlsCertificateInterop(
        PCCERT_CONTEXT& certificate
    ) noexcept : certificate_{&certificate} {}

    RuntimeInternetTlsCertificateInterop(
        PCCERT_CONTEXT& certificate,
        HCERTCHAINENGINE& trust
    ) noexcept : certificate_{&certificate}, trust_{&trust} {}

    RuntimeInternetTlsCertificateInterop(
        const RuntimeInternetTlsCertificateInterop&
    ) = delete;
    RuntimeInternetTlsCertificateInterop& operator=(
        const RuntimeInternetTlsCertificateInterop&
    ) = delete;
    RuntimeInternetTlsCertificateInterop(
        RuntimeInternetTlsCertificateInterop&&
    ) = delete;
    RuntimeInternetTlsCertificateInterop& operator=(
        RuntimeInternetTlsCertificateInterop&&
    ) = delete;

    RuntimeInternetTlsServerCertificate serverCertificate() const noexcept {
        return certificate_.context && *certificate_.context
            ? RuntimeInternetTlsServerCertificate(&certificate_)
            : RuntimeInternetTlsServerCertificate{};
    }
    RuntimeInternetTlsPeerTrust peerTrust() const noexcept {
        return trust_.engine && *trust_.engine
            ? RuntimeInternetTlsPeerTrust(&trust_)
            : RuntimeInternetTlsPeerTrust{};
    }
    static PCCERT_CONTEXT nativeCertificate(
        RuntimeInternetTlsServerCertificate certificate
    ) noexcept {
        return certificate.impl_ && certificate.impl_->context
            ? *certificate.impl_->context : nullptr;
    }
    static HCERTCHAINENGINE nativePeerTrust(
        RuntimeInternetTlsPeerTrust trust
    ) noexcept {
        return trust.impl_ && trust.impl_->engine
            ? *trust.impl_->engine : nullptr;
    }

private:
    RuntimeInternetTlsServerCertificate::Impl certificate_;
    RuntimeInternetTlsPeerTrust::Impl trust_;
};
#endif
