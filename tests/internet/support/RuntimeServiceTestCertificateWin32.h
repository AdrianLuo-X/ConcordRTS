#pragma once

// Shared original HostSmoke helpers; include inside its test namespace.
bool secureRandom(std::uint8_t* destination, std::size_t size) {
    return size <= (std::numeric_limits<ULONG>::max)()
        && BCryptGenRandom(
            nullptr,
            destination,
            static_cast<ULONG>(size),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) == 0;
}

bool shiftSystemTime(
    const SYSTEMTIME& source,
    std::int64_t deltaSeconds,
    SYSTEMTIME& destination
) {
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&source, &fileTime)) return false;
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    const std::int64_t shifted = static_cast<std::int64_t>(ticks.QuadPart)
        + deltaSeconds * 10000000LL;
    if (shifted <= 0) return false;
    ticks.QuadPart = static_cast<ULONGLONG>(shifted);
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;
    return FileTimeToSystemTime(&fileTime, &destination) != FALSE;
}

class TestCertificate {
public:
    ~TestCertificate() { reset(); }

    bool create(std::string& errorMessage) {
        reset();
        std::array<std::uint8_t, 8U> random{};
        if (!secureRandom(random.data(), random.size())) {
            errorMessage = "could not generate test certificate key name";
            return false;
        }
        container_ = L"ConcordRTS-HostSmoke-";
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (const std::uint8_t byte : random) {
            container_.push_back(hex[byte >> 4U]);
            container_.push_back(hex[byte & 0x0fU]);
        }
        if (!CryptAcquireContextW(
                &provider_,
                container_.c_str(),
                MS_ENH_RSA_AES_PROV_W,
                PROV_RSA_AES,
                CRYPT_NEWKEYSET
            )
            || !CryptGenKey(
                provider_,
                AT_KEYEXCHANGE,
                (2048U << 16U) | CRYPT_EXPORTABLE,
                &key_
            )) {
            errorMessage = "could not create test certificate private key";
            return false;
        }

        DWORD subjectBytes = 0U;
        if (!CertStrToNameW(
                X509_ASN_ENCODING,
                L"CN=localhost",
                CERT_X500_NAME_STR,
                nullptr,
                nullptr,
                &subjectBytes,
                nullptr
            )) {
            errorMessage = "could not size test certificate subject";
            return false;
        }
        std::vector<std::uint8_t> subject(subjectBytes);
        if (!CertStrToNameW(
                X509_ASN_ENCODING,
                L"CN=localhost",
                CERT_X500_NAME_STR,
                nullptr,
                subject.data(),
                &subjectBytes,
                nullptr
            )) {
            errorMessage = "could not encode test certificate subject";
            return false;
        }
        CERT_NAME_BLOB subjectBlob{subjectBytes, subject.data()};
        CERT_ALT_NAME_ENTRY alternateName{};
        alternateName.dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
        alternateName.pwszDNSName = const_cast<wchar_t*>(L"localhost");
        CERT_ALT_NAME_INFO alternateNames{1U, &alternateName};
        BYTE* alternateNameBytes = nullptr;
        DWORD alternateNameSize = 0U;
        if (!CryptEncodeObjectEx(
                X509_ASN_ENCODING,
                X509_ALTERNATE_NAME,
                &alternateNames,
                CRYPT_ENCODE_ALLOC_FLAG,
                nullptr,
                &alternateNameBytes,
                &alternateNameSize
            )) {
            errorMessage = "could not encode test certificate hostname";
            return false;
        }
        LPSTR usageIdentifiers[] = {
            const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH)
        };
        CERT_ENHKEY_USAGE usage{1U, usageIdentifiers};
        BYTE* usageBytes = nullptr;
        DWORD usageSize = 0U;
        if (!CryptEncodeObjectEx(
                X509_ASN_ENCODING,
                X509_ENHANCED_KEY_USAGE,
                &usage,
                CRYPT_ENCODE_ALLOC_FLAG,
                nullptr,
                &usageBytes,
                &usageSize
            )) {
            LocalFree(alternateNameBytes);
            errorMessage = "could not encode test certificate usage";
            return false;
        }
        CERT_EXTENSION extensions[2]{};
        extensions[0].pszObjId = const_cast<LPSTR>(szOID_SUBJECT_ALT_NAME2);
        extensions[0].Value = {alternateNameSize, alternateNameBytes};
        extensions[1].pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
        extensions[1].Value = {usageSize, usageBytes};
        CERT_EXTENSIONS extensionList{2U, extensions};
        CRYPT_KEY_PROV_INFO keyProvider{};
        keyProvider.pwszContainerName = container_.data();
        keyProvider.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        keyProvider.dwProvType = PROV_RSA_AES;
        keyProvider.dwKeySpec = AT_KEYEXCHANGE;
        CRYPT_ALGORITHM_IDENTIFIER signature{};
        signature.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
        SYSTEMTIME current{};
        SYSTEMTIME validFrom{};
        SYSTEMTIME validUntil{};
        GetSystemTime(&current);
        if (shiftSystemTime(current, -86400, validFrom)
            && shiftSystemTime(current, 7 * 86400, validUntil)) {
            certificate_ = CertCreateSelfSignCertificate(
                static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
                &subjectBlob,
                0U,
                &keyProvider,
                &signature,
                &validFrom,
                &validUntil,
                &extensionList
            );
        }
        LocalFree(usageBytes);
        LocalFree(alternateNameBytes);
        if (certificate_ == nullptr
            || !CertSetCertificateContextProperty(
                certificate_,
                CERT_KEY_PROV_INFO_PROP_ID,
                0U,
                &keyProvider
            )) {
            errorMessage = "could not create local test certificate";
            return false;
        }
        rootStore_ = CertOpenStore(
            CERT_STORE_PROV_MEMORY,
            0U,
            0,
            0U,
            nullptr
        );
        CERT_CHAIN_ENGINE_CONFIG chainConfig{};
        chainConfig.cbSize = sizeof(chainConfig);
        chainConfig.hExclusiveRoot = rootStore_;
        if (rootStore_ == nullptr
            || !CertAddCertificateContextToStore(
                rootStore_,
                certificate_,
                CERT_STORE_ADD_NEW,
                nullptr
            )
            || !CertCreateCertificateChainEngine(
                &chainConfig,
                &chainEngine_
            )) {
            errorMessage = "could not create in-memory test trust chain";
            return false;
        }
        return true;
    }

    PCCERT_CONTEXT certificate() const noexcept { return certificate_; }
    HCERTCHAINENGINE chainEngine() const noexcept { return chainEngine_; }
    RuntimeInternetTlsServerCertificate serverCertificate() const noexcept {
        return tlsBinding_.serverCertificate();
    }
    RuntimeInternetTlsPeerTrust peerTrust() const noexcept {
        return tlsBinding_.peerTrust();
    }

