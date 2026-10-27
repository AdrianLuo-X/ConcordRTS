#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/main_menu/RuntimeMainMenu.h"
#include "assets/text/CsfCodec.h"
#include "assets/text/WestwoodFontCodec.h"

#include "assets/audio/AudioSampleResolver.h"
#include "assets/video/BinkVideoStream.h"
#include "assets/video/BinkPlaybackService.h"
#include "core/io/IniFile.h"
#include "runtime/campaign/RuntimeCampaignMovieResolver.h"
#include "assets/menu/MenuImageResources.h"
#include "ui/resources/RuntimeMenuResourceWatch.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include "ui/skirmish/RuntimeSkirmishMenu.h"

#include "core/platform/PathEncoding.h"
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {

namespace fs = std::filesystem;

constexpr int kWidth = 800;
constexpr int kHeight = 600;
constexpr int kLeftWidth = 632;
constexpr int kStatusTop = 568;
constexpr int kSidebarLeft = 632;
constexpr int kSidebarTopHeight = 199;
constexpr int kSlotHeight = 42;
constexpr int kSlotCount = 8;
constexpr int kSidebarBottomTop = 535;
constexpr int kMenuButtonLeft = 653;
constexpr int kMenuButtonTop = 207;
constexpr int kBottomButtonTop = 543;
constexpr int kBottomHitTop = 535;
constexpr int kWarningLeft = 670;
// SDTP's 92x53 monitor aperture starts at local (38, 48). The
// SDWRNANM canvas replaces that embedded image without covering its bezel.
constexpr int kWarningTop = 48;
constexpr int kAnimationFrames = 6;
constexpr std::uint64_t kAnimationFrameMs = 42;
constexpr std::size_t kCreditsLinesPerPage = 18;
constexpr std::array<std::pair<int, int>, 5> kOptionResolutions{{
    {800, 600},
    {1024, 640},
    {1024, 768},
    {1280, 720},
    {1600, 900}
}};
constexpr std::array<int, 4> kOptionTargetFps{{30, 60, 120, 144}};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trimAscii(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring toWide(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0
    );
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        page = concord::platform::legacyTextCodePage();
        flags = 0;
        count = MultiByteToWideChar(
            page, flags, value.data(), static_cast<int>(value.size()), nullptr, 0
        );
    }
    if (count <= 0) return std::wstring(value.begin(), value.end());
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        page, flags, value.data(), static_cast<int>(value.size()),
        result.data(), count
    );
    return result;
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

fs::path firstExisting(std::initializer_list<fs::path> paths) {
    for (const fs::path& path : paths) {
        if (!path.empty() && regularFile(path)) return path;
    }
    return {};
}

std::string iniValueNoCase(
    const IniFile& ini,
    const std::string& wantedSection,
    const std::string& wantedKey,
    const std::string& fallback = ""
) {
    for (const std::string& sectionName : ini.getSectionNames()) {
        if (lowerAscii(sectionName) != lowerAscii(wantedSection)) continue;
        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) return fallback;
        for (auto it = section->rbegin(); it != section->rend(); ++it) {
            if (lowerAscii(it->first) == lowerAscii(wantedKey)) return it->second;
        }
        return fallback;
    }
    return fallback;
}

std::optional<std::array<int, 3>> parseWestwoodHsv(
    const std::string& value
) {
    std::array<int, 3> channels{};
    std::size_t cursor = 0;
    for (std::size_t index = 0; index < channels.size(); ++index) {
        const std::size_t comma = value.find(',', cursor);
        const std::size_t end = index + 1 == channels.size()
            ? value.size()
            : comma;
        if (end == std::string::npos) return std::nullopt;
        try {
            const std::string field = trimAscii(value.substr(cursor, end - cursor));
            std::size_t consumed = 0;
            channels[index] = std::stoi(field, &consumed);
            if (consumed != field.size()) return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
        if (channels[index] < 0 || channels[index] > 255) {
            return std::nullopt;
        }
        cursor = end + 1;
    }
    return channels;
}

COLORREF westwoodHsvColor(const std::array<int, 3>& hsv) {
    const double hue = static_cast<double>(hsv[0]) * 360.0 / 255.0;
    const double saturation = static_cast<double>(hsv[1]) / 255.0;
    const double value = static_cast<double>(hsv[2]) / 255.0;
    const double chroma = value * saturation;
    const double hueSector = std::fmod(hue / 60.0, 6.0);
    const double intermediate = chroma
        * (1.0 - std::abs(std::fmod(hueSector, 2.0) - 1.0));
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    if (hueSector < 1.0) {
        red = chroma;
        green = intermediate;
    } else if (hueSector < 2.0) {
        red = intermediate;
        green = chroma;
    } else if (hueSector < 3.0) {
        green = chroma;
        blue = intermediate;
    } else if (hueSector < 4.0) {
        green = intermediate;
        blue = chroma;
    } else if (hueSector < 5.0) {
        red = intermediate;
        blue = chroma;
    } else {
        red = chroma;
        blue = intermediate;
    }
    const double match = value - chroma;
    const auto byte = [match](double channel) {
        return static_cast<BYTE>(std::clamp(
            static_cast<int>(std::lround((channel + match) * 255.0)),
            0,
            255
        ));
    };
    return RGB(byte(red), byte(green), byte(blue));
}

struct FontColorRamp {
    COLORREF bright = RGB(255, 190, 97);
    COLORREF middle = RGB(245, 172, 68);
    COLORREF dark = RGB(97, 59, 4);
};

FontColorRamp westwoodFontColorRamp(const std::array<int, 3>& hsv) {
    const auto tone = [&hsv](double saturationScale, double valueScale) {
        std::array<int, 3> adjusted = hsv;
        adjusted[1] = std::clamp(
            static_cast<int>(std::lround(hsv[1] * saturationScale)),
            0,
            255
        );
        adjusted[2] = std::clamp(
            static_cast<int>(std::lround(hsv[2] * valueScale)),
            0,
            255
        );
        return westwoodHsvColor(adjusted);
    };

    // Westwood's LightGold is a UI/text colour spread, not one flat HSV pixel.
    // game.fnt supplies a one-bit glyph mask, so keep the authored hue and use
    // the warm highlight/face/shade ramp calibrated from the original menu.
    return FontColorRamp{
        tone(0.62, 1.00),
        tone(0.72, 0.96),
        tone(0.96, 0.38)
    };
}



struct CsfCatalog {
    std::unordered_map<std::string, std::wstring> values;

    bool load(const fs::path& path) {
        values.clear();
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        const std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        for (auto& entry : concord::csf::decodeEntries(bytes)) {
            const auto value = concord::csf::projectCarriageReturns(
                std::move(entry.firstValue), concord::csf::CarriageReturnPolicy::Space);
            values[lowerAscii(entry.label)] = std::wstring(value.begin(), value.end());
        }
        return !values.empty();
    }

    std::wstring get(const std::string& key, const wchar_t* fallback) const {
        const auto found = values.find(lowerAscii(key));
        return found == values.end() || found->second.empty()
            ? std::wstring(fallback) : found->second;
    }
};

struct WestwoodBitFont {

    std::vector<unsigned char> bytes;
    std::uint32_t stride = 0;
    std::uint32_t bitmapRows = 0;
    std::uint32_t lineHeight = 0;
    std::uint32_t glyphCount = 0;
    std::uint32_t recordSize = 0;
    std::string source;

    bool load(const fs::path& path) {
        *this = {};
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        bytes.assign(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        concord::assets::WestwoodFontMetrics metrics;
        const bool sane = concord::assets::decodeWestwoodFontHeader(bytes, metrics);
        if (!sane) { *this = {}; return false; }
        stride = metrics.stride;
        bitmapRows = metrics.bitmapRows;
        lineHeight = metrics.lineHeight;
        glyphCount = metrics.glyphCount;
        recordSize = metrics.recordSize;
        source = path.string();
        return true;
    }

    bool valid() const {
        return !bytes.empty() && recordSize > 1 && lineHeight > 0;
    }

    const unsigned char* glyph(wchar_t character) const {
        if (!valid()) return nullptr;
        return concord::assets::westwoodFontGlyph(bytes,
            {stride, bitmapRows, lineHeight, glyphCount, recordSize}, static_cast<std::uint16_t>(character),
            concord::assets::WestwoodGlyphFallback::AnyUnavailableRecord);
    }

    int measure(const std::wstring& text) const {
        int width = 0;
        int glyphs = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            wchar_t character = text[i];
            if (character >= 0xd800 && character <= 0xdbff
                && i + 1 < text.size()
                && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
                character = L'?';
                ++i;
            }
            const unsigned char* record = glyph(character);
            width += record != nullptr ? record[0] + 1 : 5;
            ++glyphs;
        }
        return glyphs > 0 ? std::max(0, width - 1) : 0;
    }

    void draw(
        unsigned char* pixels,
        int surfaceWidth,
        int surfaceHeight,
        const std::wstring& text,
        const RECT& rect,
        bool centered,
        bool verticallyCentered,
        const FontColorRamp& colors
    ) const {
        if (!valid() || pixels == nullptr || text.empty()) return;
        const int textWidth = measure(text);
        const int startX = centered
            ? rect.left + std::max(0L, rect.right - rect.left - textWidth) / 2
            : rect.left;
        const int startY = verticallyCentered
            ? rect.top + std::max(0L, rect.bottom - rect.top
                - static_cast<LONG>(lineHeight)) / 2
            : rect.top;
        const auto setPixel = [=](int x, int y, COLORREF value) {
            if (x < rect.left || x >= rect.right || y < rect.top || y >= rect.bottom
                || x < 0 || y < 0 || x >= surfaceWidth || y >= surfaceHeight) {
                return;
            }
            const std::size_t at = static_cast<std::size_t>(y * surfaceWidth + x) * 4U;
            pixels[at] = GetBValue(value);
            pixels[at + 1] = GetGValue(value);
            pixels[at + 2] = GetRValue(value);
            pixels[at + 3] = 255;
        };
        const auto glyphPixel = [this](
            const unsigned char* record,
            int glyphWidth,
            int x,
            int y
        ) {
            if (x < 0 || x >= glyphWidth
                || y < 0 || y >= static_cast<int>(bitmapRows)) {
                return false;
            }
            const unsigned char* row = record + 1U
                + static_cast<std::size_t>(y) * stride;
            return (row[x >> 3] & (0x80U >> (x & 7))) != 0;
        };
        const auto drawPass = [
            &
        ](int offsetX, int offsetY, COLORREF passColor, bool highlightsOnly) {
            int cursorX = startX;
            for (std::size_t i = 0; i < text.size(); ++i) {
                wchar_t character = text[i];
                if (character >= 0xd800 && character <= 0xdbff
                    && i + 1 < text.size()
                    && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff) {
                    character = L'?';
                    ++i;
                }
                const unsigned char* record = glyph(character);
                if (record == nullptr) {
                    cursorX += 5;
                    continue;
                }
                const int glyphWidth = record[0];
                for (std::uint32_t y = 0; y < bitmapRows; ++y) {
                    for (int x = 0; x < glyphWidth; ++x) {
                        if (glyphPixel(
                                record,
                                glyphWidth,
                                x,
                                static_cast<int>(y))
                            && (!highlightsOnly || !glyphPixel(
                                record,
                                glyphWidth,
                                x,
                                static_cast<int>(y) - 1))) {
                            setPixel(
                                cursorX + x + offsetX,
                                startY + static_cast<int>(y) + offsetY,
                                passColor
                            );
                        }
                    }
                }
                cursorX += glyphWidth + 1;
            }
        };
        drawPass(1, 1, colors.dark, false);
        drawPass(0, 0, colors.middle, false);
        drawPass(0, 0, colors.bright, true);
    }
};

using AssetImage = concord::assets::MenuImage;

bool loadBmp(const fs::path& path, AssetImage& output) {
    output = {};
    if (!regularFile(path)) return false;
    const std::wstring wide = toWide(path.string());
    HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
        nullptr, wide.c_str(), IMAGE_BITMAP, 0, 0,
        LR_LOADFROMFILE | LR_CREATEDIBSECTION
    ));
    if (bitmap == nullptr) return false;
    BITMAP facts{};
    GetObjectW(bitmap, sizeof(facts), &facts);
    if (facts.bmWidth <= 0 || facts.bmHeight <= 0) {
        DeleteObject(bitmap);
        return false;
    }
    output.width = facts.bmWidth;
    output.height = facts.bmHeight;
    output.bgra.resize(static_cast<std::size_t>(output.width * output.height * 4));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = output.width;
    info.bmiHeader.biHeight = -output.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(nullptr);
    const int copied = GetDIBits(
        dc, bitmap, 0, static_cast<UINT>(output.height), output.bgra.data(),
        &info, DIB_RGB_COLORS
    );
    if (dc != nullptr) ReleaseDC(nullptr, dc);
    DeleteObject(bitmap);
    if (copied != output.height) {
        output = {};
        return false;
    }
    for (std::size_t at = 3; at < output.bgra.size(); at += 4) output.bgra[at] = 255;
    output.source = path.string();
    return true;
}

const wchar_t* binkStreamArgument(
    concord::bink_stream::PlaybackMode mode,
    bool soundEnabled = true
) {
    if (!soundEnabled) {
        return mode == concord::bink_stream::PlaybackMode::Once
            ? L"--stream-once-muted"
            : L"--stream-muted";
    }
    return mode == concord::bink_stream::PlaybackMode::Once
        ? L"--stream-once"
        : L"--stream";
}

constexpr int binkVolumeFromPercent(int percent) {
    return (std::clamp(percent, 0, 100) * 32768 + 50) / 100;
}

class BinkMenuAnimation {
public:
    bool start(
        const RuntimeConfig& config,
        const fs::path& exportRoot,
        const fs::path& requestedMovie = {},
        concord::bink_stream::PlaybackMode playbackMode =
            concord::bink_stream::PlaybackMode::Loop,
        bool soundEnabled = true,
        int soundVolume = 32768
    ) {
        return playback_.start({
            fs::path(config.resourceRootPath), exportRoot, requestedMovie,
            playbackMode, soundEnabled, soundVolume
        });
    }

    bool poll(AssetImage& output, DWORD waitMilliseconds = 0) {
        BinkVideoFrame frame;
        // Reuse the caller's allocation without copying a shared-memory view.
        frame.bgra.swap(output.bgra);
        const bool ready = playback_.poll(frame, waitMilliseconds);
        output.bgra.swap(frame.bgra);
        if (ready) {
            output.width = frame.width;
            output.height = frame.height;
            output.source = std::move(frame.source);
        }
        return ready;
    }

    void stop() { playback_.stop(); }
    const std::string& failure() const { return playback_.failure(); }
    LONG sequence() const { return static_cast<LONG>(playback_.sequence()); }
    bool finished() const { return playback_.finished(); }

private:
    BinkPlaybackService playback_;
};

struct MenuAudio {
    bool enabled = true;
    int musicVolumePercent = 100;
    fs::path assetRoot;
    fs::path cacheRoot;
    std::unordered_map<std::string, std::string> samples{
        {"MenuClick", "umenucl1"},
        {"MenuSlideIn", "uslide1"},
        {"MenuSlideOut", "uslide2"}
    };
    std::unordered_map<std::string, std::optional<std::wstring>> waves;
    bool musicOpen = false;
    fs::path musicPath;
    ULONGLONG nextMusicProbe = 0;

    ~MenuAudio() { stopMusic(); }

    void initialize(const fs::path& soundIni) {
        IniFile ini;
        if (!soundIni.empty() && ini.loadFromFile(soundIni.string())) {
            for (auto& [eventName, sample] : samples) {
                std::string configured = trimAscii(
                    iniValueNoCase(ini, eventName, "Sounds", sample)
                );
                const std::size_t separator = configured.find_first_of(" ,\t");
                if (separator != std::string::npos) configured.resize(separator);
                while (!configured.empty()
                    && (configured.front() == '$' || configured.front() == '#')) {
                    configured.erase(configured.begin());
                }
                if (!configured.empty()) sample = configured;
            }
        }
    }

