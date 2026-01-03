#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <lua.hpp>
#include "vulkan/VulkanRenderer.h"
#include "scene/SceneManager.h"
#include "core/config.h"
#include "core/TrigLookup.h"
#include "input/InputActions.h"
#include "input/VibrationManager.h"
#include "scene/LuaInterface.h"
#include "memory/SmallAllocator.h"
#include "memory/LargeMemoryAllocator.h"
#include "physics/Box2DPhysics.h"
#include "scene/SceneLayer.h"
#include "audio/AudioManager.h"
#include "effects/ParticleSystem.h"
#include "effects/WaterEffect.h"

#ifdef DEBUG
#include "debug/ImGuiManager.h"
#endif
#include "debug/ConsoleBuffer.h"

#define LUA_SCRIPT_ID 16891582414721442785ULL
#define PAK_FILE "res.pak"

inline uint32_t clamp(uint32_t value, uint32_t min, uint32_t max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

// Convert screen coordinates to world coordinates
// World coordinates are -aspect to aspect in x, -1 to 1 in y (aspect = width/height)
// Accounts for camera offset and zoom
inline void screenToWorld(float screenX, float screenY, int windowWidth, int windowHeight,
                          float cameraOffsetX, float cameraOffsetY, float cameraZoom,
                          float *worldX, float *worldY)
{
    float aspect = windowWidth / (float)windowHeight;
    // Convert to normalized device coordinates (-1 to 1)
    float ndcX = (screenX / (float)windowWidth) * 2.0f - 1.0f;
    float ndcY = 1.0f - (screenY / (float)windowHeight) * 2.0f; // Flip Y
    // Apply inverse camera transform (aspect, zoom, then offset)
    *worldX = (ndcX * aspect / cameraZoom) + cameraOffsetX;
    *worldY = (ndcY / cameraZoom) + cameraOffsetY;
}

// Pan tracking state
static bool isPanning = false;
static float panStartCursorX = 0.0f;
static float panStartCursorY = 0.0f;
static float panStartCameraX = 0.0f;
static float panStartCameraY = 0.0f;

#ifdef DEBUG
// Structure to pass data to the hot-reload thread
struct HotReloadData
{
    SDL_Mutex *mutex;
    SDL_AtomicInt reloadComplete;
    SDL_AtomicInt reloadSuccess;
    SDL_AtomicInt reloadRequested;
};

// Global ImGuiManager pointer for callback
static ImGuiManager *g_imguiManager = nullptr;

// Callback for rendering ImGui
static void renderImGuiCallback(VkCommandBuffer commandBuffer)
{
    if (g_imguiManager)
    {
        g_imguiManager->render(commandBuffer);
    }
}

// Thread function for hot-reloading resources
// This allows F5 hot-reload to happen in the background without blocking the main thread
// The thread waits for reload requests and rebuilds shaders/resources asynchronously
static int hotReloadThread(void *data)
{
    HotReloadData *reloadData = (HotReloadData *)data;

    while (true)
    {
        // Wait for reload request
        while (SDL_GetAtomicInt(&reloadData->reloadRequested) == 0)
        {
            SDL_Delay(100);
        }

        // Lock mutex to prevent concurrent reloads
        SDL_LockMutex(reloadData->mutex);

        // Use SDL_Log directly to avoid console buffer from background thread
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Hot-reloading resources in background thread...");

        // Rebuild shaders and pak file using make
        int result = system("make shaders && make res_pak");

        // Store result
        SDL_SetAtomicInt(&reloadData->reloadSuccess, (result == 0) ? 1 : 0);
        SDL_SetAtomicInt(&reloadData->reloadComplete, 1);
        SDL_SetAtomicInt(&reloadData->reloadRequested, 0);

        SDL_UnlockMutex(reloadData->mutex);
    }

    return 0;
}
#endif

// Global log file handle
static SDL_IOStream* g_logFile = nullptr;

// Custom SDL log output function that writes to stdout, stderr, and log file
static void customLogOutput(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
    (void)category;
    SDL_LogOutputFunction* defaultLogFunc = (SDL_LogOutputFunction*)userdata;
    // Call default log function to output to stdout/stderr
    (*defaultLogFunc)(nullptr, category, priority, message);

    const char* priorityStr = "INFO";
    switch (priority)
    {
        case SDL_LOG_PRIORITY_VERBOSE: priorityStr = "VERBOSE"; break;
        case SDL_LOG_PRIORITY_DEBUG:   priorityStr = "DEBUG";   break;
        case SDL_LOG_PRIORITY_INFO:    priorityStr = "INFO";    break;
        case SDL_LOG_PRIORITY_WARN:    priorityStr = "WARN";    break;
        case SDL_LOG_PRIORITY_ERROR:   priorityStr = "ERROR";   break;
        case SDL_LOG_PRIORITY_CRITICAL: priorityStr = "CRITICAL"; break;
        default: break;
    }

    // Get current timestamp
    SDL_DateTime dateTime;
    SDL_Time ticks;
    SDL_GetCurrentTime(&ticks);
    SDL_TimeToDateTime(ticks, &dateTime, true);
    char timestamp[64];
    SDL_snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                 dateTime.year, dateTime.month, dateTime.day,
                 dateTime.hour, dateTime.minute, dateTime.second, dateTime.nanosecond / 1000000);

    // Format the log message
    char logLine[2048];
    SDL_snprintf(logLine, sizeof(logLine), "[%s] [%s] %s\n", timestamp, priorityStr, message);

    // Write to log file if open
    if (g_logFile)
    {
        SDL_WriteIO(g_logFile, logLine, SDL_strlen(logLine));
        SDL_FlushIO(g_logFile);
    }
}

