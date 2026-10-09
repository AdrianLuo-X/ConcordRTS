#include "RuntimeMultiplayerCompatibilityFingerprint.h"

#include "RuntimeLanTransport.h"
#include "RuntimeMultiplayerGameplayCommand.h"
#include "RuntimeMultiplayerLockstep.h"
#include "RuntimeMultiplayerMatchSetup.h"
#include "RuntimeMultiplayerProtocol.h"
#include "RuntimeMultiplayerSessionSystem.h"
#include "RuntimeMultiplayerSnapshotTransfer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using ManifestField = RuntimeMultiplayerProtocolManifestField;
using ManifestValue = RuntimeMultiplayerProtocolManifestValue;
using ContentComponent = RuntimeMultiplayerContentComponent;

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Runtime multiplayer compatibility fingerprint smoke failure: "
            << message << '\n';
    }
    return condition;
}

const ManifestValue* findManifestValue(
    const std::vector<ManifestValue>& manifest,
    ManifestField field
) {
    const auto found = std::find_if(
        manifest.begin(),
        manifest.end(),
        [field](const ManifestValue& value) {
            return value.field == field;
        }
    );
    return found == manifest.end() ? nullptr : &*found;
}

bool manifestFingerprintRejectedAtomically(
    const std::vector<ManifestValue>& manifest,
    const std::string& label
) {
    std::uint64_t output = 0xfeedfacecafebeefULL;
    std::string error;
    return expect(
        !runtimeMultiplayerProtocolManifestFingerprint(
            manifest,
            output,
            error
        )
            && output == 0xfeedfacecafebeefULL && !error.empty(),
        label + " manifest was not rejected transactionally"
    );
}

bool contentFingerprint(
    const std::vector<ContentComponent>& components,
    std::uint64_t& fingerprint,
    const std::string& label,
    std::size_t maximumBytes =
        runtimeMultiplayerContentDefaultMaximumComponentBytes
) {
    std::string error;
    return expect(
        runtimeMultiplayerContentFingerprint(
            components,
            fingerprint,
            error,
            maximumBytes
        ),
        label + " content fingerprint failed: " + error
    );
}

bool canonicalizationRejectedAtomically(
    std::vector<ContentComponent> components,
    const std::string& label,
    std::size_t maximumBytes =
        runtimeMultiplayerContentDefaultMaximumComponentBytes
) {
    const std::vector<ContentComponent> before = components;
    std::string error;
    return expect(
        !canonicalizeRuntimeMultiplayerContentComponents(
            components,
            error,
            maximumBytes
        )
            && components == before && !error.empty(),
        label + " content id/set was not rejected transactionally"
    );
}

