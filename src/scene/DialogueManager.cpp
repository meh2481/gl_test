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
// Minimal JSON parser — handles the dialogue JSON format:
//   { "lines": [ { "key": "value", "num": 1.0, ... }, ... ] }
// No heap allocation: uses fixed-size char arrays in DialogueLine.
// ============================================================================

static const char* jsonSkipWS(const char* p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

// Parse a JSON string value starting at '"'.
// Returns pointer after the closing '"', or p on error.
// Writes at most maxLen-1 characters into out and null-terminates.
static const char* jsonParseString(const char* p, char* out, int maxLen) {
    if (*p != '"') { if (out && maxLen > 0) out[0] = '\0'; return p; }
    p++;
    int i = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            char e = *p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            default:  c = e;    break;
            }
        }
        if (out && i < maxLen - 1) out[i++] = c;
    }
    if (out) out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

// Parse a JSON number (float).
static const char* jsonParseNumber(const char* p, float* out) {
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    float v = 0.0f;
    while (*p >= '0' && *p <= '9') v = v * 10.0f + (float)(*p++ - '0');
    if (*p == '.') {
        p++;
        float f = 0.1f;
        while (*p >= '0' && *p <= '9') { v += (float)(*p++ - '0') * f; f *= 0.1f; }
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        bool eNeg = false;
        if (*p == '-') { eNeg = true; p++; } else if (*p == '+') p++;
        int exp = 0;
        while (*p >= '0' && *p <= '9') exp = exp * 10 + (*p++ - '0');
        float mult = 1.0f;
        for (int i = 0; i < exp; i++) mult *= 10.0f;
        if (eNeg) v /= mult; else v *= mult;
    }
    if (out) *out = neg ? -v : v;
    return p;
}

// Skip past the end of the current JSON value (string, number, bool, null,
// object, or array).  Used to skip unknown fields.
static const char* jsonSkipValue(const char* p);
static const char* jsonSkipValue(const char* p) {
    p = jsonSkipWS(p);
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            if (*p == '\\') { p++; if (*p) p++; } else p++;
        }
        if (*p == '"') p++;
        return p;
    }
    if (*p == '{') {
        p++;
        p = jsonSkipWS(p);
        if (*p == '}') return p + 1;
        while (*p) {
            if (*p == '"') p = jsonSkipValue(p); // key
            p = jsonSkipWS(p);
            if (*p == ':') p++;
            p = jsonSkipValue(p); // value
            p = jsonSkipWS(p);
            if (*p == ',') { p++; p = jsonSkipWS(p); continue; }
            if (*p == '}') { p++; break; }
            break;
        }
        return p;
    }
    if (*p == '[') {
        p++;
        p = jsonSkipWS(p);
        if (*p == ']') return p + 1;
        while (*p) {
            p = jsonSkipValue(p);
            p = jsonSkipWS(p);
            if (*p == ',') { p++; p = jsonSkipWS(p); continue; }
            if (*p == ']') { p++; break; }
            break;
        }
        return p;
    }
    // number / true / false / null
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != '\n') p++;
    return p;
}

// Parse the JSON key at *p (assumes *p == '"'), copy into keyBuf.
// Returns pointer after the closing '"'.
static const char* jsonParseKey(const char* p, char* keyBuf, int keyBufLen) {
    return jsonParseString(p, keyBuf, keyBufLen);
}

