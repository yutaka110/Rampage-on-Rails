#include "AppAudio.h"

bool AppAudio::Initialize() {
    return audioSystem_.Initialize();
}

void AppAudio::Finalize() {
    audioSystem_.Shutdown();
}

void AppAudio::Update() {
    audioSystem_.Update();
}

audio::SoundHandle AppAudio::LoadSound(const std::string& path) {
    return audioSystem_.LoadSound(path);
}

audio::SoundHandle AppAudio::LoadWave(const std::string& path) {
    return audioSystem_.LoadWave(path);
}

audio::SoundHandle AppAudio::CreateTone(
    const std::string& id,
    float frequencyHz,
    float durationSeconds,
    float amplitude) {
    return audioSystem_.CreateTone(
        id, frequencyHz, durationSeconds, amplitude);
}

bool AppAudio::Play(audio::SoundHandle handle, float volume, bool loop) {
    return audioSystem_.Play(handle, volume, loop);
}

bool AppAudio::PlaySpatial(
    audio::SoundHandle handle,
    float volume,
    float pan,
    float pitch,
    bool loop) {
    return audioSystem_.PlaySpatial(handle, volume, pan, pitch, loop);
}

void AppAudio::Unload(audio::SoundHandle handle) {
    audioSystem_.UnloadSound(handle);
}

void AppAudio::Stop(audio::SoundHandle handle) { audioSystem_.Stop(handle); }
void AppAudio::SetPaused(audio::SoundHandle handle,bool paused) { audioSystem_.SetPaused(handle,paused); }
void AppAudio::SetPlayback(audio::SoundHandle handle, float volume, float pitch) {
    audioSystem_.SetPlayback(handle,volume,pitch);
}
