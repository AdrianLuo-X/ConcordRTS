#include "network/internet/RuntimeInternetServiceHostConfig.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view validConfiguration = R"ini(
[Service]
ServiceId=concord-self-hosted
ListenPort=47070
ListenScope=LoopbackIpv4
PublicRelayAddress=relay.example.invalid
CertificateStoreLocation=CurrentUser
CertificateStoreName=MY
CertificateSha1=00112233445566778899AABBCCDDEEFF00112233

[Credentials]
peer-a=9F9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F
peer-b=3465F6E6975BAA864CA957F0914ECDA9BF7EB601AC31AC20A5DC3D329279A843
)ini";

bool require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "Runtime Internet service config smoke failed: "
                  << message << '\n';
        return false;
    }
    return true;
}

std::string replaceOnce(
    std::string text,
    std::string_view from,
    std::string_view to
) {
    const std::size_t offset = text.find(from);
    if (offset != std::string::npos) {
        text.replace(offset, from.size(), to);
    }
    return text;
}

bool rejects(std::string text) {
    RuntimeInternetServiceHostDeploymentConfig unchanged;
    unchanged.serviceId = "unchanged";
    std::string error;
    const bool parsed = parseRuntimeInternetServiceHostConfigText(
        text,
        unchanged,
        error
    );
    return !parsed && !error.empty() && unchanged.serviceId == "unchanged";
}

bool testStrictParser() {
    RuntimeInternetServiceHostDeploymentConfig config;
    std::string error;
    if (!require(
            parseRuntimeInternetServiceHostConfigText(
                validConfiguration,
                config,
                error
            ),
            "valid deployment configuration was rejected"
        )
        || !require(
            config.serviceId == "concord-self-hosted"
                && config.listenPort == 47070U
                && config.listenScope
                    == RuntimeInternetTlsBindScope::LoopbackIpv4
                && config.publicRelayAddress == "relay.example.invalid"
                && config.certificate.location
                    == RuntimeInternetCertificateStoreLocation::CurrentUser
                && config.certificate.storeName == "MY"
                && config.credentials.size() == 2U,
            "parsed deployment values were not exact"
        )) {
        return false;
    }

    const std::string base(validConfiguration);
    const std::vector<std::string> invalid = {
        replaceOnce(base, "ListenPort=47070", "ListenPort=0"),
        replaceOnce(base, "ListenPort=47070", "ListenPort=65536"),
        replaceOnce(base, "LoopbackIpv4", "PublicIpv6"),
        replaceOnce(base, "relay.example.invalid", "relay name with spaces"),
        replaceOnce(base, "CurrentUser", "Anywhere"),
        replaceOnce(base, "CertificateStoreName=MY", "CertificateStoreName=M/Y"),
        replaceOnce(
            base,
            "CertificateSha1=00112233445566778899AABBCCDDEEFF00112233",
            "CertificateSha1=0011"
        ),
        replaceOnce(
            base,
            "00112233445566778899AABBCCDDEEFF00112233",
            "0000000000000000000000000000000000000000"
        ),
        replaceOnce(base, "ServiceId=", "UnknownKey=x\nServiceId="),
        replaceOnce(base, "[Credentials]", "[Unexpected]"),
        replaceOnce(base, "[Service]", "[Service]\n[Service]"),
        replaceOnce(base, "ListenPort=47070", "ListenPort=47070\nListenPort=47070"),
        replaceOnce(base, "peer-b=", "peer-a="),
        replaceOnce(
            base,
            "3465F6E6975BAA864CA957F0914ECDA9BF7EB601AC31AC20A5DC3D329279A843",
            "9F9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F"
        ),
        replaceOnce(
            base,
            "9F9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F",
            "9F9F"
        ),
        replaceOnce(
            base,
            "9F9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F",
            "ZF9F5111F7B27A781F1F1DDDE5EBC2DD2B796BFC7365C9C28B548E564176929F"
        ),
        replaceOnce(base, "peer-a=", "concord-self-hosted="),
        replaceOnce(base, "ServiceId=concord-self-hosted\n", ""),
        base + std::string(9000U, 'x')
    };
    for (std::size_t index = 0U; index < invalid.size(); ++index) {
        if (!require(
                rejects(invalid[index]),
                "strict parser accepted invalid case "
                    + std::to_string(index)
            )) {
            return false;
        }
    }
    std::string embeddedNul(base);
    embeddedNul.insert(10U, 1U, '\0');
    return require(rejects(std::move(embeddedNul)), "embedded NUL was accepted");
}

bool testCredentialVerifier() {
    RuntimeInternetServiceHostDeploymentConfig config;
    std::string error;
    if (!parseRuntimeInternetServiceHostConfigText(
            validConfiguration,
            config,
            error
        )) {
        return false;
    }
    RuntimeInternetServiceCredentialVerifier verify =
        config.credentials.makeVerifier(config.serviceId);
    const std::string credentialText = "0123456789abcdef";
    std::vector<std::uint8_t> credential(
        credentialText.begin(),
        credentialText.end()
    );
    RuntimeInternetServiceCredentialRequest request;
    request.claimedServiceId = "wire-claim-is-not-authority";
    request.claimedPeerId = "peer-a";
    request.credential = credential;
    RuntimeInternetServiceVerifiedIdentity identity;
    if (!require(
            verify(request, identity, error)
                && identity.serviceId == "concord-self-hosted"
                && identity.peerId == "peer-a",
            "valid raw client credential did not resolve authoritative identity"
        )) {
        return false;
    }

    credential.front() ^= 0x01U;
    request.credential = credential;
    identity = {"must", "clear"};
    if (!require(
            !verify(request, identity, error)
                && identity.serviceId.empty()
                && identity.peerId.empty(),
            "wrong credential was accepted or leaked prior identity"
        )) {
        return false;
    }
    credential.front() ^= 0x01U;
    request.claimedPeerId = "peer-b";
    request.credential = credential;
    if (!require(
            !verify(request, identity, error),
            "credential was accepted for a different peer claim"
        )) {
        return false;
    }
    request.claimedPeerId = "unknown-peer";
    if (!require(
            !verify(request, identity, error),
            "unknown peer was accepted"
        )) {
        return false;
    }
    request.claimedPeerId = "peer-a";
    const std::vector<std::uint8_t> tooShort(15U, 0x41U);
    request.credential = tooShort;
    if (!require(!verify(request, identity, error), "short credential was accepted")) {
        return false;
    }
    const std::vector<std::uint8_t> tooLong(4097U, 0x41U);
    request.credential = tooLong;
    return require(
        !verify(request, identity, error),
        "oversized credential was accepted"
    );
}

bool testExplicitFileLoad() {
    const std::filesystem::path path =
        std::filesystem::current_path()
        / "runtime_internet_service_config_smoke.ini";
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            validConfiguration.data(),
            static_cast<std::streamsize>(validConfiguration.size())
        );
        if (!output) return false;
    }
    RuntimeInternetServiceHostDeploymentConfig config;
    std::string error;
    const bool loaded = loadRuntimeInternetServiceHostConfig(
        path,
        config,
        error
    );
    std::error_code removeError;
    const bool removed = std::filesystem::remove(path, removeError);
    return require(
        loaded && removed && !removeError
            && config.credentials.size() == 2U,
        "explicit configuration file load failed"
    );
}

} // namespace

int main() {
    if (!testStrictParser()
        || !testCredentialVerifier()
        || !testExplicitFileLoad()) {
        return 1;
    }
    std::cout
        << "Runtime Internet service config smoke passed: "
        << "strict-schema+sha256-credential-store+explicit-file\n";
    return 0;
}
