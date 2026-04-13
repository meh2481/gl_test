#include "DialogueManager.h"
#include "../audio/AudioManager.h"
#include "../core/config.h"
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
    , autoplayEnabled_(false)
    , autoplayDelay_(0.5f)
    , autoplayTimer_(0.0f)
    , lua_(nullptr)
    , onCompleteRef_(LUA_NOREF)
{
    cfg_ = {};
    cfg_.defaultRevealSpeed  = 20.0f;
    cfg_.textSize            = 24.0f;
    cfg_.speakerTextSize     = 0.0f;
    cfg_.boldFontHandle      = -1;
    cfg_.italicFontHandle    = -1;
    cfg_.textShadowDx        = 0.0f;
    cfg_.textShadowDy        = 0.0f;
    cfg_.textShadowR         = 0.0f;
    cfg_.textShadowG         = 0.0f;
    cfg_.textShadowB         = 0.0f;
    cfg_.textShadowA         = 0.0f;
    cfg_.transitionDuration  = 0.2f;
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
    const char* currentLanguage = getCurrentLanguage();
    if (currentLanguage && currentLanguage[0] != '\0') {
        bool foundLanguage = false;
        for (Uint32 i = 0; i < hdr->languageCount; i++) {
            if (SDL_strcmp(langTable[i].code, currentLanguage) == 0) {
                langIndex = i;
                foundLanguage = true;
                break;
            }
        }
        if (!foundLanguage) {
            console_->log(SDL_LOG_PRIORITY_WARN,
                "DialogueManager: language '%s' not found in '%s', falling back to '%s'",
                currentLanguage, resourcePath, langTable[0].code);
        }
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
        "DialogueManager: loaded %d lines (lang=%s, index=%d) from '%s'",
        (int)lines_.size(), langTable[langIndex].code, (int)langIndex, resourcePath);
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
    entry.revealAudioBufferId = -1;
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
            Uint64 texId = hashCString(entries[p].resourcePath);
            pakResource_->requestResourceAsync(texId);
            def.portraits[p].textureId = texId;

            // Upload the portrait texture to the GPU so the sprite pipeline can
            // render it.  Mirror what LuaInterface::loadTexture() does: handle
            // the atlas case (packer packs small images into atlases).
            ResourceData texResData{nullptr, 0, 0};
            if (pakResource_->tryGetResource(texId, texResData) && texResData.data) {
                AtlasUV atlasUV;
                if (pakResource_->tryGetAtlasUV(texId, atlasUV)) {
                    // Atlas-packed: load the atlas image into the GPU.
                    pakResource_->requestResourceAsync(atlasUV.atlasId);
                    ResourceData atlasData{nullptr, 0, 0};
                    if (pakResource_->tryGetResource(atlasUV.atlasId, atlasData) && atlasData.data) {
                        renderer_->loadAtlasTexture(atlasUV.atlasId, atlasData);
                    }
                } else {
                    renderer_->loadTexture(texId, texResData);
                }
            }
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
    if (bodyText_) {
        if (cfg_.textShadowA > 0.0f) {
            bodyText_->setShadow(cfg_.textShadowDx, cfg_.textShadowDy,
                                 cfg_.textShadowR, cfg_.textShadowG,
                                 cfg_.textShadowB, cfg_.textShadowA);
        } else {
            bodyText_->clearShadow();
        }
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
    pauseAnimWaiting_  = false;
    pauseWaitCharIndex_= 0;
    pauseWaitDuration_ = 0.0f;
    transitionTimer_   = 0.0f;
    transitionTargetLine_ = -1;
    autoplayTimer_     = 0.0f;
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

    const DialogueLine& line = lines_[lineIndex];

    // Look up character definition (needed for both the early portrait check
    // and the full line setup below).
    const CharacterDef* charDef = loadCharacter(line.characterId);

    // *** Portrait transition check BEFORE any text setup ***
    // If the portrait is changing and a crossfade is configured, we enter
    // TRANSITIONING state immediately — without touching bodyText_ or
    // speakerText_ — so the OLD line's text keeps showing during the fade.
    // When the timer expires, update() clears activePortraitTex_ and calls
    // showLine() again; at that point no transition is triggered and the new
    // line is set up normally.
    if (layerManager_ && cfg_.portraitWidth > 0.0f &&
        cfg_.transitionDuration > 0.0f && activePortraitTex_ != 0) {
        Uint64 newTex = resolvePortrait(charDef, line.portraitTag);
        if (newTex != activePortraitTex_) {
            state_                = STATE_TRANSITIONING;
            transitionTimer_      = cfg_.transitionDuration;
            transitionTargetLine_ = lineIndex;
            return; // text setup deferred until transition completes
        }
    }

    currentLine_ = lineIndex;
    autoplayTimer_ = 0.0f;

    // Determine reveal speed.
    float speed = line.revealSpeed;
    if (speed <= 0.0f && charDef) speed = charDef->revealSpeed;
    if (speed <= 0.0f)           speed = cfg_.defaultRevealSpeed;
    bodyText_->setRevealSpeed(speed);

    // Pre-process [pause=N] tags out of the text.
    char cleanText[DIALOGUE_MAX_TEXT];
    preprocessPauses(line.text, cleanText, DIALOGUE_MAX_TEXT,
                     pausePoints_, MAX_PAUSE_POINTS, numPausePoints_);
    pauseTimer_         = 0.0f;
    pauseAnimWaiting_   = false;
    pauseWaitCharIndex_ = 0;
    pauseWaitDuration_  = 0.0f;

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

    // Stop and release any previous reveal sound source.
    if (revealSoundSourceId_ >= 0 && audioManager_) {
        audioManager_->stopSource(revealSoundSourceId_);
        audioManager_->releaseSource(revealSoundSourceId_);
        revealSoundSourceId_ = -1;
    }

    if (soundPath && audioManager_ && pakResource_) {
        // Resolve audio buffer — check char cache first to avoid reloading.
        int bufferId = -1;
        if (charDef) {
            for (int i = 0; i < charCacheCount_; i++) {
                if (charCache_[i].id != 0 &&
                    SDL_strcmp(charCache_[i].def.revealSoundPath, soundPath) == 0) {
                    bufferId = charCache_[i].revealAudioBufferId;
                    break;
                }
            }
        }

        // Load from pak if not yet cached.
        if (bufferId < 0) {
            Uint64 resId = hashCString(soundPath);
            pakResource_->requestResourceAsync(resId);
            ResourceData resData{nullptr, 0, 0};
            if (pakResource_->tryGetResource(resId, resData) && resData.data && resData.size > 0) {
                bufferId = audioManager_->loadGlaAudioFromMemory(resData.data, resData.size);
                // Cache the buffer ID in the char cache entry.
                if (charDef && bufferId >= 0) {
                    for (int i = 0; i < charCacheCount_; i++) {
                        if (charCache_[i].id != 0 &&
                            SDL_strcmp(charCache_[i].def.revealSoundPath, soundPath) == 0) {
                            charCache_[i].revealAudioBufferId = bufferId;
                            break;
                        }
                    }
                }
            }
        }

        // Create a looping source and start it immediately.
        if (bufferId >= 0) {
            revealSoundSourceId_ = audioManager_->createAudioSource(bufferId, /*looping=*/true, 1.0f);
            if (revealSoundSourceId_ >= 0) {
                audioManager_->playSource(revealSoundSourceId_);
            }
        }
    }

    // Portrait layer management.
    if (layerManager_ && cfg_.portraitWidth > 0.0f) {
        Uint64 newTex = resolvePortrait(charDef, line.portraitTag);

        // Apply portrait immediately (transition was already handled above).
        activePortraitTex_ = newTex;
        if (newTex != 0 && layerManager_) {
            float px = (line.portraitSide == 0)
                ? (cfg_.x - cfg_.portraitWidth * 0.5f)
                : (cfg_.x + cfg_.width + cfg_.portraitWidth * 0.5f);
            float py = cfg_.y;

            if (portraitLayerId_ < 0) {
                // Create the portrait layer on first use.
                portraitLayerId_ = layerManager_->createLayer(
                    newTex, cfg_.portraitWidth, cfg_.portraitHeight, 0, portraitPipelineId_);
            } else {
                // Update existing layer's texture by destroying and recreating.
                layerManager_->destroyLayer(portraitLayerId_);
                portraitLayerId_ = layerManager_->createLayer(
                    newTex, cfg_.portraitWidth, cfg_.portraitHeight, 0, portraitPipelineId_);
            }

            // Atlas-packed textures: set UV coordinates so the renderer uses the
            // correct sub-region of the atlas and the correct descriptor set.
            AtlasUV atlasUV;
            if (pakResource_->tryGetAtlasUV(newTex, atlasUV)) {
                layerManager_->setLayerAtlasUV(portraitLayerId_,
                    atlasUV.atlasId, atlasUV.u0, atlasUV.v0, atlasUV.u1, atlasUV.v1);
            }

            layerManager_->setLayerPosition(portraitLayerId_, px, py);
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

void DialogueManager::setAutoplay(bool enabled, float delaySeconds) {
    autoplayEnabled_ = enabled;
    autoplayDelay_ = (delaySeconds < 0.0f) ? 0.0f : delaySeconds;
    autoplayTimer_ = 0.0f;
    if (autoplayEnabled_ && state_ == STATE_WAITING_ADVANCE) {
        autoplayTimer_ = autoplayDelay_;
    }
}

// ---------------------------------------------------------------------------
// advance()
// ---------------------------------------------------------------------------

void DialogueManager::advance(Uint64 sceneId) {
    if (state_ == STATE_IDLE || state_ == STATE_TRANSITIONING) return;

    if (state_ == STATE_REVEALING) {
        // Stop the looping reveal sound immediately when skipping.
        if (revealSoundSourceId_ >= 0 && audioManager_) {
            audioManager_->stopSource(revealSoundSourceId_);
            audioManager_->releaseSource(revealSoundSourceId_);
            revealSoundSourceId_ = -1;
        }
        // Jump to full reveal: skip remaining pause timers too.
        pauseTimer_         = 0.0f;
        pauseAnimWaiting_   = false;
        pauseWaitCharIndex_ = 0;
        pauseWaitDuration_  = 0.0f;
        if (bodyText_) {
            bodyText_->setRevealSpeed(0.0f);
        }
        state_ = STATE_WAITING_ADVANCE;
        if (autoplayEnabled_) autoplayTimer_ = autoplayDelay_;
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

    // Always keep the speaker text running.
    if (speakerText_) speakerText_->update(dt, sceneId);

    // --- TRANSITIONING: portrait crossfade in progress ---
    if (state_ == STATE_TRANSITIONING) {
        if (bodyText_) bodyText_->update(dt, sceneId);
        transitionTimer_ -= dt;
        if (transitionTimer_ <= 0.0f) {
            transitionTimer_ = 0.0f;
            state_ = STATE_IDLE; // reset so showLine sets STATE_REVEALING
            if (transitionTargetLine_ >= 0) {
                activePortraitTex_ = 0;
                showLine(transitionTargetLine_, sceneId);
                transitionTargetLine_ = -1;
            }
        }
        return;
    }

    // --- WAITING_ADVANCE: fully revealed, waiting for player input ---
    if (state_ == STATE_WAITING_ADVANCE) {
        if (bodyText_) bodyText_->update(dt, sceneId);
        if (autoplayEnabled_) {
            autoplayTimer_ -= dt;
            if (autoplayTimer_ <= 0.0f) {
                autoplayTimer_ = 0.0f;
                advance(sceneId);
            }
        }
        return;
    }

    // --- REVEALING ---
    if (state_ != STATE_REVEALING || !bodyText_) return;

    // Pause-anim-waiting: a [pause=N] threshold was crossed; let the fade-in
    // animations of the already-revealed characters finish before we freeze.
    // We call updateFadesAndEffects so the reveal accumulator does NOT advance.
    if (pauseAnimWaiting_) {
        bodyText_->updateFadesAndEffects(dt, sceneId);
        // All fades done — now start the actual pause.
        pauseTimer_         = pauseWaitDuration_;
        pauseAnimWaiting_   = false;
        pauseWaitCharIndex_ = 0;
        pauseWaitDuration_  = 0.0f;
        return;
    }

    // Active pause countdown: use updateFadesAndEffects so the reveal accumulator is
    // frozen but wave/shake effects and any remaining fade-ins keep running.
    if (pauseTimer_ > 0.0f) {
        pauseTimer_ -= dt;
        if (pauseTimer_ > 0.0f) {
            // Pause the reveal sound while the [pause=N] delay is active.
            if (revealSoundSourceId_ >= 0 && audioManager_) {
                audioManager_->pauseSource(revealSoundSourceId_);
            }
            bodyText_->updateFadesAndEffects(dt, sceneId); // freeze reveal, keep effects
            return;
        }
        pauseTimer_ = 0.0f; // just expired; fall through to normal reveal
        // Resume the reveal sound now that the pause has ended.
        if (revealSoundSourceId_ >= 0 && audioManager_) {
            audioManager_->playSource(revealSoundSourceId_);
        }
    }

    // Normal reveal: update with full dt.
    bodyText_->update(dt, sceneId);
    int newReveal = bodyText_->getRevealCount();

    // Detect pause-point threshold crossings.
    for (int i = 0; i < numPausePoints_; i++) {
        if (pausePoints_[i].duration > 0.0f &&
            newReveal >= pausePoints_[i].charIndex &&
            lastRevealCount_ < pausePoints_[i].charIndex) {
            // Pause the reveal sound at a [pause=N] boundary.
            if (revealSoundSourceId_ >= 0 && audioManager_) {
                audioManager_->pauseSource(revealSoundSourceId_);
            }
            lastRevealCount_ = newReveal;
            float dur = pausePoints_[i].duration;
            pausePoints_[i].duration = 0.0f; // consume
            if (bodyText_->isRevealAnimComplete(pausePoints_[i].charIndex)) {
                // Fades already done (instant reveal speed): start pause immediately.
                pauseTimer_ = dur;
            } else {
                // Wait for fade-in animations to finish before freezing.
                pauseAnimWaiting_   = true;
                pauseWaitCharIndex_ = pausePoints_[i].charIndex;
                pauseWaitDuration_  = dur;
            }
            return;
        }
    }

    lastRevealCount_ = newReveal;

    // Check if reveal is complete.
    if (newReveal >= bodyText_->getTotalChars() && bodyText_->getTotalChars() > 0) {
        // Stop the looping reveal sound now that all text is visible.
        if (revealSoundSourceId_ >= 0 && audioManager_) {
            audioManager_->stopSource(revealSoundSourceId_);
            audioManager_->releaseSource(revealSoundSourceId_);
            revealSoundSourceId_ = -1;
        }
        state_ = STATE_WAITING_ADVANCE;
        if (autoplayEnabled_) autoplayTimer_ = autoplayDelay_;
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

