#pragma once

#include <SDL3/SDL_stdinc.h>
#include <lua.hpp>
#include "../memory/MemoryAllocator.h"
#include "../core/Vector.h"
#include "../core/ResourceTypes.h"
#include "../text/TextLayer.h"
#include "SceneLayer.h"

class FontManager;
class VulkanRenderer;
class AudioManager;
class ConsoleBuffer;
class PakResource;

// ============================================================================
// DialogueManager — binary-driven dialogue system
//
// Usage (Lua):
//   local dlg = createDialogueBox({ font=fh, x=100, y=500, width=600, textSize=24 })
//   -- Optional drop shadow:
//   -- textShadowDx=2, textShadowDy=-2, textShadowR=0, textShadowG=0, textShadowB=0, textShadowA=0.7
//   dialogueLoad(dlg, "res/dialogue/intro.dlg")
//   dialogueStart(dlg, function() print("done") end)
//   dialogueAdvance(dlg)             -- on player input
//   destroyDialogueBox(dlg)
// ============================================================================

// (charIndex, pauseDuration) pair used for [pause=N] pre-processing.
struct PausePoint {
    int   charIndex;
    float duration;
};

// Runtime portrait entry resolved from a CharacterBinaryHeader.
struct CharacterPortrait {
    char   tag[CHARACTER_MAX_TAG];
    Uint64 textureId;   // loaded texture resource ID (0 if not yet loaded)
};

// Runtime character definition loaded from a RESOURCE_TYPE_CHARACTER resource.
struct CharacterDef {
    static const int MAX_PORTRAITS = 16;

    char              speakerName[CHARACTER_MAX_NAME];
    Uint32            nameColor;          // RGBA packed
    float             revealSpeed;        // 0 = inherit from box
    char              revealSoundPath[CHARACTER_MAX_PATH];
    int               numPortraits;
    CharacterPortrait portraits[MAX_PORTRAITS];
};

// One line of dialogue loaded from a binary dialogue resource.
struct DialogueLine {
    Uint64 characterId;                            // resource ID of character def (0 = anonymous)
    char   portraitTag[DIALOGUE_PORTRAIT_TAG_LEN]; // which portrait to show
    Uint8  portraitSide;                           // 0 = left, 1 = right
    char   text[DIALOGUE_MAX_TEXT];                // localised body text (may contain markup)
    char   revealSoundPath[DIALOGUE_MAX_SHORT];    // override; empty = use character default
    char   voicePath[DIALOGUE_MAX_SHORT];          // optional voiced line audio path
    float  revealSpeed;                            // chars per second (0 = inherit from box/character)
};

// Dialogue box configuration.
struct DialogueBoxConfig {
    int   fontHandle;           // body text font
    int   boldFontHandle;       // for [font=bold] markup, -1 if none
    int   italicFontHandle;     // for [font=italic] markup, -1 if none
    float x, y;                 // world-space position of the text area
    float width;                // wrap width (0 = no wrap)
    float height;               // informational; not currently enforced
    float textSize;             // point size for body text
    float speakerTextSize;      // point size for speaker name (0 = same as textSize)
    float defaultRevealSpeed;   // chars/sec when line doesn't specify; 0 = instant
    float textShadowDx;         // body text drop-shadow X offset (world units)
    float textShadowDy;         // body text drop-shadow Y offset (world units)
    float textShadowR;          // body text drop-shadow colour
    float textShadowG;
    float textShadowB;
    float textShadowA;          // <= 0 disables drop shadow
    float portraitWidth;        // portrait sprite width in world units (0 = no portrait)
    float portraitHeight;       // portrait sprite height in world units
    float transitionDuration;   // inter-line portrait crossfade duration (seconds)
    int   portraitPipelineId;   // pipeline used to render portrait sprites (-1 = none)
};

class DialogueManager {
public:
    DialogueManager(MemoryAllocator*    allocator,
                    FontManager*        fontManager,
                    VulkanRenderer*     renderer,
                    AudioManager*       audioManager,
                    ConsoleBuffer*      console,
                    PakResource*        pakResource,
                    SceneLayerManager*  layerManager);
    ~DialogueManager();

    // Configure the dialogue box parameters.
    void configure(const DialogueBoxConfig& cfg);

    // Load dialogue lines from a binary dialogue resource in the pak.
    // resourcePath is the path used to compute the resource ID.
    // Returns true on success.
    bool loadDialogue(const char* resourcePath);

