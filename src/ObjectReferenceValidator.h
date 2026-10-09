#pragma once

#include "IniFile.h"
#include "RulesDatabase.h"

#include <string>
#include <vector>

enum class DiagnosticSeverity {
    Error,
    Warning,
    Info
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Info;
    std::string source;
    std::string field;
    std::string target;
    std::string message;
};

struct DiagnosticReport {
    std::vector<Diagnostic> diagnostics;

    int errorCount() const;
    int warningCount() const;
    int infoCount() const;
};

class ObjectReferenceValidator {
public:
    DiagnosticReport validate(const IniFile& ini, const RulesDatabase& rules) const;
};

std::string toString(DiagnosticSeverity severity);
