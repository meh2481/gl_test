#pragma once

#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <SDL3/SDL.h>
#include <functional>

struct DebugVertex {
    float x, y;
    float r, g, b, a;
};

// Collision hit event for destructible objects
struct CollisionHitEvent {
    int bodyIdA;
    int bodyIdB;
    float pointX, pointY;
    float normalX, normalY;
    float approachSpeed;
};

// 2D polygon for destructible objects
struct DestructiblePolygon {
    float vertices[16];  // Max 8 vertices, x/y pairs
    int vertexCount;
    float area;  // Calculated polygon area
};

// Fracture result containing new fragment polygons
struct FractureResult {
    DestructiblePolygon fragments[8];  // Max 8 fragments from a single fracture
    int fragmentCount;
};

// Properties for destructible bodies
struct DestructibleProperties {
    float strength;     // Moh's hardness scale (1-10, typical 5-7), higher = more force needed
    float brittleness;  // How easily it shatters (0.0-1.0), higher = more/smaller pieces
    bool isDestructible;
    float originalVertices[16];  // Original polygon vertices for texture UV calculation
    int originalVertexCount;
    uint64_t textureId;      // Texture for rendering fragments
    uint64_t normalMapId;    // Normal map for fragments
    int pipelineId;          // Shader pipeline for fragments
};

// Callback for when a body is fractured (returns new fragment body IDs and layer IDs)
struct FractureEvent {
    int originalBodyId;
    int originalLayerId;
    int newBodyIds[8];
    int newLayerIds[8];
    int fragmentCount;
    float impactPointX, impactPointY;
    float impactNormalX, impactNormalY;
    float impactSpeed;
};

class Box2DPhysics {
public:
    Box2DPhysics();
    ~Box2DPhysics();

    // World management
    void setGravity(float x, float y);
    void step(float timeStep, int subStepCount = 4);

    // Set the fixed timestep for physics simulation (default is 1/60)
    void setFixedTimestep(float timestep);
    float getFixedTimestep() const { return fixedTimestep_; }

    // Async physics stepping - runs physics simulation on a background thread
    // Use stepAsync() to start stepping, isStepComplete() to check, waitForStepComplete() to block
    void stepAsync(float timeStep, int subStepCount = 4);
    bool isStepComplete();
    void waitForStepComplete();

    // Body management
    int createBody(int bodyType, float x, float y, float angle = 0.0f);
    void destroyBody(int bodyId);
    void setBodyPosition(int bodyId, float x, float y);
    void setBodyAngle(int bodyId, float angle);
    void setBodyLinearVelocity(int bodyId, float vx, float vy);
    void setBodyAngularVelocity(int bodyId, float omega);
    void setBodyAwake(int bodyId, bool awake);
    void applyForce(int bodyId, float fx, float fy, float px, float py);
    void applyTorque(int bodyId, float torque);

    // Body queries
    float getBodyPositionX(int bodyId);
    float getBodyPositionY(int bodyId);
    float getBodyAngle(int bodyId);
    float getBodyLinearVelocityX(int bodyId);
    float getBodyLinearVelocityY(int bodyId);
    float getBodyAngularVelocity(int bodyId);