private:
    void reset() noexcept {
        if (chainEngine_ != nullptr) {
            CertFreeCertificateChainEngine(chainEngine_);
            chainEngine_ = nullptr;
        }
        if (rootStore_ != nullptr) {
            CertCloseStore(rootStore_, 0U);
            rootStore_ = nullptr;
        }
        if (certificate_ != nullptr) {
            CertFreeCertificateContext(certificate_);
            certificate_ = nullptr;
        }
        if (key_ != 0) {
            CryptDestroyKey(key_);
            key_ = 0;
        }
        if (provider_ != 0) {
            CryptReleaseContext(provider_, 0U);
            provider_ = 0;
        }
        if (!container_.empty()) {
            HCRYPTPROV deletion = 0;
            (void)CryptAcquireContextW(
                &deletion,
                container_.c_str(),
                MS_ENH_RSA_AES_PROV_W,
                PROV_RSA_AES,
                CRYPT_DELETEKEYSET
            );
            container_.clear();
        }
    }

    std::wstring container_;
    HCRYPTPROV provider_ = 0;
    HCRYPTKEY key_ = 0;
    PCCERT_CONTEXT certificate_ = nullptr;
    HCERTSTORE rootStore_ = nullptr;
    HCERTCHAINENGINE chainEngine_ = nullptr;
    RuntimeInternetTlsCertificateInterop tlsBinding_{certificate_, chainEngine_};
};

