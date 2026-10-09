#include "RuntimeInternetServiceHostConfig.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

namespace {

constexpr std::size_t maximumConfigBytes = 1024U * 1024U;
constexpr std::size_t maximumConfigLineBytes = 8192U;
constexpr std::size_t maximumConfiguredPeers = 1024U;
constexpr std::size_t certificateSha1Bytes = 20U;
constexpr std::size_t credentialSha256Bytes = 32U;

void secureClear(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        data[index] = 0U;
    }
    bytes.clear();
}

template <std::size_t Size>
void secureClear(std::array<std::uint8_t, Size>& bytes) noexcept {
    volatile std::uint8_t* data = bytes.data();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        data[index] = 0U;
    }
}

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

std::string trimAscii(std::string value) {
    const auto whitespace = [](unsigned char character) {
        return character == ' ' || character == '\t';
    };
    const auto first = std::find_if_not(
        value.begin(),
        value.end(),
        [&](char character) {
            return whitespace(static_cast<unsigned char>(character));
        }
    );
    const auto last = std::find_if_not(
        value.rbegin(),
        value.rend(),
        [&](char character) {
            return whitespace(static_cast<unsigned char>(character));
        }
    ).base();
    if (first >= last) return {};
    return std::string(first, last);
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : static_cast<char>(character);
        }
    );
    return value;
}

bool validIdentity(std::string_view value) noexcept {
    if (value.empty()
        || value.size() > runtimeInternetMaximumIdentityBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '-'
            || byte == '_' || byte == '.' || byte == ':';
    });
}

bool validPublicAddress(std::string_view value) noexcept {
    if (value.empty()
        || value.size() > runtimeInternetMaximumEndpointAddressBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '-'
            || byte == '.' || byte == ':';
    });
}

bool validStoreName(std::string_view value) noexcept {
    if (value.empty() || value.size() > 64U) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
        return (byte >= 'a' && byte <= 'z')
            || (byte >= 'A' && byte <= 'Z')
            || (byte >= '0' && byte <= '9') || byte == '-'
            || byte == '_';
    });
}