int main()
{
    // Set custom log output function to write to stdout before any SDL_Log calls
    SDL_LogOutputFunction defaultLogFunc = SDL_GetDefaultLogOutputFunction();
    SDL_SetLogOutputFunction(customLogOutput, &defaultLogFunc);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        assert(false);
    }

    // Open log file in the same directory as config files
    char logFilePath[MAX_PREF_PATH];
    if (getPrefFilePath(logFilePath, sizeof(logFilePath), "last_run.log"))
    {
        g_logFile = SDL_IOFromFile(logFilePath, "w");
        if (g_logFile)
        {
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Logging to: %s", logFilePath);
        }
    }

    // Create single allocator instances for the entire application
    SmallAllocator* smallAllocator = new SmallAllocator();
    LargeMemoryAllocator* largeAllocator = new LargeMemoryAllocator();

    ConsoleBuffer *consoleBuffer = static_cast<ConsoleBuffer *>(
        smallAllocator->allocate(sizeof(ConsoleBuffer), "main::ConsoleBuffer"));
    new (consoleBuffer) ConsoleBuffer(smallAllocator, largeAllocator);

    // Log machine info at startup
    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "SDL version: %d", SDL_GetVersion());
    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Platform: %s", SDL_GetPlatform());
    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "CPU count: %d", SDL_GetNumLogicalCPUCores());
    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "System RAM: %d MB", SDL_GetSystemRAM());

    Config config = loadConfig();

    SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    if (config.display == 0)
    {
        config.display = primaryDisplay;
    }

    const SDL_DisplayMode *displayMode = SDL_GetDesktopDisplayMode(config.display);
    if (displayMode == nullptr)
    {
        config.display = primaryDisplay;
        displayMode = SDL_GetDesktopDisplayMode(config.display);
        if (displayMode == nullptr)
        {
            consoleBuffer->log(SDL_LOG_PRIORITY_CRITICAL, "SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
            assert(false);
        }
    }

    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Launching on display: %u", config.display);

    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(config.display);
    int y = SDL_WINDOWPOS_CENTERED_DISPLAY(config.display);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Shader Triangle");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, displayMode->w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, displayMode->h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, SDL_WINDOW_VULKAN | config.fullscreenMode);

    SDL_Window *window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (window == nullptr)
    {
        consoleBuffer->log(SDL_LOG_PRIORITY_CRITICAL, "SDL_CreateWindowWithProperties failed: %s", SDL_GetError());
        assert(false);
    }

    PakResource pakResource(largeAllocator, consoleBuffer);
    if (!pakResource.load(PAK_FILE))
    {
        consoleBuffer->log(SDL_LOG_PRIORITY_CRITICAL, "Failed to load resource pak: %s", PAK_FILE);
        assert(false);
    }

    // Load trig lookup table
    TrigLookup *trigLookup = static_cast<TrigLookup *>(
        smallAllocator->allocate(sizeof(TrigLookup), "main::TrigLookup"));
    assert(trigLookup != nullptr);
    new (trigLookup) TrigLookup(smallAllocator, largeAllocator, consoleBuffer);
    if (!trigLookup->load(&pakResource))
    {
        consoleBuffer->log(SDL_LOG_PRIORITY_CRITICAL, "Failed to load trig lookup table");
        assert(false);
    }
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Loaded TrigLookup table" << ConsoleBuffer::endl;

    // Allocate SceneLayerManager first since Box2DPhysics needs it
    SceneLayerManager *layerManager = static_cast<SceneLayerManager *>(
        smallAllocator->allocate(sizeof(SceneLayerManager), "main::SceneLayerManager"));
    assert(layerManager != nullptr);
    new (layerManager) SceneLayerManager(smallAllocator, largeAllocator, trigLookup);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created SceneLayerManager" << ConsoleBuffer::endl;

    // Allocate Box2DPhysics with layer manager
    Box2DPhysics *physics = static_cast<Box2DPhysics *>(
        smallAllocator->allocate(sizeof(Box2DPhysics), "main::Box2DPhysics"));
    assert(physics != nullptr);
    new (physics) Box2DPhysics(smallAllocator, largeAllocator, layerManager, consoleBuffer, trigLookup);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created Box2DPhysics" << ConsoleBuffer::endl;

    // Allocate AudioManager using large allocator
    AudioManager *audioManager = static_cast<AudioManager *>(
        largeAllocator->allocate(sizeof(AudioManager), "main::AudioManager"));
    assert(audioManager != nullptr);
    new (audioManager) AudioManager(smallAllocator, consoleBuffer);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created AudioManager" << ConsoleBuffer::endl;

    // Allocate ParticleSystemManager
    ParticleSystemManager *particleManager = static_cast<ParticleSystemManager *>(
        smallAllocator->allocate(sizeof(ParticleSystemManager), "main::ParticleSystemManager"));
    assert(particleManager != nullptr);
    new (particleManager) ParticleSystemManager(trigLookup);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created ParticleSystemManager" << ConsoleBuffer::endl;

    // Allocate WaterEffectManager using large allocator
    WaterEffectManager *waterEffectManager = static_cast<WaterEffectManager *>(
        largeAllocator->allocate(sizeof(WaterEffectManager), "main::WaterEffectManager"));
    assert(waterEffectManager != nullptr);
    new (waterEffectManager) WaterEffectManager();
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created WaterEffectManager" << ConsoleBuffer::endl;

    // Allocate VulkanRenderer
    VulkanRenderer *renderer = static_cast<VulkanRenderer *>(
        smallAllocator->allocate(sizeof(VulkanRenderer), "main::VulkanRenderer"));
    assert(renderer != nullptr);
    new (renderer) VulkanRenderer(smallAllocator, largeAllocator, consoleBuffer);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created VulkanRenderer" << ConsoleBuffer::endl;

    renderer->initialize(window, config.gpuIndex);

    // Update config with the selected GPU index
    config.gpuIndex = renderer->getSelectedGpuIndex();

    // Allocate VibrationManager
    VibrationManager *vibrationManager = static_cast<VibrationManager *>(
        smallAllocator->allocate(sizeof(VibrationManager), "main::VibrationManager"));
    assert(vibrationManager != nullptr);
    new (vibrationManager) VibrationManager();
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created VibrationManager" << ConsoleBuffer::endl;

    // Create LuaInterface without SceneManager (will be set after SceneManager is created)
    LuaInterface *luaInterface = static_cast<LuaInterface *>(
        smallAllocator->allocate(sizeof(LuaInterface), "main::LuaInterface"));
    assert(luaInterface != nullptr);
    new (luaInterface) LuaInterface(pakResource, *renderer, smallAllocator, physics, layerManager,
                                    audioManager, particleManager, waterEffectManager,
                                    nullptr, vibrationManager, consoleBuffer);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created LuaInterface" << ConsoleBuffer::endl;

    // Allocate SceneManager
    SceneManager *sceneManager = static_cast<SceneManager *>(
        smallAllocator->allocate(sizeof(SceneManager), "main::SceneManager"));
    assert(sceneManager != nullptr);
    new (sceneManager) SceneManager(smallAllocator, pakResource, *renderer, physics, layerManager,
                                    audioManager, particleManager, waterEffectManager, luaInterface, consoleBuffer, trigLookup);

    // Set SceneManager pointer in LuaInterface after SceneManager is created
    luaInterface->setSceneManager(sceneManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created SceneManager and linked with LuaInterface" << ConsoleBuffer::endl;

    // Allocate KeybindingManager
    KeybindingManager *keybindings = static_cast<KeybindingManager *>(
        smallAllocator->allocate(sizeof(KeybindingManager), "main::KeybindingManager"));
    assert(keybindings != nullptr);
    new (keybindings) KeybindingManager(smallAllocator);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created KeybindingManager" << ConsoleBuffer::endl;

    // Load keybindings from config if available
    if (config.keybindings[0] != '\0')
    {
        keybindings->deserializeBindings(config.keybindings);
    }

    // Open all available game controllers
    SDL_Gamepad *gameController = nullptr;
    int numJoysticks;
    SDL_JoystickID *joysticks = SDL_GetJoysticks(&numJoysticks);
    if (joysticks)
    {
        for (int i = 0; i < numJoysticks; ++i)
        {
            if (SDL_IsGamepad(joysticks[i]))
            {
                gameController = SDL_OpenGamepad(joysticks[i]);
                if (gameController)
                {
                    vibrationManager->setGameController(gameController);
                    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Game Controller %d connected: %s", i, SDL_GetGamepadName(gameController));
                    if (vibrationManager->hasRumbleSupport())
                    {
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "  Rumble support: Yes");
                    }
                    if (vibrationManager->hasTriggerRumbleSupport())
                    {
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "  Trigger rumble support: Yes (DualSense)");
                    }
                    break; // Use the first available controller
                }
            }
        }
        SDL_free(joysticks);
    }

    // Load initial scene
    sceneManager->pushScene(LUA_SCRIPT_ID);