bool testProtocolManifest() {
    bool success = true;
    static_assert(sizeof(ManifestValue::value) == sizeof(std::uint64_t));

    std::vector<ManifestValue> manifest;
    std::string error;
    success &= expect(
        runtimeMultiplayerCurrentProtocolManifest(manifest, error)
            && !manifest.empty(),
        "current protocol manifest was not built: " + error
    );
    std::uint64_t fingerprint = 0U;
    success &= expect(
        runtimeMultiplayerProtocolManifestFingerprint(
            manifest,
            fingerprint,
            error
        )
            && fingerprint != 0U,
        "current protocol manifest did not hash: " + error
    );
    std::uint64_t directFingerprint = 0U;
    success &= expect(
        runtimeMultiplayerCurrentProtocolFingerprint(
            directFingerprint,
            error
        )
            && directFingerprint == fingerprint,
        "current protocol fingerprint helper disagreed: " + error
    );

    const auto valueEquals = [&manifest](
        ManifestField field,
        std::uint64_t expected
    ) {
        const ManifestValue* value = findManifestValue(manifest, field);
        return value != nullptr && value->value == expected;
    };
    success &= expect(
        valueEquals(
            ManifestField::LanFramingVersion,
            runtimeLanProtocolVersion
        )
            && valueEquals(
                ManifestField::MultiplayerMessageVersion,
                runtimeMultiplayerMessageVersion
            )
            && valueEquals(
                ManifestField::SessionProtocolVersion,
                runtimeMultiplayerProtocolVersion
            )
            && valueEquals(
                ManifestField::MatchSetupVersion,
                runtimeMultiplayerMatchSetupVersion
            )
            && valueEquals(
                ManifestField::GameplayCommandVersion,
                runtimeMultiplayerGameplayCommandVersion
            )
            && valueEquals(
                ManifestField::LockstepFrameVersion,
                runtimeMultiplayerGameplayFrameVersion
            )
            && valueEquals(
                ManifestField::SnapshotTransferVersion,
                runtimeMultiplayerSnapshotTransferVersion
            ),
        "current manifest did not source every module version constant"
    );
    success &= expect(
        valueEquals(
            ManifestField::LanMaximumPacketBytes,
            runtimeLanMaximumPacketBytes
        )
            && valueEquals(
                ManifestField::SessionMaximumCommandPayloadBytes,
                runtimeMultiplayerMaximumCommandPayloadBytes
            )
            && valueEquals(
                ManifestField::MatchMaximumPayloadBytes,
                runtimeMultiplayerMatchMaximumPayloadBytes
            )
            && valueEquals(
                ManifestField::GameplayMaximumPayloadBytes,
                runtimeMultiplayerGameplayMaximumPayloadBytes
            )
            && valueEquals(
                ManifestField::LockstepMaximumFrameBytes,
                runtimeMultiplayerLockstepMaximumFrameBytes
            )
            && valueEquals(
                ManifestField::SnapshotHardMaximumBytes,
                runtimeMultiplayerSnapshotHardMaximumBytes
            ),
        "current manifest did not source representative hard limits"
    );

    std::vector<ManifestValue> reordered = manifest;
    std::reverse(reordered.begin(), reordered.end());
    std::uint64_t reorderedFingerprint = 0U;
    success &= expect(
        runtimeMultiplayerProtocolManifestFingerprint(
            reordered,
            reorderedFingerprint,
            error
        )
            && reorderedFingerprint == fingerprint,
        "manifest input order changed the canonical fingerprint: " + error
    );

    for (std::size_t index = 0U; index < manifest.size(); ++index) {
        std::vector<ManifestValue> changed = manifest;
        changed[index].value =
            changed[index].value == std::numeric_limits<std::uint64_t>::max()
                ? changed[index].value - 1U
                : changed[index].value + 1U;
        std::uint64_t changedFingerprint = 0U;
        error.clear();
        success &= expect(
            runtimeMultiplayerProtocolManifestFingerprint(
                changed,
                changedFingerprint,
                error
            )
                && changedFingerprint != fingerprint,
            "manifest field mutation was not covered at index "
                + std::to_string(index) + ": " + error
        );
    }

    std::vector<ManifestValue> invalid = manifest;
    invalid.pop_back();
    success &= manifestFingerprintRejectedAtomically(invalid, "missing field");
    invalid = manifest;
    invalid.back().field = invalid.front().field;
    success &= manifestFingerprintRejectedAtomically(
        invalid,
        "duplicate field"
    );
    invalid = manifest;
    invalid.back().field = static_cast<ManifestField>(0xffffU);
    success &= manifestFingerprintRejectedAtomically(invalid, "unknown field");
    invalid = manifest;
    invalid.front().value = 0U;
    success &= manifestFingerprintRejectedAtomically(invalid, "zero value");
    return success;
}

