#include "Box2DPhysics.h"
#include <cassert>
#include <cmath>
#include <iostream>

// Default fixed timestep for physics simulation (Box2D recommended value)
static constexpr float DEFAULT_FIXED_TIMESTEP = 1.0f / 250.0f;

// Sleep threshold in meters per second. Default Box2D value (0.05 m/s) causes visible
// movement when bodies go to sleep. Lower threshold keeps bodies active until movement
// is imperceptible.
static constexpr float SLEEP_THRESHOLD = 0.001f;

// Moh's hardness scale constants for calculating break force
// The scale is roughly logarithmic - each level is ~1.5x harder than the previous
static constexpr float MOH_SCALE_MULTIPLIER = 1.5f;
static constexpr float MOH_REFERENCE_LEVEL = 5.0f;  // Reference hardness level (like glass)
static constexpr float MOH_BASE_BREAK_SPEED = 3.0f;  // Base break speed at reference level (m/s)

// Brittleness constants for fracture behavior
static constexpr float MIN_SECONDARY_FRACTURE_BRITTLENESS = 0.3f;  // Min brittleness for secondary fractures
static constexpr float BRITTLENESS_REDUCTION_FACTOR = 0.8f;  // Reduces brittleness per generation to prevent infinite shattering

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
                                timeAccumulator_(0.0f), fixedTimestep_(DEFAULT_FIXED_TIMESTEP), mouseJointGroundBody_(b2_nullBodyId) {
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

    // Clear collision events from previous step
    collisionHitEvents_.clear();

    // Step the physics simulation in fixed increments
    // This ensures framerate-independent physics behavior
    while (timeAccumulator_ >= fixedTimestep_) {
        b2World_Step(worldId_, fixedTimestep_, subStepCount);
        timeAccumulator_ -= fixedTimestep_;

        // Process collision hit events after each physics step
        b2ContactEvents contactEvents = b2World_GetContactEvents(worldId_);
        for (int i = 0; i < contactEvents.hitCount; ++i) {
            const b2ContactHitEvent& hitEvent = contactEvents.hitEvents[i];

            b2BodyId bodyIdA = b2Shape_GetBody(hitEvent.shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(hitEvent.shapeIdB);

            int internalIdA = findInternalBodyId(bodyIdA);
            int internalIdB = findInternalBodyId(bodyIdB);

            if (internalIdA >= 0 || internalIdB >= 0) {
                CollisionHitEvent event;
                event.bodyIdA = internalIdA;
                event.bodyIdB = internalIdB;
                event.pointX = hitEvent.point.x;
                event.pointY = hitEvent.point.y;
                event.normalX = hitEvent.normal.x;
                event.normalY = hitEvent.normal.y;
                event.approachSpeed = hitEvent.approachSpeed;
                collisionHitEvents_.push_back(event);
            }
        }
    }

    // Process fractures for destructible objects (must be done after collecting all collision events)
    SDL_UnlockMutex(physicsMutex_);
    processFractures();
    SDL_LockMutex(physicsMutex_);

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
    bodyDef.sleepThreshold = SLEEP_THRESHOLD;

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

void Box2DPhysics::addPolygonFixture(int bodyId, const float* vertices, int vertexCount, float density, float friction, float restitution) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());
    assert(vertexCount >= 3 && vertexCount <= 8);

    b2Vec2 points[8];
    for (int i = 0; i < vertexCount; ++i) {
        points[i] = (b2Vec2){vertices[i * 2], vertices[i * 2 + 1]};
    }

    b2Hull hull = b2ComputeHull(points, vertexCount);
    b2Polygon polygon = b2MakePolygon(&hull, 0.0f);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;

    b2CreatePolygonShape(it->second, &shapeDef, &polygon);
}

void Box2DPhysics::addSegmentFixture(int bodyId, float x1, float y1, float x2, float y2, float friction, float restitution) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Segment segment;
    segment.point1 = (b2Vec2){x1, y1};
    segment.point2 = (b2Vec2){x2, y2};

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 0.0f; // Segments are typically static, so density is 0
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;

    b2CreateSegmentShape(it->second, &shapeDef, &segment);
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

// Callback context for overlap query
struct OverlapQueryContext {
    b2BodyId foundBodyId;
    bool found;
    b2Vec2 point;
};