#ifdef DEBUG
    // Allocate ImGuiManager using large allocator
    ImGuiManager *imguiManager = static_cast<ImGuiManager *>(
        largeAllocator->allocate(sizeof(ImGuiManager), "main::ImGuiManager"));
    assert(imguiManager != nullptr);
    new (imguiManager) ImGuiManager(smallAllocator, consoleBuffer, trigLookup);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Created ImGuiManager" << ConsoleBuffer::endl;

    g_imguiManager = imguiManager;
    imguiManager->initialize(window, renderer->getInstance(), renderer->getPhysicalDevice(),
                                renderer->getDevice(), renderer->getGraphicsQueueFamilyIndex(),
                                renderer->getGraphicsQueue(), renderer->getRenderPass(),
                                renderer->getSwapchainImageCount(), renderer->getMsaaSamples());

    // Set ImGui render callback in renderer
    renderer->setImGuiRenderCallback(renderImGuiCallback);

    // Initialize hot-reload thread
    HotReloadData reloadData;
    reloadData.mutex = SDL_CreateMutex();
    assert(reloadData.mutex != nullptr);
    SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
    SDL_SetAtomicInt(&reloadData.reloadSuccess, 0);
    SDL_SetAtomicInt(&reloadData.reloadRequested, 0);

    SDL_Thread *reloadThread = SDL_CreateThread(hotReloadThread, "HotReload", &reloadData);
    assert(reloadThread != nullptr);
