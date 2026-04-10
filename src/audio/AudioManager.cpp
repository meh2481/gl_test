#include "AudioManager.h"
#include <SDL3/SDL.h>
#include "../debug/ConsoleBuffer.h"
#include "../debug/ThreadProfiler.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <cstring>

// EFX constants (in case not defined)
#ifndef AL_EFFECT_LOWPASS
#define AL_EFFECT_LOWPASS 0x0001
#endif
#ifndef AL_EFFECT_REVERB
#define AL_EFFECT_REVERB 0x0004
#endif
#ifndef AL_LOWPASS_GAIN
#define AL_LOWPASS_GAIN 0x0001
#endif
#ifndef AL_LOWPASS_GAINHF
#define AL_LOWPASS_GAINHF 0x0002
#endif
#ifndef AL_REVERB_GAIN
#define AL_REVERB_GAIN 0x0001
#endif
#ifndef AL_REVERB_DECAY_TIME
#define AL_REVERB_DECAY_TIME 0x0004
#endif


// EFX function pointers (if available)
static LPALGENEFFECTS alGenEffects = nullptr;
static LPALDELETEEFFECTS alDeleteEffects = nullptr;
static LPALISEFFECT alIsEffect = nullptr;
static LPALEFFECTI alEffecti = nullptr;
static LPALEFFECTF alEffectf = nullptr;
static LPALGENFILTERS alGenFilters = nullptr;
static LPALDELETEFILTERS alDeleteFilters = nullptr;
static LPALISFILTER alIsFilter = nullptr;
static LPALFILTERI alFilteri = nullptr;
static LPALFILTERF alFilterf = nullptr;
static LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots = nullptr;
static LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots = nullptr;
static LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot = nullptr;
static LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti = nullptr;

AudioManager::AudioManager(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer)
    : device(nullptr), context(nullptr), bufferCount(0),
      efxSupported(false), effectSlot(0), effect(0), filter(0),
      currentEffect(AUDIO_EFFECT_NONE), currentEffectIntensity(1.0f),
      ima4Supported_(false), allocator_(allocator), consoleBuffer_(consoleBuffer),
      musicWorkerThread_(nullptr), musicMutex_(nullptr), musicCondition_(nullptr),
      musicWorkerRunning_(true)
{
    assert(allocator_ != nullptr);
    consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE, "AudioManager: Using shared memory allocator");

    // Initialize arrays
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        sources[i].active = false;
        sources[i].source = 0;
        sources[i].volume = 1.0f;
        sources[i].looping = false;
        sources[i].x = 0.0f;
        sources[i].y = 0.0f;
        sources[i].z = 0.0f;
        sources[i].bufferId = -1;
    }

    for (int i = 0; i < MAX_AUDIO_BUFFERS; i++) {
        buffers[i].loaded = false;
        buffers[i].buffer = 0;
    }

    // Initialize music track slots
    for (int i = 0; i < MAX_MUSIC_TRACKS; i++) {
        musicTracks_[i].valid = false;
        musicTracks_[i].playing = false;
        musicTracks_[i].numLayers = 0;
        musicTracks_[i].numIntensities = 0;
        musicTracks_[i].currentIntensity = -1;
        for (int j = 0; j < MAX_MUSIC_LAYERS_PER_TRACK; j++) {
            musicTracks_[i].layers[j].active = false;
            musicTracks_[i].layers[j].glaState.valid = false;
            musicTracks_[i].layers[j].buffersCreated = false;
        }
    }

    // Initialize music stream worker thread
    musicMutex_ = SDL_CreateMutex();
    musicCondition_ = SDL_CreateCondition();
    assert(musicMutex_ != nullptr);
    assert(musicCondition_ != nullptr);
    musicWorkerThread_ = SDL_CreateThread(&AudioManager::musicStreamWorkerThread, "MusicStreamWorker", this);
    if (!musicWorkerThread_) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: Failed to create music stream worker thread");
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "AudioManager: Music stream worker thread started");
    }
}

AudioManager::~AudioManager() {
    // Shut down music stream worker thread
    if (musicMutex_) {
        SDL_LockMutex(musicMutex_);
        musicWorkerRunning_ = false;
        SDL_UnlockMutex(musicMutex_);
    }
    if (musicCondition_) {
        SDL_SignalCondition(musicCondition_);
    }
    if (musicWorkerThread_) {
        int rc;
        SDL_WaitThread(musicWorkerThread_, &rc);
        musicWorkerThread_ = nullptr;
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "AudioManager: Music stream worker thread stopped (rc=%d)", rc);
    }

    cleanup();

    // Cleanup music worker synchronization primitives (thread already joined above)
    if (musicCondition_) {
        SDL_DestroyCondition(musicCondition_);
        musicCondition_ = nullptr;
    }
    if (musicMutex_) {
        SDL_DestroyMutex(musicMutex_);
        musicMutex_ = nullptr;
    }
}

void AudioManager::initialize() {
    // Open default audio device
    device = alcOpenDevice(nullptr);
    assert(device != nullptr && "Failed to open audio device");

    // Create context
    context = alcCreateContext(device, nullptr);
    assert(context != nullptr && "Failed to create audio context");

    // Make context current
    ALCboolean result = alcMakeContextCurrent(context);
    assert(result == ALC_TRUE && "Failed to make audio context current");

    // Check for required AL_EXT_IMA4 extension
    ima4Supported_ = alIsExtensionPresent("AL_EXT_IMA4") == AL_TRUE;
    assert(ima4Supported_ && "AL_EXT_IMA4 extension is required for GLA audio format");
    if (ima4Supported_) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "AudioManager: AL_EXT_IMA4 supported");
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: AL_EXT_IMA4 NOT supported - audio will not function");
    }

    // Check for EFX support
    if (alcIsExtensionPresent(device, "ALC_EXT_EFX")) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "OpenAL EFX extension supported");
        initializeEFX();
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_WARN, "OpenAL EFX extension not supported - effects disabled");
        efxSupported = false;
    }

    // Set default listener properties
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat listenerOri[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, listenerOri);
}

