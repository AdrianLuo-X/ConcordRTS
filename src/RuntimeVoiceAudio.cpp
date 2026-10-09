#include "RuntimeVoiceAudio.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>
#include "new_features/RuntimeAudioPeakLimiter.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <deque>
#include <unordered_map>

namespace {
std::uint16_t le16(const unsigned char* p) { return p[0] | (unsigned(p[1]) << 8); }
std::uint32_t le32(const unsigned char* p) {
    return le16(p) | (std::uint32_t(le16(p + 2)) << 16);
}
bool readPcm(const std::string& path, WAVEFORMATEX& format, std::vector<BYTE>& pcm) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto length = file.tellg();
    if (length < 44 || length > 64 * 1024 * 1024) return false;
    std::vector<BYTE> bytes(static_cast<std::size_t>(length));
    file.seekg(0); if (!file.read(reinterpret_cast<char*>(bytes.data()), bytes.size())) return false;
    if (std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) return false;
    const std::uint64_t end = std::uint64_t(le32(bytes.data() + 4)) + 8;
    if (end > bytes.size() || end < 12) return false;
    bool haveFormat = false, haveData = false;
    for (std::size_t offset = 12; offset + 8 <= end;) {
        const auto count = le32(bytes.data() + offset + 4);
        const auto first = offset + 8;
        if (count > end - first) return false;
        const auto* data = bytes.data() + first;
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && !haveFormat) {
            if (count < 16) return false;
            format.wFormatTag = le16(data); format.nChannels = le16(data + 2);
            format.nSamplesPerSec = le32(data + 4); format.nAvgBytesPerSec = le32(data + 8);
            format.nBlockAlign = le16(data + 12); format.wBitsPerSample = le16(data + 14);
            format.cbSize = 0; haveFormat = true;
        } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0 && !haveData) {
            pcm.assign(data, data + count); haveData = true;
        }
        offset = first + count + (count & 1U);
    }
    const bool validBits = format.wFormatTag == WAVE_FORMAT_PCM
        ? (format.wBitsPerSample == 8 || format.wBitsPerSample == 16 || format.wBitsPerSample == 24 || format.wBitsPerSample == 32)
        : format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format.wBitsPerSample == 32;
    return haveFormat && haveData && !pcm.empty() && validBits
        && format.nChannels >= 1 && format.nChannels <= 2
        && format.nSamplesPerSec >= XAUDIO2_MIN_SAMPLE_RATE && format.nSamplesPerSec <= XAUDIO2_MAX_SAMPLE_RATE
        && format.nBlockAlign == format.nChannels * (format.wBitsPerSample / 8)
        && format.nAvgBytesPerSec == format.nSamplesPerSec * format.nBlockAlign
        && pcm.size() % format.nBlockAlign == 0;
}
}

struct RuntimeVoiceAudio::Impl {
    struct Voice {
        IXAudio2SourceVoice* device = nullptr;
        WAVEFORMATEX format{};
        std::vector<BYTE> pcm;
        std::deque<std::unique_ptr<std::vector<BYTE>>> queuedPcm;
        bool started = false;
        ~Voice() { if (device) device->DestroyVoice(); }
    };
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    std::unordered_map<std::uint64_t, std::unique_ptr<Voice>> voices;
    HRESULT error = S_OK;
    bool uninitializeCom = false;
    ~Impl() {
        voices.clear();
        if (master) master->DestroyVoice();
        if (engine) engine->Release();
        if (uninitializeCom) CoUninitialize();
    }
    bool checked(HRESULT value) { error = value; return SUCCEEDED(value); }
};

