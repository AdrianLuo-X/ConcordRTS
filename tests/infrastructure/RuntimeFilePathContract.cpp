#include "core/io/IniFile.h"
#include "runtime/config/RuntimeConfigPaths.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
int checks = 0;

void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

void write(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    require(!out.fail(), "fixture write failed");
}

struct CaptureError {
    std::ostringstream text;
    std::streambuf* original = std::cerr.rdbuf(text.rdbuf());
    ~CaptureError() { std::cerr.rdbuf(original); }
};

void checkIni(const fs::path& output) {
    const auto unicode = fs::path(u8"中文路径/配置-🌲.ini");
    const std::string nativeBytes("\xd6\xd0\xce\xc4\xff", 5);
    const std::string utf8Bytes("\xe4\xb8\xad\xe6\x96\x87", 6);
    const std::string content =
        "[MiXeD]\r\nKey=first\r\nKEY=last\r\nBytes=" + nativeBytes
        + "\r\nUtf8=" + utf8Bytes + "\r\nUrl=a//comment\r\n"
        "[mixed]\r\nTail=kept\r\n[IsoMapPack5]\r\n1=AB//CD; comment\r\n";
    const fs::path native = output / unicode;
    const fs::path ascii = output / "reference.ini";
    write(native, content); write(ascii, content);
    IniFile actual, reference;
    require(actual.loadFromPath(native), "native Unicode filename did not open");
    require(reference.loadFromFile(ascii.string()), "legacy reference failed");
    require(actual.getValue("mixed", "key") == "last", "case/last-key precedence changed");
    require(actual.getValue("MIXED", "bytes") == nativeBytes, "legacy INI bytes transcoded");
    require(actual.getValue("mixed", "Utf8") == utf8Bytes, "UTF-8 INI bytes transcoded");
    require(actual.getValue("mixed", "Tail") == "kept", "repeated section merge changed");
    require(actual.getValue("mixed", "Url") == "a", "slash comment handling changed");
    require(actual.getValue("IsoMapPack5", "1") == "AB//CD", "packed section slash bytes changed");
    require(actual.getSectionNames() == reference.getSectionNames(), "section enumeration order changed");
    for (const auto& name : reference.getSectionNames()) {
        require(*actual.getSection(name) == *reference.getSection(name), "section key order/bytes changed");
    }
    const auto bytesAfter = [&] {
        std::ifstream in(native, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }();
    require(bytesAfter == content, "reading changed source bytes");

    IniFile overlay;
    overlay.loadFromString("[MIXED]\nkey=overlay\n");
    actual.appendFrom(overlay);
    require(actual.getValue("mixed", "Key") == "overlay", "overlay priority changed");
    {
        CaptureError error;
        require(!actual.loadFromPath(output / fs::path(u8"中文路径/缺失.ini"), "original-label"), "missing file accepted");
        require(error.text.str() == "Failed to open INI file: original-label\n", "diagnostic spelling changed");
        require(actual.getValue("mixed", "Key") == "overlay", "open failure cleared prior state");
    }
    {
        CaptureError error;
        const fs::path missing = output / "legacy-missing.ini";
        require(!reference.loadFromFile(missing.string()), "legacy missing file accepted");
        require(error.text.str() == "Failed to open INI file: " + missing.string() + "\n", "legacy diagnostic changed");
        require(reference.getValue("mixed", "key") == "last", "legacy open failure cleared state");
    }
    {
        CaptureError error;
        const auto missing = output / fs::path(u8"中文路径/缺失-🌲.ini");
        require(!actual.loadFromPath(missing), "Unicode missing file accepted");
        const auto utf8 = missing.generic_u8string();
        require(error.text.str() == "Failed to open INI file: " + std::string(
            reinterpret_cast<const char*>(utf8.data()), utf8.size()) + "\n", "native diagnostic is not exact UTF-8");
    }
    write(native, "");
    require(actual.loadFromPath(native) && actual.getSectionNames().empty(), "empty input semantics changed");
    write(native, "\xef\xbb\xbf[First]\nKey=not-seen\n[Second]\nKey=seen\n");
    require(actual.loadFromPath(native), "BOM input rejected");
    require(actual.getValue("First", "Key", "absent") == "absent"
        && actual.getValue("Second", "Key") == "seen", "BOM was silently decoded/stripped");
    write(native, "[A]\r\nK=before\r\n\x1a\r\nK=after\r\n");
    require(actual.loadFromPath(native), "control-Z input rejected");
#ifdef _WIN32
    require(actual.getValue("A", "K") == "before", "Windows text-mode control-Z changed");
#else
    require(actual.getValue("A", "K") == "after", "POSIX byte-stream control-Z changed");
#endif
}

void checkPaths(const fs::path& output) {
    const auto cwdRoot = output / fs::path(u8"当前工程-🌲");
    const auto exeRoot = output / fs::path(u8"程序工程-🌲");
    const auto nested = cwdRoot / "nested";
    fs::create_directories(cwdRoot / "local_data");
    fs::create_directories(cwdRoot / ".git");
    fs::create_directories(nested);
    fs::create_directories(exeRoot / "local_data");
    fs::create_directories(exeRoot / "RedAlert2");
    write(cwdRoot / "CMakeLists.txt", "cwd root");
    write(exeRoot / "CMakeLists.txt", "exe root");
    const auto exeDir = exeRoot / "bin";
    fs::create_directories(exeDir);
    require(runtimeConfigProjectRoot(nested, exeDir) == fs::weakly_canonical(cwdRoot), "cwd ancestor priority changed");
    require(runtimeConfigProjectRoot(output / "unmarked", exeDir) == fs::weakly_canonical(exeRoot), "exe root fallback changed");
    require(runtimeConfigProjectRoot({}, exeDir) == fs::weakly_canonical(exeRoot), "empty cwd handling changed");
    require(runtimeConfigProjectRoot({}, {}) == fs::current_path(), "no-candidate fallback changed");
    require(runtimeConfigProjectRoot(output / "unmarked", {}) == fs::weakly_canonical(output / "unmarked"), "unmarked fallback changed");
    write(cwdRoot / "Same.MAP", "project");
    write(nested / "Same.MAP", "cwd");
    require(runtimeResolveConfigPath("Same.MAP", cwdRoot, nested) == fs::weakly_canonical(nested / "Same.MAP"), "cwd resource candidate lost priority");
    require(runtimeResolveConfigPath("missing.ini", cwdRoot, nested) == fs::weakly_canonical(cwdRoot / "missing.ini"), "missing optional path changed");
    require(runtimeResolveConfigPath({}, cwdRoot, nested).empty(), "empty path gained meaning");
    const auto absolute = exeRoot / "NoSuch.ini";
    require(runtimeResolveConfigPath(absolute, cwdRoot, nested) == fs::weakly_canonical(absolute), "absolute path changed");
    require(runtimeResolveConfigPath("../Same.MAP", cwdRoot, nested) == fs::weakly_canonical(cwdRoot / "Same.MAP"), "dot segments changed");
    require(runtimeResolveConfigPath("nested", cwdRoot, cwdRoot) == fs::weakly_canonical(nested), "existing directory treated as missing");
    // Libraries differ on whether a file used as a parent is an error or a
    // missing suffix. Keep that original library result.
    const auto blocked = cwdRoot / "Same.MAP/child.ini";
    std::error_code error;
    const auto canonical = fs::weakly_canonical(blocked, error);
    require(runtimeResolveConfigPath(blocked, cwdRoot, nested)
        == (error ? blocked.lexically_normal() : canonical), "non-directory normalization changed");
    // An oversized filename forces the error branch without touching ACLs or
    // depending on the implementation's metadata sharing flags.
    const auto oversized = cwdRoot / std::string(40000, 'x');
    (void)fs::weakly_canonical(oversized, error);
    require(static_cast<bool>(error), "oversized canonical-failure fixture did not fail");
    require(runtimeResolveConfigPath(oversized, cwdRoot, nested) == oversized.lexically_normal(),
        "canonical error lexical fallback changed");
    const auto legacy = output / "ASCII-Case.ini";
    require(runtimeConfigLegacyPathString(legacy) == legacy.generic_string(), "legacy string boundary changed");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 && fs::path(argv[1]).is_absolute(), "supply an absolute isolated output directory");
        const fs::path output(argv[1]);
        checkIni(output / "ini");
        checkPaths(output / "paths");
        std::cout << "Runtime file path contract passed checks=" << checks
            << " nativeUnicode=1 contentBytes=unchanged legacyEncoding=unchanged\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Runtime file path contract failed: " << e.what() << '\n';
        return 1;
    }
}