int hexNibble(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeHex(
    std::string_view text,
    std::size_t minimumBytes,
    std::size_t maximumBytes,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage,
    std::string_view field
) {
    bytes.clear();
    if ((text.size() & 1U) != 0U
        || text.size() / 2U < minimumBytes
        || text.size() / 2U > maximumBytes) {
        return fail(
            errorMessage,
            std::string(field) + " has an invalid hexadecimal length"
        );
    }
    std::vector<std::uint8_t> candidate(text.size() / 2U);
    for (std::size_t index = 0U; index < candidate.size(); ++index) {
        const int high = hexNibble(text[index * 2U]);
        const int low = hexNibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            secureClear(candidate);
            return fail(
                errorMessage,
                std::string(field) + " contains a non-hexadecimal byte"
            );
        }
        candidate[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    bytes = std::move(candidate);
    return true;
}

bool constantTimeDigestEqual(
    std::span<const std::uint8_t> supplied,
    std::span<const std::uint8_t> expected
) noexcept {
    std::size_t difference = supplied.size() ^ expected.size();
    for (std::size_t index = 0U; index < credentialSha256Bytes; ++index) {
        const std::uint8_t left = index < supplied.size()
            ? supplied[index]
            : 0U;
        const std::uint8_t right = index < expected.size()
            ? expected[index]
            : 0U;
        difference |= static_cast<std::size_t>(left ^ right);
    }
    volatile const std::size_t observed = difference;
    return observed == 0U;
}

bool parsePort(
    std::string_view value,
    std::uint16_t& port,
    std::string& errorMessage
) {
    unsigned int parsed = 0U;
    const auto conversion = std::from_chars(
        value.data(),
        value.data() + value.size(),
        parsed
    );
    if (value.empty() || conversion.ec != std::errc{}
        || conversion.ptr != value.data() + value.size()
        || parsed == 0U
        || parsed > (std::numeric_limits<std::uint16_t>::max)()) {
        return fail(errorMessage, "Service ListenPort is invalid");
    }
    port = static_cast<std::uint16_t>(parsed);
    return true;
}

bool nonzero(std::span<const std::uint8_t> bytes) noexcept {
    std::uint8_t combined = 0U;
    for (const std::uint8_t byte : bytes) {
        combined = static_cast<std::uint8_t>(combined | byte);
    }
    return combined != 0U;
}

#ifdef _WIN32
bool credentialSha256(
    std::span<const std::uint8_t> credential,
    std::array<std::uint8_t, credentialSha256Bytes>& digest
) noexcept {
    if (credential.size()
        > static_cast<std::size_t>((std::numeric_limits<ULONG>::max)())) {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    const NTSTATUS opened = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0U
    );
    if (opened != 0) return false;
    const NTSTATUS hashed = BCryptHash(
        algorithm,
        nullptr,
        0U,
        const_cast<PUCHAR>(credential.data()),
        static_cast<ULONG>(credential.size()),
        digest.data(),
        static_cast<ULONG>(digest.size())
    );
    BCryptCloseAlgorithmProvider(algorithm, 0U);
    return hashed == 0;
}

bool sameThumbprint(
    PCCERT_CONTEXT context,
    std::span<const std::uint8_t> expected
) noexcept {
    std::array<std::uint8_t, certificateSha1Bytes> observed{};
    DWORD observedBytes = static_cast<DWORD>(observed.size());
    if (!CertGetCertificateContextProperty(
            context,
            CERT_SHA1_HASH_PROP_ID,
            observed.data(),
            &observedBytes
        )
        || observedBytes != observed.size()) {
        return false;
    }
    return constantTimeDigestEqual(observed, expected);
}
#endif

} // namespace

struct RuntimeInternetServiceCredentialStore::Impl {
    std::map<
        std::string,
        std::array<std::uint8_t, credentialSha256Bytes>
    > credentialHashes;

    ~Impl() {
        for (auto& entry : credentialHashes) secureClear(entry.second);
    }
};

RuntimeInternetServiceCredentialStore::
RuntimeInternetServiceCredentialStore()
    : impl_(std::make_shared<Impl>()) {}

RuntimeInternetServiceCredentialStore::
~RuntimeInternetServiceCredentialStore() = default;

RuntimeInternetServiceCredentialStore::
RuntimeInternetServiceCredentialStore(
    const RuntimeInternetServiceCredentialStore&
) noexcept = default;

RuntimeInternetServiceCredentialStore&
RuntimeInternetServiceCredentialStore::operator=(
    const RuntimeInternetServiceCredentialStore&
) noexcept = default;

RuntimeInternetServiceCredentialStore::
RuntimeInternetServiceCredentialStore(
    RuntimeInternetServiceCredentialStore&&
) noexcept = default;

RuntimeInternetServiceCredentialStore&
RuntimeInternetServiceCredentialStore::operator=(
    RuntimeInternetServiceCredentialStore&&
) noexcept = default;

std::size_t RuntimeInternetServiceCredentialStore::size() const noexcept {
    return impl_ ? impl_->credentialHashes.size() : 0U;
}

bool RuntimeInternetServiceCredentialStore::insert(
    std::string peerId,
    std::array<std::uint8_t, credentialSha256Bytes> credentialSha256Value,
    std::string& errorMessage
) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    if (!validIdentity(peerId)
        || !nonzero(credentialSha256Value)) {
        secureClear(credentialSha256Value);
        return fail(
            errorMessage,
            "configured peer credential hash is invalid"
        );
    }
    if (impl_->credentialHashes.size() >= maximumConfiguredPeers) {
        secureClear(credentialSha256Value);
        return fail(errorMessage, "configured peer credential limit reached");
    }
    if (impl_->credentialHashes.contains(peerId)) {
        secureClear(credentialSha256Value);
        return fail(errorMessage, "configured peer id is duplicated");
    }
    const bool duplicateHash = std::any_of(
        impl_->credentialHashes.begin(),
        impl_->credentialHashes.end(),
        [&](const auto& entry) {
            return constantTimeDigestEqual(
                credentialSha256Value,
                entry.second
            );
        }
    );
    if (duplicateHash) {
        secureClear(credentialSha256Value);
        return fail(
            errorMessage,
            "configured credential hash is assigned to multiple peers"
        );
    }
    impl_->credentialHashes.emplace(
        std::move(peerId),
        credentialSha256Value
    );
    secureClear(credentialSha256Value);
    return true;
}