void AudioManager::suspend() {
    if (context) {
        alcSuspendContext(context);
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "AudioManager: suspended OpenAL context");
    }
}

void AudioManager::resume() {
    if (context) {
        alcProcessContext(context);
        alcMakeContextCurrent(context);
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "AudioManager: resumed OpenAL context");
    }
}

void AudioManager::cleanup() {
    // Release all music tracks first (frees GlaStreamState and OpenAL sources)
    for (int i = 0; i < MAX_MUSIC_TRACKS; i++) {
        if (musicTracks_[i].valid) {
            releaseMusicTrack(musicTracks_[i]);
        }
    }

    // Stop and delete all sources
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (sources[i].active) {
            alSourceStop(sources[i].source);
            alDeleteSources(1, &sources[i].source);
            sources[i].active = false;
        }
    }

    // Delete all buffers
    for (int i = 0; i < MAX_AUDIO_BUFFERS; i++) {
        if (buffers[i].loaded) {
            alDeleteBuffers(1, &buffers[i].buffer);
            buffers[i].loaded = false;
        }
    }

    // Cleanup EFX
    if (efxSupported) {
        if (alIsAuxiliaryEffectSlot && alIsAuxiliaryEffectSlot(effectSlot)) {
            alDeleteAuxiliaryEffectSlots(1, &effectSlot);
        }
        if (alIsEffect && alIsEffect(effect)) {
            alDeleteEffects(1, &effect);
        }
        if (alIsFilter && alIsFilter(filter)) {
            alDeleteFilters(1, &filter);
        }
    }

    // Cleanup context and device
    if (context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(context);
        context = nullptr;
    }

    if (device) {
        alcCloseDevice(device);
        device = nullptr;
    }
}

void AudioManager::initializeEFX() {
    // Load EFX function pointers
    alGenEffects = (LPALGENEFFECTS)alGetProcAddress("alGenEffects");
    alDeleteEffects = (LPALDELETEEFFECTS)alGetProcAddress("alDeleteEffects");
    alIsEffect = (LPALISEFFECT)alGetProcAddress("alIsEffect");
    alEffecti = (LPALEFFECTI)alGetProcAddress("alEffecti");
    alEffectf = (LPALEFFECTF)alGetProcAddress("alEffectf");
    alGenFilters = (LPALGENFILTERS)alGetProcAddress("alGenFilters");
    alDeleteFilters = (LPALDELETEFILTERS)alGetProcAddress("alDeleteFilters");
    alIsFilter = (LPALISFILTER)alGetProcAddress("alIsFilter");
    alFilteri = (LPALFILTERI)alGetProcAddress("alFilteri");
    alFilterf = (LPALFILTERF)alGetProcAddress("alFilterf");
    alGenAuxiliaryEffectSlots = (LPALGENAUXILIARYEFFECTSLOTS)alGetProcAddress("alGenAuxiliaryEffectSlots");
    alDeleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS)alGetProcAddress("alDeleteAuxiliaryEffectSlots");
    alIsAuxiliaryEffectSlot = (LPALISAUXILIARYEFFECTSLOT)alGetProcAddress("alIsAuxiliaryEffectSlot");
    alAuxiliaryEffectSloti = (LPALAUXILIARYEFFECTSLOTI)alGetProcAddress("alAuxiliaryEffectSloti");

    if (alGenEffects && alGenAuxiliaryEffectSlots && alGenFilters &&
        alFilteri && alFilterf && alDeleteFilters && alIsFilter) {
        // Create effect slot
        alGenAuxiliaryEffectSlots(1, &effectSlot);

        // Create effect
        alGenEffects(1, &effect);

        // Create filter
        alGenFilters(1, &filter);

        // Check for errors after all object creation
        ALenum error = alGetError();
        if (error == AL_NO_ERROR &&
            (alIsAuxiliaryEffectSlot == nullptr || alIsAuxiliaryEffectSlot(effectSlot)) &&
            (alIsEffect == nullptr || alIsEffect(effect)) &&
            (alIsFilter == nullptr || alIsFilter(filter))) {
            efxSupported = true;
            consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "EFX initialized successfully");
        } else {
            efxSupported = false;
            consoleBuffer_->log(SDL_LOG_PRIORITY_WARN, "EFX initialization failed");
        }
    } else {
        efxSupported = false;
    }
}

int AudioManager::findFreeSourceSlot() {
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (!sources[i].active) {
            return i;
        }
    }
    return -1;
}

int AudioManager::findFreeBufferSlot() {
    for (int i = 0; i < MAX_AUDIO_BUFFERS; i++) {
        if (!buffers[i].loaded) {
            return i;
        }
    }
    return -1;
}

