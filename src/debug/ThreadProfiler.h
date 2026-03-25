#pragma once

#include <cstdint>
#include <SDL3/SDL.h>
#include "../memory/SmallMemoryAllocator.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"

enum ThreadState {
    THREAD_STATE_BUSY = 0,
    THREAD_STATE_WAITING = 1,
    THREAD_STATE_IDLE = 2,
    THREAD_STATE_COUNT = 3
};

struct ThreadFrameStats {
    uint64_t stateTime[THREAD_STATE_COUNT]; // Time in each state (nanoseconds)
    uint64_t frameNumber;
};

struct ThreadStats {
    SDL_ThreadID threadId;
    char threadName[64];

    // Current frame tracking
    uint64_t currentFrameStartTime;
    ThreadState currentState;
    uint64_t stateStartTime; // When we entered current state

    // Accumulated time for current frame (in nanoseconds)
    uint64_t currentFrameStateTime[THREAD_STATE_COUNT];
    uint64_t currentFrameNumber;

    // Rolling buffer (last 60 frames)
    static const int HISTORY_SIZE = 60;
    ThreadFrameStats history[HISTORY_SIZE];
    int historyIndex;

    // Thread-safe updates
    SDL_Mutex* statsMutex;

    ThreadStats()
        : threadId(0), currentState(THREAD_STATE_IDLE),
          stateStartTime(0), currentFrameNumber(0), historyIndex(0), statsMutex(nullptr) {
        threadName[0] = '\0';
        for (int i = 0; i < THREAD_STATE_COUNT; ++i) {
            currentFrameStateTime[i] = 0;
        }
        for (int i = 0; i < HISTORY_SIZE; ++i) {
            for (int j = 0; j < THREAD_STATE_COUNT; ++j) {
                history[i].stateTime[j] = 0;
            }
            history[i].frameNumber = 0;
        }
    }
};

class ThreadProfiler {
public:
    static ThreadProfiler& instance() {
        static ThreadProfiler inst;
        return inst;
    }

    void initialize(MemoryAllocator* allocator) {
        if (allocator_ != nullptr) {
            return;
        }
        allocator_ = allocator;
        threads_ = new HashTable<SDL_ThreadID, ThreadStats*>(*allocator_, "ThreadProfiler::threads");
        globalMutex_ = SDL_CreateMutex();
        frameNumber_ = 0;
    }

    void shutdown() {
        if (allocator_ == nullptr) {
            return;
        }

        if (globalMutex_) {
            SDL_DestroyMutex(globalMutex_);
            globalMutex_ = nullptr;
        }
        for (auto it = threads_->begin(); it != threads_->end(); ++it) {
            ThreadStats* stats = it.value();
            if (stats && stats->statsMutex) {
                SDL_DestroyMutex(stats->statsMutex);
            }
            if (stats) {
                allocator_->free(stats);
            }
        }
        delete threads_;
        threads_ = nullptr;
        allocator_ = nullptr;
        frameNumber_ = 0;
    }

    // Register a thread with a human-readable name
    void registerThread(const char* threadName) {
        SDL_ThreadID threadId = SDL_GetCurrentThreadID();

        SDL_LockMutex(globalMutex_);

        if (!threads_->contains(threadId)) {
            ThreadStats* stats = (ThreadStats*)allocator_->allocate(sizeof(ThreadStats), "ThreadProfiler::ThreadStats");
            new (stats) ThreadStats();
            stats->threadId = threadId;
            stats->currentFrameNumber = frameNumber_;
            stats->currentFrameStartTime = SDL_GetTicksNS();
            stats->stateStartTime = stats->currentFrameStartTime;
            stats->statsMutex = SDL_CreateMutex();

            if (threadName) {
                SDL_strlcpy(stats->threadName, threadName, sizeof(stats->threadName));
            } else {
                SDL_snprintf(stats->threadName, sizeof(stats->threadName), "Thread-%llu", (unsigned long long)threadId);
            }

            threads_->insert(threadId, stats);
        }

        SDL_UnlockMutex(globalMutex_);
    }

