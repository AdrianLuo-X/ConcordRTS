#pragma once

#include <filesystem>

// Private OS path provider used by RuntimeConfig root discovery. Returns an
// owned native path. Discovery failure retains the original cwd fallback.
std::filesystem::path runtimeExecutableDirectory();

// Video helper discovery historically returns empty on failure (not cwd).
// The fixed Windows buffer and truncation rule remain local to this provider.
std::filesystem::path runtimeVideoExecutableDirectory();
