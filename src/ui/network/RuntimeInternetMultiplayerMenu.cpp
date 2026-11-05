#include "ui/network/private/RuntimeInternetMenuEntropySource.h"
#include "ui/network/RuntimeInternetMultiplayerMenu.h"
#include "ui/network/RuntimeInternetMenuSkin.h"
#if defined(_WIN32)
#include "ui/network/win32/RuntimeInternetMenuSkinDrawingWin32.h"
#endif
#include "network/lobby/RuntimeRoomPassword.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include "ui/network/win32/RuntimeInternetMenuWidgetViewWin32.h"
#include "runtime/menu/RuntimeInternetMenuController.h"
#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#include "ui/skirmish/RuntimeSkirmishMenu.h"
#include <windows.h>
#endif

namespace {

constexpr std::size_t maximumInternetConfigBytes = 256U * 1024U;
constexpr std::size_t maximumInternetConfigLineBytes = 16U * 1024U;
constexpr std::uint16_t relayOnlyCandidatePort = 9U;

bool fail(std::string& errorMessage, std::string reason) {
    errorMessage = std::move(reason);
    return false;
}

class RoomPasswordClearGuard {
public:
    explicit RoomPasswordClearGuard(std::string& value) : value_(value) {}
    ~RoomPasswordClearGuard() { runtimeRoomPasswordSecureClear(value_); }
    RoomPasswordClearGuard(const RoomPasswordClearGuard&) = delete;
    RoomPasswordClearGuard& operator=(const RoomPasswordClearGuard&) = delete;

private:
    std::string& value_;
};

bool asciiWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r'
        || value == '\n';
}

std::string trimAscii(std::string value) {
    std::size_t first = 0U;
    while (first < value.size() && asciiWhitespace(value[first])) ++first;
    std::size_t last = value.size();
    while (last > first && asciiWhitespace(value[last - 1U])) --last;
    return value.substr(first, last - first);
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

bool protocolIdentifier(
    std::string_view value,
    std::size_t maximumBytes
) {
    if (value.empty() || value.size() > maximumBytes) return false;
    const bool valid = std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char byte) {
        const bool alphaNumeric =
            (byte >= static_cast<unsigned char>('a')
                && byte <= static_cast<unsigned char>('z'))
            || (byte >= static_cast<unsigned char>('A')
                && byte <= static_cast<unsigned char>('Z'))
            || (byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9'));
        return alphaNumeric || byte == static_cast<unsigned char>('-')
            || byte == static_cast<unsigned char>('_')
            || byte == static_cast<unsigned char>('.');
        }
    );
    return valid && std::any_of(
        value.begin(),
        value.end(),
        [](unsigned char byte) {
            return (byte >= static_cast<unsigned char>('a')
                    && byte <= static_cast<unsigned char>('z'))
                || (byte >= static_cast<unsigned char>('A')
                    && byte <= static_cast<unsigned char>('Z'))
                || (byte >= static_cast<unsigned char>('0')
                    && byte <= static_cast<unsigned char>('9'));
        }
    );
}

bool networkName(std::string_view value, bool allowUnderscore) {
    if (value.empty()
        || value.size() > runtimeInternetMaximumEndpointAddressBytes) {
        return false;
    }
    const std::size_t colonCount = static_cast<std::size_t>(std::count(
        value.begin(),
        value.end(),
        ':'
    ));
    if (colonCount == 1U) return false; // Reject an embedded :port.
    if (colonCount >= 2U) {
        return std::all_of(value.begin(), value.end(), [](unsigned char byte) {
            return (byte >= static_cast<unsigned char>('0')
                    && byte <= static_cast<unsigned char>('9'))
                || (byte >= static_cast<unsigned char>('a')
                    && byte <= static_cast<unsigned char>('f'))
                || (byte >= static_cast<unsigned char>('A')
                    && byte <= static_cast<unsigned char>('F'))
                || byte == static_cast<unsigned char>(':')
                || byte == static_cast<unsigned char>('.');
        });
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [allowUnderscore](unsigned char byte) {
            return (byte >= static_cast<unsigned char>('0')
                    && byte <= static_cast<unsigned char>('9'))
                || (byte >= static_cast<unsigned char>('a')
                    && byte <= static_cast<unsigned char>('z'))
                || (byte >= static_cast<unsigned char>('A')
                    && byte <= static_cast<unsigned char>('Z'))
                || byte == static_cast<unsigned char>('-')
                || byte == static_cast<unsigned char>('.')
                || (allowUnderscore
                    && byte == static_cast<unsigned char>('_'));
        }
    );
}

bool endpointAddress(std::string_view value) {
    return networkName(value, true);
}

bool certificateName(std::string_view value) {
    // Wildcards are deliberately rejected; Schannel must validate exactly
    // the configured reference identity against the certificate SAN/CN.
    return networkName(value, false);
}

bool displayText(
    std::string& value,
    std::size_t maximumBytes,
    std::string_view field,
    std::string& errorMessage
) {
    value = trimAscii(std::move(value));
    if (value.empty() || value.size() > maximumBytes
        || !std::all_of(
            value.begin(),
            value.end(),
            [](unsigned char byte) {
                return byte >= 0x20U && byte != 0x7fU;
            }
        )) {
        return fail(
            errorMessage,
            std::string(field)
                + " is empty, too long, or contains control bytes"
        );
    }
    return true;
}

void secureClear(std::vector<std::uint8_t>& bytes) noexcept {
    volatile std::uint8_t* pointer = bytes.data();
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        pointer[index] = 0U;
    }
    bytes.clear();
}

int hexNibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

bool decodeHex(
    std::string_view text,
    std::size_t exactBytes,
    std::size_t minimumBytes,
    std::size_t maximumBytes,
    std::vector<std::uint8_t>& bytes,
    std::string& errorMessage,
    std::string_view field
) {
    if (text.empty() || (text.size() & 1U) != 0U
        || text.size() / 2U < minimumBytes
        || text.size() / 2U > maximumBytes
        || (exactBytes != 0U && text.size() / 2U != exactBytes)) {
        return fail(
            errorMessage,
            std::string(field) + " has an invalid hexadecimal length"
        );
    }
    std::vector<std::uint8_t> candidate(text.size() / 2U, 0U);
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
    errorMessage.clear();
    return true;
}

bool parsePort(
    std::string_view text,
    std::uint16_t& port,
    std::string& errorMessage
) {
    if (text.empty() || text.size() > 5U
        || !std::all_of(text.begin(), text.end(), [](unsigned char byte) {
            return byte >= static_cast<unsigned char>('0')
                && byte <= static_cast<unsigned char>('9');
        })) {
        return fail(errorMessage, "Internet ServicePort must be 1..65535");
    }
    std::uint32_t value = 0U;
    for (char character : text) {
        value = value * 10U
            + static_cast<std::uint32_t>(character - '0');
    }
    if (value == 0U
        || value > static_cast<std::uint32_t>(
            std::numeric_limits<std::uint16_t>::max())) {
        return fail(errorMessage, "Internet ServicePort must be 1..65535");
    }
    port = static_cast<std::uint16_t>(value);
    return true;
}

std::string hexU64(std::uint64_t value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text(16U, '0');
    for (std::size_t index = 0U; index < text.size(); ++index) {
        const std::size_t shift = (text.size() - index - 1U) * 4U;
        text[index] = digits[(value >> shift) & 0x0fU];
    }
    return text;
}

std::string hexBytes(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text(bytes.size() * 2U, '0');
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        text[index * 2U] = digits[(bytes[index] >> 4U) & 0x0fU];
        text[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return text;
}

bool compatibilityReady(
    const RuntimeMultiplayerCompatibility& compatibility,
    std::string& errorMessage
) {
    if (compatibility.protocolVersion != runtimeMultiplayerProtocolVersion
        || compatibility.protocolFingerprint == 0U
        || compatibility.contentFingerprint == 0U) {
        return fail(
            errorMessage,
            "local multiplayer compatibility fingerprint is incomplete"
        );
    }
    return true;
}

bool serviceReady(
    const RuntimeInternetMultiplayerServiceConfig& service,
    std::string& errorMessage
) {
    if (!endpointAddress(service.serviceAddress)
        || service.servicePort == 0U
        || !certificateName(service.serverName)
        || !protocolIdentifier(
            service.serviceId,
            runtimeInternetMaximumIdentityBytes
        )
        || !protocolIdentifier(
            service.peerId,
            runtimeInternetMaximumIdentityBytes
        )
        || service.credential.size()
            < runtimeInternetServiceMinimumCredentialBytes
        || service.credential.size()
            > runtimeInternetServiceMaximumCredentialBytes) {
        return fail(errorMessage, "Internet service configuration is invalid");
    }
    return true;
}

bool nonzeroRoomId(
    const std::array<std::uint8_t, 16U>& roomIdBytes
) {
    return std::any_of(
        roomIdBytes.begin(),
        roomIdBytes.end(),
        [](std::uint8_t byte) { return byte != 0U; }
    );
}

bool validEntropy(
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    std::string& errorMessage
) {
    const std::array<std::uint64_t, 9U> values = {
        entropy.queryTransactionId,
        entropy.hostTransactionId,
        entropy.joinTransactionId,
        entropy.matchRequestId,
        entropy.matchActivationTransactionId,
        entropy.matchHostTransactionId,
        entropy.matchJoinTransactionId,
        entropy.roomGeneration,
        entropy.gameplayRandomSeed
    };
    if (std::any_of(values.begin(), values.end(), [](std::uint64_t value) {
            return value == 0U;
        })
        || !nonzeroRoomId(entropy.roomIdBytes)) {
        return fail(errorMessage, "secure Internet menu entropy is incomplete");
    }
    std::set<std::uint64_t> unique(values.begin(), values.end());
    if (unique.size() != values.size()) {
        return fail(errorMessage, "Internet correlation values are not unique");
    }
    return true;
}

RuntimeInternetCandidate relayOnlyCandidate() {
    RuntimeInternetCandidate candidate;
    candidate.kind = RuntimeInternetCandidateKind::Host;
    // RuntimeInternetLobbyFlow never transmits candidate datagrams. The
    // bounded placeholder satisfies the legacy coordinator schema until the
    // connection state machine requests its mandatory authenticated relay.
    candidate.endpoint = {"127.0.0.1", relayOnlyCandidatePort};
    candidate.priority = 1U;
    return candidate;
}

RuntimeInternetConnectionConfig connectionOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    RuntimeInternetRole role,
    std::string roomId,
    std::uint64_t transactionId,
    bool needsCandidate
) {
    RuntimeInternetConnectionConfig connection;
    connection.serviceId = service.serviceId;
    connection.localPeerId = service.peerId;
    connection.roomId = std::move(roomId);
    connection.role = role;
    connection.contentFingerprint =
        runtimeInternetMultiplayerContentFingerprint(compatibility);
    if (needsCandidate) {
        connection.localCandidates = {relayOnlyCandidate()};
    }
    connection.transactionId = transactionId;
    connection.retryMilliseconds = 250U;
    connection.coordinatorTimeoutMilliseconds = 10000U;
    connection.punchTimeoutMilliseconds = 1000U;
    connection.relayTimeoutMilliseconds = 10000U;
    connection.maximumRetries = 16U;
    return connection;
}

RuntimeLanRoomAdvertisement canonicalAdvertisement(
    const RuntimeInternetRoomRecord& room,
    std::string hostDisplayName
) {
    RuntimeLanRoomAdvertisement advertisement;
    advertisement.roomId = room.roomId;
    advertisement.roomName = room.roomName;
    advertisement.hostName = std::move(hostDisplayName);
    advertisement.hostAddress = "authenticated-service";
    advertisement.contentFingerprint = room.contentFingerprint;
    advertisement.protocolVersion = runtimeLanProtocolVersion;
    advertisement.sessionPort = 1U;
    advertisement.playerCount = room.playerCount;
    advertisement.maximumPlayers = room.maximumPlayers;
    advertisement.passwordProtected = room.passwordProtected;
    return advertisement;
}

bool basicRoom(
    const RuntimeInternetRoomRecord& room,
    const RuntimeMultiplayerCompatibility& compatibility,
    std::string& errorMessage
) {
    if (!protocolIdentifier(room.roomId, runtimeInternetMaximumRoomIdBytes)
        || room.roomName.empty()
        || room.roomName.size() > runtimeInternetMaximumRoomNameBytes
        || !protocolIdentifier(
            room.hostPeerId,
            runtimeInternetMaximumIdentityBytes
        )
        || room.contentFingerprint
            != runtimeInternetMultiplayerContentFingerprint(compatibility)
        || room.maximumPlayers < 2U
        || room.maximumPlayers > runtimeMultiplayerMaximumCombatants
        || room.playerCount == 0U
        || room.playerCount > room.maximumPlayers) {
        return fail(
            errorMessage,
            "selected Internet room record is incomplete or incompatible"
        );
    }
    return true;
}

const RuntimeMultiplayerMatchSlot* slotForPeer(
    const RuntimeMultiplayerMatchSetup* setup,
    std::string_view peerId
) {
    if (setup == nullptr) return nullptr;
    const auto found = std::find_if(
        setup->slots.begin(),
        setup->slots.end(),
        [peerId](const RuntimeMultiplayerMatchSlot& slot) {
            return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Human
                && slot.peerId == peerId;
        }
    );
    return found == setup->slots.end() ? nullptr : &*found;
}

} // namespace

