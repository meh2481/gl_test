#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <AL/al.h>
#include <AL/alc.h>
#include <cassert>
#include <SDL3/SDL.h>
#include <opusfile.h>

// Maximum number of simultaneous audio sources
#define MAX_AUDIO_SOURCES 64

// Maximum number of audio buffers
#define MAX_AUDIO_BUFFERS 256

// Audio effect types
enum AudioEffect {
    AUDIO_EFFECT_NONE = 0,
    AUDIO_EFFECT_LOWPASS,
    AUDIO_EFFECT_REVERB,
    AUDIO_EFFECT_COUNT
};

// Audio source structure
struct AudioSource {
    ALuint source;
    bool active;
    float x, y, z;  // Position in 3D space
    float volume;
    bool looping;
    int bufferId;   // Index into buffer array
};

// Audio buffer structure
struct AudioBuffer {
    ALuint buffer;
    bool loaded;
};

// ============================================================================
// Music streaming constants
// ============================================================================
#define MAX_MUSIC_TRACKS            4    // Max simultaneous music tracks
#define MAX_MUSIC_LAYERS_PER_TRACK  16   // Max unique layers per music track
#define MAX_MUSIC_INTENSITIES       8    // Max intensities per music track
#define MUSIC_STREAM_BUFFERS        3    // OpenAL streaming buffers per layer
#define MUSIC_STREAM_BUFFER_FRAMES  4800 // Decoded frames per streaming buffer (100 ms at 48 kHz)
#define MUSIC_DEFAULT_FADE_DURATION 0.5f // Default fade duration in seconds

// One streamed OPUS layer within a music track
struct MusicLayerStream {
    OggOpusFile* opusFile;                       // Open OPUS handle (nullptr = inactive)
    ALuint       source;                         // Dedicated OpenAL source
    ALuint       buffers[MUSIC_STREAM_BUFFERS];  // Buffer pool
    float        volume;                         // Current rendered volume
    float        targetVolume;                   // Target volume for fade
    int          channels;                       // Channel count from OPUS file
    bool         buffersCreated;                 // True once alGenBuffers succeeded
    bool         active;                         // True if this slot is in use
};

// Per-intensity descriptor within a loaded music track
struct MusicIntensityDesc {
    Uint64 nameHash;                                   // FNV-1a hash of the intensity name
    float  layerVolumes[MAX_MUSIC_LAYERS_PER_TRACK];   // Target volume (1.0 = on, 0.0 = off) for each layer
};

// State for one loaded music track
struct MusicTrackState {
    MusicLayerStream    layers[MAX_MUSIC_LAYERS_PER_TRACK];
    int                 numLayers;
    Uint32              loopStartSample;  // Loop start in OPUS samples (48 kHz)
    Uint32              loopEndSample;    // Loop end in OPUS samples (0 = end of file)
    MusicIntensityDesc  intensities[MAX_MUSIC_INTENSITIES];
    int                 numIntensities;
    int                 currentIntensity; // Index of the active intensity (-1 = none set yet)
    float               fadeRate;         // Current fade speed (volume units per second)
    bool                pendingStop;      // If true, release track once all layers reach volume 0
    bool                playing;
    bool                valid;
};

class MemoryAllocator;
class ConsoleBuffer;

// Data needed to initialise one OPUS layer stream
struct MusicLayerInitData {
    Uint64               resourceId;  // Resource ID of the OPUS file
    const unsigned char* data;        // Pointer into pak buffer (stays valid while pak is loaded)
    Uint64               size;
};

// Data needed to initialise one intensity entry
struct MusicIntensityInitData {
    Uint64        nameHash;
    const Uint64* layerResourceIds;  // Resource IDs of layers active in this intensity
    Uint32        numLayers;
};

