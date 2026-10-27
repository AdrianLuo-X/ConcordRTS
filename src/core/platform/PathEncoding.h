#pragma once

// Process-wide narrow-string encoding: UTF-8.
//
// Windows hands us two incompatible narrow encodings by default:
//   * the CRT converts `argv` / `getenv` through the ANSI code page (936 on a
//     zh-CN system, 1252 elsewhere);
//   * `std::filesystem::path::string()` / `path(std::string)` and the
//     `std::ifstream(const std::string&)` overloads convert through the global
//     C++ locale, which starts as the classic "C" locale.
// Mixing those two conventions silently produces paths that do not exist, and
// `std::filesystem` then throws from operations such as directory iteration,
// which terminates the process (0xC0000409) without any diagnostic when the
// install directory contains non-ASCII characters.
//
// ConcordRTS therefore pins one convention: every narrow `std::string` that
// carries a path or a filesystem-like identifier is UTF-8.
//   * `enableUtf8NarrowEncoding()` installs an otherwise classic C++ locale whose
//     only difference is a UTF-8 / UTF-16 `codecvt` facet, so `path(std::string)`
//     and the narrow stream-open overloads read and write UTF-8. Numeric
//     formatting stays classic (no thousands separators), which the UI and the
//     replay/hash encoders rely on.
//   * it also switches the C locale's LC_CTYPE to ".UTF-8". `path::string()`
//     converts wide -> narrow through the code page of that locale and ignores the
//     C++ facet entirely, so this is the only supported way to keep both
//     directions in UTF-8 without changing the process ANSI code page (which the
//     game's legacy GBK data still depends on).
//   * `utf8Arguments()` converts the `argv` handed over by the CRT into UTF-8
//     before anything parses it. It passes already-UTF-8 bytes through, so a
//     UTF-8 launcher and a legacy ANSI console both work.
//
// Call `enableUtf8NarrowEncoding()` first thing in every entry point, before
// any path is constructed.

#ifndef _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#endif

#include <clocale>
#include <codecvt>
#include <cstddef>
#include <cstdlib>
#include <locale>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
// Keep <windows.h> from injecting the min/max macros and the rest of the
// rarely used surface into every translation unit that includes this header.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace concord::platform {

namespace detail {

inline std::string convertCodePage(std::string_view value, unsigned int fromCodePage, unsigned int toCodePage) {
    if (value.empty()) {
        return {};
    }
    if (fromCodePage == toCodePage) {
        return std::string(value);
    }
#ifdef _WIN32
    const int wideLength = ::MultiByteToWideChar(
        fromCodePage,
        0,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (wideLength <= 0) {
        return std::string(value);
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    ::MultiByteToWideChar(
        fromCodePage,
        0,
        value.data(),
        static_cast<int>(value.size()),
        wide.data(),
        wideLength);
    const int narrowLength = ::WideCharToMultiByte(
        toCodePage,
        0,
        wide.data(),
        wideLength,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (narrowLength <= 0) {
        return std::string(value);
    }
    std::string converted(static_cast<std::size_t>(narrowLength), '\0');
    ::WideCharToMultiByte(
        toCodePage,
        0,
        wide.data(),
        wideLength,
        converted.data(),
        narrowLength,
        nullptr,
        nullptr);
    return converted;
#else
    (void)fromCodePage;
    (void)toCodePage;
    return std::string(value);
#endif
}

} // namespace detail

// Installs the UTF-8 narrow encoding for `std::filesystem` and the stream path
// overloads. Idempotent; returns whether the C locale accepted the UTF-8
// LC_CTYPE, which is what keeps wide -> narrow conversion in UTF-8.
#if defined(_MSC_VER)
// std::codecvt_utf8_utf16 is the only standard-provided UTF-8 <-> UTF-16 facet;
// its C++17 deprecation notice does not apply to the conversion we need here.
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
[[nodiscard]] inline bool enableUtf8NarrowEncoding() {
    static const bool active = [] {
        bool ctypeInstalled = false;
#ifdef _WIN32
        // `std::filesystem::path` converts wide -> narrow through the code page of
        // the current LC_CTYPE locale (not through the C++ locale facet), so the
        // C locale has to be switched as well. Only LC_CTYPE is touched: the
        // numeric and monetary locales stay classic, which keeps the UI,
        // replay and hash encoders free of thousands separators.
        if (std::setlocale(LC_CTYPE, ".UTF-8") != nullptr) {
            ctypeInstalled = true;
        }
#endif
        try {
            std::locale utf8Locale(
                std::locale::classic(),
                new std::codecvt_utf8_utf16<wchar_t>);
            std::locale::global(utf8Locale);
            return ctypeInstalled;
        } catch (...) {
            // Without the facet the process keeps the ANSI code page behaviour,
            // which still works for ASCII install paths.
            return false;
        }
    }();
    return active;
}
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Whether `value` is a structurally valid UTF-8 byte sequence.
inline bool isValidUtf8(std::string_view value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char lead = static_cast<unsigned char>(value[index]);
        std::size_t extra = 0;
        unsigned int codePoint = 0;
        if (lead < 0x80) {
            ++index;
            continue;
        }
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            codePoint = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            codePoint = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            codePoint = lead & 0x07u;
        } else {
            return false;
        }
        if (index + extra >= value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset <= extra; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3Fu);
        }
        // Reject overlong encodings, surrogates and out-of-range code points.
        const unsigned int minimum = extra == 1 ? 0x80u : (extra == 2 ? 0x800u : 0x10000u);
        if (codePoint < minimum || codePoint > 0x10FFFFu) {
            return false;
        }
        if (codePoint >= 0xD800u && codePoint <= 0xDFFFu) {
            return false;
        }
        index += extra + 1;
    }
    return true;
}