    void play(const std::string& eventName) {
        if (!enabled) return;
        auto wave = waves.find(eventName);
        if (wave == waves.end()) {
            std::optional<std::wstring> path;
            const auto sample = samples.find(eventName);
            if (sample != samples.end()) {
                const AudioSampleResolveResult resolved = resolveAudioSampleToWave(
                    assetRoot.string(), cacheRoot.string(), sample->second
                );
                if (resolved.success && !resolved.wavPath.empty()) {
                    path = toWide(resolved.wavPath);
                }
            }
            wave = waves.emplace(eventName, std::move(path)).first;
        }
        if (wave->second) {
            PlaySoundW(
                wave->second->c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
        }
    }

    void startMusic(const fs::path& path) {
        if (!enabled || musicVolumePercent <= 0 || !regularFile(path)) return;
        if (musicOpen && musicPath == path) return;
        if (musicOpen) stopMusic();
        musicPath = path;
        nextMusicProbe = GetTickCount64() + 1000ULL;
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        const std::wstring command = L"open \"" + toWide(path.string())
            + L"\" type waveaudio alias concord_runtime_intro";
        const MCIERROR openError =
            mciSendStringW(command.c_str(), nullptr, 0, nullptr);
        if (openError == 0) {
            const std::wstring volumeCommand =
                L"setaudio concord_runtime_intro volume to "
                + std::to_wstring(
                    std::clamp(musicVolumePercent, 0, 100) * 10
                );
            (void)mciSendStringW(
                volumeCommand.c_str(),
                nullptr,
                0,
                nullptr
            );
        }
        // The Windows waveaudio MCI driver rejects the string-command
        // `repeat` flag.  Play once and let ensureMusicPlaying restart the
        // track after it reaches the stopped state.
        const MCIERROR playError = openError == 0
            ? mciSendStringW(
                L"play concord_runtime_intro from 0",
                nullptr,
                0,
                nullptr
            )
            : openError;
        if (openError == 0 && playError == 0) {
            musicOpen = true;
        } else {
            mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
            musicOpen = false;
        }
    }

    void ensureMusicPlaying() {
        if (!enabled || musicPath.empty()) return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextMusicProbe) return;
        nextMusicProbe = now + 1000ULL;

        if (!musicOpen) {
            const fs::path requestedPath = musicPath;
            startMusic(requestedPath);
            return;
        }

        wchar_t status[32]{};
        const MCIERROR statusError = mciSendStringW(
            L"status concord_runtime_intro mode",
            status,
            static_cast<UINT>(sizeof(status) / sizeof(status[0])),
            nullptr
        );
        if (statusError == 0 && std::wstring(status) == L"playing") return;

        const MCIERROR playError = mciSendStringW(
            L"play concord_runtime_intro from 0",
            nullptr,
            0,
            nullptr
        );
        if (playError == 0) return;

        const fs::path requestedPath = musicPath;
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        musicOpen = false;
        startMusic(requestedPath);
    }

    void stopMusic() {
        mciSendStringW(L"stop concord_runtime_intro", nullptr, 0, nullptr);
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        musicOpen = false;
        musicPath.clear();
        nextMusicProbe = 0;
    }
};

enum class MenuPage {
    Home,
    SinglePlayer,
    Movies,
    Credits,
    Options
};
enum class MenuPhase { SlideIn, Idle, SlideOut };
enum class MenuAction {
    None,
    OpenSinglePlayer,
    OpenHome,
    OpenMovies,
    OpenCredits,
    OpenOptions,
    PreviousMovie,
    PlayMovie,
    NextMovie,
    PreviousCreditsPage,
    NextCreditsPage,
    CycleResolution,
    CycleTargetFps,
    CycleGameSpeed,
    ApplyOptions,
    Campaign,
    LoadGame,
    Skirmish,
    Network,
    Internet,
    Exit
};

struct ButtonSpec {
    int slot = 0;
    std::wstring label;
    std::wstring tooltip;
    MenuAction action = MenuAction::None;
};

struct MenuState {
    RuntimeConfig config;
    std::unique_ptr<RuntimeMenuResourceWatch> resourceWatch;
    RuntimeMainMenuResult result = RuntimeMainMenuResult::Exit;
    bool initialized = false;
    bool smoke = false;
    bool closing = false;
    HWND window = nullptr;
    fs::path exportRoot;
    concord::assets::MenuImageResources ra2Resources;
    concord::assets::MenuImageResources allResources;
    CsfCatalog csf;
    WestwoodBitFont gameFont;
    std::unordered_map<std::string, std::vector<RuntimePaletteColor>> palettes;
    std::unordered_map<std::string, std::string> paletteSources;
    BinkMenuAnimation videoAnimation;
    bool videoFirstFramePending = false;
    std::uint64_t videoStartupStarted = 0;
    AssetImage videoFrame;
    AssetImage staticFallback;
    AssetImage mainShell;
    AssetImage statusBar;
    std::vector<AssetImage> sideTop;
    std::vector<AssetImage> warning;
    AssetImage slotBackground;
    std::vector<AssetImage> slotAnimation;
    AssetImage sideBottom;
    std::vector<AssetImage> redButton;
    AssetImage mouse;
    MenuAudio audio;
    fs::path mainThemePath;
    fs::path moviesThemePath;
    fs::path creditsThemePath;
    fs::path optionsThemePath;
    fs::path themeCatalogPath;
    MenuPage page = MenuPage::Home;
    std::vector<fs::path> moviePaths;
    std::size_t movieIndex = 0;
    std::vector<std::wstring> creditLines;
    std::size_t creditsPage = 0;
    int optionWindowWidth = 0;
    int optionWindowHeight = 0;
    int optionTargetFps = 0;
    int optionGameSpeed = 0;
    std::wstring pageStatus;
    MenuPhase phase = MenuPhase::SlideIn;
    MenuAction pending = MenuAction::None;
    std::uint64_t phaseStarted = 0;
    int focus = 0;
    int hover = -1;
    int pressed = -1;
    bool keyboardTooltip = false;
    bool mouseVisible = false;
    POINT mousePoint{};
    FontColorRamp menuTextColors{};
    std::string menuTextColorSource;
    int viewLeft = 0;
    int viewTop = 0;
    int viewWidth = kWidth;
    int viewHeight = kHeight;
    HDC backDc = nullptr;
    HBITMAP backBitmap = nullptr;
    HGDIOBJ backOldBitmap = nullptr;
    void* backBits = nullptr;
    BITMAPINFO backInfo{};
    std::vector<unsigned char> composePixels;
    bool backDirty = true;
    int warningFrame = 0;
    int phaseTick = 0;
    HFONT buttonFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT smallFont = nullptr;

    ~MenuState() {
        if (window != nullptr && IsWindow(window)) DestroyWindow(window);
        videoAnimation.stop();
        audio.stopMusic();
        if (backDc != nullptr && backOldBitmap != nullptr
            && backOldBitmap != HGDI_ERROR) {
            SelectObject(backDc, backOldBitmap);
        }
        if (backBitmap != nullptr) DeleteObject(backBitmap);
        if (backDc != nullptr) DeleteDC(backDc);
        if (buttonFont != nullptr) DeleteObject(buttonFont);
        if (titleFont != nullptr) DeleteObject(titleFont);
        if (smallFont != nullptr) DeleteObject(smallFont);
    }
};

std::unique_ptr<MenuState> prewarmedMainMenuState;

constexpr std::uint64_t kMainMenuFirstFrameDeadlineMs = 60000;

void failMainMenuVideoStartup(MenuState& state, const std::string& reason) {
    state.videoFirstFramePending = false;
    std::cerr << "Main menu Bink animation unavailable: " << reason
              << "; using the original extracted first frame.\n";
    state.videoAnimation.stop();
    state.videoFrame = state.staticFallback;
}

bool pollMainMenuVideo(MenuState& state, DWORD waitMilliseconds = 0) {
    bool changed = state.videoAnimation.poll(
        state.videoFrame,
        waitMilliseconds
    );
    if (!state.videoFirstFramePending) return changed;
    const std::uint64_t elapsed = GetTickCount64() - state.videoStartupStarted;
    if (changed) {
        state.videoFirstFramePending = false;
        std::cout << "Main menu Bink animation ready: sequence="
                  << state.videoAnimation.sequence()
                  << " startupMs=" << elapsed << "\n";
    } else if (!state.videoAnimation.failure().empty()) {
        failMainMenuVideoStartup(state, state.videoAnimation.failure());
        changed = true;
    } else if (state.videoAnimation.finished()) {
        failMainMenuVideoStartup(state, "decoder stopped before its first frame");
        changed = true;
    } else if (elapsed >= kMainMenuFirstFrameDeadlineMs) {
        failMainMenuVideoStartup(state, "first decoded frame startup deadline expired");
        changed = true;
    }
    return changed;
}

bool startMainMenuVideo(
    MenuState& state,
    const RuntimeConfig& config,
    DWORD initialWaitMilliseconds
) {
    state.videoFirstFramePending = false;
    state.videoStartupStarted = GetTickCount64();
    if (!state.videoAnimation.start(
            config,
            state.exportRoot,
            {},
            concord::bink_stream::PlaybackMode::Loop,
            false,
            0
        )) {
        failMainMenuVideoStartup(state, state.videoAnimation.failure());
        return false;
    }
    state.videoFirstFramePending = true;
    if (!pollMainMenuVideo(state, initialWaitMilliseconds)
        && state.videoFirstFramePending) {
        // The menu stays usable while its timer polls the live decoder.
        // A preview is not a successful decoded frame or a permanent fallback.
        std::cout << "Main menu Bink animation pending: original extracted preview "
                     "shown while waiting for the first decoded frame.\n";
    }
    return state.videoFirstFramePending || state.videoAnimation.sequence() > 0;
}

