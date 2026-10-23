#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <vector>

// Reporting metadata only. This analysis tool does not execute or attest a
// production session, even when its input contains fields labelled runtime.
struct OfflineReportEvidence {
    std::string kind = "offline-report";
    std::string conclusionSource = "not-evaluated";
    std::string coverage = "not-evaluated";
    std::string inputKind = "none";
    std::string inputPath;
    std::string inputStatus = "not-supplied";
    std::string inputError;
    int inputEventCount = 0;
    std::vector<std::string> unknowns;
};

namespace offline_report {

inline void writeJsonString(std::ostream& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20U) {
                output << "\\u00" << hex[character >> 4U] << hex[character & 0x0fU];
            } else {
                output << static_cast<char>(character);
            }
            break;
        }
    }
    output << '"';
}

inline void writeJsonStrings(std::ostream& output, const std::vector<std::string>& values) {
    output << '[';
    bool comma = false;
    for (const auto& value : values) {
        if (comma) output << ',';
        writeJsonString(output, value);
        comma = true;
    }
    output << ']';
}

inline void writeEvidenceJson(std::ostream& output, const OfflineReportEvidence& evidence) {
    output << "{\"kind\":";
    writeJsonString(output, evidence.kind);
    output << ",\"conclusionSource\":";
    writeJsonString(output, evidence.conclusionSource);
    output << ",\"coverage\":";
    writeJsonString(output, evidence.coverage);
    output << ",\"productionRuntimeValidation\":\"not-run\""
              ",\"productionRuntimeValidated\":false,\"producerVerified\":false,\"producerUtc\":null"
              ",\"input\":{\"kind\":";
    writeJsonString(output, evidence.inputKind);
    output << ",\"path\":";
    writeJsonString(output, evidence.inputPath);
    output << ",\"status\":";
    writeJsonString(output, evidence.inputStatus);
    output << ",\"error\":";
    writeJsonString(output, evidence.inputError);
    output << ",\"eventCount\":" << evidence.inputEventCount << "},\"unknowns\":";
    writeJsonStrings(output, evidence.unknowns);
    output << '}';
}

inline void printEvidence(std::ostream& output, const OfflineReportEvidence& evidence) {
    output << "Evidence provenance (production runtime acceptance: not-run):\n";
    writeEvidenceJson(output, evidence);
    output << '\n';
}

} // namespace offline_report