bool parseRuntimeInternetMultiplayerServiceConfigText(
    std::string_view iniText,
    RuntimeInternetMultiplayerServiceConfig& config,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (iniText.empty() || iniText.size() > maximumInternetConfigBytes
        || iniText.find('\0') != std::string_view::npos) {
        return fail(errorMessage, "Internet configuration file is invalid");
    }

    RuntimeInternetMultiplayerServiceConfig candidate;
    std::set<std::string> seenKeys;
    bool inInternet = false;
    bool sawInternet = false;
    std::string leafHex;
    std::size_t offset = 0U;
    std::size_t lineNumber = 0U;
    while (offset <= iniText.size()) {
        const std::size_t newline = iniText.find('\n', offset);
        const std::size_t end = newline == std::string_view::npos
            ? iniText.size()
            : newline;
        if (end - offset > maximumInternetConfigLineBytes) {
            secureClear(candidate.credential);
            return fail(errorMessage, "Internet configuration line is too long");
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
                if (line.back() != ']' || line.size() < 3U) {
                    secureClear(candidate.credential);
                    return fail(
                        errorMessage,
                        "malformed Internet configuration section at line "
                            + std::to_string(lineNumber)
                    );
                }
                const std::string section = lowerAscii(trimAscii(
                    line.substr(1U, line.size() - 2U)
                ));
                inInternet = section == "internet";
                if (inInternet) {
                    if (sawInternet) {
                        secureClear(candidate.credential);
                        return fail(
                            errorMessage,
                            "Internet configuration section is duplicated"
                        );
                    }
                    sawInternet = true;
                }
            } else if (inInternet) {
                const std::size_t equals = line.find('=');
                if (equals == std::string::npos) {
                    secureClear(candidate.credential);
                    return fail(
                        errorMessage,
                        "malformed Internet configuration key at line "
                            + std::to_string(lineNumber)
                    );
                }
                const std::string key = lowerAscii(trimAscii(
                    line.substr(0U, equals)
                ));
                const std::string value = trimAscii(line.substr(equals + 1U));
                static const std::set<std::string> allowed = {
                    "serviceaddress",
                    "serviceport",
                    "servername",
                    "serviceid",
                    "peerid",
                    "credentialhex",
                    "leafcertificatesha256"
                };
                if (allowed.find(key) == allowed.end()) {
                    secureClear(candidate.credential);
                    return fail(
                        errorMessage,
                        "unknown [Internet] key at line "
                            + std::to_string(lineNumber)
                    );
                }
                if (!seenKeys.insert(key).second) {
                    secureClear(candidate.credential);
                    return fail(
                        errorMessage,
                        "duplicate [Internet] key at line "
                            + std::to_string(lineNumber)
                    );
                }
                if (key == "serviceaddress") {
                    candidate.serviceAddress = value;
                } else if (key == "serviceport") {
                    if (!parsePort(value, candidate.servicePort, errorMessage)) {
                        secureClear(candidate.credential);
                        return false;
                    }
                } else if (key == "servername") {
                    candidate.serverName = value;
                } else if (key == "serviceid") {
                    candidate.serviceId = value;
                } else if (key == "peerid") {
                    candidate.peerId = value;
                } else if (key == "credentialhex") {
                    if (!decodeHex(
                            value,
                            0U,
                            runtimeInternetServiceMinimumCredentialBytes,
                            runtimeInternetServiceMaximumCredentialBytes,
                            candidate.credential,
                            errorMessage,
                            "Internet CredentialHex"
                        )) {
                        return false;
                    }
                } else {
                    leafHex = value;
                }
            }
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1U;
    }

    static const std::array<const char*, 6U> required = {
        "serviceaddress",
        "serviceport",
        "servername",
        "serviceid",
        "peerid",
        "credentialhex"
    };
    if (!sawInternet
        || std::any_of(required.begin(), required.end(), [&](const char* key) {
            return seenKeys.find(key) == seenKeys.end();
        })) {
        secureClear(candidate.credential);
        return fail(
            errorMessage,
            "[Internet] is missing one or more required service keys"
        );
    }
    if (!serviceReady(candidate, errorMessage)) {
        secureClear(candidate.credential);
        return false;
    }
    if (!leafHex.empty()) {
        std::vector<std::uint8_t> leafBytes;
        if (!decodeHex(
                leafHex,
                runtimeInternetTlsSha256Bytes,
                runtimeInternetTlsSha256Bytes,
                runtimeInternetTlsSha256Bytes,
                leafBytes,
                errorMessage,
                "Internet LeafCertificateSha256"
            )) {
            secureClear(candidate.credential);
            return false;
        }
        RuntimeInternetTlsSha256 leaf{};
        std::copy(leafBytes.begin(), leafBytes.end(), leaf.begin());
        secureClear(leafBytes);
        candidate.leafCertificateSha256 = leaf;
    } else if (seenKeys.find("leafcertificatesha256") != seenKeys.end()) {
        secureClear(candidate.credential);
        return fail(
            errorMessage,
            "Internet LeafCertificateSha256 may not be empty"
        );
    }

    config = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool loadRuntimeInternetMultiplayerServiceConfig(
    const std::filesystem::path& path,
    RuntimeInternetMultiplayerServiceConfig& config,
    std::string& errorMessage
) {
    errorMessage.clear();
    std::error_code fileError;
    if (path.empty() || !std::filesystem::is_regular_file(path, fileError)) {
        return fail(errorMessage, "Internet configuration file is missing");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, fileError);
    if (fileError || size == 0U || size > maximumInternetConfigBytes
        || size > static_cast<std::uintmax_t>(
            std::numeric_limits<std::streamsize>::max())) {
        return fail(errorMessage, "Internet configuration file size is invalid");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return fail(errorMessage, "Internet configuration file could not be opened");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.gcount()
            != static_cast<std::streamsize>(bytes.size())) {
        return fail(errorMessage, "Internet configuration file could not be read");
    }
    return parseRuntimeInternetMultiplayerServiceConfigText(
        bytes,
        config,
        errorMessage
    );
}

bool makeRuntimeInternetMultiplayerTransportConfig(
    const RuntimeInternetMultiplayerServiceConfig& service,
    RuntimeInternetServiceTransportConfig& transport,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!serviceReady(service, errorMessage)) return false;
    RuntimeInternetServiceTransportConfig candidate;
    candidate.serviceId = service.serviceId;
    candidate.peerId = service.peerId;
    candidate.credential = service.credential;
    candidate.coordinatorTls.endpoint = {
        service.serviceAddress,
        service.servicePort
    };
    candidate.coordinatorTls.serverName = service.serverName;
    candidate.coordinatorTls.leafCertificateSha256 =
        service.leafCertificateSha256;
    candidate.coordinatorTls.checkCertificateRevocation = true;
    candidate.coordinatorTls.peerTrust = {};
    candidate.relayTls.endpoint = candidate.coordinatorTls.endpoint;
    candidate.relayTls.serverName = service.serverName;
    candidate.relayTls.leafCertificateSha256 =
        service.leafCertificateSha256;
    candidate.relayTls.checkCertificateRevocation = true;
    candidate.relayTls.peerTrust = {};
    transport = std::move(candidate);
    errorMessage.clear();
    return true;
}

namespace {
class RuntimeInternetSystemEntropySource final
    : public runtime_internet_menu_entropy_detail::Source {
public:
    RuntimeSecureBytesResult fill(std::span<std::uint8_t> output) noexcept override {
        return fillRuntimeSecureBytes(output);
    }
};
} // namespace

bool runtime_internet_menu_entropy_detail::generate(
    RuntimeInternetMultiplayerMenuEntropy& entropy,
    std::string& errorMessage,
    Source& source
) {
    errorMessage.clear();
    constexpr std::size_t valueCount = 9U;
    constexpr std::size_t byteCount = valueCount * sizeof(std::uint64_t) + 16U;
    std::array<std::uint8_t, byteCount> bytes{};
    for (int attempt = 0; attempt < 8; ++attempt) {
        const RuntimeSecureBytesResult result = source.fill(bytes);
        if (!result.succeeded() || result.generated != bytes.size()) {
            std::fill(bytes.begin(), bytes.end(), static_cast<std::uint8_t>(0U));
            return fail(errorMessage, "Windows secure random generation failed");
        }
        std::array<std::uint64_t, valueCount> values{};
        for (std::size_t valueIndex = 0U; valueIndex < values.size();
             ++valueIndex) {
            std::uint64_t value = 0U;
            for (std::size_t byteIndex = 0U;
                 byteIndex < sizeof(std::uint64_t);
                 ++byteIndex) {
                value = (value << 8U) | bytes[
                    valueIndex * sizeof(std::uint64_t) + byteIndex
                ];
            }
            values[valueIndex] = value;
        }
        RuntimeInternetMultiplayerMenuEntropy candidate;
        candidate.queryTransactionId = values[0];
        candidate.hostTransactionId = values[1];
        candidate.joinTransactionId = values[2];
        candidate.matchRequestId = values[3];
        candidate.matchActivationTransactionId = values[4];
        candidate.matchHostTransactionId = values[5];
        candidate.matchJoinTransactionId = values[6];
        candidate.roomGeneration = values[7];
        candidate.gameplayRandomSeed = values[8];
        std::copy(
            bytes.begin() + static_cast<std::ptrdiff_t>(
                valueCount * sizeof(std::uint64_t)
            ),
            bytes.end(),
            candidate.roomIdBytes.begin()
        );
        std::string validationError;
        if (validEntropy(candidate, validationError)) {
            std::fill(
                bytes.begin(),
                bytes.end(),
                static_cast<std::uint8_t>(0U)
            );
            entropy = candidate;
            errorMessage.clear();
            return true;
        }
    }
    std::fill(
        bytes.begin(),
        bytes.end(),
        static_cast<std::uint8_t>(0U)
    );
    return fail(
        errorMessage,
        "Windows secure random generator returned unusable correlation values"
    );
}

bool generateRuntimeInternetMultiplayerMenuEntropy(
    RuntimeInternetMultiplayerMenuEntropy& entropy,
    std::string& errorMessage
) {
#if !defined(_WIN32)
    errorMessage.clear();
    (void)entropy;
    return fail(errorMessage, "Internet menu secure entropy requires Windows BCrypt");
#else
    RuntimeInternetSystemEntropySource source;
    return runtime_internet_menu_entropy_detail::generate(entropy, errorMessage, source);
#endif
}

std::string runtimeInternetMultiplayerContentFingerprint(
    const RuntimeMultiplayerCompatibility& compatibility
) {
    return hexU64(compatibility.contentFingerprint);
}

bool makeRuntimeInternetMultiplayerQueryOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    std::uint64_t transactionId,
    RuntimeInternetLobbyQueryOptions& options,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!serviceReady(service, errorMessage)
        || !compatibilityReady(compatibility, errorMessage)
        || transactionId == 0U) {
        if (errorMessage.empty()) {
            errorMessage = "Internet room query transaction is invalid";
        }
        return false;
    }
    RuntimeInternetLobbyQueryOptions candidate;
    candidate.connection = connectionOptions(
        service,
        compatibility,
        RuntimeInternetRole::Client,
        {},
        transactionId,
        false
    );
    options = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool makeRuntimeInternetMultiplayerHostOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeMultiplayerHostSelectionProjection& selection,
    std::string roomName,
    std::string password,
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    RuntimeInternetLobbyHostOptions& options,
    std::string& errorMessage,
    std::shared_ptr<const RuntimeMultiplayerResumeSave> resumeSave
) {
    RoomPasswordClearGuard passwordClear(password);
    errorMessage.clear();
    if (!serviceReady(service, errorMessage)
        || !compatibilityReady(compatibility, errorMessage)
        || !validEntropy(entropy, errorMessage)) {
        return false;
    }
    if (!displayText(
            roomName,
            runtimeInternetMaximumRoomNameBytes,
            "Internet room name",
            errorMessage
        )) {
        return false;
    }
    if (!password.empty() && !runtimeRoomPasswordValid(password)) {
        return fail(
            errorMessage,
            "Internet room password exceeds 128 bytes"
        );
    }
    if (selection.slots.size() < 2U
        || selection.slots.size() > runtimeMultiplayerMatchMaximumSlots
        || selection.localMapPath.empty()
        || selection.lobby.hostMember.peerId != service.peerId
        || selection.lobby.hostMember.displayName != service.peerId
        || (!resumeSave && selection.lobby.hostMember.participation
            != RuntimeMultiplayerParticipationRole::Combatant)) {
        return fail(
            errorMessage,
            "Internet host selection does not bind the directory host identity"
        );
    }

    RuntimeInternetLobbyHostOptions candidate;
    candidate.room.roomId = "room-" + hexBytes(entropy.roomIdBytes);
    candidate.room.roomName = std::move(roomName);
    candidate.room.hostPeerId = service.peerId;
    candidate.room.contentFingerprint =
        runtimeInternetMultiplayerContentFingerprint(compatibility);
    candidate.room.playerCount = selection.lobby.hostMember.participation == RuntimeMultiplayerParticipationRole::Combatant ? 1U : 0U;
    const std::size_t computerSlots = static_cast<std::size_t>(
        std::count_if(
            selection.slots.begin(),
            selection.slots.end(),
            [](const RuntimeMultiplayerMatchSlot& slot) {
                return slot.controller
                    == RuntimeMultiplayerMatchSlotController::Computer;
            }
        )
    );
    candidate.room.maximumPlayers = static_cast<std::uint8_t>(
        resumeSave ? std::count_if(resumeSave->setup.slots.begin(), resumeSave->setup.slots.end(),
            [](const auto& slot) { return slot.controller == RuntimeMultiplayerMatchSlotController::Human; })
        : selection.slots.size() - computerSlots
    );
    candidate.room.passwordProtected = !password.empty();
    if (candidate.room.passwordProtected) {
        RuntimeRoomPasswordVerifier verifier = runtimeRoomPasswordVerifier(
            candidate.room.roomId,
            password
        );
        candidate.roomPasswordVerifier.assign(
            verifier.begin(),
            verifier.end()
        );
        runtimeRoomPasswordSecureClear(verifier);
    }
    candidate.connection = connectionOptions(
        service,
        compatibility,
        RuntimeInternetRole::Host,
        candidate.room.roomId,
        entropy.hostTransactionId,
        true
    );
    candidate.network.session.roomId = candidate.room.roomId;
    candidate.network.session.hostPeerId = service.peerId;
    candidate.network.session.hostDisplayName = service.peerId;
    candidate.network.session.hostParticipation =
        selection.lobby.hostMember.participation;
    candidate.network.session.compatibility = compatibility;
    candidate.network.session.maximumMembers =
        runtimeMultiplayerMaximumMembers;
    candidate.network.maximumCombatants = candidate.room.maximumPlayers;
    candidate.network.maximumSpectators =
        runtimeMultiplayerMaximumSpectators;
    candidate.network.roomName = candidate.room.roomName;
    candidate.network.passwordProtected = false;
    candidate.network.enableDiscovery = false;
    candidate.network.discoveryPort = 0U;
    candidate.network.sessionPort = 0U;
    candidate.network.sessionBindScope = RuntimeTcpBindScope::LoopbackIpv4;
    candidate.network.roomGeneration = entropy.roomGeneration;
    candidate.lobby = selection.lobby;
    candidate.resumeSave = std::move(resumeSave);
    candidate.proxy.connectTimeoutMilliseconds = 10000;
    // A valid lobby or running match may legitimately have no gameplay bytes
    // for an arbitrary period. Liveness is owned by the authenticated
    // coordinator heartbeat; zero disables byte-idle teardown only.
    candidate.proxy.idleTimeoutMilliseconds = 0;
    options = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool makeRuntimeInternetMultiplayerJoinOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeInternetRoomRecord& room,
    std::string displayName,
    std::string password,
    bool spectator,
    std::uint64_t transactionId,
    RuntimeInternetLobbyJoinOptions& options,
    std::string& errorMessage
) {
    RoomPasswordClearGuard passwordClear(password);
    errorMessage.clear();
    if (!serviceReady(service, errorMessage)
        || !compatibilityReady(compatibility, errorMessage)
        || !basicRoom(room, compatibility, errorMessage)) {
        return false;
    }
    if (room.passwordProtected
        && !runtimeRoomPasswordValid(password)) {
        return fail(
            errorMessage,
            "selected Internet room requires a password of at most 128 bytes"
        );
    }
    if (!room.passwordProtected) runtimeRoomPasswordSecureClear(password);
    if (!spectator && room.playerCount >= room.maximumPlayers) {
        return fail(errorMessage, "selected Internet room is full");
    }
    if (transactionId == 0U
        || !displayText(
            displayName,
            runtimeMultiplayerMaximumDisplayNameBytes,
            "Internet player name",
            errorMessage
        )) {
        if (errorMessage.empty()) {
            errorMessage = "Internet join transaction is invalid";
        }
        return false;
    }
    RuntimeInternetLobbyJoinOptions candidate;
    const RuntimeInternetRole role = spectator
        ? RuntimeInternetRole::Spectator
        : RuntimeInternetRole::Client;
    candidate.connection = connectionOptions(
        service,
        compatibility,
        role,
        room.roomId,
        transactionId,
        true
    );
    candidate.room = room;
    // Public directory rooms deliberately bind the LAN host display identity
    // to its authenticated peer id, because that canonical field is not
    // duplicated in RuntimeInternetRoomRecord.
    candidate.canonicalRoom = canonicalAdvertisement(room, room.hostPeerId);
    candidate.network.room = candidate.canonicalRoom;
    candidate.network.peerId = service.peerId;
    candidate.network.displayName = std::move(displayName);
    candidate.network.participation = spectator
        ? RuntimeMultiplayerParticipationRole::Spectator
        : RuntimeMultiplayerParticipationRole::Combatant;
    candidate.network.compatibility = compatibility;
    candidate.network.connectTimeoutMilliseconds = 10000;
    if (room.passwordProtected) {
        RuntimeRoomPasswordVerifier verifier = runtimeRoomPasswordVerifier(
            room.roomId,
            password
        );
        RuntimeRoomPasswordProof proof =
            runtimeInternetRoomPasswordJoinProof(
                verifier,
                room.roomId,
                service.peerId,
                transactionId
            );
        candidate.roomAdmissionProof.assign(proof.begin(), proof.end());
        runtimeRoomPasswordSecureClear(verifier);
        runtimeRoomPasswordSecureClear(proof);
    }
    candidate.proxy.connectTimeoutMilliseconds = 10000;
    candidate.proxy.idleTimeoutMilliseconds = 0;
    options = std::move(candidate);
    errorMessage.clear();
    return true;
}