bool waitMainMenuVideoForSmoke(MenuState& state) {
    while (state.videoFirstFramePending) {
        pollMainMenuVideo(state);
        if (!state.videoFirstFramePending) break;
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                return false;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return state.videoAnimation.sequence() > 0;
}

std::vector<ButtonSpec> buttons(const MenuState& state);
void finishMenu(MenuState& state, RuntimeMainMenuResult result);

std::vector<ButtonSpec> buttons(const MenuState& state) {
    const auto text = [&state](const char* key, const wchar_t* fallback) {
        return state.csf.get(key, fallback);
    };
    if (state.page == MenuPage::SinglePlayer) {
        return {
            {0, text("GUI:NewCampaign", L"Campaign"),
                text("STT:SingleButtonNewCampaign", L"Start a new campaign."),
                MenuAction::Campaign},
            {1, text("GUI:LoadSavedGame", L"Load Game"),
                text("STT:SingleButtonLoadSavedGame", L"Load a saved game."),
                MenuAction::LoadGame},
            {2, text("GUI:Skirmish", L"Skirmish"),
                text("STT:SingleButtonSkirmish", L"Battle the computer in a skirmish game."),
                MenuAction::Skirmish},
            {-1, text("GUI:Back", L"Back"),
                text("STT:SingleButtonBack", L"Return to the Main Menu."),
                MenuAction::OpenHome}
        };
    }
    if (state.page == MenuPage::Movies) {
        const bool moviesAvailable = !state.moviePaths.empty();
        return {
            {0, text("GUI:Previous", L"Previous"),
                L"Select the previous installed movie.",
                moviesAvailable ? MenuAction::PreviousMovie : MenuAction::None},
            {1, text("GUI:Play", L"Play Movie"),
                L"Play the selected movie. Press Esc to skip it.",
                moviesAvailable ? MenuAction::PlayMovie : MenuAction::None},
            {2, text("GUI:Next", L"Next"),
                L"Select the next installed movie.",
                moviesAvailable ? MenuAction::NextMovie : MenuAction::None},
            {3, text("GUI:ViewCredits", L"Credits"),
                L"View the game credits.", MenuAction::OpenCredits},
            {-1, text("GUI:Back", L"Back"),
                L"Return to the Main Menu.", MenuAction::OpenHome}
        };
    }
    if (state.page == MenuPage::Credits) {
        return {
            {0, text("GUI:Previous", L"Previous Page"),
                L"Show the previous credits page.",
                MenuAction::PreviousCreditsPage},
            {1, text("GUI:Next", L"Next Page"),
                L"Show the next credits page.",
                MenuAction::NextCreditsPage},
            {-1, text("GUI:Back", L"Back"),
                L"Return to Movies & Credits.", MenuAction::OpenMovies}
        };
    }
    if (state.page == MenuPage::Options) {
        return {
            {0, text("GUI:Resolution", L"Resolution"),
                L"Cycle the game window resolution.",
                MenuAction::CycleResolution},
            {1, text("GUI:TargetFPS", L"Frame Rate"),
                L"Cycle the target frame rate.",
                MenuAction::CycleTargetFps},
            {2, text("GUI:GameSpeed", L"Game Speed"),
                L"Cycle the default simulation speed.",
                MenuAction::CycleGameSpeed},
            {3, text("GUI:Apply", L"Apply"),
                L"Apply these options and return to the Main Menu.",
                MenuAction::ApplyOptions},
            {-1, text("GUI:Back", L"Cancel"),
                L"Discard these changes and return to the Main Menu.",
                MenuAction::OpenHome}
        };
    }
    return {
        {0, text("GUI:SinglePlayer", L"Single Player"),
            text("STT:MainButtonSinglePlayer", L"Play a single player game."),
            MenuAction::OpenSinglePlayer},
        {1, text("GUI:Internet", L"Internet"),
            text("STT:MainButtonWWOnline", L"Play on the Internet."),
            MenuAction::Internet},
        {2, text("GUI:Network", L"Network"),
            text("STT:MainButtonNetwork", L"Play a network game."),
            MenuAction::Network},
        {3, text("GUI:MoviesAndCredits", L"Movies & Credits"),
            text("STT:MainButtonMovies", L"View movies and credits."),
            MenuAction::OpenMovies},
        {4, text("GUI:Options", L"Options"),
            text("STT:MainButtonOptions", L"Customize game settings."),
            MenuAction::OpenOptions},
        {-1, text("GUI:ExitGame", L"Exit Game"),
            text("STT:MainButtonExitGamemd", L"Quit Yuri's Revenge."), MenuAction::Exit}
    };
}

int sidebarX(const MenuState& state) {
    (void)state;
    return kSidebarLeft;
}

int buttonsTop(const MenuState& state) {
    (void)state;
    return kSidebarTopHeight;
}

int slotHeight(const MenuState& state) {
    (void)state;
    return kSlotHeight;
}

int slotCount(const MenuState& state) {
    (void)state;
    // The original shell has eight 42-pixel background slots ending at y=535.
    // Exit/Back is a separate button inside SDBTM below those slots.
    return kSlotCount;
}

int resolvedSlot(const MenuState& state, const ButtonSpec& button) {
    (void)state;
    // Negative is the dedicated SDBTM button, not the eighth normal slot.
    return button.slot;
}

int buttonTopFor(const ButtonSpec& button) {
    return button.slot < 0
        ? kBottomButtonTop
        : kMenuButtonTop + button.slot * kSlotHeight;
}

bool loadPalette(
    MenuState& state,
    const std::string& id,
    bool useAllContext = false
) {
    std::vector<RuntimePaletteColor> palette;
    std::string source;
    std::string error;
    auto& paletteResources = useAllContext ? state.allResources : state.ra2Resources;
    bool loaded = paletteResources.loadPalette(id, palette, source, error);
    if (!loaded && !useAllContext) {
        loaded = state.allResources.loadPalette(id, palette, source, error);
    }
    if (!loaded || palette.size() < 256) {
        std::cerr << "Main menu palette load failed: " << id << ": " << error << "\n";
        return false;
    }
    state.palettes[id] = std::move(palette);
    state.paletteSources[id] = source;
    return true;
}

bool loadFrames(
    MenuState& state,
    const std::string& id,
    const std::string& paletteId,
    int maximumFrames,
    bool transparentZero,
    std::vector<AssetImage>& output,
    bool useAllContext = false
) {
    output.clear();
    const auto palette = state.palettes.find(paletteId);
    if (palette == state.palettes.end()) return false;
    auto& frameResources = useAllContext ? state.allResources : state.ra2Resources;
    for (int frame = 0; frame < maximumFrames; ++frame) {
        AssetImage image;
        std::string error;
        if (!frameResources.loadFrame(
                id, frame, palette->second,
                transparentZero, image, error)) {
            if (frame == 0) {
                std::cerr << "Main menu SHP load failed: " << id << ": " << error << "\n";
            }
            break;
        }
        output.push_back(std::move(image));
    }
    return !output.empty();
}

bool loadOne(
    MenuState& state,
    const std::string& id,
    const std::string& paletteId,
    bool transparentZero,
    AssetImage& output,
    bool useAllContext = false
) {
    std::vector<AssetImage> frames;
    if (!loadFrames(
            state, id, paletteId, 1, transparentZero, frames, useAllContext)) {
        return false;
    }
    output = std::move(frames.front());
    return true;
}

void initializeFonts(MenuState& state) {
    state.buttonFont = CreateFontW(
        -17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    state.titleFont = CreateFontW(
        -16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
    state.smallFont = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, FF_DONTCARE, L"Arial Narrow"
    );
}

void loadMovieLibrary(MenuState& state) {
    state.moviePaths.clear();
    const std::array<fs::path, 6> roots{{
        fs::path(state.config.resourceRootPath) / "GameMD" / "Movies",
        fs::path(state.config.resourceRootPath) / "Movies",
        fs::path(state.config.resourceRootPath) / "GameMD",
        state.exportRoot / "movmd03",
        state.exportRoot / "langmd",
        state.exportRoot / "language"
    }};
    for (const fs::path& root : roots) {
        std::error_code error;
        fs::directory_iterator iterator(
            root,
            fs::directory_options::skip_permission_denied,
            error
        );
        const fs::directory_iterator end;
        while (!error && iterator != end) {
            const fs::directory_entry entry = *iterator;
            if (entry.is_regular_file(error)
                && lowerAscii(entry.path().extension().string()) == ".bik") {
                const std::string candidate = lowerAscii(
                    entry.path().filename().string()
                );
                const bool duplicate = std::any_of(
                    state.moviePaths.begin(),
                    state.moviePaths.end(),
                    [&candidate](const fs::path& existing) {
                        return lowerAscii(existing.filename().string()) == candidate;
                    }
                );
                if (!duplicate) state.moviePaths.push_back(entry.path());
            }
            iterator.increment(error);
        }
    }
    std::sort(
        state.moviePaths.begin(),
        state.moviePaths.end(),
        [](const fs::path& left, const fs::path& right) {
            return lowerAscii(left.filename().string())
                < lowerAscii(right.filename().string());
        }
    );
    if (state.movieIndex >= state.moviePaths.size()) state.movieIndex = 0;
}

std::wstring expandCreditsLine(
    const CsfCatalog& csf,
    const std::string& source
) {
    const std::string trimmed = trimAscii(source);
    if (trimmed.empty()) return {};
    std::wstring result;
    std::size_t cursor = 0;
    while (cursor < trimmed.size()) {
        const std::size_t open = trimmed.find('{', cursor);
        if (open == std::string::npos) {
            result += toWide(trimmed.substr(cursor));
            break;
        }
        result += toWide(trimmed.substr(cursor, open - cursor));
        const std::size_t close = trimmed.find('}', open + 1);
        if (close == std::string::npos) {
            result += toWide(trimmed.substr(open));
            break;
        }
        const std::string key = trimmed.substr(open + 1, close - open - 1);
        std::string readableKey = key;
        const std::size_t colon = readableKey.find(':');
        if (colon != std::string::npos) {
            readableKey.erase(0, colon + 1);
        }
        std::replace(readableKey.begin(), readableKey.end(), '_', ' ');
        const std::wstring fallback = toWide(readableKey);
        result += csf.get(key, fallback.c_str());
        cursor = close + 1;
    }
    return result;
}

void loadCredits(MenuState& state) {
    state.creditLines.clear();
    const fs::path path = firstExisting({
        fs::path(state.config.resourceRootPath) / "GameMD" / "creditsmd.txt",
        fs::path(state.config.resourceRootPath) / "creditsmd.txt",
        state.exportRoot / "langmd" / "creditsmd.txt",
        state.exportRoot / "lang" / "credits.txt"
    });
    std::ifstream input(path);
    std::string line;
    bool previousBlank = true;
    while (std::getline(input, line)) {
        const std::wstring expanded = expandCreditsLine(state.csf, line);
        const bool blank = expanded.empty();
        if (blank && previousBlank) continue;
        state.creditLines.push_back(expanded);
        previousBlank = blank;
    }
    while (!state.creditLines.empty() && state.creditLines.back().empty()) {
        state.creditLines.pop_back();
    }
    if (state.creditLines.empty()) {
        state.creditLines = {
            state.csf.get("CRD:CREDITS", L"Credits"),
            L"Westwood Studios",
            L"ConcordRTS"
        };
    }
    state.creditsPage = 0;
}

bool initializeMenu(MenuState& state, const RuntimeConfig& config, bool smoke) {
    state.config = config;
    state.resourceWatch = std::make_unique<RuntimeMenuResourceWatch>(config);
    state.smoke = smoke;
    state.exportRoot = fs::path(config.localDataRootPath) / "resources" / "mix_exports";
    const fs::path ra2Root = state.exportRoot / "ra2";
    concord::assets::MapPreviewResourceContext ra2Roots;
    ra2Roots.assetRootPath = ra2Root.string();
    ra2Roots.tileRootPath = ra2Root.string();
    state.ra2Resources.configureRoots(std::move(ra2Roots));
    concord::assets::MapPreviewResourceContext allRoots;
    allRoots.assetRootPath = state.exportRoot.string();
    allRoots.tileRootPath = state.exportRoot.string();
    state.allResources.configureRoots(std::move(allRoots));

    const fs::path csfPath = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "ra2md.csf",
        state.exportRoot / "langmd" / "ra2md.csf",
        state.exportRoot / "lang" / "ra2.csf"
    });
    if (!csfPath.empty()) state.csf.load(csfPath);
    loadMovieLibrary(state);
    loadCredits(state);
    const fs::path fontPath = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "game.fnt",
        fs::path(config.resourceRootPath) / "Game" / "game.fnt"
    });
    if (!fontPath.empty()) state.gameFont.load(fontPath);

    const fs::path rulesPath = firstExisting({
        fs::path(config.localDataRootPath) / "rulesmd.ini",
        state.exportRoot / "expandmd01" / "rulesmd.ini",
        fs::path(config.resourceRootPath) / "GameMD" / "rulesmd.ini"
    });
    IniFile rules;
    if (!rulesPath.empty() && rules.loadFromFile(rulesPath.string())) {
        state.menuTextColorSource = iniValueNoCase(
            rules,
            "Colors",
            "LightGold"
        );
        if (const auto hsv = parseWestwoodHsv(state.menuTextColorSource)) {
            state.menuTextColors = westwoodFontColorRamp(*hsv);
        }
    }

    const bool palettesLoaded =
        loadPalette(state, "shell.pal")
        && loadPalette(state, "sdbtnanm.pal")
        && loadPalette(state, "mainbttn.pal", true)
        && loadPalette(state, "mousepal.pal");

    bool assetsLoaded = palettesLoaded;
    assetsLoaded = loadOne(state, "mnscrnl.shp", "mainbttn.pal", true, state.mainShell)
        && assetsLoaded;
    assetsLoaded = loadOne(state, "lwscrnl.shp", "shell.pal", false, state.statusBar)
        && assetsLoaded;
    assetsLoaded = loadFrames(
        state, "sdtp.shp", "shell.pal", 2, false, state.sideTop
    ) && state.sideTop.size() >= 2 && assetsLoaded;
    assetsLoaded = loadFrames(
        state, "sdwrnanm.shp", "shell.pal", 91, true, state.warning
    ) && state.warning.size() == 91 && assetsLoaded;
    assetsLoaded = loadOne(
        state, "sdbtnbkgd.shp", "shell.pal", false, state.slotBackground
    ) && assetsLoaded;
    assetsLoaded = loadFrames(
        state, "sdbtnanm.shp", "sdbtnanm.pal", 17, true, state.slotAnimation
    ) && state.slotAnimation.size() >= 17 && assetsLoaded;
    assetsLoaded = loadOne(
        state, "sdbtm.shp", "shell.pal", false, state.sideBottom
    ) && assetsLoaded;
    loadFrames(
        state, "mnbttn.shp", "mainbttn.pal", 3, true, state.redButton, true
    );
    loadOne(state, "mouse.shp", "mousepal.pal", true, state.mouse);

    loadBmp(state.exportRoot / "langmd" / "ra2ts_l_frame.bmp", state.videoFrame);
    concord::assets::loadMenuLooseFrame(
        (state.exportRoot / "langmd" / "glslmd.shp").string(),
        (state.exportRoot / "langmd" / "glsmd.pal").string(),
        false,
        state.staticFallback
    );

    state.audio.enabled = !smoke;
    state.audio.musicVolumePercent = std::clamp(config.musicVolume, 0, 100);
    state.audio.assetRoot = state.exportRoot;
    state.audio.cacheRoot = fs::path(config.localDataRootPath) / "runtime_audio_cache";
    state.audio.initialize(firstExisting({
        state.exportRoot / "expandmd01" / "soundmd.ini",
        state.exportRoot / "ra2md" / "localmd" / "soundmd.ini"
    }));
    state.themeCatalogPath = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "thememd.ini",
        state.exportRoot / "ra2md" / "localmd" / "thememd.ini"
    });
    IniFile themeCatalog;
    if (!state.themeCatalogPath.empty()) {
        themeCatalog.loadFromFile(state.themeCatalogPath.string());
    }
    const std::string introSound = lowerAscii(iniValueNoCase(
        themeCatalog,
        "INTRO",
        "Sound",
        "Drok"
    ));
    const std::string creditsSound = lowerAscii(iniValueNoCase(
        themeCatalog,
        "CREDITS",
        "Sound",
        "OptionX"
    ));
    const std::string scoreSound = lowerAscii(iniValueNoCase(
        themeCatalog,
        "SCORE",
        "Sound",
        "ScoreX"
    ));
    const auto resolvePhaseTheme = [
        &state
    ](
        const std::string& themeId,
        const std::string& fallbackSound
    ) {
        const AudioBackgroundMusicResolveResult resolved =
            resolveBackgroundMusicThemeToWave(
                state.exportRoot.string(),
                state.audio.cacheRoot.string(),
                themeId
            );
        if (resolved.success && !resolved.wavPath.empty()) {
            return fs::path(resolved.wavPath);
        }
        return firstExisting({
            state.exportRoot / "thememd" / (fallbackSound + ".wav")
        });
    };
    state.mainThemePath = resolvePhaseTheme("INTRO", introSound);
    state.moviesThemePath = resolvePhaseTheme("SCORE", scoreSound);
    state.creditsThemePath = resolvePhaseTheme("CREDITS", creditsSound);
    state.optionsThemePath = state.moviesThemePath;
    state.optionWindowWidth = std::max(1, config.windowWidth);
    state.optionWindowHeight = std::max(1, config.windowHeight);
    state.optionTargetFps = std::max(1, config.targetFps);
    state.optionGameSpeed = std::clamp(config.gameSpeed, 0, 6);
    initializeFonts(state);
    state.phaseStarted = GetTickCount64();
    state.initialized = assetsLoaded;
    return state.initialized;
}

struct RenderSurface {
    HDC dc = nullptr;
    unsigned char* pixels = nullptr;
};

bool ensureBackBuffer(MenuState& state, HDC reference) {
    if (state.backDc != nullptr && state.backBitmap != nullptr
        && state.backOldBitmap != nullptr
        && state.backBits != nullptr
        && state.composePixels.size() == static_cast<std::size_t>(kWidth * kHeight * 4)) {
        return true;
    }
    state.backInfo = {};
    state.backInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    state.backInfo.bmiHeader.biWidth = kWidth;
    state.backInfo.bmiHeader.biHeight = -kHeight;
    state.backInfo.bmiHeader.biPlanes = 1;
    state.backInfo.bmiHeader.biBitCount = 32;
    state.backInfo.bmiHeader.biCompression = BI_RGB;
    state.backDc = CreateCompatibleDC(reference);
    if (state.backDc == nullptr) return false;
    state.backBitmap = CreateDIBSection(
        reference,
        &state.backInfo,
        DIB_RGB_COLORS,
        &state.backBits,
        nullptr,
        0
    );
    if (state.backBitmap == nullptr) return false;
    state.backOldBitmap = SelectObject(state.backDc, state.backBitmap);
    if (state.backOldBitmap == nullptr || state.backOldBitmap == HGDI_ERROR) return false;
    state.composePixels.assign(static_cast<std::size_t>(kWidth * kHeight * 4), 0);
    state.backDirty = true;
    return true;
}

bool uploadBackBuffer(MenuState& state) {
    if (state.backDc == nullptr || state.backBits == nullptr
        || state.composePixels.empty()) {
        return false;
    }
    // Finish any earlier BitBlt that reads the selected DIB, then replace its
    // complete memory image in one CPU copy.  This avoids GDI deferring a
    // SetDIBitsToDevice read while the compositor starts the next frame.
    GdiFlush();
    std::copy(
        state.composePixels.begin(),
        state.composePixels.end(),
        static_cast<unsigned char*>(state.backBits)
    );
    return true;
}

void clearSurface(RenderSurface& surface) {
    std::uint32_t* pixels = reinterpret_cast<std::uint32_t*>(surface.pixels);
    std::fill(pixels, pixels + static_cast<std::size_t>(kWidth * kHeight), 0xff000000U);
}

void blit(
    RenderSurface& surface,
    const AssetImage& image,
    int left,
    int top,
    int clipHeight = std::numeric_limits<int>::max()
) {
    if (!image.valid()) return;
    const int rows = std::min(image.height, clipHeight);
    for (int y = 0; y < rows; ++y) {
        const int dy = top + y;
        if (dy < 0 || dy >= kHeight) continue;
        for (int x = 0; x < image.width; ++x) {
            const int dx = left + x;
            if (dx < 0 || dx >= kWidth) continue;
            const std::size_t source = static_cast<std::size_t>(y * image.width + x) * 4;
            const unsigned char alpha = image.bgra[source + 3];
            if (alpha == 0) continue;
            const std::size_t dest = static_cast<std::size_t>(dy * kWidth + dx) * 4;
            if (alpha == 255) {
                surface.pixels[dest] = image.bgra[source];
                surface.pixels[dest + 1] = image.bgra[source + 1];
                surface.pixels[dest + 2] = image.bgra[source + 2];
                surface.pixels[dest + 3] = 255;
            } else {
                for (int channel = 0; channel < 3; ++channel) {
                    surface.pixels[dest + channel] = static_cast<unsigned char>(
                        (static_cast<unsigned int>(image.bgra[source + channel]) * alpha
                            + static_cast<unsigned int>(surface.pixels[dest + channel])
                                * (255U - alpha)) / 255U
                    );
                }
            }
        }
    }
}

