#include "AudioManager.h"
#include <iostream>
#include <SDL3/SDL.h>
#include "../core/Vector.h"
#include "../debug/ConsoleBuffer.h"
#include <cstring>
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <opusfile.h>

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
      currentEffect(AUDIO_EFFECT_NONE), currentEffectIntensity(1.0f), allocator_(allocator),
      consoleBuffer_(consoleBuffer)
{
    assert(allocator_ != nullptr);
    consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "AudioManager: Using shared memory allocator");

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
consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "OpenAL EFX extension supported");
        initializeEFX();
    } else {
consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "OpenAL EFX extension not supported - effects disabled");
        efxSupported = false;
    }

    // Set default listener properties
    alListener3f(AL_POSITION, 0.0f, 0.0f, 0.0f);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
    ALfloat listenerOri[] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
    alListenerfv(AL_ORIENTATION, listenerOri);
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
consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "EFX initialized successfully");
        } else {
            efxSupported = false;
consoleBuffer_->log(SDL_LOG_PRIORITY_INFO, "EFX initialization failed");
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
        std::cerr << "Unsupported audio format: " << channels << " channels, " << bitsPerSample << " bits" << std::endl;
        assert(false);
        return -1;
    }

    // Generate buffer
    alGenBuffers(1, &buffers[slot].buffer);
    ALenum error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "Failed to generate audio buffer: " << error << std::endl;
        assert(false);
        return -1;
    }

    // Upload data to buffer
    alBufferData(buffers[slot].buffer, format, data, size, sampleRate);
    error = alGetError();
    if (error != AL_NO_ERROR) {
        std::cerr << "Failed to upload audio data: " << error << std::endl;
        alDeleteBuffers(1, &buffers[slot].buffer);
        assert(false);
        return -1;
    }

    buffers[slot].loaded = true;
    bufferCount++;

    return slot;
}

int AudioManager::loadOpusAudioFromMemory(const void* data, size_t size) {
    // Open OPUS file from memory
    int error = 0;
    OggOpusFile* opusFile = op_open_memory((const unsigned char*)data, size, &error);

    if (!opusFile || error != 0) {
        std::cerr << "Failed to open OPUS data from memory, error code: " << error << std::endl;
        assert(false);
        return -1;
    }

    // Get audio info
    const OpusHead* head = op_head(opusFile, -1);
    if (!head) {
consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Failed to get OPUS header");
        assert(false);
        op_free(opusFile);
        return -1;
    }

    int channels = head->channel_count;
    int sampleRate = 48000; // OPUS always decodes to 48kHz

    // Read all audio data
    Vector<opus_int16> pcmData(*allocator_, "AudioManager::playMusic::pcmData");
    const int bufferSize = 5760 * channels; // Max frame size for 120ms at 48kHz
    opus_int16 buffer[bufferSize];

    int samplesRead;
    while ((samplesRead = op_read(opusFile, buffer, bufferSize, nullptr)) > 0) {
        for (int i = 0; i < samplesRead * channels; ++i) {
            pcmData.push_back(buffer[i]);
        }
    }

    if (samplesRead < 0) {
        std::cerr << "Error reading OPUS data: " << samplesRead << std::endl;
        op_free(opusFile);
        assert(false);
        return -1;
    }

    op_free(opusFile);

    if (pcmData.empty()) {
consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "No audio data decoded from OPUS");
        assert(false);
        return -1;
    }

    // Load the PCM data into OpenAL buffer
    int bufferId = loadAudioBufferFromMemory(pcmData.data(), pcmData.size() * sizeof(opus_int16),
                                             sampleRate, channels, 16);

    return bufferId;
}

int AudioManager::createAudioSource(int bufferId, bool looping, float volume) {
    if (bufferId < 0 || bufferId >= MAX_AUDIO_BUFFERS || !buffers[bufferId].loaded) {
        std::cerr << "Invalid buffer ID: " << bufferId << std::endl;
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
        std::cerr << "Failed to generate audio source: " << error << std::endl;
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
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    alSourcePlay(sources[sourceId].source);
}

void AudioManager::stopSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    alSourceStop(sources[sourceId].source);
}

void AudioManager::pauseSource(int sourceId) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    alSourcePause(sources[sourceId].source);
}

void AudioManager::setSourcePosition(int sourceId, float x, float y, float z) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
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
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    alSource3f(sources[sourceId].source, AL_VELOCITY, vx, vy, vz);
}

void AudioManager::setSourceVolume(int sourceId, float volume) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    sources[sourceId].volume = volume;
    alSourcef(sources[sourceId].source, AL_GAIN, volume);
}

void AudioManager::setSourcePitch(int sourceId, float pitch) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
        assert(false);
        return;
    }

    alSourcef(sources[sourceId].source, AL_PITCH, pitch);
}

void AudioManager::setSourceLooping(int sourceId, bool looping) {
    if (sourceId < 0 || sourceId >= MAX_AUDIO_SOURCES || !sources[sourceId].active) {
        std::cerr << "Invalid source ID: " << sourceId << std::endl;
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