bool makeRuntimeInternetMultiplayerMatchmakingOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeMultiplayerHostSelectionProjection& selection,
    std::string displayName,
    std::int32_t skillRating,
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    RuntimeInternetLobbyMatchmakingOptions& options,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!serviceReady(service, errorMessage)
        || !compatibilityReady(compatibility, errorMessage)
        || !validEntropy(entropy, errorMessage)
        || !displayText(
            displayName,
            runtimeInternetMatchmakingMaximumDisplayNameBytes,
            "Quick Match display name",
            errorMessage
        )) {
        return false;
    }
    if (selection.slots.size() < 2U
        || selection.slots.size() > runtimeMultiplayerMatchMaximumSlots
        || selection.localMapPath.empty()
        || selection.lobby.hostMember.peerId != service.peerId
        || selection.lobby.hostMember.displayName != displayName) {
        return fail(
            errorMessage,
            "Quick Match selection does not bind the authenticated player"
        );
    }

    RuntimeInternetLobbyMatchmakingOptions candidate;
    candidate.matchmakingClient.serviceId = service.serviceId;
    candidate.matchmakingClient.localPeerId = service.peerId;
    candidate.matchmakingClient.heartbeatMilliseconds = 5000U;
    candidate.request.requestId = entropy.matchRequestId;
    candidate.request.queueId = "quick-two-player";
    candidate.request.regionId = "global";
    candidate.request.contentFingerprint =
        runtimeInternetMultiplayerContentFingerprint(compatibility);
    candidate.request.displayName = displayName;
    candidate.request.playerCount = 2U;
    candidate.request.skillRating = skillRating;
    candidate.activationTransactionId =
        entropy.matchActivationTransactionId;

    RuntimeInternetRoomRecord templateRoom;
    templateRoom.roomId = "room-" + hexBytes(entropy.roomIdBytes);
    templateRoom.roomName = "Quick Match";
    templateRoom.hostPeerId = service.peerId;
    templateRoom.contentFingerprint = candidate.request.contentFingerprint;
    templateRoom.playerCount = 1U;
    templateRoom.maximumPlayers = 2U;
    templateRoom.passwordProtected = false;

    candidate.hostTemplate.connection = connectionOptions(
        service,
        compatibility,
        RuntimeInternetRole::Host,
        templateRoom.roomId,
        entropy.matchHostTransactionId,
        true
    );
    candidate.hostTemplate.room = templateRoom;
    candidate.hostTemplate.network.session.roomId = templateRoom.roomId;
    candidate.hostTemplate.network.session.hostPeerId = service.peerId;
    candidate.hostTemplate.network.session.hostDisplayName = displayName;
    candidate.hostTemplate.network.session.hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    candidate.hostTemplate.network.session.compatibility = compatibility;
    candidate.hostTemplate.network.session.maximumMembers = 2U;
    candidate.hostTemplate.network.roomName = templateRoom.roomName;
    candidate.hostTemplate.network.passwordProtected = false;
    candidate.hostTemplate.network.enableDiscovery = false;
    candidate.hostTemplate.network.discoveryPort = 0U;
    candidate.hostTemplate.network.sessionPort = 0U;
    candidate.hostTemplate.network.sessionBindScope =
        RuntimeTcpBindScope::LoopbackIpv4;
    candidate.hostTemplate.network.roomGeneration = entropy.roomGeneration;
    candidate.hostTemplate.lobby = selection.lobby;
    candidate.hostTemplate.proxy.connectTimeoutMilliseconds = 10000;
    candidate.hostTemplate.proxy.idleTimeoutMilliseconds = 0;

    candidate.joinTemplate.connection = connectionOptions(
        service,
        compatibility,
        RuntimeInternetRole::Client,
        templateRoom.roomId,
        entropy.matchJoinTransactionId,
        true
    );
    candidate.joinTemplate.room = templateRoom;
    candidate.joinTemplate.canonicalRoom = canonicalAdvertisement(
        templateRoom,
        displayName
    );
    candidate.joinTemplate.network.room =
        candidate.joinTemplate.canonicalRoom;
    candidate.joinTemplate.network.peerId = service.peerId;
    candidate.joinTemplate.network.displayName = std::move(displayName);
    candidate.joinTemplate.network.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    candidate.joinTemplate.network.compatibility = compatibility;
    candidate.joinTemplate.network.connectTimeoutMilliseconds = 10000;
    candidate.joinTemplate.proxy.connectTimeoutMilliseconds = 10000;
    candidate.joinTemplate.proxy.idleTimeoutMilliseconds = 0;
    options = std::move(candidate);
    errorMessage.clear();
    return true;
}

std::vector<RuntimeInternetMultiplayerRoomRow>
projectRuntimeInternetMultiplayerRoomRows(
    std::span<const RuntimeInternetRoomRecord> rooms,
    bool joiningAsSpectator
) {
    std::vector<RuntimeInternetMultiplayerRoomRow> rows;
    rows.reserve(rooms.size());
    for (const RuntimeInternetRoomRecord& room : rooms) {
        RuntimeInternetMultiplayerRoomRow row;
        row.roomId = room.roomId;
        row.joinable = joiningAsSpectator
            || room.playerCount < room.maximumPlayers;
        std::ostringstream text;
        text << room.roomName << "  ["
             << static_cast<unsigned int>(room.playerCount) << '/'
             << static_cast<unsigned int>(room.maximumPlayers) << "]  Host "
             << room.hostPeerId;
        if (room.passwordProtected) {
            text << "  [Password required]";
        }
        if (room.playerCount >= room.maximumPlayers) {
            text << (joiningAsSpectator
                ? "  [Combatants full; spectator join available]"
                : "  [Combatants full]");
        }
        row.text = text.str();
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<RuntimeInternetMultiplayerRosterRow>
projectRuntimeInternetMultiplayerRosterRows(
    std::span<const RuntimeMultiplayerRoomMember> members,
    const RuntimeMultiplayerMatchSetup* setup,
    std::string_view localPeerId,
    bool localPeerIsHost
) {
    std::vector<RuntimeInternetMultiplayerRosterRow> rows;
    rows.reserve(members.size());
    for (const RuntimeMultiplayerRoomMember& member : members) {
        RuntimeInternetMultiplayerRosterRow row;
        row.peerId = member.peerId;
        row.local = member.peerId == localPeerId;
        row.kickable = localPeerIsHost && !row.local;
        std::ostringstream text;
        text << member.displayName;
        if (member.role == RuntimeMultiplayerRoomMemberRole::Host) {
            text << "  [Host]";
        } else {
            text << "  [Client]";
        }
        if (member.participation
            == RuntimeMultiplayerParticipationRole::Spectator) {
            text << " [Spectator]";
        } else {
            text << " [Combatant]";
        }
        text << (member.ready ? " [Ready]" : " [Not ready]");
        if (const RuntimeMultiplayerMatchSlot* slot = slotForPeer(
                setup,
                member.peerId
            )) {
            text << "  " << slot->countryId << " / " << slot->colorId
                 << " / Team " << slot->teamId << " / "
                 << slot->difficultyId << " / Start "
                 << static_cast<unsigned int>(slot->startPosition);
        }
        row.text = text.str();
        rows.push_back(std::move(row));
    }
    return rows;
}

std::string projectRuntimeInternetMultiplayerConnectionStatus(
    bool coordinatorAuthenticated,
    RuntimeInternetLobbyFlowMode mode,
    RuntimeInternetConnectionState connectionState
) {
    if (!coordinatorAuthenticated) {
        return "Connecting to the authenticated TLS service...";
    }
    switch (mode) {
    case RuntimeInternetLobbyFlowMode::Matchmaking:
        return "TLS service connected. Searching Quick Match...";
    case RuntimeInternetLobbyFlowMode::QueryingRooms:
        return "TLS service connected. Refreshing formal room list...";
    case RuntimeInternetLobbyFlowMode::RoomListReady:
        return "TLS service connected. Room list is current.";
    case RuntimeInternetLobbyFlowMode::Hosting:
        return "TLS service connected. Registering the hosted room...";
    case RuntimeInternetLobbyFlowMode::Joining:
        return "TLS service connected. Opening an authenticated TCP relay...";
    case RuntimeInternetLobbyFlowMode::Lobby:
        return connectionState
                == RuntimeInternetConnectionState::GameplaySessionConnected
            ? "Authenticated TCP relay connected. Canonical lobby is active."
            : "TLS service connected. Synchronizing the canonical lobby...";
    case RuntimeInternetLobbyFlowMode::Running:
        return "Verified Internet match is starting.";
    case RuntimeInternetLobbyFlowMode::Failed:
        return "Internet service or lobby connection failed.";
    case RuntimeInternetLobbyFlowMode::Closed:
        return "Internet service connection is closed.";
    case RuntimeInternetLobbyFlowMode::Idle:
    default:
        return "TLS service connected.";
    }
}

std::string projectRuntimeInternetCommunityProfileSummary(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    if (!snapshot.registeredProfile) {
        return "No Westwood Online profile is registered for this authenticated account.";
    }
    std::ostringstream text;
    text << snapshot.profile.displayName
         << "\r\nWINS " << snapshot.profile.wins
         << "   LOSSES " << snapshot.profile.losses
         << "   DISCONNECTS " << snapshot.profile.disconnects
         << "   RANK ";
    if (snapshot.profile.rank == 0U) text << "--";
    else text << '#' << snapshot.profile.rank;
    text << "   POINTS " << snapshot.profile.points;
    return text.str();
}

std::vector<std::string> projectRuntimeInternetCommunityChannelRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.channels.size());
    for (const RuntimeInternetCommunityChannel& channel : snapshot.channels) {
        std::ostringstream text;
        text << (channel.channelId == snapshot.selectedChannelId ? "> " : "  ")
             << channel.title << "  [" << channel.participantCount << "]";
        rows.push_back(text.str());
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityChatRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.chatHistory.size());
    for (const RuntimeInternetCommunityChatMessage& message
         : snapshot.chatHistory) {
        rows.push_back(message.senderDisplayName + ": " + message.text);
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityParticipantRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.participants.size());
    for (const RuntimeInternetCommunityProfile& profile
         : snapshot.participants) {
        std::ostringstream text;
        text << profile.displayName << "  [#" << profile.rank
             << ", " << profile.points << " pts]";
        rows.push_back(text.str());
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityFriendRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.friends.size());
    for (const RuntimeInternetCommunityFriend& buddy : snapshot.friends) {
        std::ostringstream text;
        text << buddy.displayName << (buddy.online ? "  [Online]" : "  [Offline]")
             << "  #" << buddy.rank << "  " << buddy.points << " pts";
        rows.push_back(text.str());
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityLadderRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.ladder.size());
    for (const RuntimeInternetCommunityLadderEntry& entry : snapshot.ladder) {
        std::ostringstream text;
        text << '#' << entry.rank << "  " << entry.displayName
             << "  " << entry.points << " pts"
             << "  W " << entry.wins << " / L " << entry.losses
             << " / D " << entry.disconnects
             << (entry.online ? "  [Online]" : "");
        rows.push_back(text.str());
    }
    return rows;
}

std::string projectRuntimeInternetCommunityClanSummary(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    if (!snapshot.clanMember) {
        return snapshot.clanInvitations.empty()
            ? "No Clan membership. Create a Clan or wait for an invitation."
            : "No Clan membership. Pending invitations: "
                + std::to_string(snapshot.clanInvitations.size());
    }
    const RuntimeInternetCommunityClan& clan = snapshot.clan;
    return "[" + clan.tag + "] " + clan.name
        + " | rank #" + std::to_string(clan.rank)
        + " | W " + std::to_string(clan.wins)
        + " / L " + std::to_string(clan.losses)
        + " | " + std::to_string(clan.points) + " pts"
        + " | members " + std::to_string(clan.memberCount);
}

std::vector<std::string> projectRuntimeInternetCommunityClanMemberRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.clanMembers.size());
    for (const RuntimeInternetCommunityClanMember& member
        : snapshot.clanMembers) {
        rows.push_back(
            std::string(member.owner ? "Owner | " : "Member | ")
            + member.displayName
            + " | W " + std::to_string(member.wins)
            + " / L " + std::to_string(member.losses)
            + " | " + std::to_string(member.points) + " pts | "
            + (member.online ? "Online" : "Offline")
        );
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityClanInvitationRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.clanInvitations.size());
    for (const RuntimeInternetCommunityClan& clan
        : snapshot.clanInvitations) {
        rows.push_back(
            "[" + clan.tag + "] " + clan.name
            + " | rank #" + std::to_string(clan.rank)
            + " | members " + std::to_string(clan.memberCount)
        );
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityClanLadderRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.clanLadder.size());
    for (const RuntimeInternetCommunityClan& clan : snapshot.clanLadder) {
        rows.push_back(
            "#" + std::to_string(clan.rank)
            + " [" + clan.tag + "] " + clan.name
            + " | W " + std::to_string(clan.wins)
            + " / L " + std::to_string(clan.losses)
            + " | " + std::to_string(clan.points) + " pts"
            + " | " + std::to_string(clan.memberCount) + " members"
        );
    }
    return rows;
}

std::vector<std::string> projectRuntimeInternetCommunityNewsRows(
    const RuntimeInternetCommunitySnapshot& snapshot
) {
    std::vector<std::string> rows;
    rows.reserve(snapshot.news.size());
    for (const RuntimeInternetCommunityNewsItem& item : snapshot.news) {
        rows.push_back(item.title);
    }
    return rows;
}

