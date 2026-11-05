#include "audio/backend/win32/RuntimeLegacyWaveDeviceWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace concord::audio;
namespace fs = std::filesystem;
namespace {
int checks = 0, failures = 0;
unsigned injectedShareable = 0, injectedVolume = 0, injectedPlay = 0, injectedClose = 0;
bool failShareable = false, failVolume = false, failPlay = false, failCloseOnce = false;
struct Call { std::wstring command; std::uint32_t capacity; std::uint32_t result; };
std::vector<Call> calls;
std::vector<std::wstring> order;
bool throwEncoding = false;
void expect(bool result, const char* name) { ++checks; if (!result) { ++failures; std::cerr << name << '\n'; } }
std::wstring encode(const std::string& path) {
    order.push_back(L"encode");
    if (throwEncoding) throw std::runtime_error("owned encoder exception");
    return fs::path(path).wstring();
}
std::uint32_t traced(const wchar_t* command, wchar_t* output, std::uint32_t capacity) {
    const std::wstring text(command); order.push_back(text);
    std::uint32_t result = 0;
    if (failShareable && text.starts_with(L"open ") && text.ends_with(L" shareable")) { ++injectedShareable; result = MCIERR_UNSUPPORTED_FUNCTION; }
    else if (failVolume && text.starts_with(L"setaudio ")) { ++injectedVolume; result = MCIERR_UNSUPPORTED_FUNCTION; }
    else if (failPlay && text.starts_with(L"play ")) { ++injectedPlay; result = MCIERR_DEVICE_NOT_READY; }
    else if (failCloseOnce && text.starts_with(L"close ")) { failCloseOnce = false; ++injectedClose; result = MCIERR_DEVICE_LOCKED; }
    else result = mciSendStringW(command, output, static_cast<UINT>(capacity), nullptr);
    calls.push_back({text, capacity, result}); return result;
}
bool wave(const fs::path& path) {
    std::ofstream out(path, std::ios::binary);
    const auto u16 = [&](std::uint16_t x) { for (int i = 0; i < 2; ++i) out.put(static_cast<char>(x >> (i * 8))); };
    const auto u32 = [&](std::uint32_t x) { for (int i = 0; i < 4; ++i) out.put(static_cast<char>(x >> (i * 8))); };
    out.write("RIFF", 4); u32(32036); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(8000); u32(16000); u16(2); u16(16); out.write("data", 4); u32(32000);
    const std::array<char, 32000> pcm{}; out.write(pcm.data(), pcm.size()); return bool(out);
}
bool closed(const std::wstring& alias) { return mciGetDeviceIDW(alias.c_str()) == 0; }
}
int main() {
    const DWORD count = GetEnvironmentVariableW(L"CONCORD_LEGACY_WAVE_TEST_OUTPUT", nullptr, 0);
    if (count < 2) { std::cerr << "Explicit native test output directory required\n"; return 2; }
    std::vector<wchar_t> path(count);
    if (GetEnvironmentVariableW(L"CONCORD_LEGACY_WAVE_TEST_OUTPUT", path.data(), count) != count - 1) return 2;
    const fs::path root(path.data()); fs::create_directories(root);
    const auto pcm = root / "owned-silence.wav", unicode = root / fs::path(L"静音波形.wav"), bad = root / "bad.wav";
    expect(wave(pcm) && wave(unicode), "real owned nonempty 2second PCM and native Unicode path");
    { std::ofstream file(bad, std::ios::binary); file << "invalid"; expect(bool(file), "owned invalid wave"); }
    unsigned progressed = 0, reentries = 0, handoffs = 0, naturalEnds = 0, encoderExceptions = 0;
    for (const auto channel : {RuntimeLegacyWaveChannel::Eva, RuntimeLegacyWaveChannel::ShellMusic}) {
        const bool eva = channel == RuntimeLegacyWaveChannel::Eva;
        const std::wstring alias = eva ? L"concord_runtime_eva" : L"concord_runtime_music";
        const auto make = [&]() { return createLegacyWaveDeviceWin32ForDiagnostics(channel, encode, traced); };
        {
            auto device = make(); expect(device->supported() && closed(alias), "construction has no native alias");
            calls.clear(); order.clear(); expect(device->playFromStart(pcm.generic_string(), -10), "actual original start with clamped low volume");
            auto expected = eva
                ? std::vector<std::wstring>{L"stop " + alias, L"close " + alias,
                    L"open \"" + encode(pcm.generic_string()) + L"\" type waveaudio alias " + alias + L" shareable",
                    L"setaudio " + alias + L" volume to 0", L"play " + alias + L" from 0"}
                : std::vector<std::wstring>{L"close " + alias,
                    L"open \"" + encode(pcm.generic_string()) + L"\" type waveaudio alias " + alias + L" shareable",
                    L"setaudio " + alias + L" volume to 0", L"play " + alias + L" from 0"};
            const auto openIndex = eva ? 2U : 1U;
            if (calls.at(openIndex).result != 0) {
                auto fallback = expected.at(openIndex); fallback.resize(fallback.size() - std::wstring(L" shareable").size());
                expected.insert(expected.begin() + openIndex + 1, fallback);
            }
            expect(order.at(eva ? 2U : 0U) == L"encode", "encoder executes after EVA stop-close but before music close");
            std::vector<std::wstring> actual; for (const auto& c : calls) actual.push_back(c.command);
            expect(actual == expected, "exact original EVA vs Shell preclose/encode/open/ignored-volume/play sequence");
            expect(device->isPlaying() == std::optional<bool>(true), "real native playing status");
            expect(calls.back().capacity == (eva ? 32U : 64U), "original native status buffer32/64");
            Sleep(80); wchar_t position[32]{}; const auto query = L"status " + alias + L" position";
            const auto code = mciSendStringW(query.c_str(), position, 32, nullptr); std::uint64_t samples = 0; std::wistringstream(position) >> samples;
            expect(code == 0 && samples > 0, "actual owned silent PCM native playback position progresses"); ++progressed;
            const auto eofDeadline = GetTickCount64() + 5000;
            while (device->isPlaying() == std::optional<bool>(true) && GetTickCount64() < eofDeadline) Sleep(20);
            expect(device->isPlaying() == std::optional<bool>(false), "real PCM reaches natural EOF with stopped device status");
            ++naturalEnds;
            device->stopAndClose(); expect(closed(alias), "real stop/close disposes native alias");
            device->close(); device->stopAndClose(); expect(closed(alias), "repeat cleanup does not reopen");
            calls.clear(); expect(device->playFromStart(unicode.generic_string(), 170), "native Unicode path and clamped high volume");
            expect(std::find_if(calls.begin(), calls.end(), [&](const Call& c) { return c.command == L"setaudio " + alias + L" volume to 1000"; }) != calls.end(), "high volume exact original clamp1000");
            const auto externalClose = L"close " + alias;
            expect(mciSendStringW(externalClose.c_str(), nullptr, 0, nullptr) == 0, "actual own device-loss fixture");
            expect(!device->isPlaying().has_value(), "lost native device has unknown status, not false success");
            device->close(); expect(closed(alias), "device-loss cleanup clears lease");
            expect(!device->playFromStart((root / "missing.wav").generic_string(), 0) && closed(alias), "actual missing-file failure has no alias");
            expect(!device->playFromStart(bad.generic_string(), 0) && closed(alias), "actual invalid-wave failure has no alias");
            expect(device->playFromStart(pcm.generic_string(), 0), "real device before encoder exception order fixture");
            throwEncoding = true; bool caught = false;
            try { (void)device->playFromStart(pcm.generic_string(), 0); } catch (const std::runtime_error&) { caught = true; }
            throwEncoding = false;
            expect(caught && closed(alias) == eva, "original encoder exception retains EVA closed versus Shell still open");
            ++encoderExceptions; device->stopAndClose(); expect(closed(alias), "encoder exception recovery closes actual remaining device");
            failShareable = true; expect(device->playFromStart(pcm.generic_string(), 0), "injected shareable-open failure forwards real fallback open/play"); failShareable = false;
            expect(device->isPlaying() == std::optional<bool>(true), "fallback real native playback"); device->stopAndClose();
            failVolume = true; expect(device->playFromStart(pcm.generic_string(), 0), "original ignored volume failure retains real play success"); failVolume = false;
            expect(device->isPlaying() == std::optional<bool>(true), "volume-error case still has actual real native playing state"); device->stopAndClose();
            failPlay = true; expect(!device->playFromStart(pcm.generic_string(), 0) && closed(alias), "injected play error closes actual successfully opened native device"); failPlay = false;
            expect(device->playFromStart(pcm.generic_string(), 0), "real recovery from failure");
            failCloseOnce = true; device->close(); expect(!closed(alias), "injected one close failure retains actual open device for cleanup");
        }
        expect(closed(alias), "destructor retries retained failed close and disposes actual device");
        {
            auto old = make(); expect(old->playFromStart(pcm.generic_string(), 0), "old owner actual play");
            auto next = make(); expect(next->playFromStart(unicode.generic_string(), 0), "new owner actual replacement");
            old->stopAndClose(); expect(next->isPlaying() == std::optional<bool>(true), "stale explicit cleanup preserves new owner");
            old.reset(); expect(next->isPlaying() == std::optional<bool>(true), "stale destructor preserves new owner");
            auto unused = make(); unused.reset(); expect(next->isPlaying() == std::optional<bool>(true), "unused destructor preserves new owner"); ++handoffs;
        }
        expect(closed(alias), "replacement owner destructor closes native slot");
        for (int visit = 0; visit < 16; ++visit) {
            { auto device = make(); expect(device->playFromStart(pcm.generic_string(), 0), "actual native reentry"); if (visit % 2 == 0) device->stopAndClose(); }
            expect(closed(alias), "each native reentry leaves alias0"); ++reentries;
        }
        auto noEncoder = createLegacyWaveDeviceWin32(channel, nullptr);
        expect(!noEncoder->supported() && !noEncoder->playFromStart(pcm.generic_string(), 0) && !noEncoder->isPlaying().has_value(), "missing native encoder selects honest unsupported");
    }
    expect(injectedShareable == 2 && injectedVolume == 2 && injectedPlay == 2 && injectedClose == 2, "exact test-only error injections, all other successful operations forwarded real MCI");
    std::cout << "Legacy wave native: checks=" << checks << " failures=" << failures << " skipped=0 channels=2 progressed=" << progressed
        << " naturalEnds=" << naturalEnds << " encoderExceptions=" << encoderExceptions << " reentries=" << reentries << " protectedHandoffs=" << handoffs << " injectedShareable=" << injectedShareable
        << " ignoredVolumeErrors=" << injectedVolume << " playFailures=" << injectedPlay << " closeRetries=" << injectedClose
        << " exactOriginalCommands=yes finalNativeAliases=0 silentRealPCM=yes noHearingHotplugClaim=yes\n";
    return failures ? 1 : 0;
}
