#include "Box2DPhysics.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Box2DDebugDraw implementation
Box2DDebugDraw::Box2DDebugDraw() {
    SetFlags(b2Draw::e_shapeBit | b2Draw::e_jointBit);
}

void Box2DDebugDraw::Clear() {
    vertices_.clear();
}

void Box2DDebugDraw::AddVertex(const b2Vec2& pos, const b2Color& color) {
    DebugVertex v;
    v.x = pos.x;
    v.y = pos.y;
    v.r = color.r;
    v.g = color.g;
    v.b = color.b;
    v.a = color.a;
    vertices_.push_back(v);
}

void Box2DDebugDraw::DrawPolygon(const b2Vec2* vertices, int32_t vertexCount, const b2Color& color) {
    for (int32_t i = 0; i < vertexCount; ++i) {
        AddVertex(vertices[i], color);
        AddVertex(vertices[(i + 1) % vertexCount], color);
    }
}

void Box2DDebugDraw::DrawSolidPolygon(const b2Vec2* vertices, int32_t vertexCount, const b2Color& color) {
    b2Color fillColor(color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, 0.5f);
    
    // Draw filled triangles
    for (int32_t i = 1; i < vertexCount - 1; ++i) {
        AddVertex(vertices[0], fillColor);
        AddVertex(vertices[i], fillColor);
        AddVertex(vertices[i + 1], fillColor);
    }
    
    // Draw outline
    DrawPolygon(vertices, vertexCount, color);
}

void Box2DDebugDraw::DrawCircle(const b2Vec2& center, float radius, const b2Color& color) {
    const int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * 2.0f * M_PI;
        float angle2 = (float)(i + 1) / segments * 2.0f * M_PI;
        b2Vec2 p1(center.x + radius * cosf(angle1), center.y + radius * sinf(angle1));
        b2Vec2 p2(center.x + radius * cosf(angle2), center.y + radius * sinf(angle2));
        AddVertex(p1, color);
        AddVertex(p2, color);
    }
}

void Box2DDebugDraw::DrawSolidCircle(const b2Vec2& center, float radius, const b2Vec2& axis, const b2Color& color) {
    b2Color fillColor(color.r * 0.5f, color.g * 0.5f, color.b * 0.5f, 0.5f);
    
    const int segments = 16;
    // Draw filled triangles
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * 2.0f * M_PI;
        float angle2 = (float)(i + 1) / segments * 2.0f * M_PI;
        b2Vec2 p1(center.x + radius * cosf(angle1), center.y + radius * sinf(angle1));
        b2Vec2 p2(center.x + radius * cosf(angle2), center.y + radius * sinf(angle2));
        AddVertex(center, fillColor);
        AddVertex(p1, fillColor);
        AddVertex(p2, fillColor);
    }
    
    // Draw outline
    DrawCircle(center, radius, color);
    
    // Draw axis line
    b2Vec2 p(center.x + radius * axis.x, center.y + radius * axis.y);
    DrawSegment(center, p, color);
}

void Box2DDebugDraw::DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color) {
    AddVertex(p1, color);
    AddVertex(p2, color);
}

void Box2DDebugDraw::DrawTransform(const b2Transform& xf) {
    const float axisScale = 0.4f;
    b2Vec2 p1 = xf.p;
    
    b2Vec2 p2;
    p2.x = p1.x + axisScale * xf.q.c;
    p2.y = p1.y + axisScale * xf.q.s;
    DrawSegment(p1, p2, b2Color(1, 0, 0));
    
    p2.x = p1.x - axisScale * xf.q.s;
    p2.y = p1.y + axisScale * xf.q.c;
    DrawSegment(p1, p2, b2Color(0, 1, 0));
}

void Box2DDebugDraw::DrawPoint(const b2Vec2& p, float size, const b2Color& color) {
    // Draw a small cross
    float halfSize = size * 0.5f;
    AddVertex(b2Vec2(p.x - halfSize, p.y), color);
    AddVertex(b2Vec2(p.x + halfSize, p.y), color);
    AddVertex(b2Vec2(p.x, p.y - halfSize), color);
    AddVertex(b2Vec2(p.x, p.y + halfSize), color);
}