int AudioManager::loadAudioBufferFromMemory(const void* data, Uint64 size, int sampleRate, int channels, int bitsPerSample) {
    int slot = findFreeBufferSlot();
    if (slot == -1) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "No free buffer slots available");
        assert(false);
        return -1;
    }

    // Determine format
    ALenum format;
    if (channels == 1 && bitsPerSample == 8) {
        format = AL_FORMAT_MONO8;
    } else if (channels == 1 && bitsPerSample == 16) {
        format = AL_FORMAT_MONO16;
    } else if (channels == 2 && bitsPerSample == 8) {
        format = AL_FORMAT_STEREO8;
    } else if (channels == 2 && bitsPerSample == 16) {
        format = AL_FORMAT_STEREO16;
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Unsupported audio format: %d channels, %d bits", channels, bitsPerSample);
        assert(false);
        return -1;
    }

    // Generate buffer
    alGenBuffers(1, &buffers[slot].buffer);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Failed to generate audio buffer: %d", error);
        assert(false);
        return -1;
    }

    // Upload data to buffer
    alBufferData(buffers[slot].buffer, format, data, size, sampleRate);
    error = alGetError();
    if (error != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Failed to upload audio data: %d", error);
        alDeleteBuffers(1, &buffers[slot].buffer);
        assert(false);
        return -1;
    }

    buffers[slot].loaded = true;
    bufferCount++;

    return slot;
}

int AudioManager::loadGlaAudioFromMemory(const void* data, Uint64 size) {
    assert(data != nullptr);
    assert(size >= sizeof(GlaHeader));
    assert(ima4Supported_ && "AL_EXT_IMA4 required for GLA audio");

    const GlaHeader* hdr = static_cast<const GlaHeader*>(data);
    if (SDL_memcmp(hdr->sig, "GLAD", 4) != 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: loadGlaAudioFromMemory: bad magic (not GLAD)");
        assert(false);
        return -1;
    }
    if (hdr->version != GLA_VERSION) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: loadGlaAudioFromMemory: unsupported version %u", hdr->version);
        assert(false);
        return -1;
    }

    consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG,
        "AudioManager: loadGlaAudioFromMemory: %u ch, %u Hz, %u samples, blockSize=%u",
        hdr->channels, hdr->sampleRate, hdr->totalSamples, hdr->blockSizeBytes);

    int slot = findFreeBufferSlot();
    if (slot == -1) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: No free buffer slots available");
        assert(false);
        return -1;
    }

    // Select IMA4 format
    ALenum fmt = (hdr->channels == 2) ? AL_FORMAT_STEREO_IMA4 : AL_FORMAT_MONO_IMA4;

    // Audio data immediately follows the header
    const void* audioData = static_cast<const Uint8*>(data) + sizeof(GlaHeader);
    ALsizei audioSize = (ALsizei)(size - sizeof(GlaHeader));

    if (audioSize <= 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: GLA file has no audio data");
        assert(false);
        return -1;
    }

    alGenBuffers(1, &buffers[slot].buffer);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: Failed to generate GL audio buffer: %d", error);
        assert(false);
        return -1;
    }

    // Upload IMA4 data - OpenAL Soft decodes internally during playback
    alBufferData(buffers[slot].buffer, fmt, audioData, audioSize, (ALsizei)hdr->sampleRate);
    error = alGetError();
    if (error != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: Failed to upload GLA IMA4 data: error %d (size=%d, fmt=%d, rate=%u)",
            error, audioSize, fmt, hdr->sampleRate);
        alDeleteBuffers(1, &buffers[slot].buffer);
        assert(false);
        return -1;
    }

    buffers[slot].loaded = true;
    bufferCount++;
    consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "AudioManager: GLA buffer loaded into slot %d", slot);
    return slot;
}

int AudioManager::createAudioSource(int bufferId, bool looping, float volume) {
    if (bufferId < 0 || bufferId >= MAX_AUDIO_BUFFERS || !buffers[bufferId].loaded) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid buffer ID: %d", bufferId);
        assert(false);
        return -1;
    }

    int slot = findFreeSourceSlot();
    if (slot == -1) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "No free source slots available");
        assert(false);
        return -1;
    }

    // Generate source
    alGenSources(1, &sources[slot].source);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Failed to generate audio source: %d", error);
        assert(false);
        return -1;
    }

    // Set source properties
    alSourcei(sources[slot].source, AL_BUFFER, buffers[bufferId].buffer);
    alSourcef(sources[slot].source, AL_GAIN, volume);
    alSourcei(sources[slot].source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    alSource3f(sources[slot].source, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSource3f(sources[slot].source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);

    sources[slot].active = true;
    sources[slot].volume = volume;
    sources[slot].looping = looping;
    sources[slot].x = 0.0f;
    sources[slot].y = 0.0f;
    sources[slot].z = 0.0f;
    sources[slot].bufferId = bufferId;

    // Apply current effect/filter to the new source if EFX is supported
    if (efxSupported) {
        if (currentEffect == AUDIO_EFFECT_LOWPASS && alFilteri) {
            // Apply lowpass filter
            alSourcei(sources[slot].source, AL_DIRECT_FILTER, filter);
        } else if (currentEffect == AUDIO_EFFECT_REVERB) {
            // Apply reverb effect
            alSource3i(sources[slot].source, AL_AUXILIARY_SEND_FILTER, effectSlot, 0, AL_FILTER_NULL);
        }
    }

    return slot;
}

void AudioManager::playSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    alSourcePlay(sources[sourceId].source);
}

void AudioManager::stopSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    alSourceStop(sources[sourceId].source);
}

void AudioManager::pauseSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    alSourcePause(sources[sourceId].source);
}

void AudioManager::setSourcePosition(int sourceId, float x, float y, float z) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    sources[sourceId].x = x;
    sources[sourceId].y = y;
    sources[sourceId].z = z;
    alSource3f(sources[sourceId].source, AL_POSITION, x, y, z);
}

