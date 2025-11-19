#include "AudioManager.h"
#include <iostream>
#include <cstring>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

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

AudioManager::AudioManager()
    : device(nullptr), context(nullptr), bufferCount(0),
      efxSupported(false), effectSlot(0), effect(0),
      currentEffect(AUDIO_EFFECT_NONE), currentEffectIntensity(1.0f) {

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
}

AudioManager::~AudioManager() {
    cleanup();
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

    // Check for EFX support
    if (alcIsExtensionPresent(device, "ALC_EXT_EFX")) {
        std::cout << "OpenAL EFX extension supported" << std::endl;
        initializeEFX();
    } else {
        std::cout << "OpenAL EFX extension not supported - effects disabled" << std::endl;
        efxSupported = false;
    }

    // Set default listener properties
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat listenerOri[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, listenerOri);

    std::cout << "Audio system initialized" << std::endl;
}

void AudioManager::cleanup() {
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

    std::cout << "Audio system cleaned up" << std::endl;
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

    if (alGenEffects && alGenAuxiliaryEffectSlots) {
        // Create effect slot
        alGenAuxiliaryEffectSlots(1, &effectSlot);

        // Create effect
        alGenEffects(1, &effect);

        // Check for errors
        ALenum error = alGetError();
        if (error == AL_NO_ERROR) {
            efxSupported = true;
            std::cout << "EFX initialized successfully" << std::endl;
        } else {
            efxSupported = false;
            std::cout << "EFX initialization failed" << std::endl;
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

int AudioManager::loadAudioBufferFromMemory(const void* data, size_t size, int sampleRate, int channels, int bitsPerSample) {
    int slot = findFreeBufferSlot();
    if (slot == -1) {
        std::cerr << "No free buffer slots available" << std::endl;
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
        std::cerr << "Unsupported audio format: " << channels << " channels, " << bitsPerSample << " bits" << std::endl;
        return -1;
    }

    // Generate buffer
    alGenBuffers(1, &buffers[slot].buffer);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "Failed to generate audio buffer: " << error << std::endl;
        return -1;
    }

    // Upload data to buffer
    alBufferData(buffers[slot].buffer, format, data, size, sampleRate);
    error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "Failed to upload audio data: " << error << std::endl;
        alDeleteBuffers(1, &buffers[slot].buffer);
        return -1;
    }

    buffers[slot].loaded = true;
    bufferCount++;

    return slot;
}

int AudioManager::loadAudioBuffer(const char* filename) {
    // TODO: Implement file loading
    // For now, this is a placeholder
    std::cerr << "loadAudioBuffer from file not yet implemented" << std::endl;
    return -1;
}

int AudioManager::createAudioSource(int bufferId, bool looping, float volume) {
    if (bufferId < 0 || bufferId >= MAX_AUDIO_BUFFERS || !buffers[bufferId].loaded) {
        std::cerr << "Invalid buffer ID: " << bufferId << std::endl;
        return -1;
    }

    int slot = findFreeSourceSlot();
    if (slot == -1) {
        std::cerr << "No free source slots available" << std::endl;
        return -1;
    }

    // Generate source
    alGenSources(1, &sources[slot].source);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "Failed to generate audio source: " << error << std::endl;
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

    return slot;
}

void AudioManager::playSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSourcePlay(sources[sourceId].source);
}

void AudioManager::stopSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSourceStop(sources[sourceId].source);
}

void AudioManager::pauseSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSourcePause(sources[sourceId].source);
}

void AudioManager::setSourcePosition(int sourceId, float x, float y, float z) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    sources[sourceId].x = x;
    sources[sourceId].y = y;
    sources[sourceId].z = z;
    alSource3f(sources[sourceId].source, AL_POSITION, x, y, z);
}

void AudioManager::setSourceVelocity(int sourceId, float vx, float vy, float vz) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSource3f(sources[sourceId].source, AL_VELOCITY, vx, vy, vz);
}

void AudioManager::setSourceVolume(int sourceId, float volume) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    sources[sourceId].volume = volume;
    alSourcef(sources[sourceId].source, AL_GAIN, volume);
}

void AudioManager::setSourcePitch(int sourceId, float pitch) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSourcef(sources[sourceId].source, AL_PITCH, pitch);
}

void AudioManager::setSourceLooping(int sourceId, bool looping) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    sources[sourceId].looping = looping;
    alSourcei(sources[sourceId].source, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
}

void AudioManager::releaseSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        return;
    }

    alSourceStop(sources[sourceId].source);
    alDeleteSources(1, &sources[sourceId].source);
    sources[sourceId].active = false;
    sources[sourceId].source = 0;
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
        std::cerr << "EFX not supported, cannot set global effect" << std::endl;
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
            // Configure lowpass filter
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_LOWPASS);
            alEffectf(effect, AL_LOWPASS_GAIN, currentEffectIntensity);
            alEffectf(effect, AL_LOWPASS_GAINHF, 0.5f * currentEffectIntensity);
            break;

        case AUDIO_EFFECT_REVERB:
            // Configure reverb effect
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            alEffectf(effect, AL_REVERB_GAIN, currentEffectIntensity);
            alEffectf(effect, AL_REVERB_DECAY_TIME, 1.5f);
            break;

        case AUDIO_EFFECT_NONE:
        default:
            // Disable effect
            alEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_NULL);
            break;
    }

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
}

void AudioManager::update() {
    // Update logic (e.g., cleanup finished sources)
    // For now, this is a placeholder for future enhancements
}