// Parse one dialogue line object starting at *p (assumes *p == '{').
// Returns pointer after '}', or nullptr on error.
static const char* parseDialogueLine(const char* p, DialogueLine& line) {
    if (*p != '{') return nullptr;
    p++;

    // Set defaults
    line.speaker[0]         = '\0';
    line.text[0]            = '\0';
    line.portraitPath[0]    = '\0';
    line.revealSoundPath[0] = '\0';
    line.voicePath[0]       = '\0';
    line.revealSpeed        = 0.0f;
    line.pauseAfter         = 0.0f;

    char key[64];
    while (true) {
        p = jsonSkipWS(p);
        if (*p == '}') { p++; break; }
        if (*p == ',') { p++; continue; }
        if (*p == '"') {
            p = jsonParseKey(p, key, (int)sizeof(key));
            p = jsonSkipWS(p);
            if (*p == ':') p++;
            p = jsonSkipWS(p);

            if (SDL_strcmp(key, "speaker") == 0) {
                p = jsonParseString(p, line.speaker, DialogueLine::MAX_SHORT);
            } else if (SDL_strcmp(key, "text") == 0) {
                p = jsonParseString(p, line.text, DialogueLine::MAX_TEXT);
            } else if (SDL_strcmp(key, "portrait") == 0) {
                p = jsonParseString(p, line.portraitPath, DialogueLine::MAX_SHORT);
            } else if (SDL_strcmp(key, "revealSound") == 0) {
                p = jsonParseString(p, line.revealSoundPath, DialogueLine::MAX_SHORT);
            } else if (SDL_strcmp(key, "voice") == 0) {
                p = jsonParseString(p, line.voicePath, DialogueLine::MAX_SHORT);
            } else if (SDL_strcmp(key, "revealSpeed") == 0) {
                p = jsonParseNumber(p, &line.revealSpeed);
            } else if (SDL_strcmp(key, "pauseAfter") == 0) {
                p = jsonParseNumber(p, &line.pauseAfter);
            } else {
                p = jsonSkipValue(p);
            }
        } else {
            p++;  // skip unexpected chars
        }
    }
    return p;
}

// Parse the top-level JSON object and fill the lines vector.
// Returns the number of lines parsed, or -1 on error.
static int parseDialogueJson(const char* json, Vector<DialogueLine>& linesOut) {
    const char* p = jsonSkipWS(json);
    if (*p != '{') return -1;
    p++;

    char key[64];
    while (true) {
        p = jsonSkipWS(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        if (*p != '"') { p++; continue; }

        p = jsonParseKey(p, key, (int)sizeof(key));
        p = jsonSkipWS(p);
        if (*p == ':') p++;
        p = jsonSkipWS(p);

        if (SDL_strcmp(key, "lines") == 0) {
            if (*p != '[') { p = jsonSkipValue(p); continue; }
            p++;  // skip '['
            while (true) {
                p = jsonSkipWS(p);
                if (*p == ']') { p++; break; }
                if (*p == ',') { p++; continue; }
                if (*p == '{') {
                    DialogueLine line;
                    const char* after = parseDialogueLine(p, line);
                    if (after) {
                        linesOut.push_back(line);
                        p = after;
                    } else {
                        p = jsonSkipValue(p);
                    }
                } else {
                    p = jsonSkipValue(p);
                }
            }
        } else {
            p = jsonSkipValue(p);
        }
    }
    return (int)linesOut.size();
}

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
    , pauseTimer_(0.0f)
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

    // The JSON resource may not be null-terminated; make a null-terminated copy.
    char* jsonBuf = static_cast<char*>(
        allocator_->allocate(resData.size + 1, "DialogueManager::jsonBuf"));
    if (!jsonBuf) return false;
    SDL_memcpy(jsonBuf, resData.data, resData.size);
    jsonBuf[resData.size] = '\0';

    int count = parseDialogueJson(jsonBuf, lines_);
    allocator_->free(jsonBuf);

    if (count <= 0) {
        console_->log(SDL_LOG_PRIORITY_WARN, "DialogueManager: no lines parsed from '%s'", resourcePath);
        return false;
    }
    console_->log(SDL_LOG_PRIORITY_INFO, "DialogueManager: loaded %d lines from '%s'", count, resourcePath);
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
    pauseTimer_ = 0.0f;
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

    if (state_ == STATE_WAITING || state_ == STATE_PAUSING) {
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

            // If pauseAfter > 0, start the pause timer and switch to PAUSING
            float pa = (currentLine_ >= 0 && currentLine_ < (int)lines_.size())
                ? lines_[currentLine_].pauseAfter : 0.0f;
            if (pa > 0.0f) {
                state_      = STATE_PAUSING;
                pauseTimer_ = pa;
            }
        }
    }

    if (state_ == STATE_PAUSING) {
        pauseTimer_ -= dt;
        if (pauseTimer_ <= 0.0f) {
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
