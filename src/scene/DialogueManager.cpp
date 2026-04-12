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

DialogueManager::DialogueManager(MemoryAllocator* allocator,
                                 FontManager*     fontManager,
                                 VulkanRenderer*  renderer,
                                 AudioManager*    audioManager,
                                 ConsoleBuffer*   console,
                                 PakResource*     pakResource)
    : allocator_(allocator)
    , fontManager_(fontManager)
    , renderer_(renderer)
    , audioManager_(audioManager)
    , console_(console)
    , pakResource_(pakResource)
    , lines_(*allocator, "DialogueManager::lines_")
    , bodyText_(nullptr)
    , speakerText_(nullptr)
    , state_(STATE_IDLE)
    , currentLine_(-1)
    , lastRevealCount_(0)
    , revealSoundSourceId_(-1)
    , lua_(nullptr)
    , onCompleteRef_(LUA_NOREF)
{
    cfg_ = {};
    cfg_.defaultRevealSpeed = 20.0f;
    cfg_.textSize           = 24.0f;
    cfg_.speakerTextSize    = 0.0f;
    cfg_.boldFontHandle     = -1;
    cfg_.italicFontHandle   = -1;
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

    // Validate that the resource contains enough data for at least language 0.
    Uint64 expectedMin = sizeof(DialogueBinaryHeader) +
        (Uint64)hdr->lineCount * sizeof(DialogueLineRecord);
    if (resData.size < expectedMin) {
        console_->log(SDL_LOG_PRIORITY_ERROR, "DialogueManager: truncated data in '%s'", resourcePath);
        return false;
    }

    // Load language 0 (first language variant).
    const DialogueLineRecord* records =
        reinterpret_cast<const DialogueLineRecord*>(resData.data + sizeof(DialogueBinaryHeader));

    lines_.reserve(hdr->lineCount);
    for (Uint32 i = 0; i < hdr->lineCount; i++) {
        DialogueLine line;
        SDL_strlcpy(line.speaker,         records[i].speaker,         DialogueLine::MAX_SHORT);
        SDL_strlcpy(line.text,            records[i].text,            DialogueLine::MAX_TEXT);
        SDL_strlcpy(line.portraitPath,    records[i].portraitPath,    DialogueLine::MAX_SHORT);
        SDL_strlcpy(line.revealSoundPath, records[i].revealSoundPath, DialogueLine::MAX_SHORT);
        SDL_strlcpy(line.voicePath,       records[i].voicePath,       DialogueLine::MAX_SHORT);
        line.revealSpeed = records[i].revealSpeed;
        lines_.push_back(line);
    }

    console_->log(SDL_LOG_PRIORITY_INFO,
        "DialogueManager: loaded %d lines from '%s'", (int)lines_.size(), resourcePath);
    return true;
}

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

void DialogueManager::start(lua_State* L, int onCompleteRef, Uint64 sceneId) {
    if (lines_.size() == 0) return;

    // Release any previous callback
    if (lua_ && onCompleteRef_ != LUA_NOREF) {
        luaL_unref(lua_, LUA_REGISTRYINDEX, onCompleteRef_);
    }
    lua_          = L;
    onCompleteRef_ = onCompleteRef;

    // Create TextLayers if not yet created
    if (!bodyText_) {
        bodyText_ = createAndConfigureTextLayer(
            allocator_, fontManager_, renderer_, console_,
            cfg_.fontHandle, cfg_.boldFontHandle, cfg_.italicFontHandle,
            cfg_.x, cfg_.y, cfg_.textSize, cfg_.width);
    }
    if (!speakerText_ && cfg_.speakerTextSize > 0.0f) {
        float speakerY = cfg_.y + cfg_.height + cfg_.speakerTextSize * 1.2f;
        speakerText_ = createAndConfigureTextLayer(
            allocator_, fontManager_, renderer_, console_,
            cfg_.fontHandle, cfg_.boldFontHandle, cfg_.italicFontHandle,
            cfg_.x, speakerY, cfg_.speakerTextSize, 0.0f);
    }

    currentLine_     = -1;
    lastRevealCount_ = 0;
    showLine(0, sceneId);
}

void DialogueManager::showLine(int lineIndex, Uint64 sceneId) {
    if (lineIndex < 0 || lineIndex >= (int)lines_.size()) return;
    if (!bodyText_) return;

    currentLine_ = lineIndex;
    const DialogueLine& line = lines_[lineIndex];

    // Configure reveal speed
    float speed = (line.revealSpeed > 0.0f) ? line.revealSpeed : cfg_.defaultRevealSpeed;
    bodyText_->setRevealSpeed(speed);

    // Set the text (this parses markup, runs layout, rebuilds glyph layers)
    bodyText_->setString(line.text, sceneId);
    lastRevealCount_ = 0;

    // Speaker name
    if (speakerText_) {
        speakerText_->setString(line.speaker, sceneId);
        speakerText_->setRevealSpeed(0.0f);  // speaker name appears instantly
    }

    state_      = STATE_REVEALING;
}

void DialogueManager::advance(Uint64 sceneId) {
    if (state_ == STATE_IDLE) return;

    if (state_ == STATE_REVEALING) {
        // Jump to full reveal
        if (bodyText_) {
            bodyText_->setRevealSpeed(0.0f);
        }
        state_ = STATE_WAITING;
        return;
    }

    if (state_ == STATE_WAITING) {
        int nextLine = currentLine_ + 1;
        if (nextLine < (int)lines_.size()) {
            showLine(nextLine, sceneId);
        } else {
            // Dialogue complete
            state_ = STATE_IDLE;
            fireOnComplete();
        }
    }
}

void DialogueManager::update(float dt, Uint64 sceneId) {
    if (state_ == STATE_IDLE) return;

    // Update text layers
    if (bodyText_)   bodyText_->update(dt, sceneId);
    if (speakerText_) speakerText_->update(dt, sceneId);

    if (state_ == STATE_REVEALING && bodyText_) {
        int newReveal = bodyText_->getRevealCount();

        // Play reveal sound for each newly revealed character
        if (revealSoundSourceId_ >= 0 && newReveal > lastRevealCount_) {
            audioManager_->playSource(revealSoundSourceId_);
        }
        lastRevealCount_ = newReveal;

        // Check if reveal is complete
        if (newReveal >= bodyText_->getTotalChars() && bodyText_->getTotalChars() > 0) {
            state_ = STATE_WAITING;
        }
    }
}

bool DialogueManager::isRevealing() const {
    return state_ == STATE_REVEALING;
}

void DialogueManager::setRevealSoundSourceId(int sourceId) {
    revealSoundSourceId_ = sourceId;
}

void DialogueManager::fireOnComplete() {
    if (lua_ && onCompleteRef_ != LUA_NOREF) {
        lua_rawgeti(lua_, LUA_REGISTRYINDEX, onCompleteRef_);
        lua_pcall(lua_, 0, 0, 0);
    }
}

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
}
