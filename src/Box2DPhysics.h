#pragma once

#include <box2d/box2d.h>
#include <vector>
#include <unordered_map>
#include <SDL2/SDL.h>

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
    
    // Async physics stepping
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
    int nextBodyId_;
    bool debugDrawEnabled_;
    std::vector<DebugVertex> debugLineVertices_;
    std::vector<DebugVertex> debugTriangleVertices_;
    
    // Threading support
    SDL_mutex* physicsMutex_;
    SDL_atomic_t stepInProgress_;
    SDL_Thread* stepThread_;
};
