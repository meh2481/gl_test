#pragma once

#include <SDL2/SDL.h>
#include <cassert>

// Manages controller vibration/rumble effects
class VibrationManager {
public:
    VibrationManager() : gameController_(nullptr) {}

    // Set the game controller to use for vibration
    void setGameController(SDL_GameController* controller) {
        gameController_ = controller;
    }

    // Trigger vibration with specified intensities (0.0 to 1.0)
    // leftIntensity: Low frequency rumble motor (usually left)
    // rightIntensity: High frequency rumble motor (usually right)
    // duration: Duration in milliseconds
    void vibrate(float leftIntensity, float rightIntensity, uint32_t duration) {
        if (!gameController_) {
            return;
        }

        // Clamp intensities to valid range
        leftIntensity = clampFloat(leftIntensity, 0.0f, 1.0f);
        rightIntensity = clampFloat(rightIntensity, 0.0f, 1.0f);

        // Convert to SDL's 0-65535 range
        uint16_t lowFreq = static_cast<uint16_t>(leftIntensity * 65535.0f);
        uint16_t highFreq = static_cast<uint16_t>(rightIntensity * 65535.0f);

        SDL_GameControllerRumble(gameController_, lowFreq, highFreq, duration);
    }

    // Trigger DualSense trigger motor vibration (PS5 controller)
    // leftTrigger: Left trigger intensity (0.0 to 1.0)
    // rightTrigger: Right trigger intensity (0.0 to 1.0)
    // duration: Duration in milliseconds
    // Returns true if trigger rumble is supported and was triggered
    bool vibrateTriggers(float leftTrigger, float rightTrigger, uint32_t duration) {
        if (!gameController_) {
            return false;
        }

        // Clamp intensities to valid range
        leftTrigger = clampFloat(leftTrigger, 0.0f, 1.0f);
        rightTrigger = clampFloat(rightTrigger, 0.0f, 1.0f);

        // Convert to SDL's 0-65535 range
        uint16_t leftFreq = static_cast<uint16_t>(leftTrigger * 65535.0f);
        uint16_t rightFreq = static_cast<uint16_t>(rightTrigger * 65535.0f);

        // SDL_GameControllerRumbleTriggers is available in SDL 2.0.14+
        #if SDL_VERSION_ATLEAST(2, 0, 14)
        int result = SDL_GameControllerRumbleTriggers(gameController_, leftFreq, rightFreq, duration);
        return result == 0;
        #else
        return false;
        #endif
    }

    // Stop all vibration
    void stopVibration() {
        if (!gameController_) {
            return;
        }

        SDL_GameControllerRumble(gameController_, 0, 0, 0);
        
        #if SDL_VERSION_ATLEAST(2, 0, 14)
        SDL_GameControllerRumbleTriggers(gameController_, 0, 0, 0);
        #endif
    }

    // Check if the controller has rumble support
    bool hasRumbleSupport() const {
        if (!gameController_) {
            return false;
        }
        return SDL_GameControllerHasRumble(gameController_) == SDL_TRUE;
    }

    // Check if the controller has trigger rumble support (DualSense)
    bool hasTriggerRumbleSupport() const {
        if (!gameController_) {
            return false;
        }
        #if SDL_VERSION_ATLEAST(2, 0, 14)
        return SDL_GameControllerHasRumbleTriggers(gameController_) == SDL_TRUE;
        #else
        return false;
        #endif
    }

private:
    SDL_GameController* gameController_;

    float clampFloat(float value, float min, float max) const {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }
};
