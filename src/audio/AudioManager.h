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

class MemoryAllocator;
class ConsoleBuffer;

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
};

#endif // AUDIOMANAGER_H
