#pragma once
#include "audio/AudioSystem.h"

#include <string>

class AppAudio {
public:
    bool Initialize();
    void Finalize();
    void Update();

    audio::SoundHandle LoadSound(const std::string& path);
    audio::SoundHandle LoadWave(const std::string& path);
    audio::SoundHandle CreateTone(
        const std::string& id,
        float frequencyHz,
        float durationSeconds,
        float amplitude = 0.5f);
    bool Play(audio::SoundHandle handle, float volume = 1.0f, bool loop = false);
    bool PlaySpatial(
        audio::SoundHandle handle,
        float volume,
        float pan,
        float pitch = 1.0f,
        bool loop = false);
    void Stop(audio::SoundHandle handle);
    void SetPlayback(audio::SoundHandle handle, float volume, float pitch = 1.0f);
    void SetPaused(audio::SoundHandle handle, bool paused);
    void Unload(audio::SoundHandle handle);

private:
    audio::AudioSystem audioSystem_;
};