#include "ui/network/RuntimeInternetRankedPresentation.inl"

#if defined(_WIN32) \
    && !defined(RUNTIME_INTERNET_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
#include "runtime/host/RuntimeUiClockWin32.h"
#include "ui/network/diagnostics/RuntimeInternetMenuClockContract.h"
namespace {
using namespace concord::internet_menu;





constexpr UINT_PTR internetMenuPollTimer = 1U;
constexpr UINT internetMenuPollMilliseconds = 50U;

struct RuntimeInternetMenuLobbyBorrow final : concord::lan_menu::RuntimeLanMenuFlow {
    RuntimeMultiplayerLobbyFlow* backend = nullptr;
    bool startHost(RuntimeMultiplayerLanHostOptions&& options, const RuntimeMultiplayerLobbyHostCreateOptions& lobby, std::string& error, const RuntimeMultiplayerResumeSave* resume)  override { return backend->startHost(std::move(options), lobby, error, resume); }
    bool startBrowsing(std::uint16_t port, std::string& error)  override { return backend->startBrowsing(port, error); }
    bool join(RuntimeMultiplayerLanJoinOptions&& options, std::string& error)  override { return backend->join(std::move(options), error); }
    bool sendDiscoveryProbe(const std::string& address, std::string& error)  override { return backend->sendDiscoveryProbe(address, error); }
    std::vector<RuntimeLanRoomAdvertisement> discoveredRooms(std::uint64_t age) const override { return backend->discoveredRooms(age); }
    RuntimeMultiplayerLobbyFlowPollResult poll(int timeout)  override { return backend->poll(timeout); }
    RuntimeMultiplayerLobbyEditResult setSlot(std::size_t index, const RuntimeMultiplayerMatchSlot& slot, std::string& error)  override { return backend->setSlot(index, slot, error); }
    RuntimeMultiplayerLobbyEditResult setSlotController(std::size_t index, RuntimeMultiplayerMatchSlotController kind, std::string_view peer, std::string& error)  override { return backend->setSlotController(index, kind, peer, error); }
    RuntimeMultiplayerLobbyEditResult setSlotOptions(std::size_t index, std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position, std::string& error)  override { return backend->setSlotOptions(index, std::move(country), std::move(color), std::move(team), std::move(difficulty), position, error); }
    RuntimeMultiplayerLanOperationResult requestLocalSlotOptions(std::string&& country, std::string&& color, std::string&& team, std::string&& difficulty, std::uint8_t position)  override { return backend->requestLocalSlotOptions(std::move(country), std::move(color), std::move(team), std::move(difficulty), position); }
    RuntimeMultiplayerLanOperationResult kickPeer(const std::string& peer, std::string reason)  override { return backend->kickPeer(peer, std::move(reason)); }
    bool verifyLocalMap(const std::filesystem::path& path, std::string& error)  override { return backend->verifyLocalMap(path, error); }
    bool localMapVerified() const override { return backend->localMapVerified(); }
    bool downloadMissingMap(const std::filesystem::path& root, std::filesystem::path& output, std::string& error)  override { return backend->downloadMissingMap(root, output, error); }
    bool hostAuthoritative() const override { return backend->hostAuthoritative(); }
    bool canonicalSetupPublished() const override { return backend->canonicalSetupPublished(); }
    bool resuming() const override { return backend->resuming(); }
    bool resumeReady(std::string& error) const override { return backend->resumeReady(error); }
    bool canStart(std::string& error) const override { return backend->canStart(error); }
    bool initialized() const override { return backend->initialized(); }
    RuntimeMultiplayerLanOperationResult setReady(bool value)  override { return backend->setReady(value); }
    RuntimeMultiplayerLanOperationResult startMatch(int tick)  override { return backend->startMatch(tick); }
    void close()  override { backend->close(); }
    const RuntimeMultiplayerLobbyModel& lobby() const override { return backend->lobby(); }
    bool bridgePresent() const override { return backend->bridge() != nullptr; }
    bool sessionActive() const override { return backend->bridge()->lan().session().active(); }
    const std::string& localPeerId() const override { return backend->bridge()->lan().localPeerId(); }
    const std::vector<RuntimeMultiplayerRoomMember>& members() const override { return backend->bridge()->lan().session().members(); }
    RuntimeMultiplayerLanRole lanRole() const override { return backend->bridge()->lan().role(); }
    RuntimeMultiplayerLanPhase lanPhase() const override { return backend->bridge()->lan().phase(); }
    void leave() override { backend->bridge()->lan().leave(); }
};

struct RuntimeInternetMultiplayerMenuUiState : RuntimeInternetMenuFlow, RuntimeInternetMenuView, RuntimeInternetMenuController, RuntimeInternetMenuWidgetViewWin32 {
    RuntimeInternetMultiplayerMenuUiState() : RuntimeInternetMenuController(static_cast<RuntimeInternetMenuFlow&>(*this),static_cast<RuntimeInternetMenuView&>(*this)) {}
    HWND widgetHandle(RuntimeInternetMenuWidget) const;
    mutable RuntimeInternetMenuLobbyBorrow lobbyBorrow;
    bool beginRoomQuery(RuntimeInternetLobbyQueryOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override;
    bool beginMatchmaking(RuntimeInternetLobbyMatchmakingOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override;
    bool startHost(RuntimeInternetLobbyHostOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override;
    bool join(RuntimeInternetLobbyJoinOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) override;
    bool requestCommunityLogin(std::uint64_t transactionId, std::string& errorMessage) override;
    bool requestCommunityRegistration(std::uint64_t transactionId, const std::string& displayName, std::string& errorMessage) override;
    bool requestCommunityRefresh(std::uint64_t transactionId, std::string& errorMessage) override;
    bool requestCommunityChannelJoin(std::uint64_t transactionId, const std::string& channelId, std::string& errorMessage) override;
    bool requestCommunityChannelMessage(std::uint64_t transactionId, const std::string& text, std::string& errorMessage) override;
    bool requestCommunityFriendAdd(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) override;
    bool requestCommunityFriendRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override;
    bool requestCommunityClanCreate(std::uint64_t transactionId, const std::string& clanName, const std::string& clanTag, std::string& errorMessage) override;
    bool requestCommunityClanInvite(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) override;
    bool requestCommunityClanInvitationAccept(std::uint64_t transactionId, const std::string& clanId, std::string& errorMessage) override;
    bool requestCommunityClanMemberRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override;
    bool requestCommunityClanLeave(std::uint64_t transactionId, std::string& errorMessage) override;
    bool requestCommunityClanOwnershipTransfer(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) override;
    RuntimeInternetLobbyFlowPollResult poll(std::uint64_t nowMilliseconds, int transportTimeoutMilliseconds) override;
    RuntimeInternetLobbyFlowMode mode() const override;
    const std::vector<RuntimeInternetRoomRecord>& rooms() const override;
    RuntimeInternetConnectionState primaryConnectionState() const override;
    const RuntimeInternetCommunitySnapshot& communitySnapshot() const override;
    const std::vector<RuntimeInternetRankedRecord>& communityMatchHistory() const override;
    void close() noexcept override;
    bool present() const override;
    bool transportPresent() const override;
    bool coordinatorAuthenticated() const override;
    concord::lan_menu::RuntimeLanMenuFlow* lobbyFlow() const override;
    void showWidget(RuntimeInternetMenuWidget,bool) override;
    void enableWidget(RuntimeInternetMenuWidget,bool) override;
    std::string text(RuntimeInternetMenuWidget) const override;
    void setText(RuntimeInternetMenuWidget,std::string_view) override;
    bool hasWidget(RuntimeInternetMenuWidget) const override;
    bool checked(RuntimeInternetMenuWidget) const override;
    void clearText(RuntimeInternetMenuWidget) override;
    void listClear(RuntimeInternetMenuWidget) override;
    void listAdd(RuntimeInternetMenuWidget,const std::string&) override;
    void replaceRows(RuntimeInternetMenuWidget,std::span<const std::string>) override;
    std::int64_t listSelected(RuntimeInternetMenuWidget) const override;
    void listSelect(RuntimeInternetMenuWidget,std::int64_t) override;
    void listTop(RuntimeInternetMenuWidget,std::size_t) override;
    void readyLabel(bool) override;
    void leaveLabel(RuntimeInternetLeaveLabel) override;
    void invalidateSkinned() override;
    void invalidateSkin() override;
    void destroyWindow() override;
    bool accepted() const override;
    const std::string& peerId() const override;
    std::uint64_t nowMs() const override;
    void clearCredentials() override;
    bool entropy(RuntimeInternetMultiplayerMenuEntropy&,std::string&) override;
    std::string trim(std::string&&) const override;
    bool validateDisplayText(std::string&,std::size_t,std::string_view,std::string&) override;
    const RuntimeMultiplayerMatchSlot* slotForPeer(const RuntimeMultiplayerMatchSetup*,std::string_view) const override;
    bool queryOptions(const RuntimeMultiplayerCompatibility&,std::uint64_t,RuntimeInternetLobbyQueryOptions&,std::string&) override;
    bool hostOptions(const RuntimeMultiplayerCompatibility&,const RuntimeMultiplayerHostSelectionProjection&,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeInternetLobbyHostOptions&,std::string&,const std::shared_ptr<RuntimeMultiplayerResumeSave>&) override;
    bool joinOptions(const RuntimeMultiplayerCompatibility&,const RuntimeInternetRoomRecord&,bool,std::uint64_t,RuntimeInternetLobbyJoinOptions&,std::string&) override;
    bool matchmakingOptions(const RuntimeMultiplayerCompatibility&,const RuntimeMultiplayerHostSelectionProjection&,const std::string&,std::int32_t,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeInternetLobbyMatchmakingOptions&,std::string&) override;
    bool selectHostSetup(const std::string&,const RuntimeInternetMultiplayerMenuEntropy&,RuntimeMultiplayerHostSelectionProjection&,std::string&) override;
    bool selectResumeWithOriginalFailureStatus(const RuntimeMultiplayerCompatibility&,RuntimeMultiplayerResumeSave&,RuntimeMultiplayerHostSelectionProjection&,std::string&) override;
    bool resolveMap(const RuntimeConfig&,const RuntimeMultiplayerMatchMap&,std::filesystem::path&,std::string&) override;
    std::vector<RuntimeInternetMultiplayerRoomRow> roomRows(std::span<const RuntimeInternetRoomRecord>,bool) const override;
    std::vector<RuntimeInternetMultiplayerRosterRow> rosterRows(std::span<const RuntimeMultiplayerRoomMember>,const RuntimeMultiplayerMatchSetup*,std::string_view,bool) const override;
    std::string connectionStatus(bool,RuntimeInternetLobbyFlowMode,RuntimeInternetConnectionState) const override;
    std::string profileSummary(const RuntimeInternetCommunitySnapshot&) const override;
    std::string clanSummary(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> channelRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> chatRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> participantRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> friendRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> ladderRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> clanMemberRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> clanInvitationRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> clanLadderRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> newsRows(const RuntimeInternetCommunitySnapshot&) const override;
    std::vector<std::string> historyRows(std::span<const RuntimeInternetRankedRecord>,std::string_view) const override;
    std::string historyDetail(const RuntimeInternetRankedRecord&,std::string_view) const override;
    void finishMatch() override;

    // Borrowed monotonic input; the native provider outlives this UI owner.
    const RuntimeUiClock* clock = &runtimeWin32UiClock();
    RuntimeInternetMenuSkin skin;
    HBRUSH skinControlBrush = nullptr;
    RuntimeInternetMultiplayerServiceConfig serviceConfig;
    RuntimeInternetServiceTransport* transport = nullptr;
    std::unique_ptr<RuntimeInternetLobbyFlow> flowBackend;
    RuntimeMultiplayerMenuResult result;
};

RuntimeInternetMenuSkinPage internetSkinPage(InternetMenuPage page) {
    switch (page) {
    case InternetMenuPage::Account:
        return RuntimeInternetMenuSkinPage::Account;
    case InternetMenuPage::Community:
        return RuntimeInternetMenuSkinPage::Community;
    case InternetMenuPage::Browser:
        return RuntimeInternetMenuSkinPage::Browser;
    case InternetMenuPage::Ladder:
        return RuntimeInternetMenuSkinPage::Ladder;
    case InternetMenuPage::News:
        return RuntimeInternetMenuSkinPage::News;
    case InternetMenuPage::MatchHistory:
        return RuntimeInternetMenuSkinPage::MatchHistory;
    case InternetMenuPage::Lobby:
        return RuntimeInternetMenuSkinPage::Lobby;
    case InternetMenuPage::Kicked:
    default:
        return RuntimeInternetMenuSkinPage::Kicked;
    }
}

std::uint64_t internetMenuNowMilliseconds(
    const RuntimeInternetMultiplayerMenuUiState& state
) {
    return state.clock->nowMs();
}

std::wstring wideFromUtf8(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return L"(text too long)";
    }
    const int inputSize = static_cast<int>(value.size());
    const int count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        inputSize,
        nullptr,
        0
    );
    if (count <= 0) return L"(invalid text)";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            inputSize,
            result.data(),
            count
        ) != count) {
        return L"(invalid text)";
    }
    return result;
}

void showInternetMenuStartupError(
    HWND hostWindow,
    std::string_view reason
) {
    if (hostWindow == nullptr || !IsWindow(hostWindow)) return;
    std::wstring message =
        L"Internet multiplayer could not start.\r\n\r\n";
    message.append(wideFromUtf8(reason));
    message.append(
        L"\r\n\r\nCheck the [Internet] section in the selected runtime "
        L"configuration and verify that the service is reachable."
    );
    (void)MessageBoxW(
        hostWindow,
        message.c_str(),
        L"ConcordRTS Internet",
        MB_OK | MB_ICONERROR | MB_TASKMODAL
    );
}

std::string utf8FromWide(std::wstring_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(
            std::numeric_limits<int>::max())) {
        return {};
    }
    const int inputSize = static_cast<int>(value.size());
    const int count = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        inputSize,
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            inputSize,
            result.data(),
            count,
            nullptr,
            nullptr
        ) != count) {
        return {};
    }
    return result;
}

