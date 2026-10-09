#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// Sequential in-memory decoder for Westwood VQA campaign presentation clips.
// Frames are exposed as tightly packed BGRA8 pixels in top-to-bottom order.
class CampaignPresentationDecoder final {
public:
    CampaignPresentationDecoder();
    ~CampaignPresentationDecoder();

    CampaignPresentationDecoder(CampaignPresentationDecoder&&) noexcept;
    CampaignPresentationDecoder& operator=(
        CampaignPresentationDecoder&&
    ) noexcept;

    CampaignPresentationDecoder(const CampaignPresentationDecoder&) = delete;
    CampaignPresentationDecoder& operator=(
        const CampaignPresentationDecoder&
    ) = delete;

    bool loadFromMemory(std::span<const std::uint8_t> bytes);
    bool loadFromMemory(const std::vector<std::uint8_t>& bytes);
    bool loadFromMemory(const std::uint8_t* bytes, std::size_t byteCount);
    void clear();

    [[nodiscard]] bool isLoaded() const noexcept;
    [[nodiscard]] bool isHighColor() const noexcept;
    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t frameRate() const noexcept;
    [[nodiscard]] std::uint32_t frameCount() const noexcept;
    [[nodiscard]] std::uint32_t currentFrameIndex() const noexcept;
    [[nodiscard]] bool hasNextFrame() const noexcept;

    [[nodiscard]] const std::vector<std::uint8_t>& currentFrameBgra()
        const noexcept;

    // Returns false at normal end-of-stream (with an empty errorMessage), or
    // when the next frame is malformed (with a non-empty errorMessage).
    bool advance();

    // Restores all palette, codebook, delta-frame, and playback state, then
    // decodes frame zero again.
    bool reset();

    [[nodiscard]] const std::string& errorMessage() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
