#include "audio/AudioSystem.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace audio {
namespace {

struct RiffChunkHeader {
    char id[4]{};
    uint32_t size = 0;
};

bool FourCCEquals(const char id[4], const char* expected) {
    return std::memcmp(id, expected, 4) == 0;
}

bool ReadChunkHeader(std::ifstream& file, RiffChunkHeader& header) {
    return static_cast<bool>(file.read(reinterpret_cast<char*>(&header), sizeof(header)));
}

bool LoadWaveResource(const std::string& path, SoundResource& resource) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    RiffChunkHeader riff{};
    if (!ReadChunkHeader(file, riff) || !FourCCEquals(riff.id, "RIFF")) {
        return false;
    }

    char waveType[4]{};
    if (!file.read(waveType, sizeof(waveType)) || !FourCCEquals(waveType, "WAVE")) {
        return false;
    }

    SoundResource loaded{};
    loaded.path = path;

    while (file.good()) {
        RiffChunkHeader chunk{};
        if (!ReadChunkHeader(file, chunk)) {
            break;
        }

        const std::streamoff chunkStart = file.tellg();
        const std::streamoff nextChunk =
            chunkStart + static_cast<std::streamoff>(chunk.size + (chunk.size & 1u));

        if (FourCCEquals(chunk.id, "fmt ")) {
            if (chunk.size < sizeof(WAVEFORMATEX)) {
                return false;
            }

            loaded.formatBytes.assign(std::max<uint32_t>(chunk.size, sizeof(WAVEFORMATEX)), 0);
            if (!file.read(reinterpret_cast<char*>(loaded.formatBytes.data()), chunk.size)) {
                return false;
            }
        } else if (FourCCEquals(chunk.id, "data")) {
            loaded.pcmData.resize(chunk.size);
            if (chunk.size > 0 &&
                !file.read(reinterpret_cast<char*>(loaded.pcmData.data()), chunk.size)) {
                return false;
            }
        }

        file.seekg(nextChunk, std::ios::beg);

        if (loaded.IsValid()) {
            resource = std::move(loaded);
            return true;
        }
    }

    return false;
}

