#pragma once
#include <string>
class IniFile;

// The original saved bytes contain opaque numeric TeamMissions records.
// File and memory entry points share one explicit document parsing policy.
bool loadRuntimeSaveDocument(const std::string& path, IniFile& document);
bool parseRuntimeSaveDocument(const std::string& text, IniFile& document);
