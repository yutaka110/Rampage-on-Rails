#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <wrl/client.h>
#include <xaudio2.h>

namespace audio {

struct SoundHandle {
    static constexpr uint32_t kInvalidIndex = UINT32_MAX;

    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;

    bool IsValid() const {
        return index != kInvalidIndex;
    }
};

struct SoundResource {
    std::string path;
    std::vector<uint8_t> formatBytes;
    std::vector<uint8_t> pcmData;

    const WAVEFORMATEX* Format() const {
        if (formatBytes.size() < sizeof(WAVEFORMATEX)) {
            return nullptr;
        }
        return reinterpret_cast<const WAVEFORMATEX*>(formatBytes.data());
    }

    bool IsValid() const {
        return Format() != nullptr && !pcmData.empty();
    }
};

class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();

    bool Initialize();
    void Shutdown();
    void Update();

    SoundHandle LoadSound(const std::string& path);
    SoundHandle LoadWave(const std::string& path);
    SoundHandle CreateTone(
        const std::string& id,
        float frequencyHz,
        float durationSeconds,
        float amplitude = 0.5f);
    bool UnloadSound(SoundHandle handle);

    bool Play(SoundHandle handle, float volume = 1.0f, bool loop = false);
    bool PlaySpatial(
        SoundHandle handle,
        float volume,
        float pan,
        float pitch = 1.0f,
        bool loop = false);
    void SetPlayback(SoundHandle handle, float volume, float pitch = 1.0f);
    void SetPaused(SoundHandle handle, bool paused);
    void Stop(SoundHandle handle);
    void StopAll();

    const SoundResource* GetSound(SoundHandle handle) const;

private:
    struct SoundSlot {
        SoundResource resource{};
        uint32_t generation = 1;
        bool occupied = false;
    };

    struct SourceVoiceCallback final : public IXAudio2VoiceCallback {
        std::atomic_bool finished = false;

        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32 bytesRequired) override;
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override;
        void STDMETHODCALLTYPE OnStreamEnd() override;
        void STDMETHODCALLTYPE OnBufferStart(void* bufferContext) override;
        void STDMETHODCALLTYPE OnBufferEnd(void* bufferContext) override;
        void STDMETHODCALLTYPE OnLoopEnd(void* bufferContext) override;
        void STDMETHODCALLTYPE OnVoiceError(void* bufferContext, HRESULT error) override;
    };

    struct ActiveVoice {
        IXAudio2SourceVoice* voice = nullptr;
        SoundHandle sound{};
        std::unique_ptr<SourceVoiceCallback> callback;
    };

    bool IsHandleAlive(SoundHandle handle) const;
    uint32_t AllocateSlot();
    void DestroyVoice(ActiveVoice& activeVoice);

    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice* masterVoice_ = nullptr;
    bool mediaFoundationStarted_ = false;
    std::vector<SoundSlot> sounds_;
    std::vector<ActiveVoice> activeVoices_;
};

} // namespace audio