// Small epsilon for point query AABB
static constexpr float POINT_QUERY_EPSILON = 0.00002f;

// Overlap callback to find a body at a point
static bool overlapCallback(b2ShapeId shapeId, void* context) {
    OverlapQueryContext* ctx = static_cast<OverlapQueryContext*>(context);
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    // Only consider dynamic bodies
    b2BodyType bodyType = b2Body_GetType(bodyId);
    if (bodyType != b2_dynamicBody) {
        return true; // Continue query
    }

    b2Transform transform = b2Body_GetTransform(bodyId);
    b2ShapeType shapeType = b2Shape_GetType(shapeId);
    bool overlaps = false;

    if (shapeType == b2_polygonShape) {
        b2Polygon polygon = b2Shape_GetPolygon(shapeId);
        b2Vec2 localPoint = b2InvTransformPoint(transform, ctx->point);
        int count = polygon.count;
        const b2Vec2* vertices = polygon.vertices;
        overlaps = true;
        for (int i = 0; i < count; ++i) {
            b2Vec2 a = vertices[i];
            b2Vec2 b = vertices[(i + 1) % count];
            b2Vec2 edge = b2Sub(b, a);
            b2Vec2 toPoint = b2Sub(localPoint, a);
            float cross = edge.x * toPoint.y - edge.y * toPoint.x;
            if (cross < 0) {
                overlaps = false;
                break;
            }
        }
    } else if (shapeType == b2_circleShape) {
        b2Circle circle = b2Shape_GetCircle(shapeId);
        b2Vec2 localPoint = b2InvTransformPoint(transform, ctx->point);
        float dx = localPoint.x - circle.center.x;
        float dy = localPoint.y - circle.center.y;
        overlaps = (dx * dx + dy * dy) <= (circle.radius * circle.radius);
    } else {
        // For other shapes, skip
        return true;
    }

    if (overlaps) {
        ctx->foundBodyId = bodyId;
        ctx->found = true;
        return false; // Stop query after finding first overlapping dynamic body
    }
    return true; // Continue query
}

int Box2DPhysics::queryBodyAtPoint(float x, float y) {
    SDL_LockMutex(physicsMutex_);

    // Create a small AABB around the point
    b2AABB aabb;
    aabb.lowerBound = (b2Vec2){x - POINT_QUERY_EPSILON, y - POINT_QUERY_EPSILON};
    aabb.upperBound = (b2Vec2){x + POINT_QUERY_EPSILON, y + POINT_QUERY_EPSILON};

    OverlapQueryContext ctx;
    ctx.found = false;
    ctx.point = (b2Vec2){x, y};

    b2QueryFilter filter = b2DefaultQueryFilter();
    b2World_OverlapAABB(worldId_, aabb, filter, overlapCallback, &ctx);

    int result = -1;
    if (ctx.found) {
        // Find the internal ID for this body
        for (const auto& pair : bodies_) {
            if (B2_ID_EQUALS(pair.second, ctx.foundBodyId)) {
                result = pair.first;
                break;
            }
        }
    }

    SDL_UnlockMutex(physicsMutex_);
    return result;
}

int Box2DPhysics::createMouseJoint(int bodyId, float targetX, float targetY, float maxForce) {
    SDL_LockMutex(physicsMutex_);

    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    // Create a static ground body for the mouse joint if not exists
    // (Mouse joint needs a static body as bodyA)
    if (!b2Body_IsValid(mouseJointGroundBody_)) {
        b2BodyDef groundDef = b2DefaultBodyDef();
        groundDef.type = b2_staticBody;
        groundDef.position = (b2Vec2){0.0f, 0.0f};
        mouseJointGroundBody_ = b2CreateBody(worldId_, &groundDef);
    }

    b2MouseJointDef jointDef = b2DefaultMouseJointDef();
    jointDef.bodyIdA = mouseJointGroundBody_;
    jointDef.bodyIdB = it->second;
    jointDef.target = (b2Vec2){targetX, targetY};
    jointDef.hertz = 4.0f;
    jointDef.dampingRatio = 0.7f;
    jointDef.maxForce = maxForce * b2Body_GetMass(it->second);

    b2JointId jointId = b2CreateMouseJoint(worldId_, &jointDef);
    assert(b2Joint_IsValid(jointId));

    int internalId = nextJointId_++;
    joints_[internalId] = jointId;

    // Wake up the body
    b2Body_SetAwake(it->second, true);

    SDL_UnlockMutex(physicsMutex_);
    return internalId;
}