    // Start the dialogue from line 0.
    // onComplete: Lua callback (LUA_NOREF = none), called when last line is dismissed.
    void start(lua_State* L, int onCompleteRef, Uint64 sceneId);

    // Advance: if still revealing → jump to full reveal;
    //          if fully revealed → show next line or fire onComplete.
    void advance(Uint64 sceneId);

    // Enable/disable autoplay. When enabled, lines auto-advance after delaySeconds
    // once a line has fully revealed.
    void setAutoplay(bool enabled, float delaySeconds);

    // Enable/disable skip. When enabled, reveal runs at 100× speed and lines
    // advance immediately with no inter-line pause. Mutually exclusive with autoplay.
    void setSkip(bool enabled);

    // Per-frame update.
    void update(float dt, Uint64 sceneId);

    // Set the portrait backdrop SceneLayer (textureId already loaded, pipelineId already created).
    void setBackdrop(Uint64 textureId, int pipelineId);

    // --- Getters ---
    bool  isRevealing()      const;
    int   getCurrentLine()   const { return currentLine_; }
    int   getTotalLines()    const { return (int)lines_.size(); }
    bool  isActive()         const { return state_ != STATE_IDLE; }

    // Destroy all owned TextLayer glyph layers and SceneLayers (call on scene cleanup).
    void destroyLayers();

private:
    enum State {
        STATE_IDLE,
        STATE_REVEALING,
        STATE_WAITING_ADVANCE,  // reveal complete, waiting for advance()
        STATE_TRANSITIONING,    // inter-line portrait crossfade
    };

    // Load (or retrieve cached) CharacterDef for the given resource ID.
    // Returns nullptr if characterId == 0 or resource is missing.
    const CharacterDef* loadCharacter(Uint64 characterId);

    void showLine(int lineIndex, Uint64 sceneId);
    void fireOnComplete();

    // Resolve the portrait texture for the given character + tag.
    // Returns 0 if not found.
    Uint64 resolvePortrait(const CharacterDef* charDef, const char* tag);

    MemoryAllocator*   allocator_;
    FontManager*       fontManager_;
    VulkanRenderer*    renderer_;
    AudioManager*      audioManager_;
    ConsoleBuffer*     console_;
    PakResource*       pakResource_;
    SceneLayerManager* layerManager_;

    DialogueBoxConfig cfg_;
    Vector<DialogueLine> lines_;

    // Character definition cache (up to a small fixed number of characters).
    static const int MAX_CHAR_CACHE = 8;
    struct CharCache {
        Uint64      id;
        CharacterDef def;
        int         revealAudioBufferId; // -1 = not yet loaded
    };
    CharCache charCache_[MAX_CHAR_CACHE];
    int       charCacheCount_;

    // Owned TextLayers (created on start(), destroyed on destroyLayers())
    TextLayer* bodyText_;
    TextLayer* speakerText_;

    // Owned SceneLayers (portrait + backdrop)
    int portraitLayerId_;      // -1 = none
    int backdropLayerId_;      // -1 = none
    int portraitPipelineId_;   // pipeline used for portrait sprite
    Uint64 activePortraitTex_; // currently displayed portrait texture

    State  state_;
    int    currentLine_;

    int    lastRevealCount_;       // to detect new characters revealed
    int    revealSoundSourceId_;   // -1 = none (set from character def at showLine time)

    // [pause=N] pre-processed pause points for the current line.
    static const int MAX_PAUSE_POINTS = 32;
    PausePoint pausePoints_[MAX_PAUSE_POINTS];
    int        numPausePoints_;
    float      pauseTimer_;        // countdown; >0 = paused

    // When a [pause=N] charIndex is reached we freeze reveal and wait for
    // all prior fade-ins to finish before starting the actual pauseTimer_.
    bool  pauseAnimWaiting_;   // true = waiting for fade-in to finish
    int   pauseWaitCharIndex_; // which charIndex we're waiting for
    float pauseWaitDuration_;  // duration to apply once anim is done

    // TRANSITIONING state
    float transitionTimer_;
    // Next line to show after transition completes.
    int   transitionTargetLine_;

    bool  autoplayEnabled_;
    float autoplayDelay_;
    float autoplayTimer_;

    bool  skipEnabled_;

    lua_State* lua_;
    int        onCompleteRef_;     // Lua registry ref
};