    // Update thread state (call from within the thread)
    void updateThreadState(ThreadState newState) {
        SDL_ThreadID threadId = SDL_GetCurrentThreadID();
        uint64_t currentTime = SDL_GetTicksNS();

        SDL_LockMutex(globalMutex_);
        ThreadStats** statsPtr = threads_->find(threadId);
        SDL_UnlockMutex(globalMutex_);

        if (statsPtr == nullptr || *statsPtr == nullptr) {
            return; // Thread not registered
        }

        ThreadStats* stats = *statsPtr;
        SDL_LockMutex(stats->statsMutex);

        // Accumulate time in previous state
        uint64_t timeInState = currentTime - stats->stateStartTime;
        stats->currentFrameStateTime[stats->currentState] += timeInState;

        // Update state and reset timer
        stats->currentState = newState;
        stats->stateStartTime = currentTime;

        SDL_UnlockMutex(stats->statsMutex);
    }

    // Call once per frame to finalize frame data
    void endFrame() {
        uint64_t currentTime = SDL_GetTicksNS();

        SDL_LockMutex(globalMutex_);
        ++frameNumber_;

        for (auto it = threads_->begin(); it != threads_->end(); ++it) {
            ThreadStats* stats = it.value();
            SDL_LockMutex(stats->statsMutex);

            // Finalize current frame
            uint64_t timeInState = currentTime - stats->stateStartTime;
            stats->currentFrameStateTime[stats->currentState] += timeInState;

            // Store to history
            int histIdx = stats->historyIndex;
            for (int i = 0; i < THREAD_STATE_COUNT; ++i) {
                stats->history[histIdx].stateTime[i] = stats->currentFrameStateTime[i];
            }
            stats->history[histIdx].frameNumber = frameNumber_;

            // Prepare for next frame
            stats->historyIndex = (stats->historyIndex + 1) % ThreadStats::HISTORY_SIZE;
            for (int i = 0; i < THREAD_STATE_COUNT; ++i) {
                stats->currentFrameStateTime[i] = 0;
            }
            stats->currentFrameNumber = frameNumber_;
            stats->stateStartTime = currentTime;

            SDL_UnlockMutex(stats->statsMutex);
        }

        SDL_UnlockMutex(globalMutex_);
    }

    // Query thread statistics (thread-safe)
    bool getThreadStats(SDL_ThreadID threadId, ThreadStats* outStats) {
        SDL_LockMutex(globalMutex_);
        ThreadStats** statsPtr = threads_->find(threadId);
        if (statsPtr == nullptr || *statsPtr == nullptr) {
            SDL_UnlockMutex(globalMutex_);
            return false;
        }

        ThreadStats* stats = *statsPtr;
        SDL_LockMutex(stats->statsMutex);

        *outStats = *stats; // Copy entire stats

        SDL_UnlockMutex(stats->statsMutex);
        SDL_UnlockMutex(globalMutex_);

        return true;
    }

    // Get list of all thread IDs
    Vector<SDL_ThreadID> getAllThreadIds() {
        Vector<SDL_ThreadID> ids(*allocator_, "ThreadProfiler::getAllThreadIds");

        SDL_LockMutex(globalMutex_);
        for (auto it = threads_->begin(); it != threads_->end(); ++it) {
            ids.push_back(it.key());
        }
        SDL_UnlockMutex(globalMutex_);

        return ids;
    }

    uint64_t getFrameNumber() const {
        return frameNumber_;
    }

private:
    ThreadProfiler() : allocator_(nullptr), threads_(nullptr), globalMutex_(nullptr), frameNumber_(0) {}
    ~ThreadProfiler() {}

    MemoryAllocator* allocator_;
    HashTable<SDL_ThreadID, ThreadStats*>* threads_;
    SDL_Mutex* globalMutex_;
    uint64_t frameNumber_;
};