void blitScaled(
    RenderSurface& surface,
    const AssetImage& image,
    int left,
    int top,
    int width,
    int height
) {
    if (!image.valid() || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const int sy = std::clamp(y * image.height / height, 0, image.height - 1);
        const int dy = top + y;
        if (dy < 0 || dy >= kHeight) continue;
        for (int x = 0; x < width; ++x) {
            const int sx = std::clamp(x * image.width / width, 0, image.width - 1);
            const int dx = left + x;
            if (dx < 0 || dx >= kWidth) continue;
            const std::size_t source = static_cast<std::size_t>(sy * image.width + sx) * 4;
            if (image.bgra[source + 3] == 0) continue;
            const std::size_t dest = static_cast<std::size_t>(dy * kWidth + dx) * 4;
            std::copy_n(image.bgra.data() + source, 4, surface.pixels + dest);
            surface.pixels[dest + 3] = 255;
        }
    }
}

int phaseFrameForSlot(const MenuState& state, int slot, bool occupied) {
    if (state.phase == MenuPhase::Idle) {
        return occupied ? 2 : 0;
    }
    const int tick = state.phaseTick - slot;
    if (state.phase == MenuPhase::SlideIn) {
        if (tick < 0) return occupied ? 10 : 16;
        if (tick >= kAnimationFrames) return occupied ? 1 : 0;
        return (occupied ? 10 : 16) - tick;
    }
    if (tick < 0) return occupied ? 5 : 11;
    if (tick >= kAnimationFrames) return occupied ? 10 : 16;
    return (occupied ? 5 : 11) + tick;
}

void drawShadowText(
    HDC dc,
    HFONT font,
    const std::wstring& text,
    RECT rect,
    UINT format,
    const FontColorRamp& colors
) {
    if (text.empty()) return;
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    SetTextColor(dc, colors.dark);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &shadow, format);
    SetTextColor(dc, colors.middle);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, format);
    SelectObject(dc, oldFont);
}

void drawMenuText(
    MenuState& state,
    RenderSurface& surface,
    HFONT fallbackFont,
    const std::wstring& text,
    RECT rect,
    UINT format
) {
    if (state.gameFont.valid()) {
        state.gameFont.draw(
            surface.pixels,
            kWidth,
            kHeight,
            text,
            rect,
            (format & DT_CENTER) != 0,
            (format & DT_VCENTER) != 0,
            state.menuTextColors
        );
        return;
    }
    drawShadowText(
        surface.dc,
        fallbackFont,
        text,
        rect,
        format,
        state.menuTextColors
    );
}

void shadeContentPanel(RenderSurface& surface) {
    constexpr RECT panel{28, 34, 604, 542};
    for (int y = panel.top; y < panel.bottom; ++y) {
        for (int x = panel.left; x < panel.right; ++x) {
            const std::size_t at = static_cast<std::size_t>(y * kWidth + x) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                surface.pixels[at + channel] = static_cast<unsigned char>(
                    static_cast<unsigned int>(surface.pixels[at + channel]) * 2U / 5U
                );
            }
        }
    }
}

std::wstring menuPageTitle(const MenuState& state) {
    switch (state.page) {
    case MenuPage::Home:
        return state.csf.get("GUI:MainMenu", L"Main Menu");
    case MenuPage::SinglePlayer:
        return state.csf.get("GUI:SinglePlayerMenu", L"Single Player");
    case MenuPage::Movies:
        return state.csf.get("GUI:MoviesAndCredits", L"Movies & Credits");
    case MenuPage::Credits:
        return state.csf.get("CRD:CREDITS", L"Credits");
    case MenuPage::Options:
        return state.csf.get("GUI:Options", L"Options");
    }
    return {};
}

std::wstring gameSpeedLabel(const MenuState& state, int speed) {
    static constexpr std::array<const char*, 7> keys{{
        "TXT_SLOWEST", "TXT_SLOWER", "TXT_SLOW", "TXT_MEDIUM",
        "TXT_FAST", "TXT_FASTER", "TXT_FASTEST"
    }};
    static constexpr std::array<const wchar_t*, 7> fallbacks{{
        L"Slowest", L"Slower", L"Slow", L"Medium",
        L"Fast", L"Faster", L"Fastest"
    }};
    const std::size_t index = static_cast<std::size_t>(std::clamp(speed, 0, 6));
    return state.csf.get(keys[index], fallbacks[index]);
}

