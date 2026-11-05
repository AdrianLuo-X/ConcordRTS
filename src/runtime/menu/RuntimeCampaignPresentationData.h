#pragma once
#include "data/campaign/CampaignProgress.h"
#include "data/campaign/CampaignPresentationDecoder.h"
#include "render/core/RuntimeSpriteFrameImage.h"
#include <cstdint>
#include <string>
#include <vector>

// Existing owned CPU presentation data and in-memory VQA codec state.
// Resource mounts, renderer context/cache, audio and native windows stay in the view.
namespace concord::campaign_menu {
struct CampaignImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> bgra;
    std::string source;

    bool valid() const {
        return width > 0 && height > 0
            && bgra.size()
                == static_cast<std::size_t>(width * height * 4);
    }
};

enum class CampaignPage {
    Campaigns,
    Missions,
    Difficulty,
    Briefing,
    Saves
};

enum class LoadSaveKind {
    Skirmish,
    Campaign
};

struct LoadSaveEntry {
    LoadSaveKind kind = LoadSaveKind::Campaign;
    CampaignSaveInfo campaign;
    SkirmishSaveInfo skirmish;
};

enum class CampaignMapPresentationPhase {
    Unavailable,
    VqaIntro,
    OverlayFade,
    Interactive,
    FallbackList
};

struct CampaignMapAnimationVisual {
    std::string assetId;
    int x = 0;
    int y = 0;
    int frameRate = 1;
    std::vector<CampaignImage> frames;
};

struct CampaignMapPresentationState {
    std::string key;
    std::string stageSection;
    CampaignMapPresentationPhase phase =
        CampaignMapPresentationPhase::Unavailable;
    std::uint64_t phaseStartedAt = 0;
    std::uint64_t lastFrameAt = 0;
    CampaignPresentationDecoder vqa;
    CampaignImage vqaFrame;
    RuntimeShpFrameImage clickMap;
    std::string clickMapSource;
    std::vector<CampaignImage> overlays;
    std::vector<CampaignMapAnimationVisual> animations;
    std::vector<RuntimePaletteColor> palette;
    std::string paletteSource;
    std::string voiceSource;
    bool paletteFallback = false;
    // Some retail installations omit the CD-era MapVQ/ClickMap media while
    // still shipping the authoritative MapSel graph.  In that case the menu
    // reconstructs a hit-test surface from the authored Targets entries and
    // remains in the classic map flow instead of dropping to the list UI.
    bool authoredReconstruction = false;
    bool voiceStarted = false;
    bool overlaySoundPlayed = false;
    bool targetSoundPlayed = false;
    int hoverColor = 0;
    int pressedColor = 0;
    int keyboardColor = 0;
    std::string diagnostic;

    bool hasClickMap() const {
        return clickMap.width > 0 && clickMap.height > 0
            && clickMap.pixels.size() >= static_cast<std::size_t>(
                clickMap.width * clickMap.height
            );
    }

    bool usesClassicSurface() const {
        return phase == CampaignMapPresentationPhase::VqaIntro
            || phase == CampaignMapPresentationPhase::OverlayFade
            || phase == CampaignMapPresentationPhase::Interactive;
    }
};

} // namespace concord::campaign_menu