// Box2DPhysics implementation
Box2DPhysics::Box2DPhysics() : debugDrawEnabled_(false) {
    b2Vec2 gravity(0.0f, -10.0f);
    world_ = std::make_unique<b2World>(gravity);
    debugDraw_ = std::make_unique<Box2DDebugDraw>();
    world_->SetDebugDraw(debugDraw_.get());
}

Box2DPhysics::~Box2DPhysics() {
    // Bodies are destroyed when the world is destroyed
    bodies_.clear();
}

void Box2DPhysics::setGravity(float x, float y) {
    world_->SetGravity(b2Vec2(x, y));
}

void Box2DPhysics::step(float timeStep, int32_t velocityIterations, int32_t positionIterations) {
    world_->Step(timeStep, velocityIterations, positionIterations);
    
    if (debugDrawEnabled_) {
        debugDraw_->Clear();
        world_->DebugDraw();
    }
}

int Box2DPhysics::createBody(int bodyType, float x, float y, float angle) {
    b2BodyDef bodyDef;
    
    if (bodyType == 0) {
        bodyDef.type = b2_staticBody;
    } else if (bodyType == 1) {
        bodyDef.type = b2_kinematicBody;
    } else {
        bodyDef.type = b2_dynamicBody;
    }
    
    bodyDef.position.Set(x, y);
    bodyDef.angle = angle;
    
    b2Body* body = world_->CreateBody(&bodyDef);
    assert(body != nullptr);
    
    bodies_.push_back(body);
    return bodies_.size() - 1;
}

void Box2DPhysics::destroyBody(int bodyId) {
    b2Body* body = getBody(bodyId);
    if (body) {
        world_->DestroyBody(body);
        bodies_[bodyId] = nullptr;
    }
}

b2Body* Box2DPhysics::getBody(int bodyId) {
    if (bodyId < 0 || bodyId >= (int)bodies_.size()) {
        return nullptr;
    }
    return bodies_[bodyId];
}

void Box2DPhysics::setBodyPosition(int bodyId, float x, float y) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->SetTransform(b2Vec2(x, y), body->GetAngle());
}

void Box2DPhysics::setBodyAngle(int bodyId, float angle) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->SetTransform(body->GetPosition(), angle);
}

void Box2DPhysics::setBodyLinearVelocity(int bodyId, float vx, float vy) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->SetLinearVelocity(b2Vec2(vx, vy));
}

void Box2DPhysics::setBodyAngularVelocity(int bodyId, float omega) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->SetAngularVelocity(omega);
}

void Box2DPhysics::applyForce(int bodyId, float fx, float fy, float px, float py) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->ApplyForce(b2Vec2(fx, fy), b2Vec2(px, py), true);
}

void Box2DPhysics::applyTorque(int bodyId, float torque) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    body->ApplyTorque(torque, true);
}

float Box2DPhysics::getBodyPositionX(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetPosition().x;
}

float Box2DPhysics::getBodyPositionY(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetPosition().y;
}

float Box2DPhysics::getBodyAngle(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetAngle();
}

float Box2DPhysics::getBodyLinearVelocityX(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetLinearVelocity().x;
}

float Box2DPhysics::getBodyLinearVelocityY(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetLinearVelocity().y;
}

float Box2DPhysics::getBodyAngularVelocity(int bodyId) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    return body->GetAngularVelocity();
}

void Box2DPhysics::addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density, float friction, float restitution) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    
    b2PolygonShape box;
    box.SetAsBox(halfWidth, halfHeight);
    
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &box;
    fixtureDef.density = density;
    fixtureDef.friction = friction;
    fixtureDef.restitution = restitution;
    
    body->CreateFixture(&fixtureDef);
}

void Box2DPhysics::addCircleFixture(int bodyId, float radius, float density, float friction, float restitution) {
    b2Body* body = getBody(bodyId);
    assert(body != nullptr);
    
    b2CircleShape circle;
    circle.m_radius = radius;
    
    b2FixtureDef fixtureDef;
    fixtureDef.shape = &circle;
    fixtureDef.density = density;
    fixtureDef.friction = friction;
    fixtureDef.restitution = restitution;
    
    body->CreateFixture(&fixtureDef);
}

void Box2DPhysics::enableDebugDraw(bool enable) {
    debugDrawEnabled_ = enable;
}

const std::vector<DebugVertex>& Box2DPhysics::getDebugVertices() {
    return debugDraw_->GetVertices();
}
