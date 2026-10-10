#include "runtime/host/RuntimeShell.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
    const std::filesystem::path path = argc > 1 ? argv[1] : "runtime_hotkey_preferences_test.ini";
    {
        std::ofstream original(path, std::ios::binary | std::ios::trunc);
        original << "[Preserved]\nSentinel=keep\n";
        if (!original) return 1;
    }
    RuntimeConfig config;
    config.configPath = path.string();
    config.keyboardReferenceLayout = true;
    config.keyBindings = {
        {"TeamCreate_1", 561}, {"SetView1", 624}, {"StopObject", 0x653},
        {"ModifierOnly", 0x200}, {"UnknownModifier", 0x853}
    };
    std::string error;
    if (!saveRuntimeConfigPreferences(config, error)) {
        std::cerr << "hotkey preferences save failed: " << error << '\n';
        return 1;
    }
    std::ifstream input(path, std::ios::binary);
    const std::string saved((std::istreambuf_iterator<char>(input)), {});
    if (saved.find("TeamCreate_1=561") == std::string::npos
        || saved.find("SetView1=624") == std::string::npos
        || saved.find("StopObject=1619") == std::string::npos
        || saved.find("Sentinel=keep") == std::string::npos
        || saved.find("ReferenceLayout=20260913") == std::string::npos
        || saved.find("ModifierOnly=") != std::string::npos
        || saved.find("UnknownModifier=") != std::string::npos) {
        std::cerr << "modifier bindings did not survive preference persistence\n";
        return 1;
    }
    std::cout << "Hotkey preferences: retail modifiers persisted, unrelated settings preserved\n";
    return 0;
}