RuntimeVoiceAudio::RuntimeVoiceAudio(bool mute) : impl_(std::make_unique<Impl>()) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    impl_->uninitializeCom = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) { impl_->error = com; return; }
    if (!impl_->checked(XAudio2Create(&impl_->engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) return;
    // Protect the final stereo sum from clipping without changing a source
    // sample, its playback rate or its pan, and without adding lookahead delay.
    IUnknown* limiter = nullptr;
    if (!impl_->checked(createRuntimeAudioPeakLimiter(&limiter))) return;
    XAUDIO2_EFFECT_DESCRIPTOR descriptor{limiter, TRUE, 2};
    const XAUDIO2_EFFECT_CHAIN effects{1, &descriptor};
    const HRESULT mastering = impl_->engine->CreateMasteringVoice(
        &impl_->master, 2, 48000, 0, nullptr, &effects);
    // The mastering voice owns its effect reference after successful creation.
    limiter->Release();
    if (!impl_->checked(mastering)) return;
    // Verification mutes only this engine graph, before it can receive a buffer.
    if (mute && !impl_->checked(impl_->master->SetVolume(0))) {
        impl_->master->DestroyVoice(); impl_->master = nullptr;
    }
}
RuntimeVoiceAudio::~RuntimeVoiceAudio() = default;
bool RuntimeVoiceAudio::ready() const { return impl_->master != nullptr; }
long RuntimeVoiceAudio::lastError() const { return impl_->error; }
bool RuntimeVoiceAudio::open(std::uint64_t id, const std::string& path) {
    close(id);
    if (!ready() || impl_->voices.size() >= 4096) return false;
    auto voice = std::make_unique<Impl::Voice>();
    if (!readPcm(path, voice->format, voice->pcm)) { impl_->error = E_INVALIDARG; return false; }
    if (!impl_->checked(impl_->engine->CreateSourceVoice(&voice->device, &voice->format))) return false;
    impl_->voices.emplace(id, std::move(voice)); return true;
}
bool RuntimeVoiceAudio::setMix(std::uint64_t id, int gain, int pan) {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return false;
    auto& voice = *found->second;
    const auto mix = runtimeRetailAudioMix(gain, pan);
    std::array<float, 4> matrix{};
    matrix[0] = mix.left;
    matrix[voice.format.nChannels == 1 ? 1 : 3] = mix.right;
    return impl_->checked(voice.device->SetVolume(mix.volume))
        && impl_->checked(voice.device->SetOutputMatrix(impl_->master, voice.format.nChannels, 2, matrix.data()));
}
bool RuntimeVoiceAudio::stop(std::uint64_t id) {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return true;
    auto& voice = *found->second;
    if (!impl_->checked(voice.device->Stop(0)) || !impl_->checked(voice.device->FlushSourceBuffers())) return false;
    voice.started = false; return true;
}
bool RuntimeVoiceAudio::playFromStart(std::uint64_t id, bool loop, bool continuousSequence) {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end() || !stop(id)) return false;
    auto& voice = *found->second;
    XAUDIO2_BUFFER buffer{}; buffer.Flags = continuousSequence ? 0 : XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(voice.pcm.size()); buffer.pAudioData = voice.pcm.data();
    buffer.pContext = &voice.pcm;
    if (loop) buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (!impl_->checked(voice.device->SubmitSourceBuffer(&buffer)) || !impl_->checked(voice.device->Start(0))) return false;
    voice.started = true; return true;
}
unsigned RuntimeVoiceAudio::queuedBuffers(std::uint64_t id) const {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return 0;
    XAUDIO2_VOICE_STATE state{}; found->second->device->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued;
}
bool RuntimeVoiceAudio::queue(std::uint64_t id, const std::string& path) {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return false;
    auto& voice = *found->second;
    XAUDIO2_VOICE_STATE state{}; voice.device->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (!voice.started || state.BuffersQueued == 0 || state.BuffersQueued >= 4) return false;
    // GetState identifies the buffer currently owned by the mixer. Every
    // earlier queued buffer has finished; the current and future ones stay.
    if (state.pCurrentBufferContext && state.pCurrentBufferContext != &voice.pcm)
        while (!voice.queuedPcm.empty() && voice.queuedPcm.front().get() != state.pCurrentBufferContext)
            voice.queuedPcm.pop_front();
    auto bytes = std::make_unique<std::vector<BYTE>>(); WAVEFORMATEX format{};
    if (!readPcm(path, format, *bytes) || format.wFormatTag != voice.format.wFormatTag
        || format.nChannels != voice.format.nChannels || format.nSamplesPerSec != voice.format.nSamplesPerSec
        || format.wBitsPerSample != voice.format.wBitsPerSample || format.nBlockAlign != voice.format.nBlockAlign)
        return false;
    XAUDIO2_BUFFER buffer{}; buffer.AudioBytes = static_cast<UINT32>(bytes->size());
    buffer.pAudioData = bytes->data(); buffer.pContext = bytes.get();
    if (!impl_->checked(voice.device->SubmitSourceBuffer(&buffer))) return false;
    voice.queuedPcm.push_back(std::move(bytes));
    return true;
}
std::optional<bool> RuntimeVoiceAudio::isPlaying(std::uint64_t id) const {
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return false;
    XAUDIO2_VOICE_STATE state{}; found->second->device->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return found->second->started && state.BuffersQueued > 0;
}
void RuntimeVoiceAudio::close(std::uint64_t id) { impl_->voices.erase(id); }
std::size_t RuntimeVoiceAudio::voiceCount() const { return impl_->voices.size(); }
RuntimeVoiceAudioProbe RuntimeVoiceAudio::probe(std::uint64_t id) const {
    RuntimeVoiceAudioProbe result;
    const auto found = impl_->voices.find(id); if (found == impl_->voices.end()) return result;
    const auto& voice = *found->second; result.available = true; result.channels = voice.format.nChannels;
    result.sampleRate = voice.format.nSamplesPerSec; result.bitsPerSample = voice.format.wBitsPerSample;
    result.frameBytes = voice.format.nBlockAlign;
    BOOL limiterEnabled = FALSE; impl_->master->GetEffectState(0, &limiterEnabled);
    result.masteringLimiterEnabled = limiterEnabled != FALSE;
    XAUDIO2_VOICE_STATE state{}; voice.device->GetState(&state);
    result.buffersQueued = state.BuffersQueued; result.samplesPlayed = state.SamplesPlayed;
    voice.device->GetVolume(&result.volume); impl_->master->GetVolume(&result.masterVolume);
    result.matrix.resize(result.channels * 2);
    voice.device->GetOutputMatrix(impl_->master, result.channels, 2, result.matrix.data());
    return result;
}
