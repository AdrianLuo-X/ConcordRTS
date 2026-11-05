
#include "audio/backend/RuntimeVoiceAudio.h"
#include "audio/backend/RuntimeLegacyWaveDevice.h"
#include "audio/backend/RuntimeAsyncSoundDevice.h"
#include <stdexcept>
// Backend capability is explicitly unsupported. The original public PImpl
// owner and lazy allocation/lifetime remain real; no voice is ever reported
// opened/playing/ready. The native backend retains its exact HRESULT bits.
struct RuntimeVoiceAudio::Impl {};
RuntimeVoiceAudio::RuntimeVoiceAudio(bool):impl_(std::make_unique<Impl>()) {}
RuntimeVoiceAudio::~RuntimeVoiceAudio()=default;
bool RuntimeVoiceAudio::ready() const {return false;}
std::int32_t RuntimeVoiceAudio::lastError() const {return -1;}
bool RuntimeVoiceAudio::open(std::uint64_t,const std::string&) {return false;}
bool RuntimeVoiceAudio::setMix(std::uint64_t,int,int) {return false;}
bool RuntimeVoiceAudio::playFromStart(std::uint64_t,bool,bool) {return false;}
bool RuntimeVoiceAudio::queue(std::uint64_t,const std::string&) {return false;}
unsigned RuntimeVoiceAudio::queuedBuffers(std::uint64_t) const {return 0;}
std::optional<bool> RuntimeVoiceAudio::isPlaying(std::uint64_t) const {return std::nullopt;}
bool RuntimeVoiceAudio::stop(std::uint64_t) {return false;}
void RuntimeVoiceAudio::close(std::uint64_t) {}
RuntimeVoiceAudioProbe RuntimeVoiceAudio::probe(std::uint64_t) const {return {};}
std::size_t RuntimeVoiceAudio::voiceCount() const {return 0;}
namespace concord::runtime::session_detail {
concord::audio::RuntimeLegacyWaveDevice& runtimeEvaWaveDevice(){static auto p=concord::audio::makeUnsupportedLegacyWaveDevice();return *p;}
concord::audio::RuntimeLegacyWaveDevice& runtimeShellMusicWaveDevice(){static auto p=concord::audio::makeUnsupportedLegacyWaveDevice();return *p;}
concord::audio::RuntimeAsyncSoundDevice& runtimeAsyncSoundDevice(){static auto p=concord::audio::makeUnsupportedAsyncSoundDevice();return *p;}
std::wstring widenUtf8(const std::string& value){
    if(value.empty())return {};
    throw std::logic_error("Native presentation text conversion is unsupported by this headless provider");
}
std::string runtimeEncodePresentationUtf8(const std::wstring&,const std::string&){
    throw std::logic_error("Native presentation label encoding is unsupported by this headless provider");
}
}
