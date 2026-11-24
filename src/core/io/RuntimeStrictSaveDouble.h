#pragma once
#include "core/io/RuntimeSaveTextFields.h"
namespace runtime_save_text {
inline bool parseRuntimeFiniteDouble(const std::string& value, double& output) {
    try {
        const std::string trimmed = trimAsciiLocal(value);
        std::size_t consumed = 0;
        const double parsed = std::stod(trimmed, &consumed);
        if (consumed != trimmed.size() || !std::isfinite(parsed)) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}
}
