#pragma once

#include <box2d/box2d.h>
#include <vector>
#include <memory>

struct DebugVertex {
    float x, y;
    float r, g, b, a;
};

class Box2DDebugDraw : public b2Draw {
public:
    Box2DDebugDraw();
    
    void DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override;
    void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override;
    void DrawCircle(const b2Vec2& center, float radius, const b2Color& color) override;
    void DrawSolidCircle(const b2Vec2& center, float radius, const b2Vec2& axis, const b2Color& color) override;
    void DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color) override;
    void DrawTransform(const b2Transform& xf) override;
    void DrawPoint(const b2Vec2& p, float size, const b2Color& color) override;
    
    void Clear();
    const std::vector<DebugVertex>& GetVertices() const { return vertices_; }
    
private:
    std::vector<DebugVertex> vertices_;
    void AddVertex(const b2Vec2& pos, const b2Color& color);
};

class Box2DPhysics {
public:
    Box2DPhysics();
    ~Box2DPhysics();
    
    // World management
    void setGravity(float x, float y);
    void step(float timeStep, int32 velocityIterations = 8, int32 positionIterations = 3);
    
    // Body management
    int createBody(int bodyType, float x, float y, float angle = 0.0f);
    void destroyBody(int bodyId);
    void setBodyPosition(int bodyId, float x, float y);
    void setBodyAngle(int bodyId, float angle);
    void setBodyLinearVelocity(int bodyId, float vx, float vy);
    void setBodyAngularVelocity(int bodyId, float omega);
    void applyForce(int bodyId, float fx, float fy, float px, float py);
    void applyTorque(int bodyId, float torque);
    
    // Body queries
    float getBodyPositionX(int bodyId);
    float getBodyPositionY(int bodyId);
    float getBodyAngle(int bodyId);
    float getBodyLinearVelocityX(int bodyId);
    float getBodyLinearVelocityY(int bodyId);
    float getBodyAngularVelocity(int bodyId);
    
    // Fixture management
    void addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addCircleFixture(int bodyId, float radius, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    
    // Debug drawing
    void enableDebugDraw(bool enable);
    bool isDebugDrawEnabled() const { return debugDrawEnabled_; }
    const std::vector<DebugVertex>& getDebugVertices();
    
private:
    std::unique_ptr<b2World> world_;
    std::vector<b2Body*> bodies_;
    std::unique_ptr<Box2DDebugDraw> debugDraw_;
    bool debugDrawEnabled_;
    
    b2Body* getBody(int bodyId);
};
