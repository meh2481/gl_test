#pragma once

#include <SDL3/SDL_stdinc.h>
#include <lua.hpp>
#include "../memory/MemoryAllocator.h"
#include "../core/Vector.h"
#include "../text/TextLayer.h"

class FontManager;
class VulkanRenderer;
class AudioManager;
class ConsoleBuffer;
class PakResource;

// ============================================================================
// DialogueManager — M7: binary-driven dialogue system
//
// Usage (Lua):
//   local dlg = createDialogueBox({ font=fh, x=100, y=500, width=600, textSize=24 })
//   dialogueLoad(dlg, "dialogue/intro")
//   dialogueStart(dlg, function() print("done") end)
//   dialogueAdvance(dlg)         -- on player input
//   destroyDialogueBox(dlg)
// ============================================================================

// One line of dialogue loaded from a binary dialogue resource.
struct DialogueLine {
    static const int MAX_TEXT   = 1024;
    static const int MAX_SHORT  = 256;

    char  speaker[MAX_SHORT];          // speaker name (may be empty)
    char  text[MAX_TEXT];              // body text (may contain markup)
    char  portraitPath[MAX_SHORT];     // optional portrait resource path
    char  revealSoundPath[MAX_SHORT];  // optional per-char reveal sound path
    char  voicePath[MAX_SHORT];        // optional voiced line audio path
    float revealSpeed;                 // chars per second (0 = inherit from box)
};

// Dialogue box configuration.
struct DialogueBoxConfig {
    int   fontHandle;        // body text font
    int   boldFontHandle;    // for [font=bold] markup (M6), -1 if none
    int   italicFontHandle;  // for [font=italic] markup, -1 if none
    float x, y;              // world-space position of the text area
    float width;             // wrap width (0 = no wrap)
    float height;            // informational; not currently enforced
    float textSize;          // point size for body text
    float speakerTextSize;   // point size for speaker name (0 = same as textSize)
    float defaultRevealSpeed; // chars/sec when line doesn't specify; 0 = instant
};

class DialogueManager {
public:
    DialogueManager(MemoryAllocator* allocator,
                    FontManager*     fontManager,
                    VulkanRenderer*  renderer,
                    AudioManager*    audioManager,
                    ConsoleBuffer*   console,
                    PakResource*     pakResource);
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

    // Per-frame update.
    void update(float dt, Uint64 sceneId);

    // Set the source ID (from audioCreateSource) used for per-char reveal sounds.
    void setRevealSoundSourceId(int sourceId);

    // --- Getters ---
    bool  isRevealing()      const;
    int   getCurrentLine()   const { return currentLine_; }
    int   getTotalLines()    const { return (int)lines_.size(); }
    bool  isActive()         const { return state_ != STATE_IDLE; }

    // Destroy all owned TextLayer glyph layers (call on scene cleanup).
    void destroyLayers();

private:
    enum State {
        STATE_IDLE,
        STATE_REVEALING,
        STATE_WAITING,   // reveal complete, waiting for advance()
    };

    void showLine(int lineIndex, Uint64 sceneId);
    void fireOnComplete();

    MemoryAllocator* allocator_;
    FontManager*     fontManager_;
    VulkanRenderer*  renderer_;
    AudioManager*    audioManager_;
    ConsoleBuffer*   console_;
    PakResource*     pakResource_;

    DialogueBoxConfig cfg_;
    Vector<DialogueLine> lines_;

    // Owned TextLayers (created on start(), destroyed on destroyLayers())
    TextLayer* bodyText_;
    TextLayer* speakerText_;

    State  state_;
    int    currentLine_;

    int    lastRevealCount_;       // to detect new characters revealed
    int    revealSoundSourceId_;   // -1 = none

    lua_State* lua_;
    int        onCompleteRef_;     // Lua registry ref
};
