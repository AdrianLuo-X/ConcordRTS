#pragma once

#include <filesystem>

// Private diagnostic assembly; normal menu/public UI contracts stay unchanged.
int runRuntimeInternetMenuClockContract(const std::filesystem::path& output);
