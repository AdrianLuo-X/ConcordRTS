#pragma once

#include <filesystem>
#include <string>

// Synchronous, owned filesystem values. No directory enumeration, case folding,
// resource selection or INI decoding takes place here. Injected directories make
// root discovery independently testable without changing process state.
std::filesystem::path runtimeConfigProjectRoot(
    const std::filesystem::path& currentDirectory,
    const std::filesystem::path& executableDirectory);

// Absolute input wins; otherwise an existing cwd candidate wins; otherwise use
// the project root even if the optional file does not exist. weakly_canonical
// errors retain the original lexical-normalization fallback.
std::filesystem::path runtimeResolveConfigPath(
    const std::filesystem::path& value,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& currentDirectory);

// Compatibility boundary for RuntimeConfig's existing narrow string consumers:
// generic_string uses the native narrow code page on Windows, not UTF-8.
// Arbitrary Unicode output through these legacy fields is not promised. Native
// paths above never pass through this conversion before filesystem lookup.
std::string runtimeConfigLegacyPathString(const std::filesystem::path& path);