// Code page used to decode legacy game text (ANSI .ini/.csf payloads and
// retail archives). The game data is a double-byte Simplified Chinese build, so
// if a future build ever runs the process itself as UTF-8 the legacy fallback
// must stay on the data's own code page (936) instead of following the then
// UTF-8 ANSI code page.
inline unsigned int legacyTextCodePage() {
#ifdef _WIN32
    const unsigned int ansi = ::GetACP();
    return ansi == CP_UTF8 ? 936u : ansi;
#else
    return 936u;
#endif
}

// Converts a string produced by the Windows ANSI APIs (CRT `argv`, `getenv`)
// into the UTF-8 convention used everywhere else. Already-UTF-8 input is
// returned unchanged, so a UTF-8 console and a legacy ANSI console both work.
inline std::string acpToUtf8(std::string_view value) {
    if (isValidUtf8(value)) {
        return std::string(value);
    }
#ifdef _WIN32
    return detail::convertCodePage(value, legacyTextCodePage(), CP_UTF8);
#else
    return std::string(value);
#endif
}

// Converts a UTF-8 string into the Windows ANSI code page, for the rare API
// that has no wide variant.
inline std::string utf8ToAcp(std::string_view value) {
#ifdef _WIN32
    return detail::convertCodePage(value, CP_UTF8, ::GetACP());
#else
    return std::string(value);
#endif
}

// Reads an environment variable as UTF-8 (the CRT returns ACP bytes).
inline std::string environmentUtf8(const char* name) {
    if (name == nullptr) {
        return {};
    }
    const char* value = std::getenv(name);
    return value != nullptr ? acpToUtf8(value) : std::string();
}

// UTF-8 copy of `argv` (the CRT hands over ACP bytes).
inline std::vector<std::string> utf8Arguments(int argc, char** argv) {
    std::vector<std::string> arguments;
    if (argc <= 0 || argv == nullptr) {
        return arguments;
    }
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        const char* raw = argv[index] != nullptr ? argv[index] : "";
        arguments.push_back(acpToUtf8(raw));
    }
    return arguments;
}

// Mutable `char*` view of `arguments`, for APIs that take `int, char**`.
inline std::vector<char*> argumentPointers(std::vector<std::string>& arguments) {
    std::vector<char*> pointers;
    pointers.reserve(arguments.size());
    for (std::string& argument : arguments) {
        pointers.push_back(argument.data());
    }
    return pointers;
}

#ifdef _WIN32
// Makes console log output readable when the strings are UTF-8.
inline void enableUtf8ConsoleOutput() {
    ::SetConsoleOutputCP(CP_UTF8);
}
#endif

} // namespace concord::platform
