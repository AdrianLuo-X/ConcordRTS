#pragma once

#include "assets/codecs/RuntimeTmpTypes.h"

#include <string>
#include <vector>

// Complete original ordinary-TMP and raw-format-3 decoder. On failure preserve
// the original partially modified tile and exact error/warning text.
bool loadRuntimeTmpTileBytes(
    const std::vector<unsigned char>& bytes,
    const std::string& sourceName,
    RuntimeTmpTileFile& tileFile,
    std::string& errorMessage
);