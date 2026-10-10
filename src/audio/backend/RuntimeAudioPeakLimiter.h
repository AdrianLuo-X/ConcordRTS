#pragma once
#include <xapobase.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

class RuntimeAudioPeakLimiter final : public CXAPOBase {
public:
    RuntimeAudioPeakLimiter() : CXAPOBase(&registration_) {}
    HRESULT STDMETHODCALLTYPE IsInputFormatSupported(const WAVEFORMATEX* output,
        const WAVEFORMATEX* input, WAVEFORMATEX** supported) override {
        if (supported) *supported = nullptr;
        if (!validFormat(input) || (output && (!validFormat(output)
            || output->nSamplesPerSec != input->nSamplesPerSec))) return XAPO_E_FORMAT_UNSUPPORTED;
        return CXAPOBase::IsInputFormatSupported(output, input, supported);
    }
    HRESULT STDMETHODCALLTYPE IsOutputFormatSupported(const WAVEFORMATEX* input,
        const WAVEFORMATEX* output, WAVEFORMATEX** supported) override {
        if (supported) *supported = nullptr;
        if (!validFormat(output) || (input && (!validFormat(input)
            || output->nSamplesPerSec != input->nSamplesPerSec))) return XAPO_E_FORMAT_UNSUPPORTED;
        return CXAPOBase::IsOutputFormatSupported(input, output, supported);
    }
    HRESULT STDMETHODCALLTYPE LockForProcess(UINT32 inputCount,
        const XAPO_LOCKFORPROCESS_PARAMETERS* input, UINT32 outputCount,
        const XAPO_LOCKFORPROCESS_PARAMETERS* output) override {
        if (inputCount != 1 || outputCount != 1 || !input || !output
            || !validFormat(input[0].pFormat) || !validFormat(output[0].pFormat)
            || input[0].pFormat->nSamplesPerSec != output[0].pFormat->nSamplesPerSec)
            return XAPO_E_FORMAT_UNSUPPORTED;
        const HRESULT result = CXAPOBase::LockForProcess(inputCount, input, outputCount, output);
        if (FAILED(result)) return result;
        releaseCoefficient_ = std::exp(-1.0 / (0.006 * input[0].pFormat->nSamplesPerSec));
        Reset();
        return S_OK;
    }
    void STDMETHODCALLTYPE Reset() override { heldGain_ = 1.0; }
    void STDMETHODCALLTYPE Process(UINT32 inputCount,
        const XAPO_PROCESS_BUFFER_PARAMETERS* input, UINT32 outputCount,
        XAPO_PROCESS_BUFFER_PARAMETERS* output, BOOL enabled) override {
        if (inputCount != 1 || outputCount != 1 || !input || !output) return;
        const UINT32 frames = input[0].ValidFrameCount;
        output[0].ValidFrameCount = frames;
        output[0].BufferFlags = input[0].BufferFlags;
        float* destination = static_cast<float*>(output[0].pBuffer);
        if (!destination) return;
        if (input[0].BufferFlags == XAPO_BUFFER_SILENT) {
            for (UINT32 frame = 0; frame < frames; ++frame) {
                release(); destination[frame * 2] = destination[frame * 2 + 1] = 0.0f;
            }
            return;
        }
        const float* source = static_cast<const float*>(input[0].pBuffer);
        if (!source) return;
        if (!enabled) {
            if (source != destination) std::memcpy(destination, source, std::size_t(frames) * 2 * sizeof(float));
            Reset(); return;
        }
        for (UINT32 frame = 0; frame < frames; ++frame) {
            const float left = source[frame * 2], right = source[frame * 2 + 1];
            const double peak = std::max(std::abs(double(left)), std::abs(double(right)));
            if (peak > 1.0) heldGain_ = std::min(heldGain_, 0.999 / peak);
            else release();
            if (heldGain_ == 1.0) {
                destination[frame * 2] = left; destination[frame * 2 + 1] = right;
            } else {
                destination[frame * 2] = static_cast<float>(double(left) * heldGain_);
                destination[frame * 2 + 1] = static_cast<float>(double(right) * heldGain_);
            }
        }
    }
private:
    static bool validFormat(const WAVEFORMATEX* format) noexcept {
        if (!format || format->nChannels != 2 || format->wBitsPerSample != 32
            || format->nBlockAlign != 8 || format->nSamplesPerSec < XAPO_MIN_FRAMERATE
            || format->nSamplesPerSec > XAPO_MAX_FRAMERATE
            || format->nAvgBytesPerSec != format->nSamplesPerSec * 8) return false;
        if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
        if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE || format->cbSize < 22) return false;
        const auto* extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return extended->Samples.wValidBitsPerSample == 32
            && IsEqualGUID(extended->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    void release() noexcept {
        if (heldGain_ == 1.0) return;
        heldGain_ = 1.0 - (1.0 - heldGain_) * releaseCoefficient_;
        if (1.0 - heldGain_ < 1e-12) heldGain_ = 1.0;
    }
    inline static const XAPO_REGISTRATION_PROPERTIES registration_ = {
        {0x49a3f123,0x60af,0x4413,{0x8c,0xa0,0x81,0x63,0x54,0xf3,0x25,0x21}},
        L"Concord audio peak limiter", L"ConcordRTS", 1, 0,
        XAPOBASE_DEFAULT_FLAG | XAPO_FLAG_INPLACE_REQUIRED, 1, 1, 1, 1
    };
    double heldGain_ = 1.0, releaseCoefficient_ = 0.0;
};

inline HRESULT createRuntimeAudioPeakLimiter(IUnknown** effect) noexcept {
    if (!effect) return E_POINTER;
    *effect = nullptr;
    try {
        auto* limiter = new (std::nothrow) RuntimeAudioPeakLimiter;
        if (!limiter) return E_OUTOFMEMORY;
        *effect = static_cast<IUnknown*>(limiter);
        return S_OK;
    } catch (...) { return E_OUTOFMEMORY; }
}