std::string controlText(HWND control) {
    if (control == nullptr) return {};
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
    const int copied = GetWindowTextW(control, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
    return utf8FromWide(value);
}

void setControlText(HWND control, std::string_view text) {
    if (control == nullptr) return;
    const std::wstring wide = wideFromUtf8(text);
    SetWindowTextW(control, wide.c_str());
}

RuntimeInternetMenuSkinFaction internetSkinFaction(
    const RuntimeInternetMultiplayerMenuUiState& state
) {
    const bool observer = state.spectatorCheck != nullptr
        && SendMessageW(state.spectatorCheck, BM_GETCHECK, 0U, 0)
            == BST_CHECKED;
    std::string country = state.countryEdit == nullptr
        ? state.baseConfig.playerCountry
        : controlText(state.countryEdit);
    if (trimAscii(country).empty()) country = state.baseConfig.playerCountry;
    return runtimeInternetMenuSkinFactionForCountry(country, observer);
}

std::uint32_t internetSkinLadderRank(
    const RuntimeInternetMultiplayerMenuUiState& state
) {
    if (!state.flowBackend) return 0U;
    const RuntimeInternetCommunitySnapshot& snapshot =
        state.flowBackend->communitySnapshot();
    return snapshot.registeredProfile ? snapshot.profile.rank : 0U;
}

bool internetSkinButtonAssets(
    int identifier,
    std::string& released,
    std::string& pressed
) {
    switch (identifier) {
    case InternetCommunityRefresh:
    case InternetRefresh:
    case InternetNavNews:
        released = "wouref";
        pressed = "wodref";
        return true;
    case InternetNavLadder:
    case InternetClanCreate:
    case InternetClanInvite:
    case InternetClanAccept:
    case InternetClanRemove:
    case InternetClanTransfer:
    case InternetClanLeave:
        released = "woluclan";
        pressed = "woldclan";
        return true;
    case InternetChatSend:
    case InternetFriendAdd:
    case InternetFriendRemove:
        released = "wolupriv";
        pressed = "woldpriv";
        return true;
    case InternetNavGames:
    case InternetQuickMatch:
    case InternetJoin:
    case InternetHost:
    case InternetResumeHost:
    case InternetKick:
        released = "wolurslkob";
        pressed = "woldrslkob";
        return true;
    case InternetNavCommunity:
    case InternetChannelJoin:
    case InternetAccountRegister:
    case InternetApplySlot:
    case InternetResumeSwap:
    case InternetReady:
    case InternetStart:
    case InternetLeave:
        released = "wouact";
        pressed = "wodact";
        return true;
    default:
        return false;
    }
}

void enableInternetSkinButton(
    const RuntimeInternetMultiplayerMenuUiState& state,
    HWND control,
    int identifier
) {
    if (!state.skin.productionReady || control == nullptr) return;
    std::string released;
    std::string pressed;
    if (!internetSkinButtonAssets(identifier, released, pressed)) return;
    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
    style = (style & ~static_cast<LONG_PTR>(BS_TYPEMASK)) | BS_OWNERDRAW;
    SetWindowLongPtrW(control, GWL_STYLE, style);
}

bool drawInternetSkinOwnerButton(
    const RuntimeInternetMultiplayerMenuUiState& state,
    const DRAWITEMSTRUCT& item
) {
    if (!state.skin.productionReady || item.CtlType != ODT_BUTTON) {
        return false;
    }
    std::string released;
    std::string pressed;
    if (!internetSkinButtonAssets(
            static_cast<int>(item.CtlID),
            released,
            pressed
        )) {
        return false;
    }
    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    return drawRuntimeInternetMenuSkinButtonWin32(
        item.hDC,
        item.rcItem,
        state.skin,
        released,
        pressed,
        text,
        (item.itemState & ODS_SELECTED) != 0U,
        (item.itemState & ODS_DISABLED) != 0U
    );
}



HWND createControl(
    HWND parent,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    int identifier
) {
    return CreateWindowExW(
        WS_EX_NOPARENTNOTIFY,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x,
        y,
        width,
        height,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
        GetModuleHandleW(nullptr),
        nullptr
    );
}

void showControl(HWND control, bool visible) {
    if (control != nullptr) ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
}



void createInternetMenuControls(
    RuntimeInternetMultiplayerMenuUiState& state
) {
    HWND window = state.window;
    if (state.skin.productionReady && state.skinControlBrush == nullptr) {
        state.skinControlBrush = CreateSolidBrush(RGB(11, 19, 18));
    }
    state.navCommunityButton = createControl(
        window, L"BUTTON", L"Community", BS_PUSHBUTTON,
        24, 14, 126, 30, InternetNavCommunity);
    state.navGamesButton = createControl(
        window, L"BUTTON", L"Custom Match", BS_PUSHBUTTON,
        158, 14, 126, 30, InternetNavGames);
    state.navLadderButton = createControl(
        window, L"BUTTON", L"Clan / Ladder", BS_PUSHBUTTON,
        292, 14, 126, 30, InternetNavLadder);
    state.navNewsButton = createControl(
        window, L"BUTTON", L"News", BS_PUSHBUTTON,
        426, 14, 126, 30, InternetNavNews);
    state.profileSummaryText = createControl(
        window, L"STATIC", L"", SS_LEFT,
        24, 54, 560, 42, InternetProfileSummary);

    state.accountTitleText = createControl(
        window, L"STATIC", L"WELCOME TO WESTWOOD ONLINE", SS_CENTER,
        110, 82, 546, 32, 0);
    state.accountDescriptionText = createControl(
        window, L"STATIC",
        L"Create a public profile for this authenticated service account.\r\n"
        L"The handle is used by chat, Buddy List, Quick Match, and Ladder.",
        SS_CENTER,
        110, 126, 546, 54, 0);
    state.accountHandleLabel = createControl(
        window, L"STATIC", L"Profile handle", SS_LEFT,
        170, 208, 120, 22, 0);
    state.accountHandleEdit = createControl(
        window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
        292, 202, 250, 28, InternetAccountHandle);
    state.accountRegisterButton = createControl(
        window, L"BUTTON", L"Create Profile / Sign In", BS_DEFPUSHBUTTON,
        270, 250, 226, 34, InternetAccountRegister);

    state.channelLabel = createControl(
        window, L"STATIC", L"CHANNELS", SS_LEFT,
        24, 98, 180, 20, 0);
    state.channelList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 118, 180, 182, InternetChannelList);
    state.channelJoinButton = createControl(
        window, L"BUTTON", L"Join Channel", BS_PUSHBUTTON,
        24, 306, 180, 28, InternetChannelJoin);
    state.chatLabel = createControl(
        window, L"STATIC", L"CHAT", SS_LEFT,
        214, 98, 338, 20, 0);
    state.chatList = createControl(
        window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
        214, 118, 338, 216, InternetChatList);
    state.chatEdit = createControl(
        window, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL,
        214, 342, 420, 27, InternetChatEdit);
    state.chatSendButton = createControl(
        window, L"BUTTON", L"Send", BS_DEFPUSHBUTTON,
        642, 340, 80, 30, InternetChatSend);
    state.participantLabel = createControl(
        window, L"STATIC", L"PLAYERS", SS_LEFT,
        562, 98, 160, 20, 0);
    state.participantList = createControl(
        window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
        562, 118, 160, 216, InternetParticipantList);
    state.friendLabel = createControl(
        window, L"STATIC", L"BUDDY LIST", SS_LEFT,
        24, 376, 310, 20, 0);
    state.friendList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 396, 310, 62, InternetFriendList);
    state.friendEdit = createControl(
        window, L"EDIT", L"Handle or account id",
        WS_BORDER | ES_AUTOHSCROLL,
        344, 396, 190, 27, InternetFriendEdit);
    state.friendAddButton = createControl(
        window, L"BUTTON", L"Add Buddy", BS_PUSHBUTTON,
        542, 394, 88, 30, InternetFriendAdd);
    state.friendRemoveButton = createControl(
        window, L"BUTTON", L"Remove", BS_PUSHBUTTON,
        638, 394, 84, 30, InternetFriendRemove);

    state.ladderTitleText = createControl(
        window, L"STATIC",
        L"PLAYER LADDER", SS_LEFT,
        394, 278, 196, 20, 0);
    state.matchHistoryButton = createControl(window, L"BUTTON", L"对局记录", BS_PUSHBUTTON,
        598, 273, 124, 26, InternetMatchHistory);
    state.matchHistoryBackButton = createControl(window, L"BUTTON", L"返回排行榜", BS_PUSHBUTTON,
        24, 54, 132, 30, InternetMatchHistoryBack);
    state.matchHistoryList = createControl(window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 110, 698, 190, InternetMatchHistoryList);
    state.matchHistoryDetail = createControl(window, L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY,
        24, 310, 698, 140, 0);
    state.ladderList = createControl(
        window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
        394, 300, 328, 150, InternetLadderList);
    state.clanSummaryText = createControl(
        window, L"STATIC", L"", SS_LEFT,
        24, 98, 360, 40, InternetClanSummary);
    state.clanMemberList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 142, 360, 94, InternetClanMemberList);
    state.clanInvitationList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 242, 360, 48, InternetClanInvitationList);
    state.clanNameEdit = createControl(
        window, L"EDIT", L"Clan name", WS_BORDER | ES_AUTOHSCROLL,
        24, 298, 188, 27, InternetClanName);
    state.clanTagEdit = createControl(
        window, L"EDIT", L"TAG", WS_BORDER | ES_AUTOHSCROLL,
        218, 298, 64, 27, InternetClanTag);
    state.clanCreateButton = createControl(
        window, L"BUTTON", L"Create Clan", BS_PUSHBUTTON,
        288, 296, 96, 30, InternetClanCreate);
    state.clanTargetEdit = createControl(
        window, L"EDIT", L"Member handle", WS_BORDER | ES_AUTOHSCROLL,
        24, 334, 174, 27, InternetClanTarget);
    state.clanInviteButton = createControl(
        window, L"BUTTON", L"Invite", BS_PUSHBUTTON,
        204, 332, 78, 30, InternetClanInvite);
    state.clanAcceptButton = createControl(
        window, L"BUTTON", L"Accept Invite", BS_PUSHBUTTON,
        288, 332, 96, 30, InternetClanAccept);
    state.clanRemoveButton = createControl(
        window, L"BUTTON", L"Remove", BS_PUSHBUTTON,
        24, 370, 104, 30, InternetClanRemove);
    state.clanTransferButton = createControl(
        window, L"BUTTON", L"Make Owner", BS_PUSHBUTTON,
        134, 370, 116, 30, InternetClanTransfer);
    state.clanLeaveButton = createControl(
        window, L"BUTTON", L"Leave Clan", BS_PUSHBUTTON,
        256, 370, 128, 30, InternetClanLeave);
    state.clanLadderTitleText = createControl(
        window, L"STATIC", L"CLAN QUICK MATCH LADDER", SS_LEFT,
        394, 98, 328, 20, 0);
    state.clanLadderList = createControl(
        window, L"LISTBOX", L"", WS_BORDER | WS_VSCROLL,
        394, 120, 328, 150, InternetClanLadderList);
    state.communityRefreshButton = createControl(
        window, L"BUTTON", L"Refresh", BS_PUSHBUTTON,
        602, 54, 120, 30, InternetCommunityRefresh);

    state.newsTitleText = createControl(
        window, L"STATIC", L"COMMAND & CONQUER COMMUNITY NEWS", SS_LEFT,
        24, 98, 698, 22, 0);
    state.newsList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 124, 218, 326, InternetNewsList);
    state.newsDetail = createControl(
        window, L"EDIT", L"",
        WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL
            | ES_READONLY,
        252, 124, 470, 326, InternetNewsDetail);

    state.playerNameLabel = createControl(
        window, L"STATIC", L"Player name", SS_LEFT,
        24, 102, 110, 22, 0);
    state.playerNameEdit = createControl(
        window, L"EDIT", L"Player",
        WS_BORDER | ES_AUTOHSCROLL | ES_READONLY,
        138, 98, 250, 26, InternetPlayerName);
    state.roomNameLabel = createControl(
        window, L"STATIC", L"Room name", SS_LEFT,
        24, 136, 110, 22, 0);
    state.roomNameEdit = createControl(
        window, L"EDIT", L"Internet Room", WS_BORDER | ES_AUTOHSCROLL,
        138, 132, 250, 26, InternetRoomName);
    state.passwordLabel = createControl(
        window, L"STATIC", L"Room password", SS_LEFT,
        220, 170, 110, 22, 0);
    state.passwordEdit = createControl(
        window, L"EDIT", L"",
        WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD,
        338, 164, 250, 26, InternetPassword);
    state.refreshButton = createControl(
        window, L"BUTTON", L"Refresh Rooms", BS_PUSHBUTTON,
        410, 98, 150, 30, InternetRefresh);
    state.hostButton = createControl(
        window, L"BUTTON", L"Host Room", BS_PUSHBUTTON,
        572, 98, 150, 30, InternetHost);
    state.resumeHostButton = createControl(window, L"BUTTON", L"从存档恢复", BS_PUSHBUTTON,
        598, 164, 124, 30, InternetResumeHost);
    state.resumeSwapButton = createControl(window, L"BUTTON", L"与选中玩家交换席位", BS_PUSHBUTTON,
        440, 406, 282, 30, InternetResumeSwap);
    state.quickMatchButton = createControl(
        window, L"BUTTON", L"Quick Match (2)", BS_DEFPUSHBUTTON,
        410, 132, 150, 30, InternetQuickMatch);
    state.joinButton = createControl(
        window, L"BUTTON", L"Join Selected", BS_PUSHBUTTON,
        572, 132, 150, 30, InternetJoin);
    state.spectatorCheck = createControl(
        window, L"BUTTON", L"Join as spectator", BS_AUTOCHECKBOX,
        24, 168, 180, 24, InternetSpectator);
    state.roomList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 200, 698, 250, InternetRoomList);
    state.rosterList = createControl(
        window, L"LISTBOX", L"",
        WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        24, 46, 698, 250, InternetRoster);
    state.countryLabel = createControl(
        window, L"STATIC", L"Country", SS_LEFT,
        24, 302, 100, 20, 0);
    state.countryEdit = createControl(
        window, L"EDIT", L"Random", WS_BORDER | ES_AUTOHSCROLL,
        24, 322, 110, 26, InternetCountry);
    state.colorLabel = createControl(
        window, L"STATIC", L"Color", SS_LEFT,
        144, 302, 90, 20, 0);
    state.colorEdit = createControl(
        window, L"EDIT", L"Random", WS_BORDER | ES_AUTOHSCROLL,
        144, 322, 96, 26, InternetColor);
    state.teamLabel = createControl(
        window, L"STATIC", L"Team", SS_LEFT,
        250, 302, 80, 20, 0);
    state.teamEdit = createControl(
        window, L"EDIT", L"-", WS_BORDER | ES_AUTOHSCROLL,
        250, 322, 76, 26, InternetTeam);
    state.difficultyLabel = createControl(
        window, L"STATIC", L"Difficulty", SS_LEFT,
        336, 302, 100, 20, 0);
    state.difficultyEdit = createControl(
        window, L"EDIT", L"Medium", WS_BORDER | ES_AUTOHSCROLL,
        336, 322, 104, 26, InternetDifficulty);
    state.startPositionLabel = createControl(
        window, L"STATIC", L"Start (0-8)", SS_LEFT,
        450, 302, 100, 20, 0);
    state.startPositionEdit = createControl(
        window, L"EDIT", L"0",
        WS_BORDER | ES_AUTOHSCROLL | ES_NUMBER,
        450, 322, 78, 26, InternetStartPosition);
    state.applySlotButton = createControl(
        window, L"BUTTON", L"Apply Player Setup", BS_PUSHBUTTON,
        544, 318, 178, 32, InternetApplySlot);
    state.readyButton = createControl(
        window, L"BUTTON", L"Ready", BS_PUSHBUTTON,
        24, 366, 120, 32, InternetReady);
    state.startButton = createControl(
        window, L"BUTTON", L"Start Match", BS_DEFPUSHBUTTON,
        156, 366, 120, 32, InternetStart);
    state.kickButton = createControl(
        window, L"BUTTON", L"Kick Selected", BS_PUSHBUTTON,
        288, 366, 140, 32, InternetKick);
    state.leaveButton = createControl(
        window, L"BUTTON", L"Back", BS_PUSHBUTTON,
        602, 470, 120, 32, InternetLeave);
    state.statusText = createControl(
        window, L"STATIC", L"", SS_LEFT,
        24, 470, 558, 38, InternetStatus);
    state.connectionText = createControl(
        window, L"STATIC", L"Connecting to the authenticated TLS service...",
        SS_LEFT, 24, 515, 698, 30, InternetConnection);
    for (const auto& entry : {
            std::pair<HWND, int>{state.navCommunityButton, InternetNavCommunity},
            {state.navGamesButton, InternetNavGames},
            {state.navLadderButton, InternetNavLadder},
            {state.navNewsButton, InternetNavNews},
            {state.accountRegisterButton, InternetAccountRegister},
            {state.channelJoinButton, InternetChannelJoin},
            {state.chatSendButton, InternetChatSend},
            {state.friendAddButton, InternetFriendAdd},
            {state.friendRemoveButton, InternetFriendRemove},
            {state.clanCreateButton, InternetClanCreate},
            {state.clanInviteButton, InternetClanInvite},
            {state.clanAcceptButton, InternetClanAccept},
            {state.clanRemoveButton, InternetClanRemove},
            {state.clanTransferButton, InternetClanTransfer},
            {state.clanLeaveButton, InternetClanLeave},
            {state.communityRefreshButton, InternetCommunityRefresh},
            {state.refreshButton, InternetRefresh},
            {state.hostButton, InternetHost},
            {state.resumeHostButton, InternetResumeHost},
            {state.resumeSwapButton, InternetResumeSwap},
            {state.quickMatchButton, InternetQuickMatch},
            {state.joinButton, InternetJoin},
            {state.applySlotButton, InternetApplySlot},
            {state.readyButton, InternetReady},
            {state.startButton, InternetStart},
            {state.kickButton, InternetKick},
            {state.leaveButton, InternetLeave}
        }) {
        enableInternetSkinButton(state, entry.first, entry.second);
    }
    showInternetMenuPage(state, InternetMenuPage::Account);
}





