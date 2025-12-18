#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <AL/al.h>
#include <AL/alc.h>
#include <cstdint>
#include <cassert>
#include <cstddef>

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
#ifdef DEBUG
class ConsoleBuffer;
#endif

class AudioManager {
public:
#ifdef DEBUG
    AudioManager(MemoryAllocator* allocator, ConsoleBuffer* consoleBuffer);
#else
    AudioManager(MemoryAllocator* allocator);
#endif
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
    int loadAudioBufferFromMemory(const void* data, size_t size, int sampleRate, int channels, int bitsPerSample);

    // Load OPUS audio from memory into buffer slot
    // Returns buffer ID on success, -1 on failure
    int loadOpusAudioFromMemory(const void* data, size_t size);

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

#ifdef DEBUG
    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* consoleBuffer_;
#endif
};

#endif // AUDIOMANAGER_H
