#include "DialogueManager.h"
#include "../audio/AudioManager.h"
#include "../vulkan/VulkanRenderer.h"
#include "../text/FontManager.h"
#include "../resources/resource.h"
#include "../core/hash.h"
#include "../debug/ConsoleBuffer.h"
#include <SDL3/SDL.h>
#include <cassert>
#include <cstring>

// ============================================================================
// DialogueManager implementation
// ============================================================================

DialogueManager::DialogueManager(MemoryAllocator*   allocator,
                                 FontManager*       fontManager,
                                 VulkanRenderer*    renderer,
                                 AudioManager*      audioManager,
                                 ConsoleBuffer*     console,
                                 PakResource*       pakResource,
                                 SceneLayerManager* layerManager)
    : allocator_(allocator)
    , fontManager_(fontManager)
    , renderer_(renderer)
    , audioManager_(audioManager)
    , console_(console)
    , pakResource_(pakResource)
    , layerManager_(layerManager)
    , lines_(*allocator, "DialogueManager::lines_")
    , charCacheCount_(0)
    , bodyText_(nullptr)
    , speakerText_(nullptr)
    , portraitLayerId_(-1)
    , backdropLayerId_(-1)
    , portraitPipelineId_(-1)
    , activePortraitTex_(0)
    , state_(STATE_IDLE)
    , currentLine_(-1)
    , lastRevealCount_(0)
    , revealSoundSourceId_(-1)
    , numPausePoints_(0)
    , pauseTimer_(0.0f)
    , transitionTimer_(0.0f)
    , transitionTargetLine_(-1)
    , lua_(nullptr)
    , onCompleteRef_(LUA_NOREF)
{
    cfg_ = {};
    cfg_.defaultRevealSpeed  = 20.0f;
    cfg_.textSize            = 24.0f;
    cfg_.speakerTextSize     = 0.0f;
    cfg_.boldFontHandle      = -1;
    cfg_.italicFontHandle    = -1;
    cfg_.transitionDuration  = 0.2f;
    pendingLanguage_[0]      = '\0';
    SDL_memset(charCache_, 0, sizeof(charCache_));
}

DialogueManager::~DialogueManager() {
    destroyLayers();
    if (lua_ && onCompleteRef_ != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, onCompleteRef_);
        onCompleteRef_ = LUA_NOREF;
    }
}

void DialogueManager::configure(const DialogueBoxConfig& cfg) {
    cfg_ = cfg;
    portraitPipelineId_ = cfg.portraitPipelineId;
}

void DialogueManager::setLanguage(const char* isoCode) {
    if (isoCode && isoCode[0]) {
        SDL_strlcpy(pendingLanguage_, isoCode, DIALOGUE_LANG_CODE_LEN);
    } else {
        pendingLanguage_[0] = '\0';
    }
}