void AudioManager::setSourceVelocity(int sourceId, float vx, float vy, float vz) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    alSource3f(sources[sourceId].source, AL_VELOCITY, vx, vy, vz);
}

void AudioManager::setSourceVolume(int sourceId, float volume) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    sources[sourceId].volume = volume;
    alSourcef(sources[sourceId].source, AL_GAIN, volume);
}

void AudioManager::setSourcePitch(int sourceId, float pitch) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    alSourcef(sources[sourceId].source, AL_PITCH, pitch);
}

void AudioManager::setSourceLooping(int sourceId, bool looping) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Invalid source ID: %d", sourceId);
        assert(false);
        return;
    }

    sources[sourceId].looping = looping;
    alSourcei(sources[sourceId].source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

void AudioManager::releaseSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES) {
        return;
    }
    if (!sources[sourceId].active) {
        return;
    }

    alSourceStop(sources[sourceId].source);
    alDeleteSources(1, &sources[sourceId].source);
    sources[sourceId].active = false;
    sources[sourceId].source = 0;
}

bool AudioManager::isSourcePlaying(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        return false;
    }

    ALint state;
    alGetSourcei(sources[sourceId].source, AL_SOURCE_STATE, &state);
    return state == AL_PLAYING;
}

void AudioManager::setListenerPosition(float x, float y, float z) {
    alListener3f(AL_POSITION, x, y, z);
}

void AudioManager::setListenerVelocity(float vx, float vy, float vz) {
    alListener3f(AL_VELOCITY, vx, vy, vz);
}

void AudioManager::setListenerOrientation(float atX, float atY, float atZ, float upX, float upY, float upZ) {
    ALfloat orientation[] = { atX, atY, atZ, upX, upY, upZ };
    alListenerfv(AL_ORIENTATION, orientation);
}

void AudioManager::setGlobalVolume(float volume) {
    alListenerf(AL_GAIN, volume);
}

void AudioManager::setGlobalEffect(AudioEffect effect, float intensity) {
    if (!efxSupported) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "EFX not supported, cannot set global effect");
        assert(false);
        return;
    }

    currentEffect = effect;
    currentEffectIntensity = intensity;
    applyEffect();
}

void AudioManager::applyEffect() {
    if (!efxSupported) {
        return;
    }

    switch (currentEffect) {
        case AUDIO_EFFECT_LOWPASS:
            // Configure lowpass filter (filters are applied directly to sources, not through effect slots)
            if (alFilteri && alFilterf) {
                alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                alFilterf(filter, AL_LOWPASS_GAIN, currentEffectIntensity);
                alFilterf(filter, AL_LOWPASS_GAINHF, 0.5f * currentEffectIntensity);

                // Apply filter directly to all active sources
                for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                    if (sources[i].active) {
                        alSourcei(sources[i].source, AL_DIRECT_FILTER, filter);
                    }
                }
            }

            // Clear effect slot (not used for lowpass)
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_NULL);
            if (alAuxiliaryEffectSloti) {
                alAuxiliaryEffectSloti(effectSlot, AL_EFFECTSLOT_EFFECT, effect);
            }
            for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                if (sources[i].active) {
                    alSource3i(sources[i].source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
                }
            }

            // Apply to all active music layer sources
            for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
                if (!musicTracks_[t].valid) continue;
                for (int l = 0; l < musicTracks_[t].numLayers; l++) {
                    if (musicTracks_[t].layers[l].active) {
                        applyEffectToSource(musicTracks_[t].layers[l].source);
                    }
                }
            }
            break;

        case AUDIO_EFFECT_REVERB:
            // Configure reverb effect
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            alEffectf(effect, AL_REVERB_GAIN, currentEffectIntensity);
            alEffectf(effect, AL_REVERB_DECAY_TIME, 1.5f);

            // Apply effect to slot
            if (alAuxiliaryEffectSloti) {
                alAuxiliaryEffectSloti(effectSlot, AL_EFFECTSLOT_EFFECT, effect);
            }

            // Apply effect slot to all active sources
            for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                if (sources[i].active) {
                    alSource3i(sources[i].source, AL_AUXILIARY_SEND_FILTER, effectSlot, 0, AL_FILTER_NULL);
                }
            }

            // Clear direct filter (not used for reverb)
            if (alFilteri) {
                alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_NULL);
                for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                    if (sources[i].active) {
                        alSourcei(sources[i].source, AL_DIRECT_FILTER, AL_FILTER_NULL);
                    }
                }
            }

            // Apply to all active music layer sources
            for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
                if (!musicTracks_[t].valid) continue;
                for (int l = 0; l < musicTracks_[t].numLayers; l++) {
                    if (musicTracks_[t].layers[l].active) {
                        applyEffectToSource(musicTracks_[t].layers[l].source);
                    }
                }
            }
            break;

        case AUDIO_EFFECT_NONE:
        default:
            // Disable effect
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_NULL);
            if (alAuxiliaryEffectSloti) {
                alAuxiliaryEffectSloti(effectSlot, AL_EFFECTSLOT_EFFECT, effect);
            }

            // Clear effect slot from all active sources
            for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                if (sources[i].active) {
                    alSource3i(sources[i].source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
                }
            }

            // Disable filter
            if (alFilteri) {
                alFilteri(filter, AL_FILTER_TYPE, AL_FILTER_NULL);
                for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
                    if (sources[i].active) {
                        alSourcei(sources[i].source, AL_DIRECT_FILTER, AL_FILTER_NULL);
                    }
                }
            }

            // Apply to all active music layer sources
            for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
                if (!musicTracks_[t].valid) continue;
                for (int l = 0; l < musicTracks_[t].numLayers; l++) {
                    if (musicTracks_[t].layers[l].active) {
                        applyEffectToSource(musicTracks_[t].layers[l].source);
                    }
                }
            }
            break;
    }
}