void Box2DPhysics::updateMouseJointTarget(int jointId, float targetX, float targetY) {
    SDL_LockMutex(physicsMutex_);

    auto it = joints_.find(jointId);
    if (it != joints_.end()) {
        b2MouseJoint_SetTarget(it->second, (b2Vec2){targetX, targetY});
    }

    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::destroyMouseJoint(int jointId) {
    // Just use the regular destroyJoint function
    destroyJoint(jointId);
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
    float halfSize = size * 0.005f;

    // Draw a small cross
    physics->addLineVertex(p.x - halfSize, p.y, color);
    physics->addLineVertex(p.x + halfSize, p.y, color);
    physics->addLineVertex(p.x, p.y - halfSize, color);
    physics->addLineVertex(p.x, p.y + halfSize, color);
}

int Box2DPhysics::findInternalBodyId(b2BodyId bodyId) {
    for (const auto& pair : bodies_) {
        if (B2_ID_EQUALS(pair.second, bodyId)) {
            return pair.first;
        }
    }
    return -1;
}

// Destructible object management
void Box2DPhysics::setBodyDestructible(int bodyId, float strength, float brittleness,
                                        const float* vertices, int vertexCount,
                                        uint64_t textureId, uint64_t normalMapId, int pipelineId) {
    assert(vertexCount >= 3 && vertexCount <= 8);

    DestructibleProperties props;
    props.strength = strength;
    props.brittleness = brittleness;
    props.isDestructible = true;
    props.textureId = textureId;
    props.normalMapId = normalMapId;
    props.pipelineId = pipelineId;
    props.originalVertexCount = vertexCount;

    for (int i = 0; i < vertexCount * 2; ++i) {
        props.originalVertices[i] = vertices[i];
    }

    destructibles_[bodyId] = props;
}

void Box2DPhysics::clearBodyDestructible(int bodyId) {
    destructibles_.erase(bodyId);
}

bool Box2DPhysics::isBodyDestructible(int bodyId) const {
    auto it = destructibles_.find(bodyId);
    return it != destructibles_.end() && it->second.isDestructible;
}

const DestructibleProperties* Box2DPhysics::getDestructibleProperties(int bodyId) const {
    auto it = destructibles_.find(bodyId);
    if (it != destructibles_.end()) {
        return &it->second;
    }
    return nullptr;
}

// Calculate polygon area using shoelace formula
float Box2DPhysics::calculatePolygonArea(const float* vertices, int vertexCount) {
    float area = 0.0f;
    for (int i = 0; i < vertexCount; ++i) {
        int j = (i + 1) % vertexCount;
        float x0 = vertices[i * 2];
        float y0 = vertices[i * 2 + 1];
        float x1 = vertices[j * 2];
        float y1 = vertices[j * 2 + 1];
        area += x0 * y1 - x1 * y0;
    }
    return fabsf(area) * 0.5f;
}

// Calculate break force based on Moh's hardness scale
float Box2DPhysics::calculateBreakForce(float strength, float impactSpeed) const {
    // Moh's scale is roughly logarithmic - each level is ~1.5x harder than the previous
    float scaleFactor = powf(MOH_SCALE_MULTIPLIER, strength - MOH_REFERENCE_LEVEL);
    return MOH_BASE_BREAK_SPEED * scaleFactor;
}

// Calculate fragment count based on brittleness and impact energy
int Box2DPhysics::calculateFragmentCount(float brittleness, float impactSpeed, float strength) const {
    // Base fragments: 2
    // Brittleness 0.0 = always 2 pieces
    // Brittleness 1.0 = can shatter into many pieces based on impact
    float breakThreshold = calculateBreakForce(strength, impactSpeed);
    float excessEnergy = (impactSpeed - breakThreshold) / breakThreshold;

    // More brittleness + more excess energy = more fragments
    float fragmentFloat = 2.0f + brittleness * excessEnergy * 4.0f;
    int fragments = (int)fragmentFloat;

    // Clamp to valid range (2-8 fragments)
    if (fragments < 2) fragments = 2;
    if (fragments > 8) fragments = 8;

    return fragments;
}

// Split polygon along a line
void Box2DPhysics::splitPolygon(const float* vertices, int vertexCount,
                                 float lineX, float lineY, float lineDirX, float lineDirY,
                                 DestructiblePolygon& poly1, DestructiblePolygon& poly2) {
    // Line perpendicular normal
    float lineNormX = -lineDirY;
    float lineNormY = lineDirX;

    // Classify vertices as on positive or negative side of line
    float sides[8];
    for (int i = 0; i < vertexCount; ++i) {
        float vx = vertices[i * 2] - lineX;
        float vy = vertices[i * 2 + 1] - lineY;
        sides[i] = vx * lineNormX + vy * lineNormY;
    }

    poly1.vertexCount = 0;
    poly2.vertexCount = 0;

    for (int i = 0; i < vertexCount; ++i) {
        int j = (i + 1) % vertexCount;

        float x0 = vertices[i * 2];
        float y0 = vertices[i * 2 + 1];
        float x1 = vertices[j * 2];
        float y1 = vertices[j * 2 + 1];

        // Add vertex to appropriate polygon
        if (sides[i] >= 0 && poly1.vertexCount < 8) {
            poly1.vertices[poly1.vertexCount * 2] = x0;
            poly1.vertices[poly1.vertexCount * 2 + 1] = y0;
            poly1.vertexCount++;
        }
        if (sides[i] < 0 && poly2.vertexCount < 8) {
            poly2.vertices[poly2.vertexCount * 2] = x0;
            poly2.vertices[poly2.vertexCount * 2 + 1] = y0;
            poly2.vertexCount++;
        }

        // Check for edge crossing
        if ((sides[i] >= 0) != (sides[j] >= 0)) {
            // Calculate intersection point
            float t = sides[i] / (sides[i] - sides[j]);
            float intersectX = x0 + t * (x1 - x0);
            float intersectY = y0 + t * (y1 - y0);

            // Add intersection to both polygons
            if (poly1.vertexCount < 8) {
                poly1.vertices[poly1.vertexCount * 2] = intersectX;
                poly1.vertices[poly1.vertexCount * 2 + 1] = intersectY;
                poly1.vertexCount++;
            }
            if (poly2.vertexCount < 8) {
                poly2.vertices[poly2.vertexCount * 2] = intersectX;
                poly2.vertices[poly2.vertexCount * 2 + 1] = intersectY;
                poly2.vertexCount++;
            }
        }
    }

    // Calculate areas
    poly1.area = (poly1.vertexCount >= 3) ? calculatePolygonArea(poly1.vertices, poly1.vertexCount) : 0.0f;
    poly2.area = (poly2.vertexCount >= 3) ? calculatePolygonArea(poly2.vertices, poly2.vertexCount) : 0.0f;
}

// Calculate fracture based on impact
FractureResult Box2DPhysics::calculateFracture(const DestructibleProperties& props,
                                                float impactX, float impactY,
                                                float normalX, float normalY,
                                                float impactSpeed,
                                                float bodyX, float bodyY, float bodyAngle) {
    FractureResult result;
    result.fragmentCount = 0;

    // Transform impact point to local coordinates
    float cosA = cosf(-bodyAngle);
    float sinA = sinf(-bodyAngle);
    float localImpactX = (impactX - bodyX) * cosA - (impactY - bodyY) * sinA;
    float localImpactY = (impactX - bodyX) * sinA + (impactY - bodyY) * cosA;

    // Transform normal to local coordinates
    float localNormalX = normalX * cosA - normalY * sinA;
    float localNormalY = normalX * sinA + normalY * cosA;

    // Calculate primary fracture line perpendicular to impact normal
    // This creates a crack through the impact point
    float fractureDirX = -localNormalY;
    float fractureDirY = localNormalX;

    // Start with the original polygon
    DestructiblePolygon currentPoly;
    currentPoly.vertexCount = props.originalVertexCount;
    for (int i = 0; i < props.originalVertexCount * 2; ++i) {
        currentPoly.vertices[i] = props.originalVertices[i];
    }
    currentPoly.area = calculatePolygonArea(currentPoly.vertices, currentPoly.vertexCount);

    // Split the polygon along the fracture line
    DestructiblePolygon poly1, poly2;
    splitPolygon(currentPoly.vertices, currentPoly.vertexCount,
                 localImpactX, localImpactY, fractureDirX, fractureDirY,
                 poly1, poly2);

    // Add valid fragments
    if (poly1.vertexCount >= 3 && poly1.area > 0.0001f) {
        result.fragments[result.fragmentCount++] = poly1;
    }
    if (poly2.vertexCount >= 3 && poly2.area > 0.0001f && result.fragmentCount < 8) {
        result.fragments[result.fragmentCount++] = poly2;
    }

    // For high brittleness, add secondary fractures
    if (props.brittleness > MIN_SECONDARY_FRACTURE_BRITTLENESS && result.fragmentCount >= 2) {
        // Calculate secondary fracture angle based on brittleness
        float secondaryAngle = M_PI * 0.3f + (props.brittleness - MIN_SECONDARY_FRACTURE_BRITTLENESS) * M_PI * 0.3f;

        // Try to split the larger fragment
        int largestIdx = (result.fragments[0].area > result.fragments[1].area) ? 0 : 1;
        DestructiblePolygon& largest = result.fragments[largestIdx];

        if (largest.vertexCount >= 4) {
            // Calculate center of the largest fragment
            float centerX = 0, centerY = 0;
            for (int i = 0; i < largest.vertexCount; ++i) {
                centerX += largest.vertices[i * 2];
                centerY += largest.vertices[i * 2 + 1];
            }
            centerX /= largest.vertexCount;
            centerY /= largest.vertexCount;

            // Rotated fracture direction
            float cosB = cosf(secondaryAngle);
            float sinB = sinf(secondaryAngle);
            float secondaryDirX = fractureDirX * cosB - fractureDirY * sinB;
            float secondaryDirY = fractureDirX * sinB + fractureDirY * cosB;

            DestructiblePolygon sub1, sub2;
            splitPolygon(largest.vertices, largest.vertexCount,
                        centerX, centerY, secondaryDirX, secondaryDirY,
                        sub1, sub2);

            // Replace the largest with its fragments
            if (sub1.vertexCount >= 3 && sub2.vertexCount >= 3 &&
                sub1.area > 0.0001f && sub2.area > 0.0001f) {
                result.fragments[largestIdx] = sub1;
                if (result.fragmentCount < 8) {
                    result.fragments[result.fragmentCount++] = sub2;
                }
            }
        }
    }

    return result;
}

// Create a fragment body with proper physics
int Box2DPhysics::createFragmentBody(float x, float y, float angle,
                                      const DestructiblePolygon& polygon,
                                      float vx, float vy, float angularVel,
                                      float density, float friction, float restitution) {
    if (polygon.vertexCount < 3) return -1;

    SDL_LockMutex(physicsMutex_);

    // Calculate centroid of the fragment
    float centroidX = 0, centroidY = 0;
    for (int i = 0; i < polygon.vertexCount; ++i) {
        centroidX += polygon.vertices[i * 2];
        centroidY += polygon.vertices[i * 2 + 1];
    }
    centroidX /= polygon.vertexCount;
    centroidY /= polygon.vertexCount;

    // Transform centroid to world coordinates
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    float worldCentroidX = x + centroidX * cosA - centroidY * sinA;
    float worldCentroidY = y + centroidX * sinA + centroidY * cosA;

    // Create body at fragment centroid
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){worldCentroidX, worldCentroidY};
    bodyDef.rotation = b2MakeRot(angle);
    bodyDef.linearVelocity = (b2Vec2){vx, vy};
    bodyDef.angularVelocity = angularVel;
    bodyDef.sleepThreshold = SLEEP_THRESHOLD;

    b2BodyId bodyId = b2CreateBody(worldId_, &bodyDef);
    assert(b2Body_IsValid(bodyId));

    // Create polygon shape with vertices relative to centroid
    b2Vec2 points[8];
    for (int i = 0; i < polygon.vertexCount; ++i) {
        points[i] = (b2Vec2){
            polygon.vertices[i * 2] - centroidX,
            polygon.vertices[i * 2 + 1] - centroidY
        };
    }

    b2Hull hull = b2ComputeHull(points, polygon.vertexCount);
    if (hull.count >= 3) {
        b2Polygon poly = b2MakePolygon(&hull, 0.0f);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        // Scale density by area ratio to maintain consistent mass behavior
        shapeDef.density = density;
        shapeDef.material.friction = friction;
        shapeDef.material.restitution = restitution;

        b2CreatePolygonShape(bodyId, &shapeDef, &poly);
    }

    int internalId = nextBodyId_++;
    bodies_[internalId] = bodyId;

    SDL_UnlockMutex(physicsMutex_);
    return internalId;
}

