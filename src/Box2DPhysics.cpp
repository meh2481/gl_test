#include "Box2DPhysics.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Default fixed timestep for physics simulation (Box2D recommended value)
static constexpr float DEFAULT_FIXED_TIMESTEP = 1.0f / 250.0f;

// Helper function to convert b2HexColor to RGBA floats
static void hexColorToRGBA(b2HexColor hexColor, float& r, float& g, float& b, float& a) {
    r = ((hexColor >> 16) & 0xFF) / 255.0f;
    g = ((hexColor >> 8) & 0xFF) / 255.0f;
    b = (hexColor & 0xFF) / 255.0f;
    a = ((hexColor >> 24) & 0xFF) / 255.0f;

    // Box2D colors often have alpha=0, default to fully opaque
    if (a == 0.0f) {
        a = 1.0f;
    }
}

Box2DPhysics::Box2DPhysics() : nextBodyId_(0), nextJointId_(0), debugDrawEnabled_(false), stepThread_(nullptr),
                                timeAccumulator_(0.0f), fixedTimestep_(DEFAULT_FIXED_TIMESTEP) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -10.0f};
    worldId_ = b2CreateWorld(&worldDef);
    assert(b2World_IsValid(worldId_));

    physicsMutex_ = SDL_CreateMutex();
    assert(physicsMutex_ != nullptr);
    SDL_SetAtomicInt(&stepInProgress_, 0);
}

Box2DPhysics::~Box2DPhysics() {
    // Wait for any in-progress step to complete
    waitForStepComplete();

    if (b2World_IsValid(worldId_)) {
        b2DestroyWorld(worldId_);
    }

    if (physicsMutex_) {
        SDL_DestroyMutex(physicsMutex_);
    }
}

void Box2DPhysics::setGravity(float x, float y) {
    b2World_SetGravity(worldId_, (b2Vec2){x, y});
}

void Box2DPhysics::setFixedTimestep(float timestep) {
    assert(timestep > 0.0f);
    fixedTimestep_ = timestep;
}

void Box2DPhysics::step(float timeStep, int subStepCount) {
    SDL_LockMutex(physicsMutex_);

    // Accumulate the variable timestep
    timeAccumulator_ += timeStep;

    // Step the physics simulation in fixed increments
    // This ensures framerate-independent physics behavior
    while (timeAccumulator_ >= fixedTimestep_) {
        b2World_Step(worldId_, fixedTimestep_, subStepCount);
        timeAccumulator_ -= fixedTimestep_;
    }

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

    SDL_UnlockMutex(physicsMutex_);
}

int Box2DPhysics::physicsStepThread(void* data) {
    StepData* stepData = (StepData*)data;
    stepData->physics->step(stepData->timeStep, stepData->subStepCount);
    SDL_SetAtomicInt(&stepData->physics->stepInProgress_, 0);
    delete stepData;
    return 0;
}

void Box2DPhysics::stepAsync(float timeStep, int subStepCount) {
    // Don't start a new step if one is in progress
    if (SDL_GetAtomicInt(&stepInProgress_) != 0) {
        return;
    }

    SDL_SetAtomicInt(&stepInProgress_, 1);
    StepData* data = new StepData{this, timeStep, subStepCount};
    stepThread_ = SDL_CreateThread(physicsStepThread, "PhysicsStep", data);
    assert(stepThread_ != nullptr);
    SDL_DetachThread(stepThread_);
}

bool Box2DPhysics::isStepComplete() {
    return SDL_GetAtomicInt(&stepInProgress_) == 0;
}

void Box2DPhysics::waitForStepComplete() {
    while (SDL_GetAtomicInt(&stepInProgress_) != 0) {
        SDL_Delay(1);
    }
}

int Box2DPhysics::createBody(int bodyType, float x, float y, float angle) {
    SDL_LockMutex(physicsMutex_);

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

    SDL_UnlockMutex(physicsMutex_);
    return internalId;
}

void Box2DPhysics::destroyBody(int bodyId) {
    SDL_LockMutex(physicsMutex_);

    auto it = bodies_.find(bodyId);
    if (it != bodies_.end()) {
        b2DestroyBody(it->second);
        bodies_.erase(it);
    }

    SDL_UnlockMutex(physicsMutex_);
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

int Box2DPhysics::createRevoluteJoint(int bodyIdA, int bodyIdB, float anchorAx, float anchorAy,
                                       float anchorBx, float anchorBy, bool enableLimit,
                                       float lowerAngle, float upperAngle) {
    SDL_LockMutex(physicsMutex_);

    auto itA = bodies_.find(bodyIdA);
    auto itB = bodies_.find(bodyIdB);
    assert(itA != bodies_.end() && itB != bodies_.end());

    b2RevoluteJointDef jointDef = b2DefaultRevoluteJointDef();
    jointDef.bodyIdA = itA->second;
    jointDef.bodyIdB = itB->second;
    jointDef.localAnchorA = (b2Vec2){anchorAx, anchorAy};
    jointDef.localAnchorB = (b2Vec2){anchorBx, anchorBy};
    jointDef.enableLimit = enableLimit;
    jointDef.lowerAngle = lowerAngle;
    jointDef.upperAngle = upperAngle;
    jointDef.drawSize = 0.1f;

    b2JointId jointId = b2CreateRevoluteJoint(worldId_, &jointDef);
    assert(b2Joint_IsValid(jointId));

    int internalId = nextJointId_++;
    joints_[internalId] = jointId;

    SDL_UnlockMutex(physicsMutex_);
    return internalId;
}

void Box2DPhysics::destroyJoint(int jointId) {
    SDL_LockMutex(physicsMutex_);

    auto it = joints_.find(jointId);
    if (it != joints_.end()) {
        b2DestroyJoint(it->second);
        joints_.erase(it);
    }

    SDL_UnlockMutex(physicsMutex_);
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