void drawPageContent(MenuState& state, RenderSurface& surface) {
    if (state.page == MenuPage::Movies) {
        drawMenuText(
            state,
            surface,
            state.titleFont,
            state.csf.get("GUI:MoviesAndCredits", L"Movies & Credits"),
            RECT{54, 62, 578, 96},
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
        if (state.moviePaths.empty()) {
            drawMenuText(
                state,
                surface,
                state.buttonFont,
                L"No installed Bink movies were found.",
                RECT{54, 230, 578, 266},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
        } else {
            const fs::path& selected = state.moviePaths[
                std::min(state.movieIndex, state.moviePaths.size() - 1)
            ];
            drawMenuText(
                state,
                surface,
                state.buttonFont,
                toWide(selected.stem().string()),
                RECT{54, 206, 578, 242},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
            const std::wstring position = std::to_wstring(state.movieIndex + 1)
                + L" / " + std::to_wstring(state.moviePaths.size());
            drawMenuText(
                state,
                surface,
                state.smallFont,
                position,
                RECT{54, 250, 578, 280},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
            drawMenuText(
                state,
                surface,
                state.smallFont,
                L"Choose Previous/Next, then Play Movie. Esc skips playback.",
                RECT{54, 300, 578, 330},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
        }
    } else if (state.page == MenuPage::Credits) {
        const std::size_t pageCount = std::max<std::size_t>(
            1,
            (state.creditLines.size() + kCreditsLinesPerPage - 1)
                / kCreditsLinesPerPage
        );
        const std::size_t page = std::min(state.creditsPage, pageCount - 1);
        const std::size_t first = page * kCreditsLinesPerPage;
        const std::size_t last = std::min(
            state.creditLines.size(),
            first + kCreditsLinesPerPage
        );
        int y = 52;
        for (std::size_t index = first; index < last; ++index, y += 25) {
            drawMenuText(
                state,
                surface,
                state.smallFont,
                state.creditLines[index],
                RECT{46, y, 586, y + 23},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
        }
        const std::wstring position = std::to_wstring(page + 1)
            + L" / " + std::to_wstring(pageCount);
        drawMenuText(
            state,
            surface,
            state.smallFont,
            position,
            RECT{46, 504, 586, 532},
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
    } else if (state.page == MenuPage::Options) {
        const std::array<std::pair<std::wstring, std::wstring>, 3> rows{{
            {
                state.csf.get("GUI:Resolution", L"Resolution"),
                std::to_wstring(state.optionWindowWidth) + L" x "
                    + std::to_wstring(state.optionWindowHeight)
            },
            {
                state.csf.get("GUI:TargetFPS", L"Target Frame Rate"),
                std::to_wstring(state.optionTargetFps) + L" FPS"
            },
            {
                state.csf.get("GUI:GameSpeed", L"Game Speed"),
                gameSpeedLabel(state, state.optionGameSpeed)
            }
        }};
        int y = 126;
        for (const auto& row : rows) {
            drawMenuText(
                state,
                surface,
                state.buttonFont,
                row.first,
                RECT{74, y, 320, y + 34},
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
            drawMenuText(
                state,
                surface,
                state.buttonFont,
                row.second,
                RECT{320, y, 558, y + 34},
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
            y += 78;
        }
        drawMenuText(
            state,
            surface,
            state.smallFont,
            L"Apply commits these defaults for the next game; Cancel discards them.",
            RECT{54, 418, 578, 448},
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
    }
    if (!state.pageStatus.empty()) {
        drawMenuText(
            state,
            surface,
            state.smallFont,
            state.pageStatus,
            RECT{44, 470, 588, 500},
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
    }
}

bool renderBase(MenuState& state, RenderSurface& surface) {
    if (surface.pixels == nullptr || surface.dc == nullptr) return false;
    const bool cpuBitFontPath = state.gameFont.valid();
    clearSurface(surface);

    if (state.videoFrame.valid()) {
        if (state.videoFrame.width == kLeftWidth && state.videoFrame.height >= kStatusTop) {
            blit(surface, state.videoFrame, 0, 0);
        } else {
            blitScaled(surface, state.videoFrame, 0, 0, kLeftWidth, kStatusTop + 2);
        }
    } else {
        if (state.staticFallback.valid()) {
            blitScaled(surface, state.staticFallback, 0, 0, kLeftWidth, kStatusTop);
        }
        blit(surface, state.mainShell, 0, 0);
    }
    blit(surface, state.statusBar, 0, kStatusTop);

    const int sideX = sidebarX(state);
    const int top = buttonsTop(state);
    const int height = slotHeight(state);
    const int slots = slotCount(state);
    if (!state.sideTop.empty()) blit(surface, state.sideTop.front(), sideX, 0);
    if (!state.warning.empty()) {
        const std::size_t frame = static_cast<std::size_t>(
            std::clamp(state.warningFrame, 0, static_cast<int>(state.warning.size()) - 1)
        );
        blit(surface, state.warning[frame], kWarningLeft, kWarningTop);
    }
    for (int slot = 0; slot < slots; ++slot) {
        blit(surface, state.slotBackground, sideX, top + slot * height);
    }
    blit(
        surface,
        state.sideBottom,
        kSidebarLeft,
        kSidebarBottomTop,
        kHeight - kSidebarBottomTop
    );

    const std::vector<ButtonSpec> current = buttons(state);
    for (int slot = 0; slot < slots; ++slot) {
        const auto found = std::find_if(
            current.begin(), current.end(), [&state, slot](const ButtonSpec& button) {
                return resolvedSlot(state, button) == slot;
            }
        );
        const bool occupied = found != current.end();
        const int frame = phaseFrameForSlot(state, slot, occupied);
        if (frame >= 0 && frame < static_cast<int>(state.slotAnimation.size())) {
            blit(surface, state.slotAnimation[static_cast<std::size_t>(frame)],
                kSidebarLeft + 12, kSidebarTopHeight + slot * kSlotHeight);
        }
    }

    if (state.phase == MenuPhase::Idle) {
        for (std::size_t i = 0; i < current.size(); ++i) {
            int frame = 0;
            if (state.pressed == static_cast<int>(i)) frame = 2;
            else if (state.hover == static_cast<int>(i)
                || (state.keyboardTooltip && state.focus == static_cast<int>(i))) frame = 1;
            if (frame < static_cast<int>(state.redButton.size())) {
                blit(surface, state.redButton[static_cast<std::size_t>(frame)],
                    kMenuButtonLeft, buttonTopFor(current[i]));
            }
        }
    }

    if (state.page == MenuPage::Movies
        || state.page == MenuPage::Credits
        || state.page == MenuPage::Options
        || !state.pageStatus.empty()) {
        shadeContentPanel(surface);
    }

    // The CPU compositor writes only its private byte vector.  Upload it via
    // GDI before any system-font fallback drawing; the selected persistent DIB
    // itself is never mutated through its shared bits while selected in a DC.
    if (!cpuBitFontPath) {
        if (state.mouseVisible && state.mouse.valid()) {
            blit(surface, state.mouse, state.mousePoint.x, state.mousePoint.y);
        }
        if (!uploadBackBuffer(state)) return false;
    }

    const std::wstring title = menuPageTitle(state);
    drawMenuText(
        state, surface, state.titleFont, title,
        RECT{sideX + 25, 3, sideX + 143, 35},
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
    );

    if (state.page == MenuPage::Movies
        || state.page == MenuPage::Credits
        || state.page == MenuPage::Options
        || !state.pageStatus.empty()) {
        drawPageContent(state, surface);
    }

    if (state.phase == MenuPhase::Idle) {
        for (std::size_t i = 0; i < current.size(); ++i) {
            const int y = buttonTopFor(current[i]);
            drawMenuText(
                state, surface, state.buttonFont, current[i].label,
                RECT{kMenuButtonLeft, y, kMenuButtonLeft + 126, y + 25},
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
            );
        }
    }

    int tooltipIndex = state.hover;
    if (tooltipIndex < 0 && state.keyboardTooltip) tooltipIndex = state.focus;
    if (tooltipIndex >= 0 && tooltipIndex < static_cast<int>(current.size())) {
        drawMenuText(
            state, surface, state.smallFont,
            current[static_cast<std::size_t>(tooltipIndex)].tooltip,
            RECT{10, kStatusTop + 3, kLeftWidth - 8, kHeight - 1},
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
        );
    }
    const std::wstring version = state.csf.get("GUI:Version", L"Version")
        + L" 1.001";
    drawMenuText(
        state, surface, state.smallFont, version,
        RECT{sideX, kHeight - 20, kWidth, kHeight},
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
    );

    if (cpuBitFontPath) {
        if (state.mouseVisible && state.mouse.valid()) {
            blit(surface, state.mouse, state.mousePoint.x, state.mousePoint.y);
        }
        if (!uploadBackBuffer(state)) return false;
    } else {
        GdiFlush();
    }
    state.backDirty = false;
    return true;
}

void updateViewport(MenuState& state, int clientWidth, int clientHeight) {
    const double fitScale = std::min(
        static_cast<double>(std::max(1, clientWidth)) / kWidth,
        static_cast<double>(std::max(1, clientHeight)) / kHeight
    );
    // Preserve the original pixel grid whenever it fits.  1024x640, the
    // normal runtime window, therefore shows an exact centered 800x600 image
    // instead of a blurred 1.066x stretch with shifted hit targets.
    const int integerScale = static_cast<int>(std::floor(fitScale));
    const double scale = integerScale >= 1
        ? static_cast<double>(integerScale)
        : fitScale;
    state.viewWidth = std::max(1, static_cast<int>(std::lround(kWidth * scale)));
    state.viewHeight = std::max(1, static_cast<int>(std::lround(kHeight * scale)));
    state.viewLeft = (clientWidth - state.viewWidth) / 2;
    state.viewTop = (clientHeight - state.viewHeight) / 2;
}

POINT clientToBase(const MenuState& state, POINT point) {
    POINT result{-1, -1};
    if (point.x < state.viewLeft || point.y < state.viewTop
        || point.x >= state.viewLeft + state.viewWidth
        || point.y >= state.viewTop + state.viewHeight) {
        return result;
    }
    result.x = std::clamp<int>(
        (point.x - state.viewLeft) * kWidth / std::max(1, state.viewWidth),
        0, kWidth - 1
    );
    result.y = std::clamp<int>(
        (point.y - state.viewTop) * kHeight / std::max(1, state.viewHeight),
        0, kHeight - 1
    );
    return result;
}

int hitButton(const MenuState& state, POINT point) {
    if (point.x < 0 || state.phase != MenuPhase::Idle) return -1;
    const int side = sidebarX(state);
    if (point.x < side + 12 || point.x >= side + 158) return -1;
    const int top = buttonsTop(state);
    const int height = slotHeight(state);
    const std::vector<ButtonSpec> current = buttons(state);
    for (std::size_t i = 0; i < current.size(); ++i) {
        const int slot = resolvedSlot(state, current[i]);
        if (slot < 0) {
            if (point.y >= kBottomHitTop && point.y < kHeight) {
                return static_cast<int>(i);
            }
        } else {
            const int y = top + slot * height;
            if (point.y >= y && point.y < y + height) return static_cast<int>(i);
        }
    }
    return -1;
}

void finishMenu(MenuState& state, RuntimeMainMenuResult result) {
    if (state.closing) return;
    state.closing = true;
    state.result = result;
    state.videoAnimation.stop();
    state.audio.stopMusic();
    if (state.window != nullptr && IsWindow(state.window)) DestroyWindow(state.window);
}

fs::path musicPathForPage(const MenuState& state) {
    if (state.page == MenuPage::Credits && !state.creditsThemePath.empty()) {
        return state.creditsThemePath;
    }
    if (state.page == MenuPage::Options && !state.optionsThemePath.empty()) {
        return state.optionsThemePath;
    }
    if (state.page == MenuPage::Movies && !state.moviesThemePath.empty()) {
        return state.moviesThemePath;
    }
    return state.mainThemePath;
}

void startPageMusic(MenuState& state) {
    const fs::path path = musicPathForPage(state);
    if (!path.empty()) state.audio.startMusic(path);
}

void publishMenuConfig(const MenuState& state, RuntimeConfig& output) {
    output = state.config;
}

MenuAction escapeActionForPage(MenuPage page) {
    switch (page) {
    case MenuPage::Home:
        return MenuAction::Exit;
    case MenuPage::Credits:
        return MenuAction::OpenMovies;
    case MenuPage::SinglePlayer:
    case MenuPage::Movies:
    case MenuPage::Options:
        return MenuAction::OpenHome;
    }
    return MenuAction::OpenHome;
}

void resizeMenuHostForOptions(MenuState& state) {
    if (state.window == nullptr || !IsWindow(state.window)) return;
    HWND target = GetParent(state.window);
    if (target == nullptr || !IsWindow(target)) target = state.window;
    if (GetParent(target) != nullptr) return;
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(target, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(target, GWL_EXSTYLE));
    RECT bounds{
        0,
        0,
        std::max(1, state.config.windowWidth),
        std::max(1, state.config.windowHeight)
    };
    AdjustWindowRectEx(&bounds, style, GetMenu(target) != nullptr, exStyle);
    SetWindowPos(
        target,
        nullptr,
        0,
        0,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE
    );
}

void cycleResolution(MenuState& state) {
    std::size_t next = 0;
    for (std::size_t index = 0; index < kOptionResolutions.size(); ++index) {
        if (kOptionResolutions[index].first == state.optionWindowWidth
            && kOptionResolutions[index].second == state.optionWindowHeight) {
            next = (index + 1) % kOptionResolutions.size();
            break;
        }
    }
    state.optionWindowWidth = kOptionResolutions[next].first;
    state.optionWindowHeight = kOptionResolutions[next].second;
}

void cycleTargetFps(MenuState& state) {
    std::size_t next = 0;
    for (std::size_t index = 0; index < kOptionTargetFps.size(); ++index) {
        if (kOptionTargetFps[index] == state.optionTargetFps) {
            next = (index + 1) % kOptionTargetFps.size();
            break;
        }
    }
    state.optionTargetFps = kOptionTargetFps[next];
}

std::size_t creditPageCount(const MenuState& state) {
    return std::max<std::size_t>(
        1,
        (state.creditLines.size() + kCreditsLinesPerPage - 1)
            / kCreditsLinesPerPage
    );
}

std::wstring moviePlaybackStatus(bool played, const std::string& error) {
    if (played) return L"Movie playback finished.";
    const std::wstring detail = error.empty()
        ? L"unknown decoder error"
        : toWide(error);
    return L"Playback unavailable: " + detail;
}

void playSelectedMovie(MenuState& state) {
    state.pageStatus.clear();
    if (state.moviePaths.empty()) {
        state.pageStatus = L"No installed movie is available.";
        state.backDirty = true;
        return;
    }
    state.videoFirstFramePending = false;
    state.videoAnimation.stop();
    state.audio.stopMusic();
    HWND playbackHost = state.window;
    if (state.window != nullptr && IsWindow(state.window)) {
        HWND parent = GetParent(state.window);
        if (parent != nullptr && IsWindow(parent)) playbackHost = parent;
    }
    std::string error;
    const fs::path& movie = state.moviePaths[
        std::min(state.movieIndex, state.moviePaths.size() - 1)
    ];
    const bool played = runRuntimeBinkVideo(
        state.config,
        playbackHost,
        movie.string(),
        error,
        state.config.voiceVolume
    );
    state.pageStatus = moviePlaybackStatus(played, error);
    startMainMenuVideo(state, state.config, 250);
    startPageMusic(state);
    if (state.window != nullptr && IsWindow(state.window)) {
        SetFocus(state.window);
        InvalidateRect(state.window, nullptr, FALSE);
    }
    state.backDirty = true;
}

bool performImmediateAction(MenuState& state, MenuAction action) {
    if (action == MenuAction::PreviousMovie) {
        if (!state.moviePaths.empty()) {
            state.movieIndex = (state.movieIndex + state.moviePaths.size() - 1)
                % state.moviePaths.size();
        }
    } else if (action == MenuAction::PlayMovie) {
        playSelectedMovie(state);
        return true;
    } else if (action == MenuAction::NextMovie) {
        if (!state.moviePaths.empty()) {
            state.movieIndex = (state.movieIndex + 1) % state.moviePaths.size();
        }
    } else if (action == MenuAction::PreviousCreditsPage) {
        const std::size_t pages = creditPageCount(state);
        state.creditsPage = (state.creditsPage + pages - 1) % pages;
    } else if (action == MenuAction::NextCreditsPage) {
        const std::size_t pages = creditPageCount(state);
        state.creditsPage = (state.creditsPage + 1) % pages;
    } else if (action == MenuAction::CycleResolution) {
        cycleResolution(state);
    } else if (action == MenuAction::CycleTargetFps) {
        cycleTargetFps(state);
    } else if (action == MenuAction::CycleGameSpeed) {
        state.optionGameSpeed = (state.optionGameSpeed + 1) % 7;
    } else {
        return false;
    }
    state.pageStatus.clear();
    state.backDirty = true;
    if (state.window != nullptr && IsWindow(state.window)) {
        InvalidateRect(state.window, nullptr, FALSE);
    }
    return true;
}

void beginAction(MenuState& state, MenuAction action) {
    if (state.phase != MenuPhase::Idle || state.closing) return;
    if (action == MenuAction::None) {
        return;
    }
    if (action == MenuAction::Skirmish && !state.smoke) {
        prewarmRuntimeSkirmishMenu(state.config);
    }
    state.pending = action;
    state.phase = MenuPhase::SlideOut;
    state.phaseStarted = GetTickCount64();
    state.phaseTick = 0;
    state.pressed = -1;
    state.backDirty = true;
    state.audio.play("MenuSlideOut");
    if (state.window != nullptr) InvalidateRect(state.window, nullptr, FALSE);
}

void activate(MenuState& state, int logicalIndex, bool playClick) {
    const std::vector<ButtonSpec> current = buttons(state);
    if (logicalIndex < 0 || logicalIndex >= static_cast<int>(current.size())) return;
    const MenuAction action = current[static_cast<std::size_t>(logicalIndex)].action;
    if (action == MenuAction::None) return;
    if (playClick) state.audio.play("MenuClick");
    if (!performImmediateAction(state, action)) beginAction(state, action);
}

bool updateAnimation(MenuState& state, std::uint64_t now) {
    if (state.phase == MenuPhase::Idle || state.closing) return false;
    bool changed = false;
    const std::uint64_t elapsed = now - state.phaseStarted;
    const int nextTick = static_cast<int>(elapsed / kAnimationFrameMs);
    if (nextTick != state.phaseTick) {
        state.phaseTick = nextTick;
        changed = true;
    }
    const std::uint64_t duration = static_cast<std::uint64_t>(
        slotCount(state) - 1 + kAnimationFrames
    ) * kAnimationFrameMs;
    if (elapsed < duration) return changed;
    if (state.phase == MenuPhase::SlideIn) {
        state.phase = MenuPhase::Idle;
        state.phaseTick = 0;
        return true;
    }

    const MenuAction action = state.pending;
    if (action == MenuAction::Skirmish && !state.smoke
        && !runtimeSkirmishMenuReady(state.config)) {
        state.pageStatus = L"正在准备遭遇战…";
        // Keep dispatching timer, paint, mouse and close messages while the
        // worker finishes. The video and cursor continue updating normally.
        return true;
    }
    state.pending = MenuAction::None;
    if (action == MenuAction::Campaign
        || action == MenuAction::LoadGame
        || action == MenuAction::Skirmish
        || action == MenuAction::Network
        || action == MenuAction::Internet) {
        RuntimeMainMenuResult result = RuntimeMainMenuResult::Skirmish;
        if (action == MenuAction::Campaign) {
            result = RuntimeMainMenuResult::Campaign;
        } else if (action == MenuAction::LoadGame) {
            result = RuntimeMainMenuResult::LoadGame;

        } else if (action == MenuAction::Network) {
            result = RuntimeMainMenuResult::Network;
        } else if (action == MenuAction::Internet) {
            result = RuntimeMainMenuResult::Internet;
        }
        finishMenu(state, result);
        return true;
    }
    if (action == MenuAction::Exit) {
        finishMenu(state, RuntimeMainMenuResult::Exit);
        return true;
    }
    if (action == MenuAction::ApplyOptions) {
        RuntimeConfig candidate = state.config;
        candidate.windowWidth = state.optionWindowWidth;
        candidate.windowHeight = state.optionWindowHeight;
        candidate.targetFps = state.optionTargetFps;
        candidate.gameSpeed = state.optionGameSpeed;
        std::string saveError;
        if (saveRuntimeConfigPreferences(candidate, saveError)) {
            state.config = std::move(candidate);
            resizeMenuHostForOptions(state);
            state.page = MenuPage::Home;
        } else {
            state.pageStatus = L"Could not save options: "
                + toWide(saveError);
        }
    } else if (action == MenuAction::OpenSinglePlayer) {
        state.page = MenuPage::SinglePlayer;
    } else if (action == MenuAction::OpenMovies) {
        state.page = MenuPage::Movies;
    } else if (action == MenuAction::OpenCredits) {
        state.page = MenuPage::Credits;
    } else if (action == MenuAction::OpenOptions) {
        state.optionWindowWidth = std::max(1, state.config.windowWidth);
        state.optionWindowHeight = std::max(1, state.config.windowHeight);
        state.optionTargetFps = std::max(1, state.config.targetFps);
        state.optionGameSpeed = std::clamp(state.config.gameSpeed, 0, 6);
        state.page = MenuPage::Options;
    } else if (action == MenuAction::OpenHome) {
        state.page = MenuPage::Home;
    }
    if (action != MenuAction::ApplyOptions
        || state.page != MenuPage::Options) {
        state.pageStatus.clear();
    }
    state.focus = 0;
    state.hover = -1;
    state.keyboardTooltip = false;
    state.phase = MenuPhase::SlideIn;
    state.phaseStarted = now;
    state.phaseTick = 0;
    startPageMusic(state);
    state.audio.play("MenuSlideIn");
    return true;
}

bool updateDynamicState(MenuState& state) {
    if (state.closing) return false;
    const std::uint64_t now = GetTickCount64();
    bool changed = false;
    changed = pollMainMenuVideo(state) || changed;
    if (!state.warning.empty()) {
        const int nextWarning = static_cast<int>(
            (now / 75ULL) % state.warning.size()
        );
        if (nextWarning != state.warningFrame) {
            state.warningFrame = nextWarning;
            changed = true;
        }
    }
    changed = updateAnimation(state, now) || changed;
    if (changed) state.backDirty = true;
    return changed;
}

bool renderClient(MenuState& state, HDC target, int width, int height) {
    updateViewport(state, width, height);
    if (!ensureBackBuffer(state, target)) return false;
    if (state.backDirty) {
        RenderSurface base{state.backDc, state.composePixels.data()};
        if (!renderBase(state, base)) return false;
    }

    BOOL copied = FALSE;
    if (state.viewWidth == kWidth && state.viewHeight == kHeight) {
        copied = BitBlt(
            target, state.viewLeft, state.viewTop, kWidth, kHeight,
            state.backDc, 0, 0, SRCCOPY
        );
    } else {
        SetStretchBltMode(target, COLORONCOLOR);
        copied = StretchBlt(
            target,
            state.viewLeft, state.viewTop, state.viewWidth, state.viewHeight,
            state.backDc, 0, 0, kWidth, kHeight, SRCCOPY
        );
    }
    const HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RECT bar{0, 0, width, state.viewTop};
    if (bar.bottom > bar.top) FillRect(target, &bar, black);
    bar = RECT{0, state.viewTop + state.viewHeight, width, height};
    if (bar.bottom > bar.top) FillRect(target, &bar, black);
    bar = RECT{0, state.viewTop, state.viewLeft, state.viewTop + state.viewHeight};
    if (bar.right > bar.left) FillRect(target, &bar, black);
    bar = RECT{
        state.viewLeft + state.viewWidth,
        state.viewTop,
        width,
        state.viewTop + state.viewHeight
    };
    if (bar.right > bar.left) FillRect(target, &bar, black);
    return copied != FALSE;
}

LRESULT CALLBACK mainMenuWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    MenuState* state = reinterpret_cast<MenuState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<MenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);

    switch (message) {
    case WM_CREATE:
        SetTimer(window, 1, 16, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        updateViewport(*state, LOWORD(lParam), HIWORD(lParam));
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        renderClient(*state, dc, client.right, client.bottom);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER:
        state->audio.ensureMusicPlaying();
        if (updateDynamicState(*state) && IsWindow(window)) {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT client{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const POINT base = clientToBase(*state, client);
        const POINT oldMouse = state->mousePoint;
        const bool oldVisible = state->mouseVisible;
        const int oldHover = state->hover;
        const bool oldKeyboardTooltip = state->keyboardTooltip;
        state->mousePoint = base;
        state->mouseVisible = base.x >= 0;
        state->hover = hitButton(*state, base);
        state->keyboardTooltip = false;
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);
        if (oldMouse.x != state->mousePoint.x || oldMouse.y != state->mousePoint.y
            || oldVisible != state->mouseVisible || oldHover != state->hover
            || oldKeyboardTooltip != state->keyboardTooltip) {
            state->backDirty = true;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        state->backDirty = state->mouseVisible || state->hover >= 0 || state->backDirty;
        state->mouseVisible = false;
        state->hover = -1;
        if (state->pressed < 0) InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT client{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const int hit = hitButton(*state, clientToBase(*state, client));
        if (hit >= 0) {
            SetCapture(window);
            state->focus = hit;
            state->hover = hit;
            state->pressed = hit;
            state->backDirty = true;
            state->audio.play("MenuClick");
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT client{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const int hit = hitButton(*state, clientToBase(*state, client));
        const int pressed = state->pressed;
        state->pressed = -1;
        state->backDirty = pressed >= 0 || state->backDirty;
        if (GetCapture() == window) ReleaseCapture();
        if (hit >= 0 && hit == pressed) activate(*state, hit, false);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_CAPTURECHANGED:
        state->backDirty = state->pressed >= 0 || state->backDirty;
        state->pressed = -1;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KEYDOWN: {
        const int count = static_cast<int>(buttons(*state).size());
        if (state->phase != MenuPhase::Idle || count <= 0) return 0;
        if (wParam == VK_UP || (wParam == VK_TAB && GetKeyState(VK_SHIFT) < 0)) {
            state->focus = (state->focus + count - 1) % count;
            state->keyboardTooltip = true;
            state->hover = -1;
            state->backDirty = true;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_TAB) {
            state->focus = (state->focus + 1) % count;
            state->keyboardTooltip = true;
            state->hover = -1;
            state->backDirty = true;
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        }
        if (wParam == VK_RETURN || wParam == VK_SPACE) {
            activate(*state, state->focus, true);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            state->audio.play("MenuClick");
            beginAction(*state, escapeActionForPage(state->page));
            return 0;
        }
        break;
    }
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && state->mouse.valid()) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        finishMenu(*state, RuntimeMainMenuResult::Exit);
        return 0;
    case WM_NCDESTROY:
        KillTimer(window, 1);
        state->audio.stopMusic();
        state->window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerMainMenuClass(HINSTANCE instance) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = mainMenuWindowProc;
    cls.hInstance = instance;
    cls.hCursor = nullptr;
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"ConcordRTS.RuntimeMainMenu";
    if (RegisterClassExW(&cls) != 0) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createMainMenuWindow(
    MenuState& state,
    const RuntimeConfig& config,
    HWND hostWindow
) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registerMainMenuClass(instance)) return nullptr;

    if (hostWindow != nullptr && IsWindow(hostWindow)) {
        RECT hostClient{};
        GetClientRect(hostWindow, &hostClient);
        return CreateWindowExW(
            0,
            L"ConcordRTS.RuntimeMainMenu",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            std::max(1, static_cast<int>(hostClient.right)),
            std::max(1, static_cast<int>(hostClient.bottom)),
            hostWindow,
            nullptr,
            instance,
            &state
        );
    }

    const int clientWidth = std::max(
        kWidth,
        config.windowWidth > 0 ? config.windowWidth : kWidth
    );
    const int clientHeight = std::max(
        kHeight,
        config.windowHeight > 0 ? config.windowHeight : kHeight
    );
    RECT windowRect{0, 0, clientWidth, clientHeight};
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    windowWidth = std::min(windowWidth, static_cast<int>(work.right - work.left));
    windowHeight = std::min(windowHeight, static_cast<int>(work.bottom - work.top));
    const int x = work.left + (work.right - work.left - windowWidth) / 2;
    const int y = work.top + (work.bottom - work.top - windowHeight) / 2;

    return CreateWindowExW(
        0,
        L"ConcordRTS.RuntimeMainMenu",
        L"Command & Conquer: Yuri's Revenge",
        style,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        &state
    );
}

bool focusMainMenuWindow(HWND window, HWND hostWindow) {
    if (window == nullptr || !IsWindow(window)) return false;
    HWND foreground = hostWindow;
    if (foreground == nullptr || !IsWindow(foreground)) foreground = window;
    SetForegroundWindow(foreground);
    SetFocus(window);
    return GetFocus() == window;
}

bool writeBmp(
    const std::string& path,
    const unsigned char* pixels,
    std::string& error
) {
    error.clear();
    if (path.empty() || pixels == nullptr) {
        error = "invalid output path or surface";
        return false;
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error = "cannot open output BMP: " + path;
        return false;
    }
    const DWORD pixelBytes = static_cast<DWORD>(kWidth * kHeight * 4);
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + pixelBytes;
    info.biSize = sizeof(info);
    info.biWidth = kWidth;
    info.biHeight = -kHeight;
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = pixelBytes;
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(reinterpret_cast<const char*>(pixels), pixelBytes);
    if (!output) {
        error = "failed while writing output BMP: " + path;
        return false;
    }
    return true;
}

bool mixBacked(const AssetImage& image) {
    return image.valid()
        && lowerAscii(image.source).find(".mix!") != std::string::npos;
}

struct BinkVideoPageState {
    RuntimeConfig config;
    BinkMenuAnimation animation;
    AssetImage frame;
    HWND window = nullptr;
    bool skipped = false;
    bool standalone = false;
    bool campaignMovie = false;
    int viewLeft = 0;
    int viewTop = 0;
    int viewWidth = 0;
    int viewHeight = 0;
};

void updateBinkVideoViewport(BinkVideoPageState& state, int width, int height) {
    if (!state.frame.valid()) {
        state.viewLeft = 0;
        state.viewTop = 0;
        state.viewWidth = std::max(1, width);
        state.viewHeight = std::max(1, height);
        return;
    }
    const double scale = std::min(
        static_cast<double>(std::max(1, width)) / state.frame.width,
        static_cast<double>(std::max(1, height)) / state.frame.height
    );
    state.viewWidth = std::max(1, static_cast<int>(
        std::lround(state.frame.width * scale)
    ));
    state.viewHeight = std::max(1, static_cast<int>(
        std::lround(state.frame.height * scale)
    ));
    state.viewLeft = (width - state.viewWidth) / 2;
    state.viewTop = (height - state.viewHeight) / 2;
}

void drawBinkVideoPageFrame(BinkVideoPageState& state, HDC dc, const RECT& client) {
    const int campaignFooterHeight = state.campaignMovie
        ? std::min(32, std::max(0, static_cast<int>(client.bottom - client.top) - 1))
        : 0;
    HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FillRect(dc, &client, black);
    updateBinkVideoViewport(
        state,
        client.right - client.left,
        client.bottom - client.top - campaignFooterHeight
    );
    if (state.frame.valid()) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = state.frame.width;
        info.bmiHeader.biHeight = -state.frame.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(
            dc,
            state.viewLeft,
            state.viewTop,
            state.viewWidth,
            state.viewHeight,
            0,
            0,
            state.frame.width,
            state.frame.height,
            state.frame.bgra.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    }
    if (campaignFooterHeight > 0) {
        const int savedDc = SaveDC(dc);
        RECT footer{client.left, client.bottom - campaignFooterHeight,
            client.right, client.bottom};
        FillRect(dc, &footer, black);
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextW(dc, L"\uff08\u6309 \u7a7a\u683c \u8df3\u8fc7\uff09", -1, &footer,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (savedDc != 0) RestoreDC(dc, savedDc);
    }
}

LRESULT CALLBACK binkVideoPageProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    BinkVideoPageState* state = reinterpret_cast<BinkVideoPageState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<BinkVideoPageState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->window = window;
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE:
        SetTimer(window, 1, 16, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (state->animation.poll(state->frame)) {
            InvalidateRect(window, nullptr, FALSE);
        } else if ((state->animation.finished()
                || !state->animation.failure().empty())
            && IsWindow(window)) {
            DestroyWindow(window);
        }
        return 0;
    case WM_KEYDOWN:
        if (state->campaignMovie && wParam == VK_SPACE) {
            // A held key must not skip the next authored clip in a sequence.
            if ((lParam & (LPARAM{1} << 30)) == 0) {
                state->skipped = true;
                DestroyWindow(window);
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            state->skipped = true;
            DestroyWindow(window);
            return 0;
        }
        return 0;
    case WM_CLOSE:
        state->skipped = true;
        DestroyWindow(window);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        drawBinkVideoPageFrame(*state, dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT: {
        RECT client{};
        GetClientRect(window, &client);
        if (wParam != 0) drawBinkVideoPageFrame(*state, reinterpret_cast<HDC>(wParam), client);
        return 0;
    }
    case WM_DESTROY:
        state->animation.stop();
        state->window = nullptr;
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool registerBinkVideoPageClass() {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc = binkVideoPageProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"ConcordRTS.RuntimeBinkVideoPage";
    if (RegisterClassExW(&cls) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

bool runRuntimeBinkVideo(
    const RuntimeConfig& config,
    void* hostWindow,
    const std::string& moviePath,
    std::string& errorMessage,
    int voiceVolumePercent,
    bool campaignMovie
) {
    errorMessage.clear();
    if (moviePath.empty()) {
        errorMessage = "campaign has no authored Bink movie";
        return false;
    }

    RuntimeCampaignMovieResolverOptions movieOptions;
    movieOptions.resourceRootPath = config.resourceRootPath;
    movieOptions.localDataRootPath = config.localDataRootPath;
    movieOptions.modRootPath = config.modRootPath;
    std::string materializedMoviePath;
    if (!materializeRuntimeCampaignMovie(
            movieOptions,
            moviePath,
            materializedMoviePath,
            errorMessage
        )) {
        return false;
    }

    BinkVideoPageState state;
    state.config = config;
    state.campaignMovie = campaignMovie;
    const fs::path movie(materializedMoviePath);
    if (!state.animation.start(
            config,
            {},
            movie,
            concord::bink_stream::PlaybackMode::Once,
            true,
            binkVolumeFromPercent(voiceVolumePercent)
        )) {
        errorMessage = state.animation.failure();
        if (errorMessage.empty()) {
            errorMessage = "Bink decoder bridge could not start";
        }
        return false;
    }
    if (!state.animation.poll(state.frame, 2000)) {
        errorMessage = state.animation.failure().empty()
            ? "first Bink frame timed out"
            : state.animation.failure();
        state.animation.stop();
        return false;
    }

    if (!registerBinkVideoPageClass()) {
        errorMessage = "Bink video page window class could not be registered";
        state.animation.stop();
        return false;
    }

    HWND parent = static_cast<HWND>(hostWindow);
    if (parent != nullptr && !IsWindow(parent)) {
        parent = nullptr;
    }
    HWND restoreFocus = GetFocus();
    if (restoreFocus != nullptr && !IsWindow(restoreFocus)) {
        restoreFocus = nullptr;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HWND window = nullptr;
    if (parent != nullptr) {
        RECT client{};
        GetClientRect(parent, &client);
        window = CreateWindowExW(
            0,
            L"ConcordRTS.RuntimeBinkVideoPage",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            std::max(1L, client.right),
            std::max(1L, client.bottom),
            parent,
            nullptr,
            instance,
            &state
        );
    } else {
        RECT windowRect{0, 0, kWidth, kHeight};
        AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0);
        state.standalone = true;
        window = CreateWindowExW(
            0,
            L"ConcordRTS.RuntimeBinkVideoPage",
            L"Command & Conquer: Yuri's Revenge",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            instance,
            &state
        );
    }
    if (window == nullptr) {
        errorMessage = "Bink video page window could not be created";
        state.animation.stop();
        return false;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    if (parent == nullptr) {
        SetForegroundWindow(window);
    } else {
        SetForegroundWindow(parent);
        BringWindowToTop(window);
    }
    SetFocus(window);

    while (IsWindow(window)) {
        MSG message{};
        bool dispatched = false;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            dispatched = true;
            if (message.message == WM_QUIT) {
                state.skipped = true;
                if (IsWindow(window)) {
                    DestroyWindow(window);
                }
                PostQuitMessage(static_cast<int>(message.wParam));
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (!IsWindow(window)) {
                break;
            }
        }
        if (!IsWindow(window)) {
            break;
        }
        if (!dispatched) {
            WaitMessage();
        }
    }
    if (restoreFocus != nullptr && IsWindow(restoreFocus)) {
        HWND restoreRoot = GetAncestor(restoreFocus, GA_ROOT);
        if (restoreRoot != nullptr) SetForegroundWindow(restoreRoot);
        SetFocus(restoreFocus);
    } else if (parent != nullptr && IsWindow(parent)) {
        SetForegroundWindow(parent);
        SetFocus(parent);
    }
    if (!state.skipped && !state.animation.failure().empty()) {
        errorMessage = state.animation.failure();
        return false;
    }
    return true;
}

#include "ui/campaign/diagnostics/RuntimeCampaignVideoSkipSmoke.inl"

void prewarmRuntimeMainMenu(
    const RuntimeConfig& config,
    void* hostWindow
) {
    if (prewarmedMainMenuState != nullptr && prewarmedMainMenuState->initialized
        && prewarmedMainMenuState->resourceWatch->unchanged(config)) return;
    auto state = std::make_unique<MenuState>();
    initializeMenu(*state, config, false);
    if (state->initialized) {
        createMainMenuWindow(
            *state,
            config,
            static_cast<HWND>(hostWindow)
        );
    }
    prewarmedMainMenuState = std::move(state);
}

RuntimeMainMenuResult runRuntimeMainMenu(
    RuntimeConfig& config,
    RuntimeMainMenuStartPage startPage,
    void* hostWindow
) {
    // A successful multiplayer ReturnToMainMenu destroys its handed-off
    // bridge before re-entering here. Discard that normal teardown fallback;
    // failed sessions never re-enter and leave their reason for RuntimeMain.
    clearRuntimeMultiplayerVisibleFailureForMainMenu();
    std::unique_ptr<MenuState> stateOwner;
    if (prewarmedMainMenuState != nullptr
        && prewarmedMainMenuState->resourceWatch->unchanged(config)) {
        stateOwner = std::move(prewarmedMainMenuState);
    } else {
        prewarmedMainMenuState.reset();
        stateOwner = std::make_unique<MenuState>();
        initializeMenu(*stateOwner, config, false);
    }
    MenuState& state = *stateOwner;
    if (!state.initialized) {
        std::cerr << "Main menu initialization failed: original shell assets are incomplete.\n";
        return RuntimeMainMenuResult::Exit;
    }
    state.config = config;
    state.closing = false;
    state.result = RuntimeMainMenuResult::Exit;
    state.pending = MenuAction::None;
    state.phaseTick = 0;
    state.focus = 0;
    state.hover = -1;
    state.pressed = -1;
    state.keyboardTooltip = false;
    state.mouseVisible = false;
    state.pageStatus.clear();
    state.optionWindowWidth = std::max(1, config.windowWidth);
    state.optionWindowHeight = std::max(1, config.windowHeight);
    state.optionTargetFps = std::max(1, config.targetFps);
    state.optionGameSpeed = std::clamp(config.gameSpeed, 0, 6);
    state.audio.musicVolumePercent = std::clamp(config.musicVolume, 0, 100);
    state.page = startPage == RuntimeMainMenuStartPage::SinglePlayer
        ? MenuPage::SinglePlayer
        : MenuPage::Home;
    HWND requestedHost = static_cast<HWND>(hostWindow);
    if (requestedHost != nullptr && !IsWindow(requestedHost)) {
        requestedHost = nullptr;
    }
    HWND window = state.window;
    if (window != nullptr
        && IsWindow(window)
        && GetParent(window) != requestedHost) {
        DestroyWindow(window);
        window = nullptr;
    }
    if (window == nullptr || !IsWindow(window)) {
        window = createMainMenuWindow(state, config, requestedHost);
    }
    if (window == nullptr) return RuntimeMainMenuResult::Exit;
    prewarmRuntimeSkirmishMenu(config);
    state.phase = MenuPhase::SlideIn;
    state.phaseStarted = GetTickCount64();
    startMainMenuVideo(state, config, 2000);
    state.backDirty = true;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    focusMainMenuWindow(window, requestedHost);
    startPageMusic(state);
    state.audio.play("MenuSlideIn");

    MSG message{};
    while (IsWindow(window)) {
        const BOOL received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) {
            if (received == 0) PostQuitMessage(static_cast<int>(message.wParam));
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    state.audio.stopMusic();
    publishMenuConfig(state, config);
    const RuntimeMainMenuResult result = state.result;
    prewarmedMainMenuState = std::move(stateOwner);
    return result;
}

int runRuntimeMainMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& bmpPath
) {
    const fs::path preferenceSmokePath = fs::temp_directory_path()
        / ("concord_main_menu_options_"
            + std::to_string(GetCurrentProcessId())
            + "_"
            + std::to_string(GetTickCount64())
            + ".ini");
    struct PreferenceSmokeFileGuard {
        fs::path path;
        ~PreferenceSmokeFileGuard() {
            std::error_code ignored;
            fs::remove(path, ignored);
        }
    } preferenceSmokeFileGuard{preferenceSmokePath};
    RuntimeConfig smokeConfig = config;
    smokeConfig.configPath = preferenceSmokePath.string();
    MenuState state;
    if (!initializeMenu(state, smokeConfig, true)) {
        std::cerr << "Main menu smoke failed: original shell resources did not initialize.\n";
        return 1;
    }
    HWND hostedShell = CreateWindowExW(
        0,
        L"STATIC",
        L"ConcordRTS hosted shell smoke",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        0,
        0,
        kWidth,
        kHeight,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    HWND hostedPage = hostedShell == nullptr
        ? nullptr
        : createMainMenuWindow(state, smokeConfig, hostedShell);
    const bool hostedParentMatched =
        hostedPage != nullptr
        && GetParent(hostedPage) == hostedShell
        && (static_cast<DWORD>(GetWindowLongPtrW(hostedPage, GWL_STYLE))
            & WS_CHILD) != 0
        && GetAncestor(hostedPage, GA_ROOT) == hostedShell;
    bool hostedFocusMatched = false;
    const bool headlessSmoke = std::getenv("CONCORD_MAIN_MENU_HEADLESS_SMOKE") != nullptr;
    if (hostedParentMatched && !headlessSmoke) {
        ShowWindow(hostedShell, SW_SHOW);
        ShowWindow(hostedPage, SW_SHOW);
        hostedFocusMatched = focusMainMenuWindow(hostedPage, hostedShell);
    }
    if (hostedPage != nullptr && IsWindow(hostedPage)) {
        DestroyWindow(hostedPage);
    }
    const bool hostedShellSurvived =
        hostedShell != nullptr && IsWindow(hostedShell);
    if (hostedShellSurvived) {
        DestroyWindow(hostedShell);
    }
    const bool hostedWindowContract =
        hostedParentMatched && (headlessSmoke || hostedFocusMatched) && hostedShellSurvived;
    if (!hostedWindowContract) {
        std::cerr << "Main menu smoke failed: hosted child page did not "
                  << "preserve its top-level shell.\n";
        return 1;
    }
    std::cout << "Main menu hosted transition: child=yes focus="
              << (headlessSmoke ? "skipped(hidden)" : "yes")
              << " hostSurvived=yes\n";

    // Match the normal menu lifecycle: prepare the skirmish page before
    // waiting for video and exercising navigation, rather than only when
    // the final transition is requested. Its completion still has the same
    // bounded transition wait and assertions below.
    const ULONGLONG skirmishPrewarmStarted = GetTickCount64();
    prewarmRuntimeSkirmishMenu(state.config);

    bool animationTested = false;
    LONG firstAnimationSequence = 0;
    LONG secondAnimationSequence = 0;
    std::uint64_t sampledRed = 0;
    std::uint64_t sampledBlue = 0;
    const bool playbackModeContract =
        std::wstring(binkStreamArgument(
            concord::bink_stream::PlaybackMode::Loop
        )) == L"--stream"
        && std::wstring(binkStreamArgument(
            concord::bink_stream::PlaybackMode::Once
        )) == L"--stream-once"
        && std::wstring(binkStreamArgument(
            concord::bink_stream::PlaybackMode::Loop,
            false
        )) == L"--stream-muted"
        && std::wstring(binkStreamArgument(
            concord::bink_stream::PlaybackMode::Once,
            false
        )) == L"--stream-once-muted"
        && binkVolumeFromPercent(-1) == 0
        && binkVolumeFromPercent(0) == 0
        && binkVolumeFromPercent(40) == 13107
        && binkVolumeFromPercent(50) == 16384
        && binkVolumeFromPercent(100) == 32768
        && binkVolumeFromPercent(101) == 32768
        && !concord::bink_stream::completesAfterFrame(
            concord::bink_stream::PlaybackMode::Loop,
            10,
            10
        )
        && !concord::bink_stream::completesAfterFrame(
            concord::bink_stream::PlaybackMode::Once,
            9,
            10
        )
        && concord::bink_stream::completesAfterFrame(
            concord::bink_stream::PlaybackMode::Once,
            10,
            10
        );
    if (!playbackModeContract) {
        std::cerr << "Main menu smoke failed: Bink loop/one-shot playback "
                     "contract is invalid.\n";
        return 1;
    }
    const fs::path animationMovie = firstExisting({
        state.exportRoot / "langmd" / "ra2ts_l.bik",
        state.exportRoot / "lang" / "ra2ts_l.bik"
    });
    const fs::path animationDll = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "Binkw32.dll",
        fs::path(config.resourceRootPath) / "Game" / "Binkw32.dll"
    });
    if (!animationMovie.empty() && !animationDll.empty()) {
        if (!startMainMenuVideo(state, config, 0)
            || !waitMainMenuVideoForSmoke(state)) {
            std::cerr << "Main menu smoke failed: original Bink animation did not "
                         "produce its first frame: "
                      << state.videoAnimation.failure() << "\n";
            return 1;
        }
        firstAnimationSequence = state.videoAnimation.sequence();
        if (!state.videoAnimation.poll(state.videoFrame, 1000)) {
            std::cerr << "Main menu smoke failed: original Bink animation did not advance.\n";
            return 1;
        }
        secondAnimationSequence = state.videoAnimation.sequence();
        state.videoAnimation.stop();
        for (std::size_t at = 0; at + 3 < state.videoFrame.bgra.size(); at += 64) {
            sampledBlue += state.videoFrame.bgra[at];
            sampledRed += state.videoFrame.bgra[at + 2];
        }
        animationTested = state.videoFrame.width == 632
            && state.videoFrame.height == 570
            && secondAnimationSequence > firstAnimationSequence
            && sampledRed * 5ULL > sampledBlue * 6ULL;
        if (!animationTested) {
            std::cerr << "Main menu smoke failed: Bink frames did not advance or "
                         "their BGRA red/blue channels are reversed.\n";
            return 1;
        }
    }
    const bool structuralAssets =
        mixBacked(state.mainShell)
        && mixBacked(state.statusBar)
        && state.sideTop.size() >= 2 && mixBacked(state.sideTop.front())
        && !state.warning.empty() && mixBacked(state.warning.front())
        && mixBacked(state.slotBackground)
        && state.slotAnimation.size() >= 17 && mixBacked(state.slotAnimation.front())
        && mixBacked(state.sideBottom)
        && state.redButton.size() >= 3
        && state.gameFont.valid();
    const bool exactOriginalGeometry =
        state.mainShell.width == 632 && state.mainShell.height == 568
        && state.statusBar.width == 632 && state.statusBar.height == 32
        && !state.sideTop.empty()
        && state.sideTop.front().width == 168 && state.sideTop.front().height == 199
        && state.slotBackground.width == 168 && state.slotBackground.height == 42
        && state.sideBottom.width == 168 && state.sideBottom.height >= 65
        && !state.redButton.empty()
        && state.redButton.front().width == 126 && state.redButton.front().height == 25
        && kSidebarTopHeight + kSlotCount * kSlotHeight == kSidebarBottomTop
        && kMenuButtonLeft == kSidebarLeft + 21
        && kMenuButtonTop == kSidebarTopHeight + 8
        && kBottomHitTop == kSidebarBottomTop
        && kBottomButtonTop == kSidebarBottomTop + 8
        && kWarningLeft == kSidebarLeft + 38;
    if (!structuralAssets || !exactOriginalGeometry) {
        std::cerr << "Main menu smoke failed: a critical SHP was not loaded from the original MIX set.\n";
        return 1;
    }
    const auto originalLightGold = parseWestwoodHsv(
        state.menuTextColorSource
    );
    if (!originalLightGold.has_value()
        || *originalLightGold != std::array<int, 3>{{25, 255, 255}}) {
        std::cerr << "Main menu smoke failed: original [Colors] LightGold "
                     "source was not loaded.\n";
        return 1;
    }
    if (state.csf.get("GUI:Version", L"Version") != L"\u7248\u672c") {
        std::cerr << "Main menu smoke failed: localized GUI:Version text "
                     "was not loaded from ra2md.csf.\n";
        return 1;
    }
    state.phase = MenuPhase::Idle;
    const std::vector<ButtonSpec> home = buttons(state);
    state.page = MenuPage::SinglePlayer;
    const std::vector<ButtonSpec> singlePlayer = buttons(state);
    state.page = MenuPage::Movies;
    const std::vector<ButtonSpec> movies = buttons(state);
    state.page = MenuPage::Credits;
    const std::vector<ButtonSpec> credits = buttons(state);
    state.page = MenuPage::Options;
    const std::vector<ButtonSpec> options = buttons(state);
    state.page = MenuPage::Home;
    const std::wstring internetLabel = state.csf.get("GUI:Internet", L"Internet");
    const std::wstring networkLabel = state.csf.get("GUI:Network", L"Network");
    const std::wstring internetTooltip = state.csf.get(
        "STT:MainButtonWWOnline", L"Play on the Internet."
    );
    const std::wstring networkTooltip = state.csf.get(
        "STT:MainButtonNetwork", L"Play a network game."
    );
    const bool networkEntriesExposed = home.size() >= 3
        && home[1].label == internetLabel
        && home[1].tooltip == internetTooltip
        && home[1].action == MenuAction::Internet
        && home[2].label == networkLabel
        && home[2].tooltip == networkTooltip
        && home[2].action == MenuAction::Network;
    const bool navigationLayout = home.size() == 6
        && home.front().action == MenuAction::OpenSinglePlayer
        && home[0].slot == 0
        && home[1].slot == 1
        && home[2].slot == 2
        && home[3].slot == 3
        && home[3].action == MenuAction::OpenMovies
        && home[4].slot == 4
        && home[4].action == MenuAction::OpenOptions
        && home.back().action == MenuAction::Exit
        && resolvedSlot(state, home.back()) < 0
        && singlePlayer.size() == 4
        && singlePlayer[0].action == MenuAction::Campaign
        && singlePlayer[1].action == MenuAction::LoadGame
        && singlePlayer[2].action == MenuAction::Skirmish
        && singlePlayer.back().action == MenuAction::OpenHome
        && resolvedSlot(state, singlePlayer.back()) < 0
        && movies.size() == 5
        && movies[0].action == MenuAction::PreviousMovie
        && movies[1].action == MenuAction::PlayMovie
        && movies[2].action == MenuAction::NextMovie
        && movies[3].action == MenuAction::OpenCredits
        && movies.back().action == MenuAction::OpenHome
        && credits.size() == 3
        && credits[0].action == MenuAction::PreviousCreditsPage
        && credits[1].action == MenuAction::NextCreditsPage
        && credits.back().action == MenuAction::OpenMovies
        && options.size() == 5
        && options[0].action == MenuAction::CycleResolution
        && options[1].action == MenuAction::CycleTargetFps
        && options[2].action == MenuAction::CycleGameSpeed
        && options[3].action == MenuAction::ApplyOptions
        && options.back().action == MenuAction::OpenHome;

    bool uniqueMovieNames = true;
    bool stableMovieOrder = true;
    bool hasEaLogo = false;
    bool hasSmallMenuMovie = false;
    for (std::size_t index = 0; index < state.moviePaths.size(); ++index) {
        const std::string name = lowerAscii(
            state.moviePaths[index].filename().string()
        );
        hasEaLogo = hasEaLogo || name == "ea_wwlogo.bik";
        hasSmallMenuMovie = hasSmallMenuMovie || name == "ra2ts_s.bik";
        if (index > 0) {
            const std::string previous = lowerAscii(
                state.moviePaths[index - 1].filename().string()
            );
            uniqueMovieNames = uniqueMovieNames && previous != name;
            stableMovieOrder = stableMovieOrder && previous < name;
        }
    }
    const bool movieLibraryContract = state.moviePaths.size() > 2
        && uniqueMovieNames
        && stableMovieOrder
        && hasEaLogo
        && hasSmallMenuMovie;
    const bool creditsContract = state.creditLines.size() > kCreditsLinesPerPage
        && std::none_of(
            state.creditLines.begin(),
            state.creditLines.end(),
            [](const std::wstring& line) {
                return line.find(L'{') != std::wstring::npos
                    || line.find(L'}') != std::wstring::npos;
            }
        )
        && expandCreditsLine(
            CsfCatalog{},
            "{CRD_TTL:NETWORK_PROGRAMMING}"
        ) == L"NETWORK PROGRAMMING";
    const bool themeContract = regularFile(state.mainThemePath)
        && regularFile(state.moviesThemePath)
        && regularFile(state.creditsThemePath)
        && regularFile(state.optionsThemePath)
        && regularFile(state.themeCatalogPath)
        && lowerAscii(state.mainThemePath.filename().string()) == "drok.wav"
        && lowerAscii(state.moviesThemePath.filename().string()) == "scorex.wav"
        && lowerAscii(state.creditsThemePath.filename().string()) == "optionx.wav"
        && lowerAscii(state.optionsThemePath.filename().string()) == "scorex.wav";
    const bool backHierarchy =
        escapeActionForPage(MenuPage::Home) == MenuAction::Exit
        && escapeActionForPage(MenuPage::SinglePlayer) == MenuAction::OpenHome
        && escapeActionForPage(MenuPage::Movies) == MenuAction::OpenHome
        && escapeActionForPage(MenuPage::Credits) == MenuAction::OpenMovies
        && escapeActionForPage(MenuPage::Options) == MenuAction::OpenHome;

    const auto settleTransition = [&state]() {
        if (state.phase == MenuPhase::Idle) return true;
        const std::uint64_t duration = static_cast<std::uint64_t>(
            slotCount(state) - 1 + kAnimationFrames
        ) * kAnimationFrameMs;
        updateAnimation(state, state.phaseStarted + duration);
        if (state.phase == MenuPhase::SlideIn) {
            updateAnimation(state, state.phaseStarted + duration);
        }
        return state.phase == MenuPhase::Idle;
    };

    state.page = MenuPage::Home;
    state.phase = MenuPhase::Idle;
    beginAction(state, MenuAction::OpenMovies);
    const bool moviesTransition = settleTransition()
        && state.page == MenuPage::Movies
        && musicPathForPage(state) == state.moviesThemePath;
    const std::size_t originalMovieIndex = state.movieIndex;
    const bool movieSelection = !state.moviePaths.empty()
        && performImmediateAction(
            state,
            MenuAction::NextMovie
        )
        && state.movieIndex
            == (originalMovieIndex + 1) % state.moviePaths.size()
        && performImmediateAction(state, MenuAction::PreviousMovie)
        && state.movieIndex == originalMovieIndex;
    beginAction(state, MenuAction::OpenCredits);
    const bool creditsTransition = settleTransition()
        && state.page == MenuPage::Credits
        && musicPathForPage(state) == state.creditsThemePath;
    state.creditsPage = 0;
    const std::size_t pages = creditPageCount(state);
    const bool creditsPaging = pages > 1
        && performImmediateAction(state, MenuAction::PreviousCreditsPage)
        && state.creditsPage == pages - 1
        && performImmediateAction(state, MenuAction::NextCreditsPage)
        && state.creditsPage == 0;

    const RuntimeConfig originalConfig = state.config;
    state.page = MenuPage::Home;
    state.phase = MenuPhase::Idle;
    beginAction(state, MenuAction::OpenOptions);
    const bool optionsTransition = settleTransition()
        && state.page == MenuPage::Options
        && state.optionWindowWidth == originalConfig.windowWidth
        && state.optionWindowHeight == originalConfig.windowHeight
        && state.optionTargetFps == originalConfig.targetFps
        && state.optionGameSpeed == originalConfig.gameSpeed
        && musicPathForPage(state) == state.optionsThemePath;
    performImmediateAction(state, MenuAction::CycleResolution);
    performImmediateAction(state, MenuAction::CycleTargetFps);
    performImmediateAction(state, MenuAction::CycleGameSpeed);
    beginAction(state, MenuAction::OpenHome);
    const bool cancelTransition = settleTransition();
    RuntimeConfig canceledConfig;
    publishMenuConfig(state, canceledConfig);
    const bool optionsCancel = cancelTransition
        && state.page == MenuPage::Home
        && canceledConfig.windowWidth == originalConfig.windowWidth
        && canceledConfig.windowHeight == originalConfig.windowHeight
        && canceledConfig.targetFps == originalConfig.targetFps
        && canceledConfig.gameSpeed == originalConfig.gameSpeed
        && musicPathForPage(state) == state.mainThemePath;

    state.page = MenuPage::Options;
    state.phase = MenuPhase::Idle;
    state.optionWindowWidth = originalConfig.windowWidth;
    state.optionWindowHeight = originalConfig.windowHeight;
    state.optionTargetFps = originalConfig.targetFps;
    state.optionGameSpeed = originalConfig.gameSpeed;
    performImmediateAction(state, MenuAction::CycleResolution);
    performImmediateAction(state, MenuAction::CycleTargetFps);
    performImmediateAction(state, MenuAction::CycleGameSpeed);
    const int appliedWidth = state.optionWindowWidth;
    const int appliedHeight = state.optionWindowHeight;
    const int appliedFps = state.optionTargetFps;
    const int appliedSpeed = state.optionGameSpeed;
    beginAction(state, MenuAction::ApplyOptions);
    const bool applyTransition = settleTransition();
    RuntimeConfig appliedConfig;
    publishMenuConfig(state, appliedConfig);
    const bool optionsApply = applyTransition
        && state.page == MenuPage::Home
        && appliedConfig.windowWidth == appliedWidth
        && appliedConfig.windowHeight == appliedHeight
        && appliedConfig.targetFps == appliedFps
        && appliedConfig.gameSpeed == appliedSpeed
        && (appliedWidth != originalConfig.windowWidth
            || appliedHeight != originalConfig.windowHeight)
        && appliedFps != originalConfig.targetFps
        && appliedSpeed != originalConfig.gameSpeed;
    RuntimeConfigOverrides persistedOverrides;
    persistedOverrides.configPath = preferenceSmokePath.string();
    const RuntimeConfig persistedOptions = loadRuntimeConfig(
        persistedOverrides
    );
    const bool optionsPersisted = fs::is_regular_file(preferenceSmokePath)
        && persistedOptions.windowWidth == appliedWidth
        && persistedOptions.windowHeight == appliedHeight
        && persistedOptions.targetFps == appliedFps
        && persistedOptions.gameSpeed == appliedSpeed;

    state.optionWindowWidth = kOptionResolutions.front().first;
    state.optionWindowHeight = kOptionResolutions.front().second;
    bool legalResolutionCycle = true;
    for (std::size_t count = 0; count < kOptionResolutions.size(); ++count) {
        cycleResolution(state);
        legalResolutionCycle = legalResolutionCycle && std::find(
            kOptionResolutions.begin(),
            kOptionResolutions.end(),
            std::pair<int, int>{state.optionWindowWidth, state.optionWindowHeight}
        ) != kOptionResolutions.end();
    }
    legalResolutionCycle = legalResolutionCycle
        && state.optionWindowWidth == kOptionResolutions.front().first
        && state.optionWindowHeight == kOptionResolutions.front().second;
    state.optionTargetFps = kOptionTargetFps.front();
    bool legalFpsCycle = true;
    for (std::size_t count = 0; count < kOptionTargetFps.size(); ++count) {
        cycleTargetFps(state);
        legalFpsCycle = legalFpsCycle && std::find(
            kOptionTargetFps.begin(),
            kOptionTargetFps.end(),
            state.optionTargetFps
        ) != kOptionTargetFps.end();
    }
    legalFpsCycle = legalFpsCycle
        && state.optionTargetFps == kOptionTargetFps.front();
    state.optionGameSpeed = 0;
    bool legalSpeedCycle = true;
    for (int count = 0; count < 7; ++count) {
        performImmediateAction(state, MenuAction::CycleGameSpeed);
        legalSpeedCycle = legalSpeedCycle
            && state.optionGameSpeed >= 0
            && state.optionGameSpeed <= 6;
    }
    legalSpeedCycle = legalSpeedCycle && state.optionGameSpeed == 0;
    const bool optionBoundaries = legalResolutionCycle
        && legalFpsCycle
        && legalSpeedCycle;

    MenuState noMovieState;
    noMovieState.audio.enabled = false;
    playSelectedMovie(noMovieState);
    const bool playbackFeedback = !noMovieState.pageStatus.empty()
        && moviePlaybackStatus(false, "decoder failed").find(L"decoder failed")
            != std::wstring::npos;
    state.config = originalConfig;
    state.page = MenuPage::Home;
    state.phase = MenuPhase::Idle;
    state.pageStatus.clear();

    if (!navigationLayout || !networkEntriesExposed
        || !movieLibraryContract || !creditsContract || !themeContract
        || !backHierarchy || !moviesTransition || !movieSelection
        || !creditsTransition || !creditsPaging || !optionsTransition
        || !optionsCancel || !optionsApply || !optionsPersisted
        || !optionBoundaries
        || !playbackFeedback) {
        std::cerr << "Main menu smoke failed: navigation/content/options contract "
                     "is invalid (layout=" << navigationLayout
                  << " networkExposed=" << networkEntriesExposed
                  << " movies=" << movieLibraryContract
                  << "/" << moviesTransition << "/" << movieSelection
                  << " credits=" << creditsContract
                  << "/" << creditsTransition << "/" << creditsPaging
                  << " themes=" << themeContract
                  << " back=" << backHierarchy
                  << " options=" << optionsTransition
                  << "/" << optionsCancel << "/" << optionsApply
                  << "/persisted=" << optionsPersisted
                  << "/" << optionBoundaries
                  << " feedback=" << playbackFeedback << ").\n";
        return 1;
    }
    state.hover = static_cast<int>(home.size()) - 1;
    state.focus = state.hover;
    state.mouseVisible = state.mouse.valid();
    state.mousePoint = POINT{744, 552};

    HDC screen = GetDC(nullptr);
    if (screen == nullptr || !ensureBackBuffer(state, screen)) {
        if (screen != nullptr) ReleaseDC(nullptr, screen);
        std::cerr << "Main menu smoke failed: offscreen surface creation failed.\n";
        return 1;
    }
    RenderSurface surface{state.backDc, state.composePixels.data()};
    state.backDirty = true;
    if (!renderBase(state, surface)) {
        ReleaseDC(nullptr, screen);
        std::cerr << "Main menu smoke failed: persistent back-buffer render failed.\n";
        return 1;
    }

    struct TextRampPixels {
        std::size_t bright = 0;
        std::size_t middle = 0;
        std::size_t dark = 0;
        std::size_t oldFlat = 0;
        std::size_t legacyYellow = 0;
    } rampPixels;
    const COLORREF oldFlatLightGold = westwoodHsvColor(*originalLightGold);
    const COLORREF legacyFlatYellow = RGB(255, 232, 0);
    const FontColorRamp expectedLightGoldRamp{
        RGB(255, 190, 97),
        RGB(245, 172, 68),
        RGB(97, 59, 4)
    };
    const FontColorRamp fallbackLightGoldRamp{};
    const auto countColor = [&](std::size_t at, COLORREF color) {
        return state.composePixels[at] == GetBValue(color)
            && state.composePixels[at + 1] == GetGValue(color)
            && state.composePixels[at + 2] == GetRValue(color);
    };
    for (const ButtonSpec& button : home) {
        const int top = buttonTopFor(button);
        for (int y = top; y < top + 25; ++y) {
            for (int x = kMenuButtonLeft; x < kMenuButtonLeft + 126; ++x) {
                const std::size_t at = static_cast<std::size_t>(
                    (y * kWidth + x) * 4
                );
                if (countColor(at, state.menuTextColors.bright)) {
                    ++rampPixels.bright;
                } else if (countColor(at, state.menuTextColors.middle)) {
                    ++rampPixels.middle;
                } else if (countColor(at, state.menuTextColors.dark)) {
                    ++rampPixels.dark;
                }
                if (countColor(at, oldFlatLightGold)) ++rampPixels.oldFlat;
                if (countColor(at, legacyFlatYellow)) ++rampPixels.legacyYellow;
            }
        }
    }
    const auto countLegacyYellowIn = [&](const RECT& rect) {
        for (int y = rect.top; y < rect.bottom; ++y) {
            for (int x = rect.left; x < rect.right; ++x) {
                const std::size_t at = static_cast<std::size_t>(
                    (y * kWidth + x) * 4
                );
                if (countColor(at, legacyFlatYellow)) {
                    ++rampPixels.legacyYellow;
                }
            }
        }
    };
    countLegacyYellowIn(RECT{kSidebarLeft + 25, 3, kSidebarLeft + 143, 35});
    countLegacyYellowIn(RECT{kSidebarLeft, kHeight - 20, kWidth, kHeight});
    const auto luminance = [](COLORREF color) {
        return 3 * static_cast<int>(GetRValue(color))
            + 6 * static_cast<int>(GetGValue(color))
            + static_cast<int>(GetBValue(color));
    };
    const auto warmGold = [](COLORREF color) {
        return GetRValue(color) > GetGValue(color)
            && GetGValue(color) > GetBValue(color);
    };
    const std::size_t facePixelCount = rampPixels.bright + rampPixels.middle;
    const auto faceChannelMean = [&](BYTE bright, BYTE middle) {
        return facePixelCount == 0 ? 0.0
            : (static_cast<double>(rampPixels.bright) * bright
                + static_cast<double>(rampPixels.middle) * middle)
                / static_cast<double>(facePixelCount);
    };
    const double faceRed = faceChannelMean(
        GetRValue(state.menuTextColors.bright),
        GetRValue(state.menuTextColors.middle)
    );
    const double faceGreen = faceChannelMean(
        GetGValue(state.menuTextColors.bright),
        GetGValue(state.menuTextColors.middle)
    );
    const double faceBlue = faceChannelMean(
        GetBValue(state.menuTextColors.bright),
        GetBValue(state.menuTextColors.middle)
    );
    const bool originalScreenshotFaceRange = faceRed >= 245.0 && faceRed <= 255.0
        && faceGreen >= 160.0 && faceGreen <= 195.0
        && faceBlue >= 60.0 && faceBlue <= 100.0;
    const bool finalTextRamp = rampPixels.bright >= 24
        && rampPixels.middle >= 24
        && rampPixels.dark >= 24
        && rampPixels.oldFlat == 0
        && rampPixels.legacyYellow == 0
        && state.menuTextColors.bright == expectedLightGoldRamp.bright
        && state.menuTextColors.middle == expectedLightGoldRamp.middle
        && state.menuTextColors.dark == expectedLightGoldRamp.dark
        && fallbackLightGoldRamp.bright == expectedLightGoldRamp.bright
        && fallbackLightGoldRamp.middle == expectedLightGoldRamp.middle
        && fallbackLightGoldRamp.dark == expectedLightGoldRamp.dark
        && originalScreenshotFaceRange
        && warmGold(state.menuTextColors.bright)
        && warmGold(state.menuTextColors.middle)
        && warmGold(state.menuTextColors.dark)
        && luminance(state.menuTextColors.bright)
            > luminance(state.menuTextColors.middle)
        && luminance(state.menuTextColors.middle)
            > luminance(state.menuTextColors.dark);
    if (!finalTextRamp) {
        ReleaseDC(nullptr, screen);
        std::cerr << "Main menu smoke failed: final button pixels do not "
                     "contain the ordered LightGold bright/middle/dark ramp "
                  << "(counts=" << rampPixels.bright << ","
                  << rampPixels.middle << "," << rampPixels.dark
                  << "; oldFlat=" << rampPixels.oldFlat
                  << "; legacyYellow=" << rampPixels.legacyYellow
                  << "; faceMean=" << faceRed << "," << faceGreen << ","
                  << faceBlue << ").\n";
        return 1;
    }
    std::string error;
    const bool written = writeBmp(bmpPath, state.composePixels.data(), error);
    if (!written) {
        ReleaseDC(nullptr, screen);
        std::cerr << "Main menu smoke failed: " << error << "\n";
        return 1;
    }

    bool featurePagesRendered = true;
    for (const MenuPage page : {
            MenuPage::Movies,
            MenuPage::Credits,
            MenuPage::Options
        }) {
        state.page = page;
        state.hover = -1;
        state.focus = 0;
        state.backDirty = true;
        featurePagesRendered = renderBase(state, surface)
            && featurePagesRendered;
    }
    if (!featurePagesRendered) {
        ReleaseDC(nullptr, screen);
        std::cerr << "Main menu smoke failed while rendering Movies, Credits, "
                     "or Options.\n";
        return 1;
    }

    fs::path singlePlayerPath(bmpPath);
    singlePlayerPath.replace_filename(
        singlePlayerPath.stem().string()
            + "_single_player"
            + singlePlayerPath.extension().string()
    );
    state.page = MenuPage::SinglePlayer;
    state.hover = 2;
    state.focus = 2;
    state.mousePoint = POINT{744, buttonsTop(state) + 2 * slotHeight(state) + 20};
    state.backDirty = true;
    const bool singleRendered = renderBase(state, surface)
        && writeBmp(singlePlayerPath.string(), state.composePixels.data(), error);
    ReleaseDC(nullptr, screen);
    if (!singleRendered) {
        std::cerr << "Main menu smoke failed while rendering Single Player: "
                  << error << "\n";
        return 1;
    }
    std::cout << "Main menu smoke: 800x600"
              << " sideTopFrames=" << state.sideTop.size()
              << " warningFrames=" << state.warning.size()
              << " slotFrames=" << state.slotAnimation.size()
              << " redButtonFrames=" << state.redButton.size()
              << " font=" << (state.gameFont.valid() ? "game.fnt" : "system")
              << " lightGoldRamp="
              << static_cast<int>(GetRValue(state.menuTextColors.bright)) << ","
              << static_cast<int>(GetGValue(state.menuTextColors.bright)) << ","
              << static_cast<int>(GetBValue(state.menuTextColors.bright)) << "/"
              << static_cast<int>(GetRValue(state.menuTextColors.middle)) << ","
              << static_cast<int>(GetGValue(state.menuTextColors.middle)) << ","
              << static_cast<int>(GetBValue(state.menuTextColors.middle)) << "/"
              << static_cast<int>(GetRValue(state.menuTextColors.dark)) << ","
              << static_cast<int>(GetGValue(state.menuTextColors.dark)) << ","
              << static_cast<int>(GetBValue(state.menuTextColors.dark))
              << " rampPixels=" << rampPixels.bright << ","
              << rampPixels.middle << "," << rampPixels.dark
              << " legacyYellow=" << rampPixels.legacyYellow
              << " video=" << (animationTested ? "live-bink" : "static-fallback")
              << " animationSequence=" << firstAnimationSequence
              << "->" << secondAnimationSequence
              << " sampledRed=" << sampledRed
              << " sampledBlue=" << sampledBlue
              << " bmp=" << bmpPath
              << " singlePlayerBmp=" << singlePlayerPath.string() << "\n";
    // Drive the production skirmish transition (smoke normally bypasses
    // asynchronous navigation). While resources load, the source menu stays
    // alive; completion preserves the requested result and is not retriggered.
    state.smoke = false;
    state.audio.enabled = false;
    state.closing = false;
    state.phase = MenuPhase::Idle;
    state.pending = MenuAction::None;
    const ULONGLONG preparationStarted = GetTickCount64();
    beginAction(state, MenuAction::Skirmish);
    state.phaseStarted = GetTickCount64() - 1000;
    int pendingTicks = 0;
    while (!state.closing && GetTickCount64() - preparationStarted < 30000) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                std::cerr << "Main menu smoke failed: skirmish transition "
                             "received WM_QUIT.\n";
                return 1;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        updateDynamicState(state);
        if (!state.closing) {
            if (state.pending != MenuAction::Skirmish
                || state.pageStatus.empty()) {
                std::cerr << "Main menu smoke failed: skirmish pending contract "
                          << "changed (pending=" << static_cast<int>(state.pending)
                          << " statusEmpty=" << state.pageStatus.empty()
                          << " transitionMs="
                          << GetTickCount64() - preparationStarted << ").\n";
                return 1;
            }
            ++pendingTicks;
            Sleep(5);
        }
    }
    if (!state.closing || state.result != RuntimeMainMenuResult::Skirmish
        || state.pending != MenuAction::None) {
        std::cerr << "Main menu smoke failed: skirmish transition did not "
                     "complete within the original 30000ms wait "
                  << "(closing=" << state.closing
                  << " result=" << static_cast<int>(state.result)
                  << " pending=" << static_cast<int>(state.pending)
                  << " pendingTicks=" << pendingTicks
                  << " transitionMs=" << GetTickCount64() - preparationStarted
                  << " prewarmMs=" << GetTickCount64() - skirmishPrewarmStarted
                  << ").\n";
        return 1;
    }
    std::cout << "Main menu skirmish transition: pendingTicks=" << pendingTicks
              << " result=skirmish completed=yes transitionMs="
              << GetTickCount64() - preparationStarted
              << " prewarmMs=" << GetTickCount64() - skirmishPrewarmStarted << "\n";
    return 0;
}
