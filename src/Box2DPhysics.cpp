#include "Box2DPhysics.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Helper function to convert b2HexColor to RGBA floats
static void hexColorToRGBA(b2HexColor hexColor, float& r, float& g, float& b, float& a) {
    r = ((hexColor >> 16) & 0xFF) / 255.0f;
    g = ((hexColor >> 8) & 0xFF) / 255.0f;
    b = (hexColor & 0xFF) / 255.0f;
    a = ((hexColor >> 24) & 0xFF) / 255.0f;
}

Box2DPhysics::Box2DPhysics() : nextBodyId_(0), debugDrawEnabled_(false) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -10.0f};
    worldId_ = b2CreateWorld(&worldDef);
    assert(b2World_IsValid(worldId_));
}

Box2DPhysics::~Box2DPhysics() {
    if (b2World_IsValid(worldId_)) {
        b2DestroyWorld(worldId_);
    }
}

void Box2DPhysics::setGravity(float x, float y) {
    b2World_SetGravity(worldId_, (b2Vec2){x, y});
}

void Box2DPhysics::step(float timeStep, int subStepCount) {
    b2World_Step(worldId_, timeStep, subStepCount);

    if (debugDrawEnabled_) {
        debugLineVertices_.clear();
        debugTriangleVertices_.clear();

        b2DebugDraw debugDraw = {0};
        debugDraw.DrawPolygonFcn = DrawPolygon;
        debugDraw.DrawSolidPolygonFcn = DrawSolidPolygon;
        debugDraw.DrawCircleFcn = DrawCircle;
        debugDraw.DrawSolidCircleFcn = DrawSolidCircle;
        debugDraw.DrawSegmentFcn = DrawSegment;
        debugDraw.DrawTransformFcn = DrawTransform;
        debugDraw.DrawPointFcn = DrawPoint;
        debugDraw.context = this;
        debugDraw.drawShapes = true;
        debugDraw.drawJoints = true;
        debugDraw.drawBounds = false;
        debugDraw.drawMass = false;
        debugDraw.drawContacts = false;
        debugDraw.drawGraphColors = false;
        debugDraw.drawContactNormals = false;
        debugDraw.drawContactImpulses = false;
        debugDraw.drawFrictionImpulses = false;
        debugDraw.useDrawingBounds = false;

        b2World_Draw(worldId_, &debugDraw);
    }
}

int Box2DPhysics::createBody(int bodyType, float x, float y, float angle) {
    b2BodyDef bodyDef = b2DefaultBodyDef();

    if (bodyType == 0) {
        bodyDef.type = b2_staticBody;
    } else if (bodyType == 1) {
        bodyDef.type = b2_kinematicBody;
    } else {
        bodyDef.type = b2_dynamicBody;
    }

    bodyDef.position = (b2Vec2){x, y};
    bodyDef.rotation = b2MakeRot(angle);

    b2BodyId bodyId = b2CreateBody(worldId_, &bodyDef);
    assert(b2Body_IsValid(bodyId));

    int internalId = nextBodyId_++;
    bodies_[internalId] = bodyId;
    return internalId;
}

void Box2DPhysics::destroyBody(int bodyId) {
    auto it = bodies_.find(bodyId);
    if (it != bodies_.end()) {
        b2DestroyBody(it->second);
        bodies_.erase(it);
    }
}

void Box2DPhysics::setBodyPosition(int bodyId, float x, float y) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Rot rotation = b2Body_GetRotation(it->second);
    b2Body_SetTransform(it->second, (b2Vec2){x, y}, rotation);
}

void Box2DPhysics::setBodyAngle(int bodyId, float angle) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Vec2 position = b2Body_GetPosition(it->second);
    b2Body_SetTransform(it->second, position, b2MakeRot(angle));
}

void Box2DPhysics::setBodyLinearVelocity(int bodyId, float vx, float vy) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_SetLinearVelocity(it->second, (b2Vec2){vx, vy});
}

void Box2DPhysics::setBodyAngularVelocity(int bodyId, float omega) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_SetAngularVelocity(it->second, omega);
}