RuntimeInternetServiceCredentialVerifier
RuntimeInternetServiceCredentialStore::makeVerifier(
    std::string serviceIdValue
) const {
    const std::shared_ptr<Impl> credentials = impl_;
    return [credentials, serviceIdValue = std::move(serviceIdValue)](
        const RuntimeInternetServiceCredentialRequest& request,
        RuntimeInternetServiceVerifiedIdentity& identity,
        std::string& errorMessage
    ) {
        identity = {};
        static constexpr std::array<
            std::uint8_t,
            credentialSha256Bytes
        > unknownCredentialHash{};
        const std::array<
            std::uint8_t,
            credentialSha256Bytes
        >* expected = nullptr;
        if (credentials) {
            const auto found = credentials->credentialHashes.find(
                request.claimedPeerId
            );
            if (found != credentials->credentialHashes.end()) {
                expected = &found->second;
            }
        }
        std::array<std::uint8_t, credentialSha256Bytes> observed{};
#ifdef _WIN32
        const bool hashed = request.credential.size()
                >= runtimeInternetServiceMinimumCredentialBytes
            && request.credential.size()
                <= runtimeInternetServiceMaximumCredentialBytes
            && credentialSha256(request.credential, observed);
#else
        const bool hashed = false;
#endif
        const std::span<const std::uint8_t> expectedBytes = expected
            ? std::span<const std::uint8_t>(*expected)
            : std::span<const std::uint8_t>(unknownCredentialHash);
        const bool matches = constantTimeDigestEqual(
            observed,
            expectedBytes
        );
        secureClear(observed);
        if (expected == nullptr || !hashed || !matches) {
            errorMessage = "credential rejected";
            return false;
        }
        identity.serviceId = serviceIdValue;
        identity.peerId = request.claimedPeerId;
        errorMessage.clear();
        return true;
    };
}