class AudioManager {
public:
    AudioManager(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
    ~AudioManager();

    // Initialize the audio system
    void initialize();

    // Cleanup the audio system
    void cleanup();

    // Load audio from file into buffer slot
    // Returns buffer ID on success, -1 on failure
    int loadAudioBuffer(const char* filename);

    // Load audio from memory into buffer slot
    // Returns buffer ID on success, -1 on failure
    int loadAudioBufferFromMemory(const void* data, Uint64 size, int sampleRate, int channels, int bitsPerSample);

    // Load OPUS audio from memory into buffer slot
    // Returns buffer ID on success, -1 on failure
    int loadOpusAudioFromMemory(const void* data, Uint64 size);

    // Create an audio source
    // Returns source ID on success, -1 on failure
    int createAudioSource(int bufferId, bool looping = false, float volume = 1.0f);

    // Play an audio source
    void playSource(int sourceId);

    // Stop an audio source
    void stopSource(int sourceId);

    // Pause an audio source
    void pauseSource(int sourceId);

    // Set source position in 3D space
    void setSourcePosition(int sourceId, float x, float y, float z);

    // Set source velocity (for doppler effect)
    void setSourceVelocity(int sourceId, float vx, float vy, float vz);

    // Set source volume (0.0 to 1.0)
    void setSourceVolume(int sourceId, float volume);

    // Set source pitch (default 1.0)
    void setSourcePitch(int sourceId, float pitch);

    // Set source looping
    void setSourceLooping(int sourceId, bool looping);

    // Release an audio source
    void releaseSource(int sourceId);

    // Check if a source is currently playing
    bool isSourcePlaying(int sourceId);

    // Set listener position in 3D space
    void setListenerPosition(float x, float y, float z);

    // Set listener velocity (for doppler effect)
    void setListenerVelocity(float vx, float vy, float vz);

    // Set listener orientation (at and up vectors)
    void setListenerOrientation(float atX, float atY, float atZ, float upX, float upY, float upZ);

    // Set global volume (0.0 to 1.0)
    void setGlobalVolume(float volume);

    // Set global audio effect
    void setGlobalEffect(AudioEffect effect, float intensity = 1.0f);

    // Update audio system (call once per frame)
    void update();

    // Release all active audio sources (for scene cleanup)
    void clearAllSources();

    // Suspend audio processing (call when app goes to background)
    void suspend();

    // Resume audio processing (call when app returns to foreground)
    void resume();

    // Async Opus decode: queue a decode job and return immediately
    // Returns job ID for tracking
    // Call getOpusDecodeResult() to retrieve decoded PCM
    int decodeOpusAudioAsync(const void* data, Uint64 size);

    // Wait for a decode job to complete and get result
    // Returns ownership of decoded PCM samples in outBuffer (int16)
    // Caller must release the returned buffer via freeDecodedBuffer()
    // Returns channels and sample rate via output parameters
    // Returns true on success, false on error
    bool getOpusDecodeResult(int jobId, opus_int16*& outBuffer, Uint64& outSampleCount, int& outChannels, int& outSampleRate);

    void freeDecodedBuffer(opus_int16* buffer);

    // ========================================================================
    // Music streaming API
    // ========================================================================

    // Load a music track from pre-parsed binary data.
    // uniqueLayers    - array of all unique OPUS layers referenced by any intensity.
    // intensities     - per-intensity descriptor (name hash + which layer resource IDs are active).
    // Returns a track ID (0..MAX_MUSIC_TRACKS-1) on success, -1 on failure.
    int loadMusicTrack(Uint32 loopStartSample, Uint32 loopEndSample,
                       const MusicLayerInitData* uniqueLayers, int numUniqueLayers,
                       const MusicIntensityInitData* intensities, int numIntensities);

    // Start playing a loaded music track.
    // fadeDuration: seconds to fade in from silence (default MUSIC_DEFAULT_FADE_DURATION).
    void playMusicTrack(int trackId, float fadeDuration = MUSIC_DEFAULT_FADE_DURATION);

    // Stop a music track, fading out over fadeDuration seconds before freeing resources.
    // fadeDuration: seconds to fade to silence before stopping (default MUSIC_DEFAULT_FADE_DURATION).
    void stopMusicTrack(int trackId, float fadeDuration = MUSIC_DEFAULT_FADE_DURATION);

    // Switch to a named intensity (by FNV hash of the name string).
    // Layers not in the new intensity fade out; new layers fade in.
    // fadeDuration: seconds for the cross-fade (default MUSIC_DEFAULT_FADE_DURATION).
    void setMusicIntensity(int trackId, Uint64 intensityNameHash, float fadeDuration = MUSIC_DEFAULT_FADE_DURATION);

private:
    ALCdevice* device;
    ALCcontext* context;

    AudioSource sources[MAX_AUDIO_SOURCES];
    AudioBuffer buffers[MAX_AUDIO_BUFFERS];
    int bufferCount;

    // OpenAL EFX extension support
    bool efxSupported;
    ALuint effectSlot;
    ALuint effect;
    ALuint filter;  // Filter for lowpass
    AudioEffect currentEffect;
    float currentEffectIntensity;

    // Helper function to find free source slot
    int findFreeSourceSlot();

    // Helper function to find free buffer slot
    int findFreeBufferSlot();

    // Initialize EFX (effects extension)
    void initializeEFX();

    // Apply current effect settings
    void applyEffect();

    // Apply current global effect to a single OpenAL source
    void applyEffectToSource(ALuint alSource);

    // Memory allocator for temporary allocations
    MemoryAllocator* allocator_;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* consoleBuffer_;

    // Audio decode worker thread infrastructure
    struct AudioDecodeJob {
        int jobId;
        const void* compressedData;  // Pointer to compressed Opus data
        Uint64 compressedSize;
        opus_int16* decodedPcm;  // Output: decoded PCM samples
        Uint64 decodedSampleCount;
        int channels;  // Output: channel count
        int sampleRate;  // Output: sample rate
        bool completed;
        bool failed;
    };

    static int audioDecodeWorkerThread(void* data);
    void submitDecodeJob(AudioDecodeJob* job);
    void destroyDecodeJob(AudioDecodeJob* job);

    SDL_Thread* decodeWorkerThread_;
    SDL_Mutex* decodeMutex_;
    SDL_Condition* decodeCondition_;
    bool decodeWorkerRunning_;
    int nextDecodeJobId_;
    AudioDecodeJob* pendingDecodeJob_;  // Single job queue
    AudioDecodeJob* completedDecodeJob_;  // Result holder

    // ========================================================================
    // Music streaming internals
    // ========================================================================

    MusicTrackState musicTracks_[MAX_MUSIC_TRACKS];

    SDL_Thread*    musicWorkerThread_;
    SDL_Mutex*     musicMutex_;
    SDL_Condition* musicCondition_;
    bool           musicWorkerRunning_;

    // Worker thread entry point
    static int musicStreamWorkerThread(void* data);

    // Stream all active tracks; called from worker thread.
    // dt is elapsed time in seconds since the last call.
    void streamMusicTracks(float dt);

    // Fill one OpenAL buffer with up to frameCount decoded PCM frames from
    // the given layer, handling loop wrap-around transparently.
    // Returns the number of stereo/mono frames written (may be 0 on error).
    int fillStreamBuffer(MusicLayerStream& layer, ALuint alBuffer,
                         int frameCount, Uint32 loopStartSample, Uint32 loopEndSample);

    // Release all OpenAL and opusfile resources held by a single layer.
    void releaseMusicLayer(MusicLayerStream& layer);

    // Release all resources for one track slot and mark it invalid.
    void releaseMusicTrack(MusicTrackState& track);
};

#endif // AUDIOMANAGER_H