#endif

    bool running = true;
    SDL_Event event;
    float lastTime = SDL_GetTicks() / 1000.0f;
    while (running)
    {
        float currentTime = SDL_GetTicks() / 1000.0f;
        float deltaTime = (currentTime - lastTime);
        lastTime = currentTime;
        while (SDL_PollEvent(&event))
        {
#ifdef DEBUG
            // Process event for ImGui first
            imguiManager->processEvent(&event);
#endif
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                // Handle actions bound to this key
                const ActionList &actionList = keybindings->getActionsForKey(event.key.key);
                for (int i = 0; i < actionList.count; ++i)
                {
                    sceneManager->handleAction(actionList.actions[i]);
                }

                // Handle special case: ALT+ENTER for fullscreen toggle
                if (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT))
                {
                    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
                    if (flags & SDL_WINDOW_FULLSCREEN)
                    {
                        SDL_SetWindowFullscreen(window, false);
                        config.fullscreenMode = 0;
                        config.display = SDL_GetDisplayForWindow(window);
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Toggled to windowed on display: %u", config.display);
                    }
                    else
                    {
                        SDL_SetWindowFullscreen(window, true);
                        config.fullscreenMode = SDL_WINDOW_FULLSCREEN;
                        config.display = SDL_GetDisplayForWindow(window);
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Toggled to fullscreen on display: %u", config.display);
                    }
                    saveConfig(config);
                }
#ifdef DEBUG
                // Handle special case: F5 for hot reload
                if (event.key.key == SDLK_F5)
                {
                    // Check if reload thread is ready
                    if (SDL_GetAtomicInt(&reloadData.reloadRequested) == 0)
                    {
                        *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Requesting hot-reload..." << ConsoleBuffer::endl;
                        SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
                        SDL_SetAtomicInt(&reloadData.reloadRequested, 1);
                    }
                }
#endif
            }
            // Handle gamepad button press
            if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            {
                const ActionList &actionList = keybindings->getActionsForGamepadButton(event.gbutton.button);
                for (int i = 0; i < actionList.count; ++i)
                {
                    sceneManager->handleAction(actionList.actions[i]);
                }
            }
            // Handle gamepad connection
            if (event.type == SDL_EVENT_GAMEPAD_ADDED && !gameController)
            {
                gameController = SDL_OpenGamepad(event.gdevice.which);
                if (gameController)
                {
                    vibrationManager->setGameController(gameController);
                    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Game Controller connected: %s", SDL_GetGamepadName(gameController));
                    if (vibrationManager->hasRumbleSupport())
                    {
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "  Rumble support: Yes");
                    }
                    if (vibrationManager->hasTriggerRumbleSupport())
                    {
                        consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "  Trigger rumble support: Yes (DualSense)");
                    }
                }
            }
            // Handle gamepad disconnection
            if (event.type == SDL_EVENT_GAMEPAD_REMOVED)
            {
                if (gameController && event.gdevice.which == SDL_GetGamepadID(gameController))
                {
                    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Game Controller disconnected" << ConsoleBuffer::endl;
                    vibrationManager->setGameController(nullptr);
                    SDL_CloseGamepad(gameController);
                    gameController = nullptr;
                }
            }
            // Handle mouse button press for drag actions
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT)
            {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                screenToWorld(event.button.x, event.button.y, windowWidth, windowHeight,
                                sceneManager->getCameraOffsetX(), sceneManager->getCameraOffsetY(),
                                sceneManager->getCameraZoom(), &worldX, &worldY);
                sceneManager->setCursorPosition(worldX, worldY);
                sceneManager->handleAction(ACTION_DRAG_START);
            }
            // Handle mouse button release for drag actions
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_LEFT)
            {
                sceneManager->handleAction(ACTION_DRAG_END);
            }
            // Handle middle mouse button press for pan
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_RIGHT)
            {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);
                screenToWorld(event.button.x, event.button.y, windowWidth, windowHeight,
                                sceneManager->getCameraOffsetX(), sceneManager->getCameraOffsetY(),
                                sceneManager->getCameraZoom(), &worldX, &worldY);
                sceneManager->setCursorPosition(worldX, worldY);
                isPanning = true;
                panStartCursorX = worldX;
                panStartCursorY = worldY;
                panStartCameraX = sceneManager->getCameraOffsetX();
                panStartCameraY = sceneManager->getCameraOffsetY();
                sceneManager->handleAction(ACTION_PAN_START);
            }
            // Handle middle mouse button release for pan
            if (event.type == SDL_EVENT_MOUSE_BUTTON_UP && event.button.button == SDL_BUTTON_RIGHT)
            {
                isPanning = false;
                sceneManager->handleAction(ACTION_PAN_END);
            }
            // Handle mouse wheel for zoom
            if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