void AudioManager::applyEffectToSource(ALuint alSource) {
    if (!efxSupported) {
        return;
    }
    switch (currentEffect) {
        case AUDIO_EFFECT_LOWPASS:
            if (alFilteri) {
                alSourcei(alSource, AL_DIRECT_FILTER, filter);
            }
            alSource3i(alSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
            break;
        case AUDIO_EFFECT_REVERB:
            alSource3i(alSource, AL_AUXILIARY_SEND_FILTER, effectSlot, 0, AL_FILTER_NULL);
            if (alFilteri) {
                alSourcei(alSource, AL_DIRECT_FILTER, AL_FILTER_NULL);
            }
            break;
        case AUDIO_EFFECT_NONE:
        default:
            alSource3i(alSource, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL);
            if (alFilteri) {
                alSourcei(alSource, AL_DIRECT_FILTER, AL_FILTER_NULL);
            }
            break;
    }
}

void AudioManager::update() {
    // Cleanup finished non-looping sources
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (sources[i].active && !sources[i].looping && !isSourcePlaying(i)) {
            releaseSource(i);
        }
    }
}

void AudioManager::clearAllSources() {
    for (int i = 0; i < MAX_AUDIO_SOURCES; i++) {
        if (sources[i].active) {
            releaseSource(i);
        }
    }
}

// ============================================================================
// Music streaming implementation
// ============================================================================

void AudioManager::seekGlaStream(GlaStreamState& st, Uint32 targetSample) {
    assert(st.valid);
    assert(st.samplesPerBlock > 0);
    Uint32 blockIdx = targetSample / st.samplesPerBlock;
    Uint64 maxBlocks = st.dataSize / st.blockSizeBytes;
    if (blockIdx >= maxBlocks) {
        blockIdx = (maxBlocks > 0) ? (Uint32)(maxBlocks - 1) : 0;
    }
    st.byteOffset = (Uint64)blockIdx * st.blockSizeBytes;
    st.currentSample = blockIdx * st.samplesPerBlock;
    consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
        "AudioManager: seekGlaStream: target=%u -> block=%u, byteOffset=%llu, sample=%u",
        targetSample, blockIdx, (unsigned long long)st.byteOffset, st.currentSample);
}

int AudioManager::loadMusicTrack(
    Uint32 loopStartSample, Uint32 loopEndSample,
    const MusicLayerInitData* uniqueLayers, int numUniqueLayers,
    const MusicIntensityInitData* intensities, int numIntensities)
{
    assert(uniqueLayers != nullptr);
    assert(intensities != nullptr);
    assert(numUniqueLayers > 0 && numUniqueLayers <= MAX_MUSIC_LAYERS_PER_TRACK);
    assert(numIntensities > 0 && numIntensities <= MAX_MUSIC_INTENSITIES);
    assert(ima4Supported_ && "AL_EXT_IMA4 required for GLA music streaming");

    // Find a free track slot.
    int trackId = -1;
    for (int i = 0; i < MAX_MUSIC_TRACKS; i++) {
        if (!musicTracks_[i].valid) {
            trackId = i;
            break;
        }
    }
    if (trackId < 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
            "AudioManager: No free music track slots (MAX_MUSIC_TRACKS=%d)", MAX_MUSIC_TRACKS);
        return -1;
    }

    consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG,
        "AudioManager: Loading music track %d (%d layers, %d intensities, loop %u->%u)",
        trackId, numUniqueLayers, numIntensities, loopStartSample, loopEndSample);

    MusicTrackState& track = musicTracks_[trackId];
    track.numLayers       = numUniqueLayers;
    track.numIntensities  = numIntensities;
    track.loopStartSample = loopStartSample;
    track.loopEndSample   = loopEndSample;
    track.currentIntensity = -1;
    track.fadeRate        = 1.0f / MUSIC_DEFAULT_FADE_DURATION;
    track.pendingStop     = false;
    track.playing         = false;

    // Open each unique GLA layer.
    for (int i = 0; i < numUniqueLayers; i++) {
        MusicLayerStream& layer = track.layers[i];
        layer.active         = false;
        layer.glaState.valid = false;
        layer.buffersCreated = false;
        layer.volume         = 0.0f;
        layer.targetVolume   = 0.0f;

        // Validate GLA header
        const Uint8* data = uniqueLayers[i].data;
        Uint64 size = uniqueLayers[i].size;
        assert(data != nullptr && size >= sizeof(GlaHeader));

        const GlaHeader* hdr = reinterpret_cast<const GlaHeader*>(data);
        if (SDL_memcmp(hdr->sig, "GLAD", 4) != 0) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                "AudioManager: layer %d of track %d has bad GLA magic", i, trackId);
            assert(false);
            for (int j = 0; j < i; j++) releaseMusicLayer(track.layers[j]);
            track.valid = false;
            return -1;
        }
        if (hdr->version != GLA_VERSION) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                "AudioManager: layer %d of track %d has unsupported GLA version %u", i, trackId, hdr->version);
            assert(false);
            for (int j = 0; j < i; j++) releaseMusicLayer(track.layers[j]);
            track.valid = false;
            return -1;
        }

        // Initialise GlaStreamState: blockData points into the pak buffer
        GlaStreamState& st = layer.glaState;
        st.blockData      = data + sizeof(GlaHeader);
        st.dataSize       = size - sizeof(GlaHeader);
        st.byteOffset     = 0;
        st.sampleRate     = hdr->sampleRate;
        st.channels       = hdr->channels;
        st.blockSizeBytes = hdr->blockSizeBytes;
        st.samplesPerBlock = hdr->samplesPerBlock;
        st.totalSamples   = hdr->totalSamples;
        st.currentSample  = 0;
        st.valid          = true;

        // Create the OpenAL streaming source.
        alGenSources(1, &layer.source);
        alSourcef(layer.source, AL_GAIN, 0.0f);
        alSource3f(layer.source, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSource3f(layer.source, AL_VELOCITY, 0.0f, 0.0f, 0.0f);
        alSourcei(layer.source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(layer.source, AL_LOOPING, AL_FALSE);

        // Apply any active global effect.
        applyEffectToSource(layer.source);

        // Create the buffer pool for this layer.
        alGenBuffers(MUSIC_STREAM_BUFFERS, layer.buffers);
        layer.buffersCreated = true;
        layer.active = true;

        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG,
            "AudioManager: Music track %d layer %d opened (%d ch, %u Hz, %u samples, blockSize=%u)",
            trackId, i, st.channels, st.sampleRate, st.totalSamples, st.blockSizeBytes);
    }

    // Build intensity descriptors (which layers are on/off per intensity).
    for (int i = 0; i < numIntensities; i++) {
        MusicIntensityDesc& desc = track.intensities[i];
        desc.nameHash = intensities[i].nameHash;

        // Default all layers to off.
        for (int l = 0; l < numUniqueLayers; l++) {
            desc.layerVolumes[l] = 0.0f;
        }

        // Set layers that belong to this intensity to 1.0.
        for (Uint32 li = 0; li < intensities[i].numLayers; li++) {
            Uint64 wantedId = intensities[i].layerResourceIds[li];
            for (int l = 0; l < numUniqueLayers; l++) {
                if (uniqueLayers[l].resourceId == wantedId) {
                    desc.layerVolumes[l] = 1.0f;
                    break;
                }
            }
        }
    }

    track.valid = true;
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO,
        "AudioManager: Music track %d loaded successfully", trackId);
    return trackId;
}