bool parseRuntimeInternetServiceHostConfigText(
    std::string_view iniText,
    RuntimeInternetServiceHostDeploymentConfig& config,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (iniText.empty() || iniText.size() > maximumConfigBytes
        || iniText.find('\0') != std::string_view::npos) {
        return fail(errorMessage, "service configuration file is invalid");
    }

    enum class Section : std::uint8_t { None, Service, Credentials };
    Section section = Section::None;
    bool sawService = false;
    bool sawCredentials = false;
    std::set<std::string> serviceKeys;
    std::set<std::string> credentialPeerIds;
    RuntimeInternetServiceHostDeploymentConfig candidate;
    std::vector<std::uint8_t> thumbprint;
    std::size_t offset = 0U;
    std::size_t lineNumber = 0U;
    while (offset <= iniText.size()) {
        const std::size_t newline = iniText.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos
            ? iniText.size()
            : newline;
        if (end - offset > maximumConfigLineBytes) {
            return fail(errorMessage, "service configuration line is too long");
        }
        std::string line(iniText.substr(offset, end - offset));
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNumber == 1U && line.size() >= 3U
            && static_cast<unsigned char>(line[0]) == 0xefU
            && static_cast<unsigned char>(line[1]) == 0xbbU
            && static_cast<unsigned char>(line[2]) == 0xbfU) {
            line.erase(0U, 3U);
        }
        line = trimAscii(std::move(line));
        if (!line.empty() && line.front() != ';'
            && line.front() != '#') {
            if (line.front() == '[') {
                if (line.size() < 3U || line.back() != ']') {
                    return fail(
                        errorMessage,
                        "malformed service configuration section at line "
                            + std::to_string(lineNumber)
                    );
                }
                const std::string name = lowerAscii(trimAscii(
                    line.substr(1U, line.size() - 2U)
                ));
                if (name == "service") {
                    if (sawService) {
                        return fail(
                            errorMessage,
                            "[Service] section is duplicated"
                        );
                    }
                    sawService = true;
                    section = Section::Service;
                } else if (name == "credentials") {
                    if (sawCredentials) {
                        return fail(
                            errorMessage,
                            "[Credentials] section is duplicated"
                        );
                    }
                    sawCredentials = true;
                    section = Section::Credentials;
                } else {
                    return fail(
                        errorMessage,
                        "unknown service configuration section at line "
                            + std::to_string(lineNumber)
                    );
                }
            } else {
                const std::size_t equals = line.find('=');
                if (section == Section::None
                    || equals == std::string::npos) {
                    return fail(
                        errorMessage,
                        "malformed service configuration key at line "
                            + std::to_string(lineNumber)
                    );
                }
                const std::string rawKey = trimAscii(
                    line.substr(0U, equals)
                );
                const std::string value = trimAscii(line.substr(equals + 1U));
                if (rawKey.empty() || value.empty()) {
                    return fail(
                        errorMessage,
                        "empty service configuration key or value at line "
                            + std::to_string(lineNumber)
                    );
                }
                if (section == Section::Service) {
                    const std::string key = lowerAscii(rawKey);
                    static const std::set<std::string> allowed = {
                        "serviceid",
                        "listenport",
                        "listenscope",
                        "publicrelayaddress",
                        "certificatestorelocation",
                        "certificatestorename",
                        "certificatesha1"
                    };
                    if (!allowed.contains(key)) {
                        return fail(
                            errorMessage,
                            "unknown [Service] key at line "
                                + std::to_string(lineNumber)
                        );
                    }
                    if (!serviceKeys.insert(key).second) {
                        return fail(
                            errorMessage,
                            "duplicate [Service] key at line "
                                + std::to_string(lineNumber)
                        );
                    }
                    if (key == "serviceid") {
                        candidate.serviceId = value;
                    } else if (key == "listenport") {
                        if (!parsePort(
                                value,
                                candidate.listenPort,
                                errorMessage
                            )) {
                            return false;
                        }
                    } else if (key == "listenscope") {
                        const std::string scope = lowerAscii(value);
                        if (scope == "anyipv4") {
                            candidate.listenScope =
                                RuntimeInternetTlsBindScope::AnyIpv4;
                        } else if (scope == "loopbackipv4") {
                            candidate.listenScope =
                                RuntimeInternetTlsBindScope::LoopbackIpv4;
                        } else {
                            return fail(
                                errorMessage,
                                "Service ListenScope is invalid"
                            );
                        }
                    } else if (key == "publicrelayaddress") {
                        candidate.publicRelayAddress = value;
                    } else if (key == "certificatestorelocation") {
                        const std::string location = lowerAscii(value);
                        if (location == "currentuser") {
                            candidate.certificate.location =
                                RuntimeInternetCertificateStoreLocation::
                                    CurrentUser;
                        } else if (location == "localmachine") {
                            candidate.certificate.location =
                                RuntimeInternetCertificateStoreLocation::
                                    LocalMachine;
                        } else {
                            return fail(
                                errorMessage,
                                "Service CertificateStoreLocation is invalid"
                            );
                        }
                    } else if (key == "certificatestorename") {
                        candidate.certificate.storeName = value;
                    } else if (!decodeHex(
                            value,
                            certificateSha1Bytes,
                            certificateSha1Bytes,
                            thumbprint,
                            errorMessage,
                            "Service CertificateSha1"
                        )) {
                        return false;
                    }
                } else {
                    if (!validIdentity(rawKey)) {
                        return fail(
                            errorMessage,
                            "invalid peer id in [Credentials] at line "
                            + std::to_string(lineNumber)
                        );
                    }
                    if (!credentialPeerIds.insert(rawKey).second) {
                        return fail(
                            errorMessage,
                            "configured peer id is duplicated"
                        );
                    }
                    std::vector<std::uint8_t> credentialHashBytes;
                    if (!decodeHex(
                            value,
                            credentialSha256Bytes,
                            credentialSha256Bytes,
                            credentialHashBytes,
                            errorMessage,
                            "peer CredentialSha256"
                        )) {
                        return false;
                    }
                    std::array<
                        std::uint8_t,
                        credentialSha256Bytes
                    > credentialHash{};
                    std::copy(
                        credentialHashBytes.begin(),
                        credentialHashBytes.end(),
                        credentialHash.begin()
                    );
                    secureClear(credentialHashBytes);
                    if (!candidate.credentials.insert(
                            rawKey,
                            credentialHash,
                            errorMessage
                        )) {
                        secureClear(credentialHash);
                        return false;
                    }
                    secureClear(credentialHash);
                }
            }
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }

    static const std::array<const char*, 7U> required = {
        "serviceid",
        "listenport",
        "listenscope",
        "publicrelayaddress",
        "certificatestorelocation",
        "certificatestorename",
        "certificatesha1"
    };
    if (!sawService || !sawCredentials
        || std::any_of(required.begin(), required.end(), [&](const char* key) {
            return !serviceKeys.contains(key);
        })
        || candidate.credentials.size() == 0U) {
        secureClear(thumbprint);
        return fail(
            errorMessage,
            "service configuration is missing a required section or key"
        );
    }
    if (!validIdentity(candidate.serviceId)
        || !validPublicAddress(candidate.publicRelayAddress)
        || !validStoreName(candidate.certificate.storeName)
        || thumbprint.size() != certificateSha1Bytes
        || !nonzero(thumbprint)) {
        secureClear(thumbprint);
        return fail(errorMessage, "service configuration value is invalid");
    }
    if (credentialPeerIds.contains(candidate.serviceId)) {
        secureClear(thumbprint);
        return fail(errorMessage, "service id may not also be a peer id");
    }
    std::copy(
        thumbprint.begin(),
        thumbprint.end(),
        candidate.certificate.sha1Thumbprint.begin()
    );
    secureClear(thumbprint);
    config = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool loadRuntimeInternetServiceHostConfig(
    const std::filesystem::path& path,
    RuntimeInternetServiceHostDeploymentConfig& config,
    std::string& errorMessage
) {
    errorMessage.clear();
    std::error_code fileError;
    if (path.empty() || !std::filesystem::is_regular_file(path, fileError)) {
        return fail(errorMessage, "service configuration file is missing");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, fileError);
    if (fileError || size == 0U || size > maximumConfigBytes
        || size > static_cast<std::uintmax_t>(
            (std::numeric_limits<std::streamsize>::max)()
        )) {
        return fail(errorMessage, "service configuration file size is invalid");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return fail(
            errorMessage,
            "service configuration file could not be opened"
        );
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream
        || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
        return fail(
            errorMessage,
            "service configuration file could not be read"
        );
    }
    RuntimeInternetServiceHostDeploymentConfig candidate;
    if (!parseRuntimeInternetServiceHostConfigText(
            bytes,
            candidate,
            errorMessage
        )) {
        return false;
    }
    candidate.communityStatePath = path;
    candidate.communityStatePath += ".community.state";
    config = std::move(candidate);
    errorMessage.clear();
    return true;
}

struct RuntimeInternetServiceCertificateContext::Impl {
#ifdef _WIN32
    PCCERT_CONTEXT context = nullptr;

    ~Impl() {
        if (context != nullptr) CertFreeCertificateContext(context);
    }
#endif
};

RuntimeInternetServiceCertificateContext::
RuntimeInternetServiceCertificateContext()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetServiceCertificateContext::
~RuntimeInternetServiceCertificateContext() = default;

RuntimeInternetServiceCertificateContext::
RuntimeInternetServiceCertificateContext(
    RuntimeInternetServiceCertificateContext&&
) noexcept = default;

RuntimeInternetServiceCertificateContext&
RuntimeInternetServiceCertificateContext::operator=(
    RuntimeInternetServiceCertificateContext&&
) noexcept = default;

const void* RuntimeInternetServiceCertificateContext::
nativeCertificateContext() const noexcept {
#ifdef _WIN32
    return impl_ ? impl_->context : nullptr;
#else
    return nullptr;
#endif
}

bool RuntimeInternetServiceCertificateContext::valid() const noexcept {
    return nativeCertificateContext() != nullptr;
}

void RuntimeInternetServiceCertificateContext::reset() {
    impl_ = std::make_unique<Impl>();
}

bool loadRuntimeInternetServiceCertificate(
    const RuntimeInternetServiceCertificateSelection& selection,
    RuntimeInternetServiceCertificateContext& certificate,
    std::string& errorMessage
) {
    errorMessage.clear();
    certificate.reset();
    if (!validStoreName(selection.storeName)
        || !nonzero(selection.sha1Thumbprint)) {
        return fail(errorMessage, "server certificate selection is invalid");
    }
#ifdef _WIN32
    const std::wstring storeName(
        selection.storeName.begin(),
        selection.storeName.end()
    );
    const DWORD location = selection.location
            == RuntimeInternetCertificateStoreLocation::CurrentUser
        ? CERT_SYSTEM_STORE_CURRENT_USER
        : selection.location
                == RuntimeInternetCertificateStoreLocation::LocalMachine
            ? CERT_SYSTEM_STORE_LOCAL_MACHINE
            : 0U;
    if (location == 0U) {
        return fail(errorMessage, "server certificate store location is invalid");
    }
    HCERTSTORE store = CertOpenStore(
        CERT_STORE_PROV_SYSTEM_W,
        0U,
        0,
        location | CERT_STORE_OPEN_EXISTING_FLAG
            | CERT_STORE_READONLY_FLAG,
        storeName.c_str()
    );
    if (store == nullptr) {
        return fail(errorMessage, "server certificate store could not be opened");
    }
    PCCERT_CONTEXT selected = nullptr;
    std::size_t matches = 0U;
    PCCERT_CONTEXT current = nullptr;
    while ((current = CertEnumCertificatesInStore(store, current)) != nullptr) {
        if (!sameThumbprint(current, selection.sha1Thumbprint)) continue;
        ++matches;
        if (selected == nullptr) {
            selected = CertDuplicateCertificateContext(current);
        }
    }
    CertCloseStore(store, 0U);
    if (matches != 1U || selected == nullptr) {
        if (selected != nullptr) CertFreeCertificateContext(selected);
        return fail(
            errorMessage,
            matches == 0U
                ? "server certificate thumbprint was not found"
                : "server certificate thumbprint is not unique"
        );
    }
    certificate.impl_->context = selected;
    return true;
#else
    (void)selection;
    return fail(
        errorMessage,
        "Windows certificate store is unavailable on this platform"
    );
#endif
}