#ifdef DEBUG
                // Don't zoom if ImGui wants the mouse (e.g., hovering over ImGui window)
                if (!imguiManager->wantCaptureMouse())
                {
                    sceneManager->applyScrollZoom(event.wheel.y);
                }
#else
                sceneManager->applyScrollZoom(event.wheel.y);
#endif
            }
            // Handle mouse motion for cursor tracking
            if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                int windowWidth, windowHeight;
                float worldX, worldY;
                SDL_GetWindowSize(window, &windowWidth, &windowHeight);

                // Update camera offset while panning (before calculating cursor position)
                if (isPanning)
                {
                    // Calculate cursor position with original camera offset for delta calculation
                    screenToWorld(event.motion.x, event.motion.y, windowWidth, windowHeight,
                                    panStartCameraX, panStartCameraY,
                                    sceneManager->getCameraZoom(), &worldX, &worldY);
                    float deltaX = worldX - panStartCursorX;
                    float deltaY = worldY - panStartCursorY;
                    sceneManager->setCameraOffset(panStartCameraX - deltaX, panStartCameraY - deltaY);
                }

                // Calculate final cursor position with current camera offset
                screenToWorld(event.motion.x, event.motion.y, windowWidth, windowHeight,
                                sceneManager->getCameraOffsetX(), sceneManager->getCameraOffsetY(),
                                sceneManager->getCameraZoom(), &worldX, &worldY);
                sceneManager->setCursorPosition(worldX, worldY);
            }
        }