void AudioManager::playMusicTrack(int trackId, float fadeDuration) {
    assert(trackId >= 0 && trackId < MAX_MUSIC_TRACKS);
    assert(musicTracks_[trackId].valid);

    SDL_LockMutex(musicMutex_);

    MusicTrackState& track = musicTracks_[trackId];
    track.playing     = true;
    track.pendingStop = false;
    track.fadeRate    = (fadeDuration > 0.0f) ? (1.0f / fadeDuration) : 0.0f;

    // Activate the first intensity by default if none has been chosen yet.
    if (track.currentIntensity < 0 && track.numIntensities > 0) {
        track.currentIntensity = 0;
        const MusicIntensityDesc& desc = track.intensities[0];
        for (int l = 0; l < track.numLayers; l++) {
            track.layers[l].targetVolume = desc.layerVolumes[l];
            if (fadeDuration <= 0.0f) {
                // Snap immediately — no fade-in.
                track.layers[l].volume = desc.layerVolumes[l];
            } else {
                // Start silent and fade in.
                track.layers[l].volume = 0.0f;
            }
            alSourcef(track.layers[l].source, AL_GAIN, track.layers[l].volume);
        }
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG,
            "AudioManager: Music track %d starting with intensity 0 (hash %llu)",
            trackId, (unsigned long long)desc.nameHash);
    }

    // Pre-fill all streaming buffers for each layer and start playback.
    for (int l = 0; l < track.numLayers; l++) {
        MusicLayerStream& layer = track.layers[l];
        if (!layer.active || !layer.glaState.valid) continue;

        // Seek all layers to the beginning so they are synchronised.
        seekGlaStream(layer.glaState, 0);

        // Fill and queue all stream buffers.
        for (int b = 0; b < MUSIC_STREAM_BUFFERS; b++) {
            int filled = fillStreamBuffer(layer, layer.buffers[b],
                MUSIC_STREAM_BUFFER_FRAMES,
                track.loopStartSample, track.loopEndSample);
            if (filled > 0) {
                alSourceQueueBuffers(layer.source, 1, &layer.buffers[b]);
            }
        }

        alSourcePlay(layer.source);
        consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG,
            "AudioManager: Music track %d layer %d started (vol=%.2f)",
            trackId, l, layer.volume);
    }

    SDL_SignalCondition(musicCondition_);
    SDL_UnlockMutex(musicMutex_);
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "AudioManager: Music track %d playing", trackId);
}

void AudioManager::stopMusicTrack(int trackId, float fadeDuration) {
    assert(trackId >= 0 && trackId < MAX_MUSIC_TRACKS);
    if (!musicTracks_[trackId].valid) return;

    SDL_LockMutex(musicMutex_);

    MusicTrackState& track = musicTracks_[trackId];
    if (fadeDuration <= 0.0f) {
        // Instant stop: release immediately.
        releaseMusicTrack(track);
        SDL_UnlockMutex(musicMutex_);
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "AudioManager: Music track %d stopped", trackId);
    } else {
        // Fade out then release once all layers reach silence.
        track.fadeRate    = 1.0f / fadeDuration;
        track.pendingStop = true;
        for (int l = 0; l < track.numLayers; l++) {
            track.layers[l].targetVolume = 0.0f;
        }
        SDL_UnlockMutex(musicMutex_);
        consoleBuffer_->log(SDL_LOG_PRIORITY_INFO,
            "AudioManager: Music track %d fading out over %.2f s", trackId, fadeDuration);
    }
}