bool testContentCanonicalizationAndFingerprint() {
    bool success = true;
    const std::vector<ContentComponent> authored{
        {"\tRules\\RULESMD.INI \r\n", {0x10U, 0x20U, 0x30U}},
        {"MAPS\\Official\\Two.MAP", {0x00U, 0xffU}}
    };
    std::vector<ContentComponent> canonical = authored;
    std::string error;
    success &= expect(
        canonicalizeRuntimeMultiplayerContentComponents(
            canonical,
            error
        )
            && canonical.size() == 2U
            && canonical[0U].canonicalResourceId
                == "maps/official/two.map"
            && canonical[1U].canonicalResourceId == "rules/rulesmd.ini",
        "content ids were not normalized and sorted: " + error
    );

    std::uint64_t baseline = 0U;
    success &= contentFingerprint(authored, baseline, "authored");
    success &= expect(baseline != 0U, "content fingerprint was zero");
    std::vector<ContentComponent> alternate{
        {"maps/official/two.map", {0x00U, 0xffU}},
        {"RULES/RULESMD.INI", {0x10U, 0x20U, 0x30U}}
    };
    std::uint64_t alternateFingerprint = 0U;
    success &= contentFingerprint(
        alternate,
        alternateFingerprint,
        "reordered/case alternate"
    );
    success &= expect(
        alternateFingerprint == baseline,
        "component order or legal id spelling changed content fingerprint"
    );

    std::vector<ContentComponent> changed = alternate;
    changed[0U].bytes[0U] ^= 1U;
    std::uint64_t changedFingerprint = 0U;
    success &= contentFingerprint(changed, changedFingerprint, "changed bytes");
    success &= expect(
        changedFingerprint != baseline,
        "changed component bytes did not change content fingerprint"
    );
    changed = alternate;
    changed[0U].canonicalResourceId = "maps/official/other.map";
    success &= contentFingerprint(changed, changedFingerprint, "changed id");
    success &= expect(
        changedFingerprint != baseline,
        "changed component id did not change content fingerprint"
    );
    changed = alternate;
    changed.push_back({"art/extra.ini", {1U}});
    success &= contentFingerprint(changed, changedFingerprint, "changed set");
    success &= expect(
        changedFingerprint != baseline,
        "changed component set did not change content fingerprint"
    );

    const std::vector<ContentComponent> ambiguousLeft{
        {"ab", {static_cast<std::uint8_t>('c')}}
    };
    const std::vector<ContentComponent> ambiguousRight{
        {"a", {
            static_cast<std::uint8_t>('b'),
            static_cast<std::uint8_t>('c')
        }}
    };
    std::uint64_t leftFingerprint = 0U;
    std::uint64_t rightFingerprint = 0U;
    success &= contentFingerprint(
        ambiguousLeft,
        leftFingerprint,
        "ambiguous left"
    );
    success &= contentFingerprint(
        ambiguousRight,
        rightFingerprint,
        "ambiguous right"
    );
    success &= expect(
        leftFingerprint != rightFingerprint,
        "explicit lengths did not separate an ambiguous concatenation"
    );

    const std::vector<ContentComponent> emptyFileComponent{
        {"empty/resource.ini", {}}
    };
    success &= contentFingerprint(
        emptyFileComponent,
        changedFingerprint,
        "empty component bytes"
    );
    success &= canonicalizationRejectedAtomically({}, "empty component set");

    const std::vector<ContentComponent> duplicate{
        {"Maps\\A.INI", {1U}},
        {" maps/a.ini ", {2U}}
    };
    success &= canonicalizationRejectedAtomically(
        duplicate,
        "canonical duplicate"
    );

    const std::vector<std::string> unsafeIds{
        "", "   ", "/absolute/file", "\\absolute\\file",
        "C:\\local\\file", "../escape", "a/../escape", "a//b",
        "a/./b", "a/", "a\tb", "a\x01b"
    };
    for (const std::string& unsafeId : unsafeIds) {
        success &= canonicalizationRejectedAtomically(
            {{unsafeId, {1U}}},
            "unsafe id"
        );
    }
    success &= canonicalizationRejectedAtomically(
        {{std::string(
              runtimeMultiplayerContentMaximumCanonicalResourceIdBytes + 1U,
              'a'
          ),
          {1U}}},
        "oversized id"
    );
    success &= canonicalizationRejectedAtomically(
        {{"bounded/file", {1U, 2U, 3U, 4U, 5U}}},
        "oversized component",
        4U
    );
    success &= canonicalizationRejectedAtomically(
        {{"bounded/file", {}}},
        "zero configured component maximum",
        0U
    );
    success &= canonicalizationRejectedAtomically(
        {{"bounded/file", {}}},
        "configured component maximum above hard cap",
        runtimeMultiplayerContentHardMaximumComponentBytes + 1U
    );
    std::vector<ContentComponent> tooMany(
        runtimeMultiplayerContentMaximumComponents + 1U
    );
    for (std::size_t index = 0U; index < tooMany.size(); ++index) {
        tooMany[index].canonicalResourceId =
            "component/" + std::to_string(index);
    }
    success &= canonicalizationRejectedAtomically(
        std::move(tooMany),
        "excessive component count"
    );

    std::uint64_t unchanged = 0x123456789abcdef0ULL;
    error.clear();
    success &= expect(
        !runtimeMultiplayerContentFingerprint(
            duplicate,
            unchanged,
            error
        )
            && unchanged == 0x123456789abcdef0ULL && !error.empty(),
        "failed content fingerprint changed its output"
    );
    return success;
}

struct TemporaryContentFiles {
    std::filesystem::path directory;
    std::filesystem::path payload;
    std::filesystem::path payloadCopy;
    std::filesystem::path empty;

    ~TemporaryContentFiles() {
        std::error_code cleanupError;
        if (!payload.empty()) std::filesystem::remove(payload, cleanupError);
        cleanupError.clear();
        if (!payloadCopy.empty()) {
            std::filesystem::remove(payloadCopy, cleanupError);
        }
        cleanupError.clear();
        if (!empty.empty()) std::filesystem::remove(empty, cleanupError);
        cleanupError.clear();
        if (!directory.empty()) {
            std::filesystem::remove(directory, cleanupError);
        }
    }
};