#ifdef DEBUG
        // Sync particle editor preview controls with camera
        if (sceneManager->isParticleEditorActive())
        {
            ParticleEditorState &editorState = imguiManager->getEditorState();

            // If preview controls were changed by user, update the camera
            if (editorState.previewCameraChanged)
            {
                luaInterface->setCameraOffset(editorState.previewOffsetX, editorState.previewOffsetY);
                luaInterface->setCameraZoom(editorState.previewZoom);
                editorState.previewCameraChanged = false;
            }

            // If reset was requested, also reset the camera
            if (editorState.previewResetRequested)
            {
                luaInterface->setCameraOffset(0.0f, 0.0f);
                luaInterface->setCameraZoom(1.0f);
                editorState.previewResetRequested = false;
            }

            // Otherwise, sync preview controls FROM the camera (mouse zoom/pan updates)
            imguiManager->syncPreviewWithCamera(
                sceneManager->getCameraOffsetX(),
                sceneManager->getCameraOffsetY(),
                sceneManager->getCameraZoom());
        }
#endif

        // Update renderer with current camera transform
        renderer->setCameraTransform(sceneManager->getCameraOffsetX(),
                                        sceneManager->getCameraOffsetY(),
                                        sceneManager->getCameraZoom());

        if (!sceneManager->updateActiveScene(deltaTime))
        {
            running = false;
        }

#ifdef DEBUG
        // Check if hot-reload completed
        if (SDL_GetAtomicInt(&reloadData.reloadComplete) == 1)
        {
            if (SDL_GetAtomicInt(&reloadData.reloadSuccess) == 1)
            {
                *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Hot-reload complete, applying changes..." << ConsoleBuffer::endl;
                // Reload pak
                pakResource.reload(PAK_FILE);
                // Reload current scene with new resources
                sceneManager->reloadCurrentScene();
            }
            else
            {
                *consoleBuffer << SDL_LOG_PRIORITY_ERROR << "Hot-reload failed!" << ConsoleBuffer::endl;
                assert(false);
            }
            SDL_SetAtomicInt(&reloadData.reloadComplete, 0);
        }
#endif

#ifdef DEBUG
        // Start ImGui frame
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        imguiManager->newFrame(width, height);

        // Show console window
        imguiManager->showConsoleWindow();

        // Show memory allocator window
        imguiManager->showMemoryAllocatorWindow(smallAllocator, largeAllocator, currentTime);

        // Show particle editor if scene wants it active
        bool sceneWantsEditor = sceneManager->isParticleEditorActive();
        bool editorWasActive = imguiManager->isParticleEditorActive();

        if (sceneWantsEditor && !editorWasActive)
        {
            // Transitioning from inactive to active - activate the editor
            imguiManager->setParticleEditorActive(true);
        }
        else if (!sceneWantsEditor && editorWasActive)
        {
            // Transitioning from active to inactive - deactivate and destroy preview
            if (luaInterface)
            {
                imguiManager->destroyPreviewSystem(&luaInterface->getParticleSystemManager());
            }
            imguiManager->setParticleEditorActive(false);
            // Reset background color to black when exiting editor
            renderer->setClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }

        if (sceneWantsEditor && luaInterface)
        {
            imguiManager->showParticleEditorWindow(
                &luaInterface->getParticleSystemManager(),
                &sceneManager->getPakResource(),
                renderer,
                luaInterface,
                deltaTime,
                sceneManager);
        }