void AudioManager::setMusicIntensity(int trackId, Uint64 intensityNameHash, float fadeDuration) {
    assert(trackId >= 0 && trackId < MAX_MUSIC_TRACKS);
    assert(musicTracks_[trackId].valid);

    SDL_LockMutex(musicMutex_);

    MusicTrackState& track = musicTracks_[trackId];
    int newIdx = -1;
    for (int i = 0; i < track.numIntensities; i++) {
        if (track.intensities[i].nameHash == intensityNameHash) {
            newIdx = i;
            break;
        }
    }

    if (newIdx < 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_WARN,
            "AudioManager: Unknown intensity hash %llu for track %d",
            (unsigned long long)intensityNameHash, trackId);
        SDL_UnlockMutex(musicMutex_);
        return;
    }

    if (newIdx == track.currentIntensity) {
        SDL_UnlockMutex(musicMutex_);
        return; // Already at this intensity; nothing to do.
    }

    track.currentIntensity = newIdx;
    track.fadeRate          = (fadeDuration > 0.0f) ? (1.0f / fadeDuration) : 0.0f;
    const MusicIntensityDesc& desc = track.intensities[newIdx];
    for (int l = 0; l < track.numLayers; l++) {
        track.layers[l].targetVolume = desc.layerVolumes[l];
        if (fadeDuration <= 0.0f) {
            // Snap immediately.
            track.layers[l].volume = desc.layerVolumes[l];
            alSourcef(track.layers[l].source, AL_GAIN, track.layers[l].volume);
        }
    }

    SDL_UnlockMutex(musicMutex_);
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO,
        "AudioManager: Music track %d intensity -> %d (hash %llu)",
        trackId, newIdx, (unsigned long long)intensityNameHash);
}

int AudioManager::fillStreamBuffer(MusicLayerStream& layer, ALuint alBuffer,
                                    int frameCount, Uint32 loopStart, Uint32 loopEnd)
{
    GlaStreamState& st = layer.glaState;
    assert(st.valid);
    assert(frameCount > 0);
    assert(st.samplesPerBlock > 0);
    assert(st.blockSizeBytes > 0);

    // If already at or past loop end, seek back to loop start first.
    if (loopEnd > 0 && st.currentSample >= loopEnd) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
            "AudioManager: fillStreamBuffer: currentSample %u >= loopEnd %u, seeking to loopStart %u",
            st.currentSample, loopEnd, loopStart);
        seekGlaStream(st, loopStart);
    }

    // If we've reached the end of data, seek back to loop start.
    if (st.byteOffset >= st.dataSize) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
            "AudioManager: fillStreamBuffer: EOF, seeking to loopStart %u", loopStart);
        seekGlaStream(st, loopStart);
    }

    // Desired number of complete IMA4 blocks.
    int numBlocks = frameCount / (int)st.samplesPerBlock;
    if (numBlocks < 1) numBlocks = 1;

    // Clamp to loop boundary (if set).
    if (loopEnd > 0 && st.currentSample < loopEnd) {
        Uint32 samplesUntilLoop = loopEnd - st.currentSample;
        int blocksUntilLoop = (int)(samplesUntilLoop / st.samplesPerBlock);
        if (blocksUntilLoop < numBlocks) {
            // Always deliver at least 1 block; the next fill will wrap.
            numBlocks = (blocksUntilLoop > 0) ? blocksUntilLoop : 1;
        }
    }

    // Clamp to available data in the file.
    Uint64 bytesAvail = st.dataSize - st.byteOffset;
    int blocksAvail = (int)(bytesAvail / (Uint64)st.blockSizeBytes);
    if (blocksAvail <= 0) {
        // Exhausted — seek to loop start and recalculate.
        seekGlaStream(st, loopStart);
        bytesAvail = st.dataSize - st.byteOffset;
        blocksAvail = (int)(bytesAvail / (Uint64)st.blockSizeBytes);
        consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
            "AudioManager: fillStreamBuffer: re-seeked; blocksAvail=%d", blocksAvail);
    }
    if (numBlocks > blocksAvail) numBlocks = blocksAvail;
    if (numBlocks <= 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_WARN, "AudioManager: fillStreamBuffer: no blocks available");
        return 0;
    }

    // Submit the IMA4 data directly to OpenAL — no decode step needed.
    Uint64 dataBytes = (Uint64)numBlocks * st.blockSizeBytes;
    ALenum fmt = (st.channels == 2) ? AL_FORMAT_STEREO_IMA4 : AL_FORMAT_MONO_IMA4;
    alBufferData(alBuffer, fmt,
                 st.blockData + st.byteOffset,
                 (ALsizei)dataBytes,
                 (ALsizei)st.sampleRate);

    ALenum err = alGetError();
    if (err != AL_NO_ERROR) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
            "AudioManager: alBufferData IMA4 error %d in fillStreamBuffer (blocks=%d, bytes=%llu)",
            err, numBlocks, (unsigned long long)dataBytes);
        return 0;
    }

    st.byteOffset    += dataBytes;
    st.currentSample += (Uint32)numBlocks * st.samplesPerBlock;

    consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
        "AudioManager: fillStreamBuffer: %d blocks, %llu bytes, sample now %u",
        numBlocks, (unsigned long long)dataBytes, st.currentSample);

    return numBlocks * (int)st.samplesPerBlock;
}