    // Shape management
    void addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addCircleFixture(int bodyId, float radius, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addPolygonFixture(int bodyId, const float* vertices, int vertexCount, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addSegmentFixture(int bodyId, float x1, float y1, float x2, float y2, float friction = 0.3f, float restitution = 0.0f);

    // Joint management
    int createRevoluteJoint(int bodyIdA, int bodyIdB, float anchorAx, float anchorAy, float anchorBx, float anchorBy, bool enableLimit = false, float lowerAngle = 0.0f, float upperAngle = 0.0f);
    void destroyJoint(int jointId);

    // Mouse joint (for drag debugging)
    int queryBodyAtPoint(float x, float y);
    int createMouseJoint(int bodyId, float targetX, float targetY, float maxForce = 1000.0f);
    void updateMouseJointTarget(int jointId, float targetX, float targetY);
    void destroyMouseJoint(int jointId);

    // Debug drawing
    void enableDebugDraw(bool enable);
    bool isDebugDrawEnabled() const { return debugDrawEnabled_; }
    const std::vector<DebugVertex>& getDebugLineVertices();
    const std::vector<DebugVertex>& getDebugTriangleVertices();

    // Collision events - returns hit events from last physics step
    const std::vector<CollisionHitEvent>& getCollisionHitEvents() const { return collisionHitEvents_; }

    // Destructible object management
    void setBodyDestructible(int bodyId, float strength, float brittleness,
                             const float* vertices, int vertexCount,
                             uint64_t textureId, uint64_t normalMapId, int pipelineId);
    void clearBodyDestructible(int bodyId);
    bool isBodyDestructible(int bodyId) const;
    const DestructibleProperties* getDestructibleProperties(int bodyId) const;

    // Get fracture events from last physics step
    const std::vector<FractureEvent>& getFractureEvents() const { return fractureEvents_; }

    // Process destructible collisions and generate fractures
    // Returns body/layer IDs that should be created (caller must create layers)
    // This is called automatically during step() but results can be queried
    void processFractures();

    // Calculate fracture based on impact point and properties
    static FractureResult calculateFracture(const DestructibleProperties& props,
                                            float impactX, float impactY,
                                            float normalX, float normalY,
                                            float impactSpeed,
                                            float bodyX, float bodyY, float bodyAngle);

    // Calculate polygon area using shoelace formula
    static float calculatePolygonArea(const float* vertices, int vertexCount);

    // Split polygon along a line, returns two polygons
    static void splitPolygon(const float* vertices, int vertexCount,
                             float lineX, float lineY, float lineDirX, float lineDirY,
                             DestructiblePolygon& poly1, DestructiblePolygon& poly2);

    // Create a fragment body with proper physics properties
    int createFragmentBody(float x, float y, float angle,
                          const DestructiblePolygon& polygon,
                          float vx, float vy, float angularVel,
                          float density, float friction, float restitution);

    // Fracture callback - set to receive notifications when objects fracture
    using FractureCallback = std::function<void(const FractureEvent&)>;
    void setFractureCallback(FractureCallback callback) { fractureCallback_ = callback; }

private:
    // Debug draw callbacks
    static void DrawPolygon(const b2Vec2* vertices, int vertexCount, b2HexColor color, void* context);
    static void DrawSolidPolygon(b2Transform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context);
    static void DrawCircle(b2Vec2 center, float radius, b2HexColor color, void* context);
    static void DrawSolidCircle(b2Transform transform, float radius, b2HexColor color, void* context);
    static void DrawSegment(b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* context);
    static void DrawTransform(b2Transform transform, void* context);
    static void DrawPoint(b2Vec2 p, float size, b2HexColor color, void* context);

    void addLineVertex(float x, float y, b2HexColor color);
    void addTriangleVertex(float x, float y, b2HexColor color);

    // Thread function for async physics step
    static int physicsStepThread(void* data);

    struct StepData {
        Box2DPhysics* physics;
        float timeStep;
        int subStepCount;
    };

    b2WorldId worldId_;
    std::unordered_map<int, b2BodyId> bodies_;
    std::unordered_map<int, b2JointId> joints_;
    std::unordered_map<int, DestructibleProperties> destructibles_;  // Destructible properties per body
    int nextBodyId_;
    int nextJointId_;
    bool debugDrawEnabled_;
    std::vector<DebugVertex> debugLineVertices_;
    std::vector<DebugVertex> debugTriangleVertices_;

    // Fixed timestep accumulator for framerate-independent physics
    float timeAccumulator_;
    float fixedTimestep_;

    // Threading support
    SDL_Mutex* physicsMutex_;
    SDL_AtomicInt stepInProgress_;
    SDL_Thread* stepThread_;

    // Ground body for mouse joint (lazy initialized, protected by mutex)
    b2BodyId mouseJointGroundBody_;

    // Collision events from last physics step
    std::vector<CollisionHitEvent> collisionHitEvents_;

    // Fracture events from last physics step
    std::vector<FractureEvent> fractureEvents_;

    // Fracture callback
    FractureCallback fractureCallback_;

    // Bodies pending destruction after fracture (processed after step)
    std::vector<int> pendingDestructions_;

    // Helper to convert b2BodyId to internal ID
    int findInternalBodyId(b2BodyId bodyId);

    // Calculate required force to break based on Moh's hardness
    float calculateBreakForce(float strength, float impactSpeed) const;

    // Determine number of fracture pieces based on brittleness and impact
    int calculateFragmentCount(float brittleness, float impactSpeed, float strength) const;
};