#endif

        renderer->render(currentTime);
    }

    // Save current fullscreen state and display to config
    SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN)
    {
        config.fullscreenMode = SDL_WINDOW_FULLSCREEN;
    }
    else
    {
        config.fullscreenMode = 0;
    }
    config.display = SDL_GetDisplayForWindow(window);
    consoleBuffer->log(SDL_LOG_PRIORITY_INFO, "Saving display: %u", config.display);

    // Save keybindings to config
    keybindings->serializeBindings(config.keybindings, MAX_KEYBINDING_STRING);

    saveConfig(config);

    // Close game controller if open
    if (gameController)
    {
        SDL_CloseGamepad(gameController);
    }

#ifdef DEBUG
    // Cleanup ImGui
    g_imguiManager = nullptr;
    imguiManager->cleanup();

    // Cleanup hot-reload thread
    // Note: We can't cleanly stop the thread since it's in an infinite loop
    // In a production app, we'd use a flag to signal thread exit
    // For debug builds this is acceptable as the process is exiting anyway
    SDL_DetachThread(reloadThread);
    SDL_DestroyMutex(reloadData.mutex);

    // Destroy ImGuiManager
    imguiManager->~ImGuiManager();
    largeAllocator->free(imguiManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed ImGuiManager" << ConsoleBuffer::endl;
#endif
    renderer->cleanup();

    // Destroy SceneManager
    sceneManager->~SceneManager();
    smallAllocator->free(sceneManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed SceneManager" << ConsoleBuffer::endl;

    // Destroy LuaInterface
    luaInterface->~LuaInterface();
    smallAllocator->free(luaInterface);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed LuaInterface" << ConsoleBuffer::endl;

    // Destroy VibrationManager
    vibrationManager->~VibrationManager();
    smallAllocator->free(vibrationManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed VibrationManager" << ConsoleBuffer::endl;

    // Destroy VulkanRenderer
    renderer->~VulkanRenderer();
    smallAllocator->free(renderer);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed VulkanRenderer" << ConsoleBuffer::endl;

    // Destroy WaterEffectManager
    waterEffectManager->~WaterEffectManager();
    largeAllocator->free(waterEffectManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed WaterEffectManager" << ConsoleBuffer::endl;

    // Destroy ParticleSystemManager
    particleManager->~ParticleSystemManager();
    smallAllocator->free(particleManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed ParticleSystemManager" << ConsoleBuffer::endl;

    // Destroy AudioManager
    audioManager->~AudioManager();
    largeAllocator->free(audioManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed AudioManager" << ConsoleBuffer::endl;

    // Destroy Box2DPhysics
    physics->~Box2DPhysics();
    smallAllocator->free(physics);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed Box2DPhysics" << ConsoleBuffer::endl;

    // Destroy SceneLayerManager
    layerManager->~SceneLayerManager();
    smallAllocator->free(layerManager);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed SceneLayerManager" << ConsoleBuffer::endl;

    // Destroy KeybindingManager
    keybindings->~KeybindingManager();
    smallAllocator->free(keybindings);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed KeybindingManager" << ConsoleBuffer::endl;

    // Destroy TrigLookup
    trigLookup->~TrigLookup();
    smallAllocator->free(trigLookup);
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Destroyed TrigLookup" << ConsoleBuffer::endl;

    // Destroy ConsoleBuffer last (before allocators)
    *consoleBuffer << SDL_LOG_PRIORITY_INFO << "Cleaning up ConsoleBuffer" << ConsoleBuffer::endl;
    consoleBuffer->~ConsoleBuffer();
    smallAllocator->free(consoleBuffer);

    SDL_DestroyWindow(window);

    delete largeAllocator;
    delete smallAllocator;

    // Close log file
    if (g_logFile)
    {
        SDL_CloseIO(g_logFile);
        g_logFile = nullptr;
    }

    SDL_Quit();
    return 0;
}