void replaceListRows(HWND list, std::span<const std::string> rows) {
    if (list == nullptr) return;
    SendMessageW(list, LB_RESETCONTENT, 0U, 0);
    for (const std::string& row : rows) {
        const std::wstring text = wideFromUtf8(row);
        SendMessageW(
            list,
            LB_ADDSTRING,
            0U,
            reinterpret_cast<LPARAM>(text.c_str())
        );
    }
}











RuntimeMultiplayerLobbyFlow* activeLobby(
    RuntimeInternetMultiplayerMenuUiState& state
) {
    return state.flowBackend ? state.flowBackend->lobbyFlow() : nullptr;
}











































bool selectHostSetup(
    RuntimeInternetMultiplayerMenuUiState& state,
    std::string playerDisplayName,
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    RuntimeMultiplayerHostSelectionProjection& projection,
    std::string& errorMessage
) {
    ShowWindow(state.window, SW_HIDE);
    auto hostConfig = state.baseConfig;
    hostConfig.multiplayerActive = true;
    hostConfig.noCooldown = false;
    RuntimeWindowParentWin32 skirmishParent(state.hostWindow);
    RuntimeSkirmishSelection selection = runRuntimeSkirmishMenu(
        hostConfig,
        skirmishParent.id()
    );
    if (state.window != nullptr && IsWindow(state.window)) {
        ShowWindow(state.window, SW_SHOW);
        SetForegroundWindow(state.window);
    }
    if (!selection.accepted) {
        return fail(errorMessage, "Host map and rules selection was cancelled");
    }
    return projectRuntimeMultiplayerHostSelection(
        state.baseConfig,
        selection,
        std::move(playerDisplayName),
        state.serviceConfig.peerId,
        entropy.gameplayRandomSeed,
        projection,
        errorMessage
    );
}















void finishInternetMatch(RuntimeInternetMultiplayerMenuUiState& state) {
    RuntimeMultiplayerLobbyFlow* lobby = activeLobby(state);
    if (!lobby || !state.flowBackend) return;
    RuntimeMultiplayerLaunchConfigResult launch;
    std::string errorMessage;
    if (!lobby->makeLaunchConfig(
            state.baseConfig,
            launch,
            errorMessage
        )) {
        setMenuStatus(state, errorMessage);
        return;
    }
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> bridge =
        state.flowBackend->takeRunningBridge(errorMessage);
    if (!bridge) {
        setMenuStatus(state, errorMessage);
        return;
    }
    if (!bridge->beginPreAttachMaintenance(errorMessage)) {
        setMenuStatus(state, errorMessage);
        return;
    }
    state.result.launchConfig = std::move(launch);
    state.result.bridge = std::move(bridge);
    state.result.accepted = true;
    state.result.error.clear();
    closeInternetMenu(state);
}