void AudioManager::releaseMusicLayer(MusicLayerStream& layer) {
    if (!layer.active) return;

    // Stop and unqueue all buffers from the source.
    if (layer.source) {
        alSourceStop(layer.source);
        ALint queued = 0;
        alGetSourcei(layer.source, AL_BUFFERS_QUEUED, &queued);
        while (queued > 0) {
            ALuint tmp;
            alSourceUnqueueBuffers(layer.source, 1, &tmp);
            queued--;
        }
        alDeleteSources(1, &layer.source);
        layer.source = 0;
    }

    if (layer.buffersCreated) {
        alDeleteBuffers(MUSIC_STREAM_BUFFERS, layer.buffers);
        layer.buffersCreated = false;
    }

    // Clear stream state (does NOT free the pak buffer — it's owned by the resource system)
    layer.glaState.valid = false;

    layer.active = false;
    layer.volume = 0.0f;
    layer.targetVolume = 0.0f;
}

void AudioManager::releaseMusicTrack(MusicTrackState& track) {
    if (!track.valid) return;
    for (int i = 0; i < track.numLayers; i++) {
        releaseMusicLayer(track.layers[i]);
    }
    track.numLayers      = 0;
    track.numIntensities = 0;
    track.currentIntensity = -1;
    track.playing = false;
    track.valid   = false;
    consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "AudioManager: Music track released");
}

// ============================================================================
// Music stream worker thread
// ============================================================================

int AudioManager::musicStreamWorkerThread(void* arg) {
    AudioManager* self = static_cast<AudioManager*>(arg);
    assert(self != nullptr);

    ThreadProfiler& profiler = ThreadProfiler::instance();
    profiler.registerThread("MusicStreamWorker");

    Uint64 lastTicks = SDL_GetTicks();

    while (true) {
        SDL_LockMutex(self->musicMutex_);
        if (!self->musicWorkerRunning_) {
            SDL_UnlockMutex(self->musicMutex_);
            break;
        }

        Uint64 now = SDL_GetTicks();
        float dt = (float)(now - lastTicks) / 1000.0f;
        lastTicks = now;
        // Clamp dt to avoid large jumps after stalls.
        if (dt > 0.5f) dt = 0.5f;

        bool anyPlaying = false;
        for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
            if (self->musicTracks_[t].valid && self->musicTracks_[t].playing) {
                anyPlaying = true;
                break;
            }
        }

        if (anyPlaying) {
            profiler.updateThreadState(THREAD_STATE_BUSY);
            self->streamMusicTracks(dt);
        }

        SDL_UnlockMutex(self->musicMutex_);

        if (!anyPlaying) {
            profiler.updateThreadState(THREAD_STATE_WAITING);
            // Wait for a signal (e.g., playMusicTrack) or check every 100 ms.
            SDL_LockMutex(self->musicMutex_);
            SDL_WaitConditionTimeout(self->musicCondition_, self->musicMutex_, 100);
            SDL_UnlockMutex(self->musicMutex_);
        } else {
            // Run at ~50 ms intervals while music is playing.
            SDL_Delay(50);
        }
    }

    self->consoleBuffer_->log(SDL_LOG_PRIORITY_DEBUG, "MusicStreamWorker: thread exiting");
    return 0;
}

void AudioManager::streamMusicTracks(float dt) {
    for (int t = 0; t < MAX_MUSIC_TRACKS; t++) {
        MusicTrackState& track = musicTracks_[t];
        if (!track.valid || !track.playing) continue;

        for (int l = 0; l < track.numLayers; l++) {
            MusicLayerStream& layer = track.layers[l];
            if (!layer.active || !layer.glaState.valid) continue;

            // Fade volume toward target.
            if (layer.volume != layer.targetVolume) {
                if (track.fadeRate <= 0.0f) {
                    layer.volume = layer.targetVolume;
                } else {
                    float delta = track.fadeRate * dt;
                    if (layer.volume < layer.targetVolume) {
                        layer.volume += delta;
                        if (layer.volume > layer.targetVolume) layer.volume = layer.targetVolume;
                    } else {
                        layer.volume -= delta;
                        if (layer.volume < layer.targetVolume) layer.volume = layer.targetVolume;
                    }
                }
                alSourcef(layer.source, AL_GAIN, layer.volume);
            }

            // Refill any processed streaming buffers.
            ALint processed = 0;
            alGetSourcei(layer.source, AL_BUFFERS_PROCESSED, &processed);
            while (processed > 0) {
                ALuint buf;
                alSourceUnqueueBuffers(layer.source, 1, &buf);
                int filled = fillStreamBuffer(layer, buf,
                    MUSIC_STREAM_BUFFER_FRAMES,
                    track.loopStartSample, track.loopEndSample);
                if (filled > 0) {
                    alSourceQueueBuffers(layer.source, 1, &buf);
                }
                processed--;
            }

            // If the source stopped (buffer underrun), restart it.
            ALint state = AL_STOPPED;
            alGetSourcei(layer.source, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                ALint queued = 0;
                alGetSourcei(layer.source, AL_BUFFERS_QUEUED, &queued);
                if (queued > 0) {
                    consoleBuffer_->log(SDL_LOG_PRIORITY_WARN,
                        "AudioManager: Music layer %d/%d underrun, restarting", t, l);
                    alSourcePlay(layer.source);
                }
            }
        }

        // If a fade-out stop was requested, release the track once all layers are silent.
        if (track.pendingStop) {
            bool allSilent = true;
            for (int l = 0; l < track.numLayers; l++) {
                if (track.layers[l].active && track.layers[l].volume > 0.0f) {
                    allSilent = false;
                    break;
                }
            }
            if (allSilent) {
                consoleBuffer_->log(SDL_LOG_PRIORITY_INFO,
                    "AudioManager: Music track %d fade-out complete, releasing", t);
                releaseMusicTrack(track);
            }
        }
    }
}