bool DialogueManager::loadDialogue(const char* resourcePath) {
    lines_.clear();

    Uint64 resId = hashCString(resourcePath);
    pakResource_->requestResourceAsync(resId);
    ResourceData resData{nullptr, 0, 0};
    if (!pakResource_->tryGetResource(resId, resData) || !resData.data || resData.size == 0) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: failed to load '%s'", resourcePath);
        return false;
    }

    if (resData.size < sizeof(DialogueBinaryHeader)) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: resource too small '%s'", resourcePath);
        return false;
    }

    const DialogueBinaryHeader* hdr =
        reinterpret_cast<const DialogueBinaryHeader*>(resData.data);

    if (hdr->lineCount == 0 || hdr->languageCount == 0) {
        console_->log(SDL_LOG_PRIORITY_WARN, "DialogueManager: no lines in '%s'", resourcePath);
        return false;
    }

    // Validate minimum resource size: header + language table + at least language 0 lines.
    Uint64 headerBytes  = sizeof(DialogueBinaryHeader);
    Uint64 langBytes    = (Uint64)hdr->languageCount * sizeof(DialogueLanguageEntry);
    Uint64 allLinesBytes = (Uint64)hdr->lineCount * hdr->languageCount * sizeof(DialogueLineRecord);

    if (resData.size < headerBytes + langBytes + allLinesBytes) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: truncated data in '%s'", resourcePath);
        return false;
    }

    // Read language table.
    const DialogueLanguageEntry* langTable =
        reinterpret_cast<const DialogueLanguageEntry*>(resData.data + headerBytes);

    // Determine which language index to load.
    Uint32 langIndex = 0;
    if (pendingLanguage_[0] != '\0') {
        for (Uint32 i = 0; i < hdr->languageCount; i++) {
            if (SDL_strcmp(langTable[i].code, pendingLanguage_) == 0) {
                langIndex = i;
                break;
            }
        }
        // If not found, langIndex stays 0 (fallback).
    }

    // Records are ordered language-major:
    //   records[langIndex * lineCount .. langIndex * lineCount + lineCount - 1]
    const DialogueLineRecord* allRecords =
        reinterpret_cast<const DialogueLineRecord*>(resData.data + headerBytes + langBytes);
    const DialogueLineRecord* records = allRecords + langIndex * hdr->lineCount;

    lines_.reserve(hdr->lineCount);
    for (Uint32 i = 0; i < hdr->lineCount; i++) {
        DialogueLine line;
        SDL_memset(&line, 0, sizeof(line));
        line.characterId = records[i].characterId;
        SDL_strlcpy(line.portraitTag,    records[i].portraitTag,    DIALOGUE_PORTRAIT_TAG_LEN);
        line.portraitSide = records[i].portraitSide;
        SDL_strlcpy(line.text,           records[i].text,           DIALOGUE_MAX_TEXT);
        SDL_strlcpy(line.revealSoundPath,records[i].revealSoundPath,DIALOGUE_MAX_SHORT);
        SDL_strlcpy(line.voicePath,      records[i].voicePath,      DIALOGUE_MAX_SHORT);
        line.revealSpeed = records[i].revealSpeed;
        lines_.push_back(line);
    }

    console_->log(SDL_LOG_PRIORITY_INFO,
        "DialogueManager: loaded %d lines (lang[%d]) from '%s'",
        (int)lines_.size(), (int)langIndex, resourcePath);
    return true;
}

// ---------------------------------------------------------------------------
// Character definition loading
// ---------------------------------------------------------------------------

const CharacterDef* DialogueManager::loadCharacter(Uint64 characterId) {
    if (characterId == 0) return nullptr;

    // Check cache.
    for (int i = 0; i < charCacheCount_; i++) {
        if (charCache_[i].id == characterId) {
            return &charCache_[i].def;
        }
    }

    if (charCacheCount_ >= MAX_CHAR_CACHE) {
        console_->log(SDL_LOG_PRIORITY_WARN, "DialogueManager: character cache full");
        return nullptr;
    }

    pakResource_->requestResourceAsync(characterId);
    ResourceData resData{nullptr, 0, 0};
    if (!pakResource_->tryGetResource(characterId, resData) || !resData.data || resData.size == 0) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: failed to load character def");
        return nullptr;
    }

    if (resData.size < sizeof(CharacterBinaryHeader)) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: character def resource too small");
        return nullptr;
    }

    const CharacterBinaryHeader* binHdr =
        reinterpret_cast<const CharacterBinaryHeader*>(resData.data);

    CharCache& entry = charCache_[charCacheCount_];
    entry.id = characterId;
    CharacterDef& def = entry.def;
    SDL_memset(&def, 0, sizeof(def));

    SDL_strlcpy(def.speakerName,    binHdr->speakerName,    CHARACTER_MAX_NAME);
    SDL_strlcpy(def.revealSoundPath,binHdr->revealSoundPath,CHARACTER_MAX_PATH);
    def.nameColor   = binHdr->nameColor;
    def.revealSpeed = binHdr->revealSpeed;

    Uint32 numP = binHdr->numPortraits;
    if (numP > (Uint32)CharacterDef::MAX_PORTRAITS) numP = (Uint32)CharacterDef::MAX_PORTRAITS;
    def.numPortraits = (int)numP;

    // Validate size includes portrait entries.
    Uint64 minSize = sizeof(CharacterBinaryHeader) + numP * sizeof(CharacterPortraitEntry);
    if (resData.size < minSize) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: character def truncated");
        def.numPortraits = 0;
    } else {
        const CharacterPortraitEntry* entries =
            reinterpret_cast<const CharacterPortraitEntry*>(
                resData.data + sizeof(CharacterBinaryHeader));
        for (int p = 0; p < def.numPortraits; p++) {
            SDL_strlcpy(def.portraits[p].tag, entries[p].tag, CHARACTER_MAX_TAG);
            // Load the texture now so it's ready at showLine time.
            Uint64 texId = hashCString(entries[p].resourcePath);
            pakResource_->requestResourceAsync(texId);
            def.portraits[p].textureId = texId;
        }
    }

    charCacheCount_++;
    return &entry.def;
}