bool RuntimeInternetMultiplayerMenuUiState::beginRoomQuery(RuntimeInternetLobbyQueryOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) { return flowBackend->beginRoomQuery(std::move(options), nowMilliseconds, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::beginMatchmaking(RuntimeInternetLobbyMatchmakingOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) { return flowBackend->beginMatchmaking(std::move(options), nowMilliseconds, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::startHost(RuntimeInternetLobbyHostOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) { return flowBackend->startHost(std::move(options), nowMilliseconds, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::join(RuntimeInternetLobbyJoinOptions&& options, std::uint64_t nowMilliseconds, std::string& errorMessage) { return flowBackend->join(std::move(options), nowMilliseconds, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityLogin(std::uint64_t transactionId, std::string& errorMessage) { return flowBackend->requestCommunityLogin(transactionId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityRegistration(std::uint64_t transactionId, const std::string& displayName, std::string& errorMessage) { return flowBackend->requestCommunityRegistration(transactionId, displayName, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityRefresh(std::uint64_t transactionId, std::string& errorMessage) { return flowBackend->requestCommunityRefresh(transactionId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityChannelJoin(std::uint64_t transactionId, const std::string& channelId, std::string& errorMessage) { return flowBackend->requestCommunityChannelJoin(transactionId, channelId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityChannelMessage(std::uint64_t transactionId, const std::string& text, std::string& errorMessage) { return flowBackend->requestCommunityChannelMessage(transactionId, text, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityFriendAdd(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) { return flowBackend->requestCommunityFriendAdd(transactionId, handleOrPeerId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityFriendRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) { return flowBackend->requestCommunityFriendRemove(transactionId, peerId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanCreate(std::uint64_t transactionId, const std::string& clanName, const std::string& clanTag, std::string& errorMessage) { return flowBackend->requestCommunityClanCreate(transactionId, clanName, clanTag, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanInvite(std::uint64_t transactionId, const std::string& handleOrPeerId, std::string& errorMessage) { return flowBackend->requestCommunityClanInvite(transactionId, handleOrPeerId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanInvitationAccept(std::uint64_t transactionId, const std::string& clanId, std::string& errorMessage) { return flowBackend->requestCommunityClanInvitationAccept(transactionId, clanId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanMemberRemove(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) { return flowBackend->requestCommunityClanMemberRemove(transactionId, peerId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanLeave(std::uint64_t transactionId, std::string& errorMessage) { return flowBackend->requestCommunityClanLeave(transactionId, errorMessage); }
bool RuntimeInternetMultiplayerMenuUiState::requestCommunityClanOwnershipTransfer(std::uint64_t transactionId, const std::string& peerId, std::string& errorMessage) { return flowBackend->requestCommunityClanOwnershipTransfer(transactionId, peerId, errorMessage); }
RuntimeInternetLobbyFlowPollResult RuntimeInternetMultiplayerMenuUiState::poll(std::uint64_t nowMilliseconds, int transportTimeoutMilliseconds) { return flowBackend->poll(nowMilliseconds, transportTimeoutMilliseconds); }
RuntimeInternetLobbyFlowMode RuntimeInternetMultiplayerMenuUiState::mode() const { return flowBackend->mode(); }
const std::vector<RuntimeInternetRoomRecord>& RuntimeInternetMultiplayerMenuUiState::rooms() const { return flowBackend->rooms(); }
RuntimeInternetConnectionState RuntimeInternetMultiplayerMenuUiState::primaryConnectionState() const { return flowBackend->primaryConnectionState(); }
const RuntimeInternetCommunitySnapshot& RuntimeInternetMultiplayerMenuUiState::communitySnapshot() const { return flowBackend->communitySnapshot(); }
const std::vector<RuntimeInternetRankedRecord>& RuntimeInternetMultiplayerMenuUiState::communityMatchHistory() const { return flowBackend->communityMatchHistory(); }
void RuntimeInternetMultiplayerMenuUiState::close() noexcept { flowBackend->close(); }
bool RuntimeInternetMultiplayerMenuUiState::present() const { return flowBackend != nullptr; }
bool RuntimeInternetMultiplayerMenuUiState::transportPresent() const { return transport != nullptr; }
bool RuntimeInternetMultiplayerMenuUiState::coordinatorAuthenticated() const { return transport->coordinatorAuthenticated(); }

HWND RuntimeInternetMultiplayerMenuUiState::widgetHandle(RuntimeInternetMenuWidget widget) const {
    switch(widget) {
    case RuntimeInternetMenuWidget::hostWindow: return hostWindow;
    case RuntimeInternetMenuWidget::window: return window;
    case RuntimeInternetMenuWidget::playerNameLabel: return playerNameLabel;
    case RuntimeInternetMenuWidget::playerNameEdit: return playerNameEdit;
    case RuntimeInternetMenuWidget::roomNameLabel: return roomNameLabel;
    case RuntimeInternetMenuWidget::roomNameEdit: return roomNameEdit;
    case RuntimeInternetMenuWidget::passwordLabel: return passwordLabel;
    case RuntimeInternetMenuWidget::passwordEdit: return passwordEdit;
    case RuntimeInternetMenuWidget::refreshButton: return refreshButton;
    case RuntimeInternetMenuWidget::hostButton: return hostButton;
    case RuntimeInternetMenuWidget::resumeHostButton: return resumeHostButton;
    case RuntimeInternetMenuWidget::resumeSwapButton: return resumeSwapButton;
    case RuntimeInternetMenuWidget::joinButton: return joinButton;
    case RuntimeInternetMenuWidget::spectatorCheck: return spectatorCheck;
    case RuntimeInternetMenuWidget::quickMatchButton: return quickMatchButton;
    case RuntimeInternetMenuWidget::roomList: return roomList;
    case RuntimeInternetMenuWidget::rosterList: return rosterList;
    case RuntimeInternetMenuWidget::countryLabel: return countryLabel;
    case RuntimeInternetMenuWidget::countryEdit: return countryEdit;
    case RuntimeInternetMenuWidget::colorLabel: return colorLabel;
    case RuntimeInternetMenuWidget::colorEdit: return colorEdit;
    case RuntimeInternetMenuWidget::teamLabel: return teamLabel;
    case RuntimeInternetMenuWidget::teamEdit: return teamEdit;
    case RuntimeInternetMenuWidget::difficultyLabel: return difficultyLabel;
    case RuntimeInternetMenuWidget::difficultyEdit: return difficultyEdit;
    case RuntimeInternetMenuWidget::startPositionLabel: return startPositionLabel;
    case RuntimeInternetMenuWidget::startPositionEdit: return startPositionEdit;
    case RuntimeInternetMenuWidget::applySlotButton: return applySlotButton;
    case RuntimeInternetMenuWidget::readyButton: return readyButton;
    case RuntimeInternetMenuWidget::startButton: return startButton;
    case RuntimeInternetMenuWidget::kickButton: return kickButton;
    case RuntimeInternetMenuWidget::leaveButton: return leaveButton;
    case RuntimeInternetMenuWidget::statusText: return statusText;
    case RuntimeInternetMenuWidget::connectionText: return connectionText;
    case RuntimeInternetMenuWidget::navCommunityButton: return navCommunityButton;
    case RuntimeInternetMenuWidget::navGamesButton: return navGamesButton;
    case RuntimeInternetMenuWidget::navLadderButton: return navLadderButton;
    case RuntimeInternetMenuWidget::navNewsButton: return navNewsButton;
    case RuntimeInternetMenuWidget::profileSummaryText: return profileSummaryText;
    case RuntimeInternetMenuWidget::accountTitleText: return accountTitleText;
    case RuntimeInternetMenuWidget::accountDescriptionText: return accountDescriptionText;
    case RuntimeInternetMenuWidget::accountHandleLabel: return accountHandleLabel;
    case RuntimeInternetMenuWidget::accountHandleEdit: return accountHandleEdit;
    case RuntimeInternetMenuWidget::accountRegisterButton: return accountRegisterButton;
    case RuntimeInternetMenuWidget::channelLabel: return channelLabel;
    case RuntimeInternetMenuWidget::channelList: return channelList;
    case RuntimeInternetMenuWidget::channelJoinButton: return channelJoinButton;
    case RuntimeInternetMenuWidget::chatLabel: return chatLabel;
    case RuntimeInternetMenuWidget::chatList: return chatList;
    case RuntimeInternetMenuWidget::chatEdit: return chatEdit;
    case RuntimeInternetMenuWidget::chatSendButton: return chatSendButton;
    case RuntimeInternetMenuWidget::participantLabel: return participantLabel;
    case RuntimeInternetMenuWidget::participantList: return participantList;
    case RuntimeInternetMenuWidget::friendLabel: return friendLabel;
    case RuntimeInternetMenuWidget::friendList: return friendList;
    case RuntimeInternetMenuWidget::friendEdit: return friendEdit;
    case RuntimeInternetMenuWidget::friendAddButton: return friendAddButton;
    case RuntimeInternetMenuWidget::friendRemoveButton: return friendRemoveButton;
    case RuntimeInternetMenuWidget::ladderTitleText: return ladderTitleText;
    case RuntimeInternetMenuWidget::ladderList: return ladderList;
    case RuntimeInternetMenuWidget::clanSummaryText: return clanSummaryText;
    case RuntimeInternetMenuWidget::clanMemberList: return clanMemberList;
    case RuntimeInternetMenuWidget::clanInvitationList: return clanInvitationList;
    case RuntimeInternetMenuWidget::clanNameEdit: return clanNameEdit;
    case RuntimeInternetMenuWidget::clanTagEdit: return clanTagEdit;
    case RuntimeInternetMenuWidget::clanCreateButton: return clanCreateButton;
    case RuntimeInternetMenuWidget::clanTargetEdit: return clanTargetEdit;
    case RuntimeInternetMenuWidget::clanInviteButton: return clanInviteButton;
    case RuntimeInternetMenuWidget::clanAcceptButton: return clanAcceptButton;
    case RuntimeInternetMenuWidget::clanRemoveButton: return clanRemoveButton;
    case RuntimeInternetMenuWidget::clanTransferButton: return clanTransferButton;
    case RuntimeInternetMenuWidget::clanLeaveButton: return clanLeaveButton;
    case RuntimeInternetMenuWidget::clanLadderTitleText: return clanLadderTitleText;
    case RuntimeInternetMenuWidget::clanLadderList: return clanLadderList;
    case RuntimeInternetMenuWidget::communityRefreshButton: return communityRefreshButton;
    case RuntimeInternetMenuWidget::newsTitleText: return newsTitleText;
    case RuntimeInternetMenuWidget::newsList: return newsList;
    case RuntimeInternetMenuWidget::newsDetail: return newsDetail;
    case RuntimeInternetMenuWidget::matchHistoryButton: return matchHistoryButton;
    case RuntimeInternetMenuWidget::matchHistoryBackButton: return matchHistoryBackButton;
    case RuntimeInternetMenuWidget::matchHistoryList: return matchHistoryList;
    case RuntimeInternetMenuWidget::matchHistoryDetail: return matchHistoryDetail;
    }
    return nullptr;
}
void RuntimeInternetMultiplayerMenuUiState::showWidget(RuntimeInternetMenuWidget widget,bool visible) { showControl(widgetHandle(widget),visible); }
void RuntimeInternetMultiplayerMenuUiState::enableWidget(RuntimeInternetMenuWidget widget,bool enabled) { EnableWindow(widgetHandle(widget),enabled?TRUE:FALSE); }
std::string RuntimeInternetMultiplayerMenuUiState::text(RuntimeInternetMenuWidget widget) const { return controlText(widgetHandle(widget)); }
void RuntimeInternetMultiplayerMenuUiState::setText(RuntimeInternetMenuWidget widget,std::string_view value) { setControlText(widgetHandle(widget),value); }
bool RuntimeInternetMultiplayerMenuUiState::hasWidget(RuntimeInternetMenuWidget widget) const { return widgetHandle(widget)!=nullptr; }
bool RuntimeInternetMultiplayerMenuUiState::checked(RuntimeInternetMenuWidget widget) const { return SendMessageW(widgetHandle(widget),BM_GETCHECK,0U,0)==BST_CHECKED; }
void RuntimeInternetMultiplayerMenuUiState::clearText(RuntimeInternetMenuWidget widget) { SetWindowTextW(widgetHandle(widget),L""); }
void RuntimeInternetMultiplayerMenuUiState::listClear(RuntimeInternetMenuWidget widget) { SendMessageW(widgetHandle(widget),LB_RESETCONTENT,0U,0); }
void RuntimeInternetMultiplayerMenuUiState::listAdd(RuntimeInternetMenuWidget widget,const std::string& value) { const std::wstring wide=wideFromUtf8(value);SendMessageW(widgetHandle(widget),LB_ADDSTRING,0U,reinterpret_cast<LPARAM>(wide.c_str())); }
void RuntimeInternetMultiplayerMenuUiState::replaceRows(RuntimeInternetMenuWidget widget,std::span<const std::string> rows) { replaceListRows(widgetHandle(widget),rows); }
std::int64_t RuntimeInternetMultiplayerMenuUiState::listSelected(RuntimeInternetMenuWidget widget) const { return static_cast<std::int64_t>(SendMessageW(widgetHandle(widget),LB_GETCURSEL,0U,0)); }
void RuntimeInternetMultiplayerMenuUiState::listSelect(RuntimeInternetMenuWidget widget,std::int64_t index) { SendMessageW(widgetHandle(widget),LB_SETCURSEL,static_cast<WPARAM>(index),0); }
void RuntimeInternetMultiplayerMenuUiState::listTop(RuntimeInternetMenuWidget widget,std::size_t index) { SendMessageW(widgetHandle(widget),LB_SETTOPINDEX,static_cast<WPARAM>(index),0); }
void RuntimeInternetMultiplayerMenuUiState::readyLabel(bool ready) { SetWindowTextW(readyButton,ready?L"Unready":L"Ready"); }
void RuntimeInternetMultiplayerMenuUiState::leaveLabel(RuntimeInternetLeaveLabel label) { SetWindowTextW(leaveButton,label==RuntimeInternetLeaveLabel::Close?L"Close":label==RuntimeInternetLeaveLabel::LeaveRoom?L"Leave Room":L"Main Menu"); }
void RuntimeInternetMultiplayerMenuUiState::invalidateSkinned() { if(window!=nullptr&&skin.productionReady)InvalidateRect(window,nullptr,TRUE); }
void RuntimeInternetMultiplayerMenuUiState::invalidateSkin() { if(skin.productionReady)InvalidateRect(window,nullptr,TRUE); }
void RuntimeInternetMultiplayerMenuUiState::destroyWindow() { if(window!=nullptr&&IsWindow(window))DestroyWindow(window); }
bool RuntimeInternetMultiplayerMenuUiState::accepted() const { return result.accepted; }
const std::string& RuntimeInternetMultiplayerMenuUiState::peerId() const { return serviceConfig.peerId; }
std::uint64_t RuntimeInternetMultiplayerMenuUiState::nowMs() const { return internetMenuNowMilliseconds(*this); }
void RuntimeInternetMultiplayerMenuUiState::clearCredentials() { secureClear(serviceConfig.credential); }
bool RuntimeInternetMultiplayerMenuUiState::entropy(RuntimeInternetMultiplayerMenuEntropy& value,std::string& error) { return generateRuntimeInternetMultiplayerMenuEntropy(value,error); }
std::string RuntimeInternetMultiplayerMenuUiState::trim(std::string&& value) const { return trimAscii(std::move(value)); }
bool RuntimeInternetMultiplayerMenuUiState::validateDisplayText(std::string& value,std::size_t maximum,std::string_view field,std::string& error) { return displayText(value,maximum,field,error); }
const RuntimeMultiplayerMatchSlot* RuntimeInternetMultiplayerMenuUiState::slotForPeer(const RuntimeMultiplayerMatchSetup* setup,std::string_view peer) const { return ::slotForPeer(setup,peer); }
bool RuntimeInternetMultiplayerMenuUiState::queryOptions(const RuntimeMultiplayerCompatibility& compatibility,std::uint64_t transaction,RuntimeInternetLobbyQueryOptions& options,std::string& error) { return makeRuntimeInternetMultiplayerQueryOptions(serviceConfig,compatibility,transaction,options,error); }
bool RuntimeInternetMultiplayerMenuUiState::hostOptions(const RuntimeMultiplayerCompatibility& compatibility,const RuntimeMultiplayerHostSelectionProjection& projection,const RuntimeInternetMultiplayerMenuEntropy& entropy,RuntimeInternetLobbyHostOptions& options,std::string& error,const std::shared_ptr<RuntimeMultiplayerResumeSave>& resume) { return makeRuntimeInternetMultiplayerHostOptions(serviceConfig,compatibility,projection,controlText(roomNameEdit),controlText(passwordEdit),entropy,options,error,resume); }
bool RuntimeInternetMultiplayerMenuUiState::joinOptions(const RuntimeMultiplayerCompatibility& compatibility,const RuntimeInternetRoomRecord& room,bool spectator,std::uint64_t transaction,RuntimeInternetLobbyJoinOptions& options,std::string& error) { return makeRuntimeInternetMultiplayerJoinOptions(serviceConfig,compatibility,room,controlText(playerNameEdit),controlText(passwordEdit),spectator,transaction,options,error); }
bool RuntimeInternetMultiplayerMenuUiState::matchmakingOptions(const RuntimeMultiplayerCompatibility& compatibility,const RuntimeMultiplayerHostSelectionProjection& projection,const std::string& player,std::int32_t rating,const RuntimeInternetMultiplayerMenuEntropy& entropy,RuntimeInternetLobbyMatchmakingOptions& options,std::string& error) { return makeRuntimeInternetMultiplayerMatchmakingOptions(serviceConfig,compatibility,projection,player,rating,entropy,options,error); }
bool RuntimeInternetMultiplayerMenuUiState::selectHostSetup(const std::string& player,const RuntimeInternetMultiplayerMenuEntropy& entropy,RuntimeMultiplayerHostSelectionProjection& projection,std::string& error) { return ::selectHostSetup(*this,player,entropy,projection,error); }
bool RuntimeInternetMultiplayerMenuUiState::selectResumeWithOriginalFailureStatus(const RuntimeMultiplayerCompatibility& compatibility,RuntimeMultiplayerResumeSave& save,RuntimeMultiplayerHostSelectionProjection& projection,std::string& error) {
    RuntimeWindowParentWin32 resumeParent(window);
    if(!selectRuntimeMultiplayerResumeSave(baseConfig,compatibility,serviceConfig.peerId,serviceConfig.peerId,resumeParent.id(),save,projection,error)) {
        setMenuStatus(*this,error);return false;
    }
    return true;
}
bool RuntimeInternetMultiplayerMenuUiState::resolveMap(const RuntimeConfig& config,const RuntimeMultiplayerMatchMap& map,std::filesystem::path& output,std::string& error) { return resolveRuntimeMultiplayerMenuMap(config,map,output,error); }
std::vector<RuntimeInternetMultiplayerRoomRow> RuntimeInternetMultiplayerMenuUiState::roomRows(std::span<const RuntimeInternetRoomRecord> rows,bool spectator) const { return projectRuntimeInternetMultiplayerRoomRows(rows,spectator); }
std::vector<RuntimeInternetMultiplayerRosterRow> RuntimeInternetMultiplayerMenuUiState::rosterRows(std::span<const RuntimeMultiplayerRoomMember> members,const RuntimeMultiplayerMatchSetup* setup,std::string_view peer,bool host) const { return projectRuntimeInternetMultiplayerRosterRows(members,setup,peer,host); }
std::string RuntimeInternetMultiplayerMenuUiState::connectionStatus(bool authenticated,RuntimeInternetLobbyFlowMode mode,RuntimeInternetConnectionState state) const { return projectRuntimeInternetMultiplayerConnectionStatus(authenticated,mode,state); }
std::string RuntimeInternetMultiplayerMenuUiState::profileSummary(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityProfileSummary(snapshot); }
std::string RuntimeInternetMultiplayerMenuUiState::clanSummary(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityClanSummary(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::channelRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityChannelRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::chatRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityChatRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::participantRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityParticipantRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::friendRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityFriendRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::ladderRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityLadderRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::clanMemberRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityClanMemberRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::clanInvitationRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityClanInvitationRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::clanLadderRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityClanLadderRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::newsRows(const RuntimeInternetCommunitySnapshot& snapshot) const { return projectRuntimeInternetCommunityNewsRows(snapshot); }
std::vector<std::string> RuntimeInternetMultiplayerMenuUiState::historyRows(std::span<const RuntimeInternetRankedRecord> records,std::string_view peer) const { return projectRuntimeInternetRankedHistoryRows(records,peer); }
std::string RuntimeInternetMultiplayerMenuUiState::historyDetail(const RuntimeInternetRankedRecord& record,std::string_view peer) const { return projectRuntimeInternetRankedHistoryDetail(record,peer); }
void RuntimeInternetMultiplayerMenuUiState::finishMatch() { finishInternetMatch(*this); }
concord::lan_menu::RuntimeLanMenuFlow* RuntimeInternetMultiplayerMenuUiState::lobbyFlow() const {
    RuntimeMultiplayerLobbyFlow* backend=flowBackend?flowBackend->lobbyFlow():nullptr;
    lobbyBorrow.backend=backend;
    return backend?&lobbyBorrow:nullptr;
}

LRESULT CALLBACK internetMultiplayerMenuWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    auto* state = reinterpret_cast<RuntimeInternetMultiplayerMenuUiState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<RuntimeInternetMultiplayerMenuUiState*>(
            create->lpCreateParams
        );
        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)
        );
    }
    if (!state) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_ERASEBKGND:
        if (state->skin.productionReady) return 1;
        break;
    case WM_PRINTCLIENT:
        if (state->skin.productionReady && wParam != 0U) {
            RECT client{};
            GetClientRect(window, &client);
            drawRuntimeInternetMenuSkinSurfaceWin32(
                reinterpret_cast<HDC>(wParam),
                client,
                state->skin,
                internetSkinPage(state->page),
                internetSkinFaction(*state),
                internetSkinLadderRank(*state)
            );
            return 0;
        }
        break;
    case WM_PAINT:
        if (state->skin.productionReady) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            drawRuntimeInternetMenuSkinSurfaceWin32(
                dc,
                client,
                state->skin,
                internetSkinPage(state->page),
                internetSkinFaction(*state),
                internetSkinLadderRank(*state)
            );
            EndPaint(window, &paint);
            return 0;
        }
        break;
    case WM_DRAWITEM:
        if (lParam != 0
            && drawInternetSkinOwnerButton(
                *state,
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam)
            )) {
            return TRUE;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (state->skin.productionReady) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(222, 216, 177));
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        if (state->skin.productionReady && state->skinControlBrush != nullptr) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkColor(dc, RGB(11, 19, 18));
            SetTextColor(dc, RGB(222, 216, 177));
            return reinterpret_cast<LRESULT>(state->skinControlBrush);
        }
        break;
    case WM_COMMAND:
        if(HIWORD(wParam)==LBN_SELCHANGE && handleInternetSelection(*state,LOWORD(wParam)))return 0;
        if(HIWORD(wParam)==LBN_DBLCLK && handleInternetDoubleClick(*state,LOWORD(wParam)))return 0;
        if (HIWORD(wParam) != BN_CLICKED) break;
        if(handleInternetClick(*state,LOWORD(wParam)))return 0;
        break;
    case WM_TIMER:
        if (wParam == internetMenuPollTimer) pollInternetMenu(*state);
        return 0;
    case WM_CLOSE:
        closeInternetMenu(*state);
        return 0;
    case WM_DESTROY:
        KillTimer(window, internetMenuPollTimer);
        if (state->skinControlBrush != nullptr) {
            DeleteObject(state->skinControlBrush);
            state->skinControlBrush = nullptr;
        }
        state->window = nullptr;
        state->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerInternetMenuClass(std::string& errorMessage) {
    constexpr const wchar_t* className =
        L"ConcordRTS.RuntimeInternetMultiplayerMenu";
    WNDCLASSEXW existing{};
    existing.cbSize = sizeof(existing);
    if (GetClassInfoExW(GetModuleHandleW(nullptr), className, &existing)) {
        errorMessage.clear();
        return true;
    }
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = internetMultiplayerMenuWindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512U));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) == 0U) {
        return fail(errorMessage, "could not register Internet menu window");
    }
    errorMessage.clear();
    return true;
}

HWND createInternetMenuWindow(
    RuntimeInternetMultiplayerMenuUiState& state,
    std::string& errorMessage
) {
    if (!registerInternetMenuClass(errorMessage)) return nullptr;
    constexpr const wchar_t* className =
        L"ConcordRTS.RuntimeInternetMultiplayerMenu";
    const bool hosted = state.hostWindow != nullptr
        && IsWindow(state.hostWindow);
    const DWORD style = hosted
        ? WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN
        : WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    int width = 766;
    int height = 590;
    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (hosted) {
        RECT client{};
        GetClientRect(state.hostWindow, &client);
        width = std::max(1L, client.right - client.left);
        height = std::max(1L, client.bottom - client.top);
        x = 0;
        y = 0;
    }
    HWND window = CreateWindowExW(
        hosted ? 0U : WS_EX_APPWINDOW,
        className,
        L"Concord RTS - Internet Multiplayer",
        style,
        x,
        y,
        width,
        height,
        hosted ? state.hostWindow : nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        &state
    );
    if (!window) {
        fail(errorMessage, "could not create Internet menu window");
    }
    return window;
}

#include "ui/network/diagnostics/RuntimeInternetMenuClockSmoke.inl"

} // namespace

int runRuntimeInternetMenuClockContract(const std::filesystem::path& output) {
    return internetMenuClockContract(output);
}
#endif

#if !defined(RUNTIME_INTERNET_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
RuntimeMultiplayerMenuResult runRuntimeInternetMultiplayerMenu(
    const RuntimeConfig& baseConfig,
    RuntimeWindowId hostWindow
) {
    RuntimeMultiplayerMenuResult unavailable;
#if !defined(_WIN32)
    (void)baseConfig;
    (void)hostWindow;
    unavailable.error = "Internet multiplayer menu requires Win32";
    return unavailable;
#else
    RuntimeInternetMultiplayerMenuUiState state;
    state.baseConfig = baseConfig;
    if (loadRuntimeInternetMenuSkin(baseConfig, state.skin)) {
        const RuntimeInternetMenuSkinAsset* observer =
            state.skin.asset("obs_alli");
        const RuntimeInternetMenuSkinAsset* wol = state.skin.asset("wouact");
        std::clog << "Internet menu original PCX skin loaded: observer="
                  << (observer == nullptr ? std::string{} : observer->source)
                  << " wol="
                  << (wol == nullptr ? std::string{} : wol->source)
                  << " assets=" << state.skin.assets.size() << "\n";
    } else {
        std::clog << "Internet menu original PCX skin unavailable; using "
                     "generic controls (diagnostics="
                  << state.skin.diagnostics.size() << ")\n";
    }
    const auto resolvedParent = resolveRuntimeWindowParentWin32(hostWindow);
    if(!resolvedParent.supported) {
        state.result.error = "unsupported Internet multiplayer menu parent identity";
        return std::move(state.result);
    }
    state.hostWindow = resolvedParent.window;
    if (!loadRuntimeInternetMultiplayerServiceConfig(
            baseConfig.configPath,
            state.serviceConfig,
            state.result.error
        )
        || !makeRuntimeMultiplayerMenuCompatibility(
            baseConfig,
            state.compatibility,
            state.result.error
        )) {
        showInternetMenuStartupError(state.hostWindow, state.result.error);
        secureClear(state.serviceConfig.credential);
        return std::move(state.result);
    }

    RuntimeInternetServiceTransportConfig transportConfig;
    if (!makeRuntimeInternetMultiplayerTransportConfig(
            state.serviceConfig,
            transportConfig,
            state.result.error
        )) {
        showInternetMenuStartupError(state.hostWindow, state.result.error);
        secureClear(state.serviceConfig.credential);
        return std::move(state.result);
    }
    auto transport = std::make_unique<RuntimeInternetServiceTransport>();
    if (!transport->open(std::move(transportConfig), state.result.error)) {
        showInternetMenuStartupError(state.hostWindow, state.result.error);
        secureClear(state.serviceConfig.credential);
        return std::move(state.result);
    }
    state.transport = transport.get();
    state.flowBackend = std::make_unique<RuntimeInternetLobbyFlow>(
        std::move(transport)
    );
    state.window = createInternetMenuWindow(state, state.result.error);
    if (!state.window) {
        showInternetMenuStartupError(state.hostWindow, state.result.error);
        state.flowBackend->close();
        secureClear(state.serviceConfig.credential);
        return std::move(state.result);
    }
    createInternetMenuControls(state);
    initializeInternetMenu(state);
    SetTimer(
        state.window,
        internetMenuPollTimer,
        internetMenuPollMilliseconds,
        nullptr
    );
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);

    MSG message{};
    while (!state.done && state.window != nullptr
        && IsWindow(state.window)) {
        const BOOL messageResult = GetMessageW(&message, nullptr, 0U, 0U);
        if (messageResult <= 0) {
            if (messageResult == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!state.result.accepted && state.flowBackend) state.flowBackend->close();
    secureClear(state.serviceConfig.credential);
    // WM_QUIT/GetMessage failure can bypass WM_CLOSE. Retire our page while
    // its stack state/skin brush are still alive, after the original flow close
    // and credential clear and before the original result transfer.
    if (state.window != nullptr && IsWindow(state.window)) DestroyWindow(state.window);
    return std::move(state.result);
#endif
}

int runRuntimeInternetMultiplayerMenuUiSmoke(
    const RuntimeConfig& baseConfig,
    RuntimeWindowId hostWindow
) {
    RuntimeMultiplayerMenuResult result = runRuntimeInternetMultiplayerMenu(
        baseConfig,
        hostWindow
    );
    if (result.bridge) result.bridge->lan().close();
    return result.error.empty() ? 0 : 1;
}

int runRuntimeInternetMultiplayerMenuSkinWindowSmoke(
    const RuntimeConfig& baseConfig,
    const std::string& outputPath
) {
#if !defined(_WIN32)
    (void)baseConfig;
    (void)outputPath;
    return 1;
#else
    RuntimeInternetMultiplayerMenuUiState state;
    state.baseConfig = baseConfig;
    if (!loadRuntimeInternetMenuSkin(baseConfig, state.skin)) {
        std::cerr << "Internet menu production skin smoke failed: assets unavailable\n";
        return 1;
    }
    std::string windowError;
    state.window = createInternetMenuWindow(state, windowError);
    if (state.window == nullptr) {
        std::cerr << "Internet menu production skin smoke failed: "
                  << windowError << "\n";
        return 1;
    }
    createInternetMenuControls(state);
    const bool resumeMenuSmoke = GetEnvironmentVariableW(L"CONCORD_RESUME_MENU_SMOKE", nullptr, 0) != 0;
    const bool rankedMenuSmoke = GetEnvironmentVariableW(L"CONCORD_RANKED_MENU_SMOKE", nullptr, 0) != 0;
    setControlText(state.countryEdit, "Americans");
    setControlText(
        state.profileSummaryText,
        "Commander | rank #1 | W 20 / L 5 | 1210 pts"
    );
    setControlText(
        state.clanSummaryText,
        "[AC] Allied Command | rank #1 | members 2"
    );
    const std::array<std::string, 2> clanMembers{{
        "Commander | owner | online",
        "Wingman | member | online"
    }};
    const std::array<std::string, 1> clanLadder{{
        "#1 [AC] Allied Command | W 8 / L 2 | 1210 pts"
    }};
    const std::array<std::string, 1> playerLadder{{
        "#1 Commander | W 20 / L 5 | 1210 pts"
    }};
    replaceListRows(state.clanMemberList, clanMembers);
    replaceListRows(state.clanLadderList, clanLadder);
    replaceListRows(state.ladderList, playerLadder);
    showInternetMenuPage(state, InternetMenuPage::Ladder);
    if (rankedMenuSmoke) {
        RECT button{}, title{}, intersection{};
        GetWindowRect(state.matchHistoryButton, &button); GetWindowRect(state.ladderTitleText, &title);
        if (!(GetWindowLongPtrW(state.matchHistoryButton, GWL_STYLE) & WS_VISIBLE)
            || IntersectRect(&intersection, &button, &title)) { DestroyWindow(state.window); return 1; }
        SendMessageW(state.window, WM_COMMAND, MAKEWPARAM(InternetMatchHistory, BN_CLICKED), 0);
        if (state.page != InternetMenuPage::MatchHistory
            || (GetWindowLongPtrW(state.clanMemberList, GWL_STYLE) & WS_VISIBLE)
            || (GetWindowLongPtrW(state.profileSummaryText, GWL_STYLE) & WS_VISIBLE)
            || !(GetWindowLongPtrW(state.matchHistoryDetail, GWL_STYLE) & ES_READONLY)) { DestroyWindow(state.window); return 1; }
        SendMessageW(state.window, WM_COMMAND, MAKEWPARAM(InternetMatchHistoryBack, BN_CLICKED), 0);
        if (state.page != InternetMenuPage::Ladder) { DestroyWindow(state.window); return 1; }
        SendMessageW(state.window, WM_COMMAND, MAKEWPARAM(InternetMatchHistory, BN_CLICKED), 0);
        const std::uint64_t utc = 1789430400000ULL;
        const RuntimeInternetRankedPolicy policy;
        std::vector<RuntimeInternetRankedRecord> records;
        for (int mode = 0; mode < 4; ++mode) {
            RuntimeInternetRankedMatch match;
            if (!RuntimeInternetRankedMatch::authorize("ranked-20260915-" + std::to_string(mode), "ranked-room-" + std::to_string(mode),
                    {"Commander", "Opponent"}, policy, 1, utc, match, windowError)) { DestroyWindow(state.window); return 1; }
            match.start(policy, 2, utc + 1000);
            if (mode == 0) match.forfeit("Opponent", 3, utc + 30000);
            else {
                match.report("Commander", RuntimeInternetRankedResult::Win, policy, 3, utc + 30000);
                if (mode == 2) match.report("Opponent", RuntimeInternetRankedResult::Win, policy, 4, utc + 31000);
                if (mode != 3) match.expire(match.deadlineMilliseconds, utc + 150000);
            }
            records.push_back(match.record);
        }
        replaceListRows(state.matchHistoryList, projectRuntimeInternetRankedHistoryRows(records, "Commander"));
        SendMessageW(state.matchHistoryList, LB_SETCURSEL, 2, 0);
        setControlText(state.matchHistoryDetail, projectRuntimeInternetRankedHistoryDetail(records[2], "Commander"));
        setMenuStatus(state, "对局记录已结案；时间以 UTC 显示。");
        std::cout << "Ranked history menu: production button/back, page visibility, read-only detail and four result states passed.\n";
    }
    if (resumeMenuSmoke) {
        showInternetMenuPage(state, InternetMenuPage::Browser);
        std::array<wchar_t, 64> label{};GetWindowTextW(state.resumeHostButton, label.data(), static_cast<int>(label.size()));
        RECT button{}, neighbor{}, intersection{};
        GetWindowRect(state.resumeHostButton, &button);GetWindowRect(state.passwordEdit, &neighbor);
        if (std::wstring(label.data()) != L"从存档恢复"
            || !(GetWindowLongPtrW(state.resumeHostButton, GWL_STYLE) & WS_VISIBLE) || IntersectRect(&intersection, &button, &neighbor)) {
            DestroyWindow(state.window);return 1;
        }
    }
    const LONG_PTR ladderStyle = GetWindowLongPtrW(
        state.navLadderButton,
        GWL_STYLE
    );
    if ((ladderStyle & BS_TYPEMASK) != BS_OWNERDRAW
        || state.skin.asset("woluclan") == nullptr
        || state.skin.asset("private") == nullptr) {
        DestroyWindow(state.window);
        std::cerr << "Internet menu production skin smoke failed: production controls are not skinned\n";
        return 1;
    }

    RECT client{};
    GetClientRect(state.window, &client);
    const int width = std::max<LONG>(1, client.right - client.left);
    const int height = std::max<LONG>(1, client.bottom - client.top);
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(
        screen,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0U
    );
    ReleaseDC(nullptr, screen);
    if (memory == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) DeleteObject(bitmap);
        if (memory != nullptr) DeleteDC(memory);
        DestroyWindow(state.window);
        std::cerr << "Internet menu production skin smoke failed: DIB creation\n";
        return 1;
    }
    HGDIOBJ previous = SelectObject(memory, bitmap);
    if (rankedMenuSmoke) {
        // WM_PRINT's default child pass includes this hidden test window's
        // non-client offset. Use client-relative child rectangles instead.
        SendMessageW(state.window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
        for (HWND child = GetWindow(state.window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
            if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE)) continue;
            RECT bounds{}; GetWindowRect(child, &bounds);
            MapWindowPoints(nullptr, state.window, reinterpret_cast<POINT*>(&bounds), 2);
            const int saved = SaveDC(memory);
            SetViewportOrgEx(memory, bounds.left, bounds.top, nullptr);
            IntersectClipRect(memory, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
            SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT | PRF_NONCLIENT | PRF_ERASEBKGND);
            RestoreDC(memory, saved);
        }
    } else {
    SendMessageW(
        state.window,
        WM_PRINT,
        reinterpret_cast<WPARAM>(memory),
        PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND
    );
    }
    GdiFlush();
    const std::uint8_t* pixels = static_cast<const std::uint8_t*>(bits);
    bool variedPixels = false;
    for (int pixel = 1; pixel < width * height; ++pixel) {
        if (pixels[pixel * 4] != pixels[0]
            || pixels[pixel * 4 + 1] != pixels[1]
            || pixels[pixel * 4 + 2] != pixels[2]) {
            variedPixels = true;
            break;
        }
    }

    const std::filesystem::path base(outputPath);
    const std::filesystem::path productionPath = base.parent_path()
        / (base.stem().string() + "_production"
            + (base.extension().empty()
                ? std::string(".bmp")
                : base.extension().string()));
    std::ofstream output(
        productionPath,
        std::ios::binary | std::ios::trunc
    );
    const DWORD pixelBytes = static_cast<DWORD>(width * height * 4);
    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelBytes;
    bitmapInfo.bmiHeader.biSizeImage = pixelBytes;
    output.write(
        reinterpret_cast<const char*>(&fileHeader),
        sizeof(fileHeader)
    );
    output.write(
        reinterpret_cast<const char*>(&bitmapInfo.bmiHeader),
        sizeof(bitmapInfo.bmiHeader)
    );
    output.write(reinterpret_cast<const char*>(bits), pixelBytes);
    const bool written = output.good();
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    DestroyWindow(state.window);
    if (!variedPixels || !written) {
        std::cerr << "Internet menu production skin smoke failed: production capture\n";
        return 1;
    }
    std::cout << "Internet menu production skin smoke passed: page=" << (rankedMenuSmoke ? "RankedHistory" : resumeMenuSmoke ? "ResumeBrowser" : "Clan/Ladder")
              << " ownerDraw=yes source="
              << state.skin.asset("woluclan")->source
              << " output=" << productionPath.generic_string() << "\n";
    if (resumeMenuSmoke) {
        extern int runRuntimeMultiplayerResumeMenuWindowSmoke(const std::string&);
        const auto lanPath = base.parent_path() / (base.stem().string() + "_lan.bmp");
        if (runRuntimeMultiplayerResumeMenuWindowSmoke(lanPath.string()) != 0) return 1;
        std::cout << "Resume menu controls: LAN and Internet buttons, captions, selection notifications and browser capture passed.\n";
    }
    return 0;
#endif
}
#endif

#if defined(_WIN32) && !defined(RUNTIME_INTERNET_MULTIPLAYER_MENU_PURE_HELPERS_ONLY)
#include "ui/network/diagnostics/RuntimeInternetMenuHostLifecycleSmoke.inl"
#endif