void Box2DPhysics::setBodyAwake(int bodyId, bool awake) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_SetAwake(it->second, awake);
}

void Box2DPhysics::applyForce(int bodyId, float fx, float fy, float px, float py) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_ApplyForce(it->second, (b2Vec2){fx, fy}, (b2Vec2){px, py}, true);
}

void Box2DPhysics::applyTorque(int bodyId, float torque) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_ApplyTorque(it->second, torque, true);
}

float Box2DPhysics::getBodyPositionX(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Vec2 position = b2Body_GetPosition(it->second);
    return position.x;
}

float Box2DPhysics::getBodyPositionY(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Vec2 position = b2Body_GetPosition(it->second);
    return position.y;
}

float Box2DPhysics::getBodyAngle(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Rot rotation = b2Body_GetRotation(it->second);
    return b2Rot_GetAngle(rotation);
}

float Box2DPhysics::getBodyLinearVelocityX(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Vec2 velocity = b2Body_GetLinearVelocity(it->second);
    return velocity.x;
}

float Box2DPhysics::getBodyLinearVelocityY(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Vec2 velocity = b2Body_GetLinearVelocity(it->second);
    return velocity.y;
}

float Box2DPhysics::getBodyAngularVelocity(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    return b2Body_GetAngularVelocity(it->second);
}

void Box2DPhysics::addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density, float friction, float restitution) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Polygon box = b2MakeBox(halfWidth, halfHeight);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;

    b2CreatePolygonShape(it->second, &shapeDef, &box);
}

void Box2DPhysics::addCircleFixture(int bodyId, float radius, float density, float friction, float restitution) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Circle circle;
    circle.center = (b2Vec2){0.0f, 0.0f};
    circle.radius = radius;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;

    b2CreateCircleShape(it->second, &shapeDef, &circle);
}

void Box2DPhysics::enableDebugDraw(bool enable) {
    debugDrawEnabled_ = enable;
}

const std::vector<DebugVertex>& Box2DPhysics::getDebugLineVertices() {
    return debugLineVertices_;
}

const std::vector<DebugVertex>& Box2DPhysics::getDebugTriangleVertices() {
    return debugTriangleVertices_;
}

void Box2DPhysics::addLineVertex(float x, float y, b2HexColor hexColor) {
    DebugVertex v;
    v.x = x;
    v.y = y;
    hexColorToRGBA(hexColor, v.r, v.g, v.b, v.a);
    debugLineVertices_.push_back(v);
}

void Box2DPhysics::addTriangleVertex(float x, float y, b2HexColor hexColor) {
    DebugVertex v;
    v.x = x;
    v.y = y;
    hexColorToRGBA(hexColor, v.r, v.g, v.b, v.a);
    debugTriangleVertices_.push_back(v);
}

void Box2DPhysics::DrawPolygon(const b2Vec2* vertices, int vertexCount, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);

    for (int i = 0; i < vertexCount; ++i) {
        physics->addLineVertex(vertices[i].x, vertices[i].y, color);
        physics->addLineVertex(vertices[(i + 1) % vertexCount].x, vertices[(i + 1) % vertexCount].y, color);
    }
}

void Box2DPhysics::DrawSolidPolygon(b2Transform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);

    // Draw filled triangles
    b2HexColor fillColor = static_cast<b2HexColor>((color & 0x00FFFFFF) | 0x80000000); // Make semi-transparent
    for (int i = 1; i < vertexCount - 1; ++i) {
        b2Vec2 v0 = b2TransformPoint(transform, vertices[0]);
        b2Vec2 v1 = b2TransformPoint(transform, vertices[i]);
        b2Vec2 v2 = b2TransformPoint(transform, vertices[i + 1]);

        physics->addTriangleVertex(v0.x, v0.y, fillColor);
        physics->addTriangleVertex(v1.x, v1.y, fillColor);
        physics->addTriangleVertex(v2.x, v2.y, fillColor);
    }

    // Draw outline
    for (int i = 0; i < vertexCount; ++i) {
        b2Vec2 v1 = b2TransformPoint(transform, vertices[i]);
        b2Vec2 v2 = b2TransformPoint(transform, vertices[(i + 1) % vertexCount]);
        physics->addLineVertex(v1.x, v1.y, color);
        physics->addLineVertex(v2.x, v2.y, color);
    }

    (void)radius; // Unused
}