bool writeFile(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes
) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }
    return static_cast<bool>(output);
}

bool loaderRejectedAtomically(
    const std::filesystem::path& path,
    const std::string& canonicalId,
    std::size_t maximumBytes,
    const std::string& label
) {
    ContentComponent output{"unchanged/id", {0xdeU, 0xadU}};
    const ContentComponent before = output;
    std::string error;
    return expect(
        !loadRuntimeMultiplayerContentComponentFile(
            path,
            canonicalId,
            output,
            error,
            maximumBytes
        )
            && output == before && !error.empty(),
        label + " file load was not rejected transactionally"
    );
}

bool testBoundedFileLoader() {
    bool success = true;
    TemporaryContentFiles files;
    std::error_code fileError;
    const auto unique = std::chrono::high_resolution_clock::now()
        .time_since_epoch().count();
    files.directory = std::filesystem::temp_directory_path(fileError)
        / ("concord_multiplayer_content_" + std::to_string(unique));
    if (fileError
        || !std::filesystem::create_directory(files.directory, fileError)
        || fileError) {
        return expect(false, "temporary content directory could not be made");
    }
    files.payload = files.directory / "payload.bin";
    files.payloadCopy = files.directory / "payload-copy.bin";
    files.empty = files.directory / "empty.bin";
    const std::vector<std::uint8_t> payloadBytes{
        0x00U, 0x10U, 0xffU, 0x7fU, 0x42U
    };
    success &= expect(
        writeFile(files.payload, payloadBytes)
            && writeFile(files.payloadCopy, payloadBytes)
            && writeFile(files.empty, {}),
        "temporary content fixtures could not be written"
    );

    ContentComponent loaded;
    std::string error;
    success &= expect(
        loadRuntimeMultiplayerContentComponentFile(
            files.payload,
            " Rules\\Mode.INI ",
            loaded,
            error
        )
            && loaded.canonicalResourceId == "rules/mode.ini"
            && loaded.bytes == payloadBytes,
        "bounded file loader lost id or bytes: " + error
    );
    ContentComponent loadedCopy;
    success &= expect(
        loadRuntimeMultiplayerContentComponentFile(
            files.payloadCopy,
            "rules/mode.ini",
            loadedCopy,
            error
        )
            && loadedCopy == loaded,
        "local filesystem path leaked into loaded authority data: " + error
    );
    std::uint64_t firstFingerprint = 0U;
    std::uint64_t copyFingerprint = 0U;
    success &= contentFingerprint({loaded}, firstFingerprint, "loaded file");
    success &= contentFingerprint(
        {loadedCopy},
        copyFingerprint,
        "loaded copy"
    );
    success &= expect(
        firstFingerprint == copyFingerprint,
        "identical resources at different local paths hashed differently"
    );

    ContentComponent loadedEmpty;
    success &= expect(
        loadRuntimeMultiplayerContentComponentFile(
            files.empty,
            "empty/file.ini",
            loadedEmpty,
            error
        )
            && loadedEmpty.canonicalResourceId == "empty/file.ini"
            && loadedEmpty.bytes.empty(),
        "explicit empty-file contract was not accepted: " + error
    );

    success &= loaderRejectedAtomically(
        files.directory / "missing.bin",
        "missing/file.ini",
        runtimeMultiplayerContentDefaultMaximumComponentBytes,
        "missing"
    );
    success &= loaderRejectedAtomically(
        files.directory,
        "directory/not-file",
        runtimeMultiplayerContentDefaultMaximumComponentBytes,
        "directory"
    );
    success &= loaderRejectedAtomically(
        files.payload,
        "too-large/file.ini",
        4U,
        "configured oversize"
    );
    success &= loaderRejectedAtomically(
        files.payload,
        "../unsafe.ini",
        runtimeMultiplayerContentDefaultMaximumComponentBytes,
        "unsafe canonical id"
    );
    success &= loaderRejectedAtomically(
        files.payload,
        "limits/zero.ini",
        0U,
        "zero maximum"
    );
    success &= loaderRejectedAtomically(
        files.payload,
        "limits/hard.ini",
        runtimeMultiplayerContentHardMaximumComponentBytes + 1U,
        "maximum above hard cap"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testProtocolManifest();
    success &= testContentCanonicalizationAndFingerprint();
    success &= testBoundedFileLoader();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer compatibility fingerprint smoke:"
        << " manifest=canonical fnv64=yes content=canonical"
        << " fileLimitMiB=64 hardMiB=128 emptyFile=allowed\n";
    return 0;
}