// Process fractures for destructible bodies
void Box2DPhysics::processFractures() {
    fractureEvents_.clear();

    // Process each collision event
    for (const auto& hit : collisionHitEvents_) {
        int destructibleId = -1;
        int otherBodyId = -1;

        // Check if either body is destructible
        if (isBodyDestructible(hit.bodyIdA)) {
            destructibleId = hit.bodyIdA;
            otherBodyId = hit.bodyIdB;
        } else if (isBodyDestructible(hit.bodyIdB)) {
            destructibleId = hit.bodyIdB;
            otherBodyId = hit.bodyIdA;
        }

        if (destructibleId < 0) continue;

        const DestructibleProperties* props = getDestructibleProperties(destructibleId);
        if (!props) continue;

        // Check if impact exceeds break threshold
        float breakForce = calculateBreakForce(props->strength, hit.approachSpeed);
        if (hit.approachSpeed < breakForce) continue;

        // Check if already pending destruction
        bool alreadyPending = false;
        for (int pending : pendingDestructions_) {
            if (pending == destructibleId) {
                alreadyPending = true;
                break;
            }
        }
        if (alreadyPending) continue;

        // Get body state
        auto bodyIt = bodies_.find(destructibleId);
        if (bodyIt == bodies_.end()) continue;

        b2Vec2 pos = b2Body_GetPosition(bodyIt->second);
        float angle = b2Rot_GetAngle(b2Body_GetRotation(bodyIt->second));
        b2Vec2 vel = b2Body_GetLinearVelocity(bodyIt->second);
        float angularVel = b2Body_GetAngularVelocity(bodyIt->second);

        // Calculate fracture
        FractureResult fracture = calculateFracture(*props,
                                                     hit.pointX, hit.pointY,
                                                     hit.normalX, hit.normalY,
                                                     hit.approachSpeed,
                                                     pos.x, pos.y, angle);

        if (fracture.fragmentCount < 2) continue;

        // Create fracture event
        FractureEvent event;
        event.originalBodyId = destructibleId;
        event.originalLayerId = -1;  // Will be set by caller
        event.fragmentCount = fracture.fragmentCount;
        event.impactPointX = hit.pointX;
        event.impactPointY = hit.pointY;
        event.impactNormalX = hit.normalX;
        event.impactNormalY = hit.normalY;
        event.impactSpeed = hit.approachSpeed;

        // Create fragment bodies
        for (int i = 0; i < fracture.fragmentCount; ++i) {
            int fragBodyId = createFragmentBody(pos.x, pos.y, angle,
                                                 fracture.fragments[i],
                                                 vel.x, vel.y, angularVel,
                                                 1.0f, 0.3f, 0.3f);
            event.newBodyIds[i] = fragBodyId;
            event.newLayerIds[i] = -1;  // Will be set by caller
            event.fragmentAreas[i] = fracture.fragments[i].area;

            // Make fragments also destructible if original was brittle enough
            // Reduce brittleness to prevent infinite shattering
            if (props->brittleness > 0.5f && fragBodyId >= 0) {
                setBodyDestructible(fragBodyId, props->strength,
                                   props->brittleness * BRITTLENESS_REDUCTION_FACTOR,
                                   fracture.fragments[i].vertices, fracture.fragments[i].vertexCount,
                                   props->textureId, props->normalMapId, props->pipelineId);
            }
        }

        fractureEvents_.push_back(event);
        pendingDestructions_.push_back(destructibleId);

        // Call fracture callback if set
        if (fractureCallback_) {
            fractureCallback_(event);
        }
    }

    // Destroy pending bodies
    for (int bodyId : pendingDestructions_) {
        clearBodyDestructible(bodyId);
        destroyBody(bodyId);
    }
    pendingDestructions_.clear();
}