std::wstring ToWidePath(const std::string& path) {
    if (path.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        path.data(),
        static_cast<int>(path.size()),
        nullptr,
        0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (size <= 0) {
        codePage = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(
            codePage,
            flags,
            path.data(),
            static_cast<int>(path.size()),
            nullptr,
            0);
    }
    if (size <= 0) {
        return {};
    }

    std::wstring widePath(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        path.data(),
        static_cast<int>(path.size()),
        widePath.data(),
        size);
    return widePath;
}

bool CopyWaveFormat(IMFMediaType* mediaType, SoundResource& resource) {
    if (mediaType == nullptr) {
        return false;
    }

    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatSize = 0;
    HRESULT hr = MFCreateWaveFormatExFromMFMediaType(
        mediaType,
        &waveFormat,
        &waveFormatSize);
    if (FAILED(hr) || waveFormat == nullptr || waveFormatSize < sizeof(WAVEFORMATEX)) {
        if (waveFormat != nullptr) {
            CoTaskMemFree(waveFormat);
        }
        return false;
    }

    resource.formatBytes.assign(
        reinterpret_cast<const uint8_t*>(waveFormat),
        reinterpret_cast<const uint8_t*>(waveFormat) + waveFormatSize);
    CoTaskMemFree(waveFormat);
    return true;
}

bool LoadMediaFoundationResource(const std::string& path, SoundResource& resource) {
    const std::wstring widePath = ToWidePath(path);
    if (widePath.empty()) {
        return false;
    }

    Microsoft::WRL::ComPtr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader);
    if (FAILED(hr) || !reader) {
        return false;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    hr = reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) {
        return false;
    }
    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = reader->SetCurrentMediaType(
        MF_SOURCE_READER_FIRST_AUDIO_STREAM,
        nullptr,
        outputType.Get());
    if (FAILED(hr)) {
        return false;
    }

    SoundResource loaded{};
    loaded.path = path;

    Microsoft::WRL::ComPtr<IMFMediaType> currentType;
    hr = reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &currentType);
    if (FAILED(hr) || !CopyWaveFormat(currentType.Get(), loaded)) {
        return false;
    }

    while (true) {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        Microsoft::WRL::ComPtr<IMFSample> sample;
        hr = reader->ReadSample(
            MF_SOURCE_READER_FIRST_AUDIO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timestamp,
            &sample);
        if (FAILED(hr)) {
            return false;
        }

        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            currentType.Reset();
            if (SUCCEEDED(reader->GetCurrentMediaType(
                    MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                    &currentType))) {
                CopyWaveFormat(currentType.Get(), loaded);
            }
        }

        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }

        if (!sample) {
            continue;
        }

        Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
        hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr) || !buffer) {
            return false;
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        hr = buffer->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            return false;
        }

        if (data != nullptr && currentLength > 0) {
            const size_t oldSize = loaded.pcmData.size();
            loaded.pcmData.resize(oldSize + currentLength);
            std::memcpy(loaded.pcmData.data() + oldSize, data, currentLength);
        }

        buffer->Unlock();
    }

    if (!loaded.IsValid()) {
        return false;
    }

    resource = std::move(loaded);
    return true;
}

} // namespace

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnVoiceProcessingPassStart(UINT32) {
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnVoiceProcessingPassEnd() {
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnStreamEnd() {
    finished.store(true, std::memory_order_release);
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnBufferStart(void*) {
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnBufferEnd(void*) {
    finished.store(true, std::memory_order_release);
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnLoopEnd(void*) {
}

void STDMETHODCALLTYPE AudioSystem::SourceVoiceCallback::OnVoiceError(void*, HRESULT) {
    finished.store(true, std::memory_order_release);
}

AudioSystem::~AudioSystem() {
    Shutdown();
}

bool AudioSystem::Initialize() {
    if (xAudio2_) {
        return true;
    }

    HRESULT hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        return false;
    }

    hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
    if (FAILED(hr)) {
        xAudio2_.Reset();
        return false;
    }

    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
        xAudio2_.Reset();
        return false;
    }
    mediaFoundationStarted_ = true;

    return true;
}

void AudioSystem::Shutdown() {
    StopAll();
    sounds_.clear();

    if (masterVoice_ != nullptr) {
        masterVoice_->DestroyVoice();
        masterVoice_ = nullptr;
    }

    xAudio2_.Reset();

    if (mediaFoundationStarted_) {
        MFShutdown();
        mediaFoundationStarted_ = false;
    }
}

void AudioSystem::Update() {
    for (size_t i = 0; i < activeVoices_.size();) {
        SourceVoiceCallback* callback = activeVoices_[i].callback.get();
        if (callback != nullptr &&
            callback->finished.load(std::memory_order_acquire)) {
            DestroyVoice(activeVoices_[i]);
            activeVoices_.erase(activeVoices_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

SoundHandle AudioSystem::LoadSound(const std::string& path) {
    for (uint32_t i = 0; i < sounds_.size(); ++i) {
        const SoundSlot& slot = sounds_[i];
        if (slot.occupied && slot.resource.path == path) {
            return SoundHandle{i, slot.generation};
        }
    }

    SoundResource resource{};
    if (!LoadMediaFoundationResource(path, resource) &&
        !LoadWaveResource(path, resource)) {
        return {};
    }

    const uint32_t slotIndex = AllocateSlot();
    SoundSlot& slot = sounds_[slotIndex];
    slot.resource = std::move(resource);
    slot.occupied = true;
    return SoundHandle{slotIndex, slot.generation};
}

SoundHandle AudioSystem::LoadWave(const std::string& path) {
    for (uint32_t i = 0; i < sounds_.size(); ++i) {
        const SoundSlot& slot = sounds_[i];
        if (slot.occupied && slot.resource.path == path) {
            return SoundHandle{i, slot.generation};
        }
    }

    SoundResource resource{};
    if (!LoadWaveResource(path, resource)) {
        return {};
    }

    const uint32_t slotIndex = AllocateSlot();
    SoundSlot& slot = sounds_[slotIndex];
    slot.resource = std::move(resource);
    slot.occupied = true;
    return SoundHandle{slotIndex, slot.generation};
}

SoundHandle AudioSystem::CreateTone(
    const std::string& id,
    float frequencyHz,
    float durationSeconds,
    float amplitude) {
    const std::string resourceId = "procedural-tone://" + id;
    for (uint32_t i = 0; i < sounds_.size(); ++i) {
        const SoundSlot& slot = sounds_[i];
        if (slot.occupied && slot.resource.path == resourceId) {
            return SoundHandle{i, slot.generation};
        }
    }

    constexpr uint32_t kSampleRate = 48000;
    constexpr float kTau = 6.28318530717958647692f;
    const float safeFrequency = (std::clamp)(frequencyHz, 40.0f, 12000.0f);
    const float safeDuration = (std::clamp)(durationSeconds, 0.015f, 1.0f);
    const float safeAmplitude = (std::clamp)(amplitude, 0.0f, 0.95f);
    const uint32_t sampleCount = (std::max)(
        1u,
        static_cast<uint32_t>(std::ceil(safeDuration * kSampleRate)));

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(
        format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    SoundResource resource{};
    resource.path = resourceId;
    resource.formatBytes.resize(sizeof(WAVEFORMATEX));
    std::memcpy(resource.formatBytes.data(), &format, sizeof(format));
    resource.pcmData.resize(
        static_cast<size_t>(sampleCount) * sizeof(int16_t));
    for (uint32_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const float time = static_cast<float>(sampleIndex) /
            static_cast<float>(kSampleRate);
        const float normalizedTime = static_cast<float>(sampleIndex) /
            static_cast<float>((std::max)(1u, sampleCount - 1u));
        const float attack = (std::min)(1.0f, normalizedTime / 0.08f);
        const float release = (std::min)(1.0f, (1.0f - normalizedTime) / 0.32f);
        const float envelope = attack * release;
        const float fundamental = std::sin(kTau * safeFrequency * time);
        const float harmonic = std::sin(kTau * safeFrequency * 2.0f * time) * 0.16f;
        const float value = (std::clamp)(
            (fundamental + harmonic) * safeAmplitude * envelope,
            -1.0f,
            1.0f);
        const int16_t pcm = static_cast<int16_t>(
            std::lround(value * 32767.0f));
        std::memcpy(
            resource.pcmData.data() +
                static_cast<size_t>(sampleIndex) * sizeof(int16_t),
            &pcm,
            sizeof(pcm));
    }

    const uint32_t slotIndex = AllocateSlot();
    SoundSlot& slot = sounds_[slotIndex];
    slot.resource = std::move(resource);
    slot.occupied = true;
    return SoundHandle{slotIndex, slot.generation};
}

bool AudioSystem::UnloadSound(SoundHandle handle) {
    if (!IsHandleAlive(handle)) {
        return false;
    }

    Stop(handle);

    SoundSlot& slot = sounds_[handle.index];
    slot.resource = {};
    slot.occupied = false;
    ++slot.generation;
    if (slot.generation == 0) {
        slot.generation = 1;
    }
    return true;
}

bool AudioSystem::Play(SoundHandle handle, float volume, bool loop) {
    return PlaySpatial(handle, volume, 0.0f, 1.0f, loop);
}

bool AudioSystem::PlaySpatial(
    SoundHandle handle,
    float volume,
    float pan,
    float pitch,
    bool loop) {
    if (!xAudio2_ || !IsHandleAlive(handle)) {
        return false;
    }

    Update();

    const SoundResource& resource = sounds_[handle.index].resource;
    const WAVEFORMATEX* format = resource.Format();
    if (format == nullptr || resource.pcmData.empty()) {
        return false;
    }

    auto callback = std::make_unique<SourceVoiceCallback>();
    IXAudio2SourceVoice* voice = nullptr;
    HRESULT hr = xAudio2_->CreateSourceVoice(
        &voice,
        format,
        0,
        XAUDIO2_DEFAULT_FREQ_RATIO,
        callback.get());
    if (FAILED(hr) || voice == nullptr) {
        return false;
    }

    const float safeVolume = (std::max)(0.0f, volume);
    voice->SetVolume(safeVolume);
    voice->SetFrequencyRatio((std::clamp)(pitch, 0.5f, 2.0f));

    if (format->nChannels == 1 && masterVoice_ != nullptr) {
        XAUDIO2_VOICE_DETAILS masterDetails{};
        masterVoice_->GetVoiceDetails(&masterDetails);
        if (masterDetails.InputChannels >= 2) {
            std::vector<float> matrix(masterDetails.InputChannels, 0.0f);
            const float safePan = (std::clamp)(pan, -1.0f, 1.0f);
            matrix[0] = std::sqrt(0.5f * (1.0f - safePan));
            matrix[1] = std::sqrt(0.5f * (1.0f + safePan));
            voice->SetOutputMatrix(
                masterVoice_,
                1,
                masterDetails.InputChannels,
                matrix.data());
        }
    }

    XAUDIO2_BUFFER buffer{};
    buffer.pAudioData = resource.pcmData.data();
    buffer.AudioBytes = static_cast<UINT32>(resource.pcmData.size());
    buffer.Flags = loop ? 0 : XAUDIO2_END_OF_STREAM;
    buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;

    hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        voice->DestroyVoice();
        return false;
    }

    hr = voice->Start();
    if (FAILED(hr)) {
        voice->DestroyVoice();
        return false;
    }

    activeVoices_.push_back(ActiveVoice{voice, handle, std::move(callback)});
    return true;
}

void AudioSystem::SetPlayback(SoundHandle handle, float volume, float pitch) {
    if (!IsHandleAlive(handle) || !std::isfinite(volume) || !std::isfinite(pitch)) return;
    for(auto& active : activeVoices_) {
        if(active.sound.index == handle.index && active.sound.generation == handle.generation && active.voice) {
            active.voice->SetVolume((std::clamp)(volume,0.0f,1.0f));
            active.voice->SetFrequencyRatio((std::clamp)(pitch,0.5f,2.0f));
        }
    }
}

void AudioSystem::SetPaused(SoundHandle handle, bool paused) {
    if(!IsHandleAlive(handle)) return;
    for(auto& active : activeVoices_) {
        if(active.sound.index!=handle.index || active.sound.generation!=handle.generation) continue;
        // Stop without flushing the submitted buffer: Start resumes its exact
        // sample position when the presentation clock resumes after focus loss.
        if(paused) active.voice->Stop();
        else active.voice->Start();
    }
}

void AudioSystem::Stop(SoundHandle handle) {
    for (size_t i = 0; i < activeVoices_.size();) {
        if (activeVoices_[i].sound.index == handle.index &&
            activeVoices_[i].sound.generation == handle.generation) {
            DestroyVoice(activeVoices_[i]);
            activeVoices_.erase(activeVoices_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
}

void AudioSystem::StopAll() {
    for (ActiveVoice& activeVoice : activeVoices_) {
        DestroyVoice(activeVoice);
    }
    activeVoices_.clear();
}

const SoundResource* AudioSystem::GetSound(SoundHandle handle) const {
    if (!IsHandleAlive(handle)) {
        return nullptr;
    }
    return &sounds_[handle.index].resource;
}

bool AudioSystem::IsHandleAlive(SoundHandle handle) const {
    if (!handle.IsValid() || handle.index >= sounds_.size()) {
        return false;
    }

    const SoundSlot& slot = sounds_[handle.index];
    return slot.occupied && slot.generation == handle.generation;
}

uint32_t AudioSystem::AllocateSlot() {
    for (uint32_t i = 0; i < sounds_.size(); ++i) {
        if (!sounds_[i].occupied) {
            return i;
        }
    }

    sounds_.push_back(SoundSlot{});
    return static_cast<uint32_t>(sounds_.size() - 1);
}

void AudioSystem::DestroyVoice(ActiveVoice& activeVoice) {
    if (activeVoice.voice == nullptr) {
        return;
    }

    activeVoice.voice->Stop(0);
    activeVoice.voice->FlushSourceBuffers();
    activeVoice.voice->DestroyVoice();
    activeVoice.voice = nullptr;
}

} // namespace audio