void Box2DPhysics::DrawCircle(b2Vec2 center, float radius, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);

    const int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * 2.0f * M_PI;
        float angle2 = (float)(i + 1) / segments * 2.0f * M_PI;

        b2Vec2 p1 = {center.x + radius * cosf(angle1), center.y + radius * sinf(angle1)};
        b2Vec2 p2 = {center.x + radius * cosf(angle2), center.y + radius * sinf(angle2)};

        physics->addLineVertex(p1.x, p1.y, color);
        physics->addLineVertex(p2.x, p2.y, color);
    }
}

void Box2DPhysics::DrawSolidCircle(b2Transform transform, float radius, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);

    b2Vec2 center = transform.p;

    // Draw filled triangles
    b2HexColor fillColor = static_cast<b2HexColor>((color & 0x00FFFFFF) | 0x80000000); // Make semi-transparent
    const int segments = 16;
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * 2.0f * M_PI;
        float angle2 = (float)(i + 1) / segments * 2.0f * M_PI;

        b2Vec2 p1 = {center.x + radius * cosf(angle1), center.y + radius * sinf(angle1)};
        b2Vec2 p2 = {center.x + radius * cosf(angle2), center.y + radius * sinf(angle2)};

        physics->addTriangleVertex(center.x, center.y, fillColor);
        physics->addTriangleVertex(p1.x, p1.y, fillColor);
        physics->addTriangleVertex(p2.x, p2.y, fillColor);
    }

    // Draw outline
    for (int i = 0; i < segments; ++i) {
        float angle1 = (float)i / segments * 2.0f * M_PI;
        float angle2 = (float)(i + 1) / segments * 2.0f * M_PI;

        b2Vec2 p1 = {center.x + radius * cosf(angle1), center.y + radius * sinf(angle1)};
        b2Vec2 p2 = {center.x + radius * cosf(angle2), center.y + radius * sinf(angle2)};

        physics->addLineVertex(p1.x, p1.y, color);
        physics->addLineVertex(p2.x, p2.y, color);
    }

    // Draw axis line
    b2Vec2 axis = b2RotateVector(transform.q, (b2Vec2){radius, 0.0f});
    physics->addLineVertex(center.x, center.y, color);
    physics->addLineVertex(center.x + axis.x, center.y + axis.y, color);
}

void Box2DPhysics::DrawSegment(b2Vec2 p1, b2Vec2 p2, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);
    physics->addLineVertex(p1.x, p1.y, color);
    physics->addLineVertex(p2.x, p2.y, color);
}

void Box2DPhysics::DrawTransform(b2Transform xf, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);
    const float axisScale = 0.4f;

    b2Vec2 p1 = xf.p;

    // X-axis (red)
    b2Vec2 p2 = b2TransformPoint(xf, (b2Vec2){axisScale, 0.0f});
    physics->addLineVertex(p1.x, p1.y, static_cast<b2HexColor>(0xFFFF0000));
    physics->addLineVertex(p2.x, p2.y, static_cast<b2HexColor>(0xFFFF0000));

    // Y-axis (green)
    p2 = b2TransformPoint(xf, (b2Vec2){0.0f, axisScale});
    physics->addLineVertex(p1.x, p1.y, static_cast<b2HexColor>(0xFF00FF00));
    physics->addLineVertex(p2.x, p2.y, static_cast<b2HexColor>(0xFF00FF00));
}

void Box2DPhysics::DrawPoint(b2Vec2 p, float size, b2HexColor color, void* context) {
    Box2DPhysics* physics = static_cast<Box2DPhysics*>(context);
    float halfSize = size * 0.5f;

    // Draw a small cross
    physics->addLineVertex(p.x - halfSize, p.y, color);
    physics->addLineVertex(p.x + halfSize, p.y, color);
    physics->addLineVertex(p.x, p.y - halfSize, color);
    physics->addLineVertex(p.x, p.y + halfSize, color);
}
