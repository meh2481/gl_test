#include "AudioManager.h"
#include <SDL3/SDL.h>
#include "../core/Vector.h"
#include "../debug/ConsoleBuffer.h"
#include "../debug/ThreadProfiler.h"
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
    consoleBuffer_(consoleBuffer), nextDecodeJobId_(1), decodeWorkerRunning_(true),
    pendingDecodeJob_(nullptr), completedDecodeJob_(nullptr),
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
            musicTracks_[i].layers[j].opusFile = nullptr;
            musicTracks_[i].layers[j].buffersCreated = false;
        }
    }

    // Initialize decode worker thread
    decodeMutex_ = SDL_CreateMutex();
    decodeCondition_ = SDL_CreateCondition();
    if (decodeMutex_ && decodeCondition_) {
        decodeWorkerThread_ = SDL_CreateThread(&AudioManager::audioDecodeWorkerThread, "AudioDecodeWorker", this);
        if (!decodeWorkerThread_) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: Failed to create decode worker thread");
        }
    } else {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: Failed to create decode mutex/condition");
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

    // Shut down decode worker thread
    if (decodeMutex_) {
        SDL_LockMutex(decodeMutex_);
        decodeWorkerRunning_ = false;
        SDL_UnlockMutex(decodeMutex_);
    }
    if (decodeCondition_) {
        SDL_SignalCondition(decodeCondition_);
    }
    if (decodeWorkerThread_) {
        int returnCode;
        SDL_WaitThread(decodeWorkerThread_, &returnCode);
    }

    // Wait for any pending decode job to complete
    if (decodeMutex_ && pendingDecodeJob_) {
        SDL_LockMutex(decodeMutex_);
        while (pendingDecodeJob_ && !pendingDecodeJob_->completed && !pendingDecodeJob_->failed) {
            SDL_UnlockMutex(decodeMutex_);
            SDL_Delay(1);
            SDL_LockMutex(decodeMutex_);
        }
        SDL_UnlockMutex(decodeMutex_);
    }

    if (pendingDecodeJob_) {
        destroyDecodeJob(pendingDecodeJob_);
        pendingDecodeJob_ = nullptr;
    }
    if (completedDecodeJob_) {
        destroyDecodeJob(completedDecodeJob_);
        completedDecodeJob_ = nullptr;
    }

    cleanup();

    // Cleanup decode worker synchronization primitives
    if (decodeCondition_) {
        SDL_DestroyCondition(decodeCondition_);
        decodeCondition_ = nullptr;
    }
    if (decodeMutex_) {
        SDL_DestroyMutex(decodeMutex_);
        decodeMutex_ = nullptr;
    }

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
    // Release all music tracks first (frees OggOpusFile handles and OpenAL sources)
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

int AudioManager::loadOpusAudioFromMemory(const void* data, Uint64 size) {
    // Open OPUS file from memory
    int error = 0;
    OggOpusFile* opusFile = op_open_memory((const unsigned char*)data, size, &error);

    if (!opusFile || error != 0) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Failed to open OPUS data from memory, error code: %d", error);
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
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "Error reading OPUS data: %d", samplesRead);
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
// Audio Decode Worker Thread
// ============================================================================

int AudioManager::audioDecodeWorkerThread(void* arg) {
    AudioManager* self = static_cast<AudioManager*>(arg);
    ThreadProfiler& profiler = ThreadProfiler::instance();
    profiler.registerThread("AudioDecodeWorker");

    while (true) {
        profiler.updateThreadState(THREAD_STATE_WAITING);

        AudioDecodeJob* job = nullptr;

        // Wait for a job
        {
            SDL_LockMutex(self->decodeMutex_);
            while (!self->pendingDecodeJob_ && self->decodeWorkerRunning_) {
                SDL_WaitCondition(self->decodeCondition_, self->decodeMutex_);
            }

            if (!self->decodeWorkerRunning_ && !self->pendingDecodeJob_) {
                SDL_UnlockMutex(self->decodeMutex_);
                break;
            }

            job = self->pendingDecodeJob_;
            SDL_UnlockMutex(self->decodeMutex_);
        }

        if (!job) continue;

        // Process decode job
        profiler.updateThreadState(THREAD_STATE_BUSY);

        // Create OpusFile from memory
        OggOpusFile* opusFile = nullptr;
        int error = 0;
        opusFile = op_open_memory(
            static_cast<const unsigned char*>(job->compressedData),
            job->compressedSize,
            &error
        );

        if (!opusFile) {
            job->failed = true;
            job->completed = true;
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            SDL_UnlockMutex(self->decodeMutex_);
            continue;
        }

        // Get audio info
        const OpusHead* opusInfo = op_head(opusFile, -1);
        if (!opusInfo) {
            op_free(opusFile);
            job->failed = true;
            job->completed = true;
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            SDL_UnlockMutex(self->decodeMutex_);
            continue;
        }

        job->channels = opusInfo->channel_count;
        job->sampleRate = 48000;  // Opus is always 48 kHz

        // Decode the entire file into PCM
        int samplesPerChannel = op_pcm_total(opusFile, -1);
        if (samplesPerChannel <= 0) {
            op_free(opusFile);
            job->failed = true;
            job->completed = true;
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            SDL_UnlockMutex(self->decodeMutex_);
            continue;
        }

        // Allocate PCM buffer (interleaved samples)
        job->decodedSampleCount = static_cast<Uint64>(samplesPerChannel) * static_cast<Uint64>(job->channels);
        job->decodedPcm = static_cast<opus_int16*>(self->allocator_->allocate(job->decodedSampleCount * sizeof(opus_int16), "AudioManager::decodedPcm"));
        if (!job->decodedPcm) {
            op_free(opusFile);
            job->failed = true;
            job->completed = true;
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            SDL_UnlockMutex(self->decodeMutex_);
            continue;
        }
        opus_int16* pcmBuffer = job->decodedPcm;

        // Decode all samples
        int totalSamplesDecoded = 0;
        bool decodeError = false;
        while (totalSamplesDecoded < samplesPerChannel) {
            int samplesRead = op_read(
                opusFile,
                pcmBuffer + totalSamplesDecoded * job->channels,
                (samplesPerChannel - totalSamplesDecoded) * job->channels,
                nullptr
            );

            if (samplesRead <= 0) {
                if (samplesRead < 0) {
                    // Decode error
                    job->failed = true;
                    decodeError = true;
                    break;
                }
                break;  // EOF
            }

            totalSamplesDecoded += samplesRead;
        }

        op_free(opusFile);

        if (decodeError) {
            job->completed = true;
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            SDL_UnlockMutex(self->decodeMutex_);
            continue;
        }

        // Mark as complete
        {
            SDL_LockMutex(self->decodeMutex_);
            if (self->completedDecodeJob_) {
                self->destroyDecodeJob(self->completedDecodeJob_);
            }
            self->completedDecodeJob_ = job;
            self->pendingDecodeJob_ = nullptr;
            job->completed = true;
            SDL_UnlockMutex(self->decodeMutex_);
        }
    }

    return 0;
}

void AudioManager::submitDecodeJob(AudioDecodeJob* job) {
    SDL_LockMutex(decodeMutex_);
    if (pendingDecodeJob_) {
        destroyDecodeJob(pendingDecodeJob_);
    }
    pendingDecodeJob_ = job;
    SDL_UnlockMutex(decodeMutex_);
    SDL_SignalCondition(decodeCondition_);
}

int AudioManager::decodeOpusAudioAsync(const void* data, Uint64 size) {
    AudioDecodeJob* job = static_cast<AudioDecodeJob*>(allocator_->allocate(sizeof(AudioDecodeJob), "AudioManager::AudioDecodeJob"));
    if (!job) {
        return -1;
    }
    job->compressedData = data;
    job->compressedSize = size;
    job->jobId = nextDecodeJobId_++;
    job->decodedPcm = nullptr;
    job->decodedSampleCount = 0;
    job->channels = 0;
    job->sampleRate = 0;
    job->completed = false;
    job->failed = false;

    submitDecodeJob(job);
    return job->jobId;
}

bool AudioManager::getOpusDecodeResult(
    int jobId,
    opus_int16*& outBuffer,
    Uint64& outSampleCount,
    int& outChannels,
    int& outSampleRate
) {
    AudioDecodeJob* job = nullptr;
    ThreadProfiler& profiler = ThreadProfiler::instance();

    // Wait for job to complete
    while (true) {
        SDL_LockMutex(decodeMutex_);
        if (completedDecodeJob_ && completedDecodeJob_->jobId == jobId) {
            job = completedDecodeJob_;
            completedDecodeJob_ = nullptr;
            SDL_UnlockMutex(decodeMutex_);
            profiler.updateThreadState(THREAD_STATE_BUSY);
            break;
        }
        SDL_UnlockMutex(decodeMutex_);

        if (pendingDecodeJob_ && pendingDecodeJob_->jobId == jobId) {
            // Job still pending, wait a bit
            profiler.updateThreadState(THREAD_STATE_IDLE);
            SDL_Delay(1);
        } else {
            // Job not found
            profiler.updateThreadState(THREAD_STATE_BUSY);
            return false;
        }
    }

    if (!job || job->failed) {
        if (job) {
            destroyDecodeJob(job);
        }
        return false;
    }

    outBuffer = job->decodedPcm;
    outSampleCount = job->decodedSampleCount;
    outChannels = job->channels;
    outSampleRate = job->sampleRate;
    job->decodedPcm = nullptr;
    job->decodedSampleCount = 0;
    destroyDecodeJob(job);
    return true;
}

void AudioManager::freeDecodedBuffer(opus_int16* buffer) {
    if (buffer) {
        allocator_->free(buffer);
    }
}

void AudioManager::destroyDecodeJob(AudioDecodeJob* job) {
    if (!job) {
        return;
    }
    if (job->decodedPcm) {
        allocator_->free(job->decodedPcm);
        job->decodedPcm = nullptr;
    }
    allocator_->free(job);
}

// ============================================================================
// Music streaming implementation
// ============================================================================

int AudioManager::loadMusicTrack(
    Uint32 loopStartSample, Uint32 loopEndSample,
    const MusicLayerInitData* uniqueLayers, int numUniqueLayers,
    const MusicIntensityInitData* intensities, int numIntensities)
{
    assert(uniqueLayers != nullptr);
    assert(intensities != nullptr);
    assert(numUniqueLayers > 0 && numUniqueLayers <= MAX_MUSIC_LAYERS_PER_TRACK);
    assert(numIntensities > 0 && numIntensities <= MAX_MUSIC_INTENSITIES);

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

    // Open each unique OPUS layer.
    for (int i = 0; i < numUniqueLayers; i++) {
        MusicLayerStream& layer = track.layers[i];
        layer.active        = false;
        layer.opusFile      = nullptr;
        layer.buffersCreated = false;
        layer.volume        = 0.0f;
        layer.targetVolume  = 0.0f;

        int error = 0;
        layer.opusFile = op_open_memory(
            uniqueLayers[i].data, (size_t)uniqueLayers[i].size, &error);
        if (!layer.opusFile) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                "AudioManager: Failed to open OPUS stream for layer %d of track %d (error %d)",
                i, trackId, error);
            // Release what we've opened so far and bail.
            for (int j = 0; j < i; j++) {
                releaseMusicLayer(track.layers[j]);
            }
            track.valid = false;
            return -1;
        }

        const OpusHead* head = op_head(layer.opusFile, -1);
        layer.channels = head ? head->channel_count : 2;

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
            "AudioManager: Music track %d layer %d opened (%d ch)", trackId, i, layer.channels);
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
        if (!layer.active || !layer.opusFile) continue;

        // Re-seek to start so all layers are synchronised.
        op_pcm_seek(layer.opusFile, 0);

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
    assert(layer.opusFile != nullptr);
    assert(frameCount > 0);

    // Allocate a temporary PCM buffer on the heap (avoid large stack allocation).
    int totalSamples = frameCount * layer.channels;
    opus_int16* pcm = static_cast<opus_int16*>(
        allocator_->allocate((Uint64)totalSamples * sizeof(opus_int16), "fillStreamBuffer::pcm"));
    if (!pcm) {
        consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR, "AudioManager: OOM in fillStreamBuffer");
        return 0;
    }

    int framesWritten = 0;
    while (framesWritten < frameCount) {
        // If a non-zero loopEnd is set, check whether we need to wrap.
        if (loopEnd > 0) {
            ogg_int64_t pos = op_pcm_tell(layer.opusFile);
            if (pos < 0) {
                consoleBuffer_->log(SDL_LOG_PRIORITY_WARN,
                    "AudioManager: op_pcm_tell failed (pos=%lld)", (long long)pos);
                break;
            }
            if ((Uint32)pos >= loopEnd) {
                int err = op_pcm_seek(layer.opusFile, (ogg_int64_t)loopStart);
                if (err != 0) {
                    consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                        "AudioManager: op_pcm_seek to loop start %u failed (err=%d)", loopStart, err);
                    break;
                }
                consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
                    "AudioManager: Music looped back to sample %u", loopStart);
            }
        }

        int remaining = frameCount - framesWritten;
        // If close to loopEnd, only decode up to the boundary so the next
        // iteration handles the wrap correctly.
        if (loopEnd > 0) {
            ogg_int64_t pos = op_pcm_tell(layer.opusFile);
            if (pos >= 0 && (Uint32)pos < loopEnd) {
                int toEnd = (int)((ogg_int64_t)loopEnd - pos);
                if (toEnd < remaining) {
                    remaining = toEnd;
                }
            }
        }

        int read = op_read(layer.opusFile,
                           pcm + framesWritten * layer.channels,
                           remaining * layer.channels,
                           nullptr);
        if (read < 0) {
            consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                "AudioManager: op_read error %d in fillStreamBuffer", read);
            break;
        }
        if (read == 0) {
            // EOF and no loop end set: seek to loopStart for seamless looping.
            int err = op_pcm_seek(layer.opusFile, (ogg_int64_t)loopStart);
            if (err != 0) {
                consoleBuffer_->log(SDL_LOG_PRIORITY_ERROR,
                    "AudioManager: op_pcm_seek to loop start on EOF failed (err=%d)", err);
                break;
            }
            consoleBuffer_->log(SDL_LOG_PRIORITY_TRACE,
                "AudioManager: Music looped (EOF) back to sample %u", loopStart);
            continue;
        }
        framesWritten += read;
    }

    if (framesWritten > 0) {
        ALenum fmt = (layer.channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
        alBufferData(alBuffer, fmt,
                     pcm, framesWritten * layer.channels * (int)sizeof(opus_int16),
                     48000);
    }

    allocator_->free(pcm);
    return framesWritten;
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

    if (layer.opusFile) {
        op_free(layer.opusFile);
        layer.opusFile = nullptr;
    }

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
            if (!layer.active || !layer.opusFile) continue;

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