Uint64 DialogueManager::resolvePortrait(const CharacterDef* charDef, const char* tag) {
    if (!charDef || !tag || tag[0] == '\0') return 0;
    for (int i = 0; i < charDef->numPortraits; i++) {
        if (SDL_strcmp(charDef->portraits[i].tag, tag) == 0) {
            return charDef->portraits[i].textureId;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// TextLayer creation helper
// ---------------------------------------------------------------------------

static TextLayer* createAndConfigureTextLayer(MemoryAllocator* alloc,
                                              FontManager*     fm,
                                              VulkanRenderer*  renderer,
                                              ConsoleBuffer*   console,
                                              int              fontHandle,
                                              int              boldHandle,
                                              int              italicHandle,
                                              float            x, float y,
                                              float            pointSize,
                                              float            wrapWidth) {
    void* mem = alloc->allocate(sizeof(TextLayer), "DialogueManager::TextLayer");
    assert(mem != nullptr);
    TextLayer* tl = new (mem) TextLayer(alloc, fm, renderer, console);
    tl->setFont(fontHandle);
    tl->setPosition(x, y);
    tl->setSize(pointSize);
    tl->setWrapWidth(wrapWidth);
    if (boldHandle >= 0 || italicHandle >= 0)
        tl->setFontFamily(boldHandle, italicHandle, -1);
    return tl;
}

// ---------------------------------------------------------------------------
// start()
// ---------------------------------------------------------------------------

void DialogueManager::start(lua_State* L, int onCompleteRef, Uint64 sceneId) {
    if (lines_.size() == 0) return;

    // Release any previous callback.
    if (lua_ && onCompleteRef_ != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, onCompleteRef_);
    }
    lua_           = L;
    onCompleteRef_ = onCompleteRef;

    // Create TextLayers if not yet created.
    if (!bodyText_) {
        bodyText_ = createAndConfigureTextLayer(
            allocator_, fontManager_, renderer_, console_,
            cfg_.fontHandle, cfg_.boldFontHandle, cfg_.italicFontHandle,
            cfg_.x, cfg_.y, cfg_.textSize, cfg_.width);
    }
    if (!speakerText_ && cfg_.speakerTextSize > 0.0f) {
        float speakerY = cfg_.y + cfg_.speakerTextSize * 1.5f;
        speakerText_ = createAndConfigureTextLayer(
            allocator_, fontManager_, renderer_, console_,
            cfg_.fontHandle, cfg_.boldFontHandle, cfg_.italicFontHandle,
            cfg_.x, speakerY, cfg_.speakerTextSize, 0.0f);
    }

    currentLine_       = -1;
    lastRevealCount_   = 0;
    numPausePoints_    = 0;
    pauseTimer_        = 0.0f;
    transitionTimer_   = 0.0f;
    transitionTargetLine_ = -1;
    showLine(0, sceneId);
}

// ---------------------------------------------------------------------------
// [pause=N] pre-processing
// ---------------------------------------------------------------------------

// Strips [pause=N] tags from `src`, writes cleaned text to `dst` (max dstLen),
// and fills out pausePoints/numPausePoints.
// charCount tracks visible codepoints only — markup tags like [wave]...[/wave]
// are copied through unchanged but their bracket/tag bytes are not counted, so
// pause point indices match TextLayer's getRevealCount() which also skips markup.
static void preprocessPauses(const char* src, char* dst, int dstLen,
                              PausePoint* pausePoints, int maxPoints, int& numPoints) {
    numPoints = 0;
    int out = 0;
    int charCount = 0; // visible character index (codepoint count, excluding markup tags)
    bool inMarkup = false; // true while inside [...] brackets
    while (*src && out < dstLen - 1) {
        if (*src == '[') {
            // Check for [pause=N]
            const char* p = src + 1;
            // Case-insensitive "pause"
            if ((p[0]=='p'||p[0]=='P') &&
                (p[1]=='a'||p[1]=='A') &&
                (p[2]=='u'||p[2]=='U') &&
                (p[3]=='s'||p[3]=='S') &&
                (p[4]=='e'||p[4]=='E') &&
                p[5] == '=') {
                // Parse the number.
                const char* numStart = p + 6;
                char* numEnd = nullptr;
                float duration = (float)SDL_strtod(numStart, &numEnd);
                if (numEnd && *numEnd == ']' && numPoints < maxPoints) {
                    PausePoint& pp = pausePoints[numPoints++];
                    pp.charIndex = charCount;
                    pp.duration  = (duration > 0.0f) ? duration : 0.0f;
                    src = numEnd + 1; // skip past ']'
                    continue;
                }
            }
            // Not a [pause=N] tag: copy '[' and enter markup mode.
            inMarkup = true;
            dst[out++] = *src++;
            continue;
        }
        if (*src == ']' && inMarkup) {
            inMarkup = false;
            dst[out++] = *src++;
            continue;
        }
        dst[out++] = *src++;
        if (!inMarkup) {
            // Count visible codepoints: only the leading byte of each UTF-8 sequence
            // (not continuation bytes 0x80..0xBF).
            unsigned char c = (unsigned char)dst[out - 1];
            if ((c & 0xC0) != 0x80) {
                charCount++;
            }
        }
    }
    dst[out] = '\0';
}

// ---------------------------------------------------------------------------
// showLine()
// ---------------------------------------------------------------------------

void DialogueManager::showLine(int lineIndex, Uint64 sceneId) {
    if (lineIndex < 0 || lineIndex >= (int)lines_.size()) return;
    if (!bodyText_) return;

    currentLine_ = lineIndex;
    const DialogueLine& line = lines_[lineIndex];

    // Look up character definition.
    const CharacterDef* charDef = loadCharacter(line.characterId);

    // Determine reveal speed.
    float speed = line.revealSpeed;
    if (speed <= 0.0f && charDef) speed = charDef->revealSpeed;
    if (speed <= 0.0f)           speed = cfg_.defaultRevealSpeed;
    bodyText_->setRevealSpeed(speed);

    // Pre-process [pause=N] tags out of the text.
    char cleanText[DIALOGUE_MAX_TEXT];
    preprocessPauses(line.text, cleanText, DIALOGUE_MAX_TEXT,
                     pausePoints_, MAX_PAUSE_POINTS, numPausePoints_);
    pauseTimer_ = 0.0f;

    // Set body text (parses markup, runs layout, rebuilds glyph layers).
    bodyText_->setString(cleanText, sceneId);
    lastRevealCount_ = 0;

    // Speaker name.
    if (speakerText_) {
        const char* name = (charDef && charDef->speakerName[0]) ? charDef->speakerName : "";
        // Apply character name colour (RRGGBBAA packed), defaulting to white.
        if (charDef && charDef->nameColor != 0) {
            float r = ((charDef->nameColor >> 24) & 0xFF) / 255.0f;
            float g = ((charDef->nameColor >> 16) & 0xFF) / 255.0f;
            float b = ((charDef->nameColor >>  8) & 0xFF) / 255.0f;
            float a = ((charDef->nameColor       ) & 0xFF) / 255.0f;
            speakerText_->setColor(r, g, b, a);
        } else {
            speakerText_->setColor(1.0f, 1.0f, 1.0f, 1.0f);
        }
        speakerText_->setString(name, sceneId);
        speakerText_->setRevealSpeed(0.0f); // instant
    }

    // Reveal sound: line override > character default > none.
    revealSoundSourceId_ = -1;
    const char* soundPath = nullptr;
    if (line.revealSoundPath[0])
        soundPath = line.revealSoundPath;
    else if (charDef && charDef->revealSoundPath[0])
        soundPath = charDef->revealSoundPath;

    if (soundPath) {
        // The sound source is pre-loaded; we just look up the resource ID here.
        // The caller (LuaInterface / game code) is responsible for pre-loading the
        // audio source. We store the resource ID and play it via audioManager_.
        // This mirrors the previous revealSoundSourceId_ pattern: we keep a
        // per-frame playback approach.  Nothing to do here beyond storing the fact
        // that a sound should play; actual source ID resolution is app-level.
        (void)soundPath; // sound integration is app-level (see revealSoundSourceId_)
    }

    // Portrait layer management.
    if (layerManager_ && cfg_.portraitWidth > 0.0f) {
        Uint64 newTex = resolvePortrait(charDef, line.portraitTag);

        bool portraitChanged = (newTex != activePortraitTex_);
        if (portraitChanged && cfg_.transitionDuration > 0.0f && activePortraitTex_ != 0) {
            // Begin transition: fade old portrait out, then swap and fade in.
            // We enter TRANSITIONING state and let update() advance it.
            state_                = STATE_TRANSITIONING;
            transitionTimer_      = cfg_.transitionDuration;
            transitionTargetLine_ = lineIndex;
            // Store the new texture so update() can pick it up when the timer expires.
            // We overwrite activePortraitTex_ in update() after the fade.
            // Keep activePortraitTex_ at the OLD value for now so the fade-out is correct.
            return; // do not yet set STATE_REVEALING
        }

        // Apply portrait immediately (no transition needed).
        activePortraitTex_ = newTex;
        if (newTex != 0 && layerManager_) {
            if (portraitLayerId_ < 0) {
                // Create the portrait layer on first use.
                portraitLayerId_ = layerManager_->createLayer(
                    newTex, cfg_.portraitWidth, cfg_.portraitHeight, 0, portraitPipelineId_);
                // Position: left or right of the text box.
                float px = (line.portraitSide == 0)
                    ? (cfg_.x - cfg_.portraitWidth * 0.6f)
                    : (cfg_.x + cfg_.width + cfg_.portraitWidth * 0.1f);
                float py = cfg_.y;
                layerManager_->setLayerPosition(portraitLayerId_, px, py);
            } else {
                // Update existing layer's texture by destroying and recreating.
                layerManager_->destroyLayer(portraitLayerId_);
                portraitLayerId_ = layerManager_->createLayer(
                    newTex, cfg_.portraitWidth, cfg_.portraitHeight, 0, portraitPipelineId_);
                float px = (line.portraitSide == 0)
                    ? (cfg_.x - cfg_.portraitWidth * 0.6f)
                    : (cfg_.x + cfg_.width + cfg_.portraitWidth * 0.1f);
                float py = cfg_.y;
                layerManager_->setLayerPosition(portraitLayerId_, px, py);
            }
        } else if (portraitLayerId_ >= 0) {
            // No portrait for this line — hide.
            layerManager_->setLayerEnabled(portraitLayerId_, false);
        }
    }

    state_ = STATE_REVEALING;
}

// ---------------------------------------------------------------------------
// setBackdrop()
// ---------------------------------------------------------------------------

void DialogueManager::setBackdrop(Uint64 textureId, int pipelineId) {
    if (!layerManager_) return;
    if (backdropLayerId_ >= 0) {
        layerManager_->destroyLayer(backdropLayerId_);
        backdropLayerId_ = -1;
    }
    if (textureId != 0) {
        backdropLayerId_ = layerManager_->createLayer(
            textureId, cfg_.width, cfg_.height, 0, pipelineId);
        layerManager_->setLayerPosition(backdropLayerId_, cfg_.x, cfg_.y);
    }
}

// ---------------------------------------------------------------------------
// advance()
// ---------------------------------------------------------------------------

void DialogueManager::advance(Uint64 sceneId) {
    if (state_ == STATE_IDLE || state_ == STATE_TRANSITIONING) return;

    if (state_ == STATE_REVEALING) {
        // Jump to full reveal: skip remaining pause timers too.
        pauseTimer_ = 0.0f;
        if (bodyText_) {
            bodyText_->setRevealSpeed(0.0f);
        }
        state_ = STATE_WAITING_ADVANCE;
        return;
    }

    if (state_ == STATE_WAITING_ADVANCE) {
        int nextLine = currentLine_ + 1;
        if (nextLine < (int)lines_.size()) {
            showLine(nextLine, sceneId);
        } else {
            // Dialogue complete.
            state_ = STATE_IDLE;
            fireOnComplete();
        }
    }
}

// ---------------------------------------------------------------------------
// update()
// ---------------------------------------------------------------------------

void DialogueManager::update(float dt, Uint64 sceneId) {
    if (state_ == STATE_IDLE) return;

    // Update text layers.
    if (bodyText_)    bodyText_->update(dt, sceneId);
    if (speakerText_) speakerText_->update(dt, sceneId);

    if (state_ == STATE_TRANSITIONING) {
        transitionTimer_ -= dt;
        if (transitionTimer_ <= 0.0f) {
            // Transition complete: apply the new portrait and show the target line.
            transitionTimer_ = 0.0f;
            state_ = STATE_IDLE; // reset so showLine sets STATE_REVEALING
            if (transitionTargetLine_ >= 0) {
                // Clear activePortraitTex_ so showLine doesn't re-trigger transition.
                activePortraitTex_ = 0;
                showLine(transitionTargetLine_, sceneId);
                transitionTargetLine_ = -1;
            }
        }
        return;
    }

    if (state_ == STATE_REVEALING && bodyText_) {
        // Handle [pause=N] points.
        if (pauseTimer_ > 0.0f) {
            pauseTimer_ -= dt;
            if (pauseTimer_ > 0.0f) {
                // Still paused: keep reveal frozen.
                bodyText_->setRevealSpeed(0.0f);
                return;
            }
            // Timer just expired: restore reveal speed and let the frame continue.
            pauseTimer_ = 0.0f;
            float speed = cfg_.defaultRevealSpeed;
            if (currentLine_ >= 0 && currentLine_ < (int)lines_.size()) {
                const DialogueLine& line = lines_[currentLine_];
                if (line.revealSpeed > 0.0f) speed = line.revealSpeed;
            }
            bodyText_->setRevealSpeed(speed);
        }

        int newReveal = bodyText_->getRevealCount();

        // Check if a pause point is reached.
        for (int i = 0; i < numPausePoints_; i++) {
            if (pausePoints_[i].duration > 0.0f &&
                newReveal >= pausePoints_[i].charIndex &&
                lastRevealCount_ < pausePoints_[i].charIndex) {
                pauseTimer_ = pausePoints_[i].duration;
                pausePoints_[i].duration = 0.0f; // consume it
                bodyText_->setRevealSpeed(0.0f);
                lastRevealCount_ = newReveal;
                return;
            }
        }

        // Play reveal sound for each newly revealed character.
        if (revealSoundSourceId_ >= 0 && newReveal > lastRevealCount_) {
            audioManager_->playSource(revealSoundSourceId_);
        }
        lastRevealCount_ = newReveal;

        // Check if reveal is complete.
        if (newReveal >= bodyText_->getTotalChars() && bodyText_->getTotalChars() > 0) {
            state_ = STATE_WAITING_ADVANCE;
        }
    }
}

// ---------------------------------------------------------------------------
// isRevealing()
// ---------------------------------------------------------------------------

bool DialogueManager::isRevealing() const {
    return state_ == STATE_REVEALING;
}

// ---------------------------------------------------------------------------
// fireOnComplete()
// ---------------------------------------------------------------------------

void DialogueManager::fireOnComplete() {
    if (lua_ && onCompleteRef_ != LUA_NOREF) {
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, onCompleteRef_);
        lua_pcall(lua_, 0, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// destroyLayers()
// ---------------------------------------------------------------------------

void DialogueManager::destroyLayers() {
    if (bodyText_) {
        bodyText_->destroyGlyphLayers();
        bodyText_->~TextLayer();
        allocator_->free(bodyText_);
        bodyText_ = nullptr;
    }
    if (speakerText_) {
        speakerText_->destroyGlyphLayers();
        speakerText_->~TextLayer();
        allocator_->free(speakerText_);
        speakerText_ = nullptr;
    }
    if (layerManager_) {
        if (portraitLayerId_ >= 0) {
            layerManager_->destroyLayer(portraitLayerId_);
            portraitLayerId_ = -1;
        }
        if (backdropLayerId_ >= 0) {
            layerManager_->destroyLayer(backdropLayerId_);
            backdropLayerId_ = -1;
        }
    }
    activePortraitTex_ = 0;
}

