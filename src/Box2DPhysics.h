#pragma once

#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <SDL3/SDL.h>

struct DebugVertex {
    float x, y;
    float r, g, b, a;
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

    // Joint management
    int createRevoluteJoint(int bodyIdA, int bodyIdB, float anchorAx, float anchorAy, float anchorBx, float anchorBy, bool enableLimit = false, float lowerAngle = 0.0f, float upperAngle = 0.0f);
    void destroyJoint(int jointId);

    // Debug drawing
    void enableDebugDraw(bool enable);
    bool isDebugDrawEnabled() const { return debugDrawEnabled_; }
    const std::vector<DebugVertex>& getDebugLineVertices();
    const std::vector<DebugVertex>& getDebugTriangleVertices();

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
};
