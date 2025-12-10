#include "Box2DPhysics.h"
#include "scene/SceneLayer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

// Default fixed timestep for physics simulation (Box2D recommended value)
static constexpr float DEFAULT_FIXED_TIMESTEP = 1.0f / 250.0f;

// Sleep threshold in meters per second. Default Box2D value (0.05 m/s) causes visible
// movement when bodies go to sleep. Lower threshold keeps bodies active until movement
// is imperceptible.
static constexpr float SLEEP_THRESHOLD = 0.001f;

// Mohs hardness scale constants for calculating break force
// The scale is roughly logarithmic - each level is ~1.3x harder than the previous
// Adjusted so strength 0.5 behaves like real-world hardness ~4, strength 4 like glass (5)
static constexpr float MOHS_SCALE_MULTIPLIER = 1.3f;
static constexpr float MOHS_REFERENCE_LEVEL = 4.0f;  // Reference hardness level (like calcite/fluorite)
static constexpr float MOHS_BASE_BREAK_SPEED = 2.0f;  // Base break speed at reference level (m/s)

// Brittleness constants for fracture behavior
static constexpr float MIN_SECONDARY_FRACTURE_BRITTLENESS = 0.3f;  // Min brittleness for secondary fractures
static constexpr float MIN_FRAGMENT_AREA = 0.001f;  // Minimum fragment area - objects smaller than this disappear
static constexpr float MIN_REFRACTURE_AREA_MULTIPLIER = 4.0f;  // Fragments must be this many times MIN_FRAGMENT_AREA to be refracturable
static constexpr float MIN_FRAGMENT_LAYER_SIZE = 0.04f;  // Minimum layer size for fragments

// Minimum bounding box dimension for UV mapping (prevents division by zero)
static constexpr float MIN_DIMENSION_FOR_UV_MAPPING = 0.0001f;

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
                                timeAccumulator_(0.0f), fixedTimestep_(DEFAULT_FIXED_TIMESTEP), mouseJointGroundBody_(b2_nullBodyId),
                                nextForceFieldId_(0) {
    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){0.0f, -10.0f};
    worldDef.hitEventThreshold = 0.0f;
    // Increase contact stiffness for faster overlap resolution and reduce sinking
    // Default is 30 Hz with damping ratio 10, which causes slow overlap recovery
    // Higher hertz = stiffer contacts = faster overlap resolution
    worldDef.contactHertz = 120.0f;
    // Lower damping ratio = more responsive overlap correction (default is 10)
    worldDef.contactDampingRatio = 5.0f;
    worldId_ = b2CreateWorld(&worldDef);
    assert(b2World_IsValid(worldId_));

    // Ensure hit event threshold is set
    b2World_SetHitEventThreshold(worldId_, 0.0f);

    physicsMutex_ = SDL_CreateMutex();
    assert(physicsMutex_ != nullptr);
    SDL_SetAtomicInt(&stepInProgress_, 0);

    b2SetLengthUnitsPerMeter(LENGTH_UNITS_PER_METER);
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

        // Apply force fields AFTER the world step using fresh overlap data
        // Forces will be applied in the next step
        applyForceFields();
        applyRadialForceFields();

        // Process collision hit events after each physics step
        b2ContactEvents contactEvents = b2World_GetContactEvents(worldId_);
        for (int i = 0; i < contactEvents.beginCount; ++i) {
            const b2ContactBeginTouchEvent& beginEvent = contactEvents.beginEvents[i];
            b2BodyId bodyIdA = b2Shape_GetBody(beginEvent.shapeIdA);
            b2BodyId bodyIdB = b2Shape_GetBody(beginEvent.shapeIdB);
            b2Vec2 velA = b2Body_GetLinearVelocity(bodyIdA);
            b2Vec2 velB = b2Body_GetLinearVelocity(bodyIdB);
            b2Vec2 relativeVel = b2Sub(velA, velB);
            float approachSpeed = -b2Dot(relativeVel, beginEvent.manifold.normal);
            if (approachSpeed > 0.0f) {
                // Treat as hit event
                int internalIdA = findInternalBodyId(bodyIdA);
                int internalIdB = findInternalBodyId(bodyIdB);
                if (internalIdA >= 0 || internalIdB >= 0) {
                    CollisionHitEvent event;
                    event.bodyIdA = internalIdA;
                    event.bodyIdB = internalIdB;
                    event.pointX = beginEvent.manifold.points[0].point.x; // Use first contact point
                    event.pointY = beginEvent.manifold.points[0].point.y;
                    event.normalX = beginEvent.manifold.normal.x;
                    event.normalY = beginEvent.manifold.normal.y;
                    event.approachSpeed = approachSpeed;
                    collisionHitEvents_.push_back(event);

                    if (collisionCallback_ && internalIdA >= 0 && internalIdB >= 0) {
                        collisionCallback_(internalIdA, internalIdB, event.pointX, event.pointY, event.normalX, event.normalY, event.approachSpeed);
                    }
                }
            }
        }
    }

    // Process sensor events after each physics step
    b2SensorEvents sensorEvents = b2World_GetSensorEvents(worldId_);
    for (int i = 0; i < sensorEvents.beginCount; ++i) {
        const b2SensorBeginTouchEvent& beginEvent = sensorEvents.beginEvents[i];
        if (!b2Shape_IsValid(beginEvent.sensorShapeId) || !b2Shape_IsValid(beginEvent.visitorShapeId)) continue;
        b2BodyId sensorBody = b2Shape_GetBody(beginEvent.sensorShapeId);
        b2BodyId visitorBody = b2Shape_GetBody(beginEvent.visitorShapeId);
        b2Vec2 visitorPos = b2Body_GetPosition(visitorBody);
        b2Vec2 visitorVel = b2Body_GetLinearVelocity(visitorBody);
        int sensorInternalId = findInternalBodyId(sensorBody);
        int visitorInternalId = findInternalBodyId(visitorBody);
        if (sensorInternalId >= 0 && visitorInternalId >= 0 && sensorCallback_) {
            SensorEvent event;
            event.sensorBodyId = sensorInternalId;
            event.visitorBodyId = visitorInternalId;
            event.visitorX = visitorPos.x;
            event.visitorY = visitorPos.y;
            event.visitorVelX = visitorVel.x;
            event.visitorVelY = visitorVel.y;
            event.isBegin = true;
            sensorCallback_(event);
        }
    }
    for (int i = 0; i < sensorEvents.endCount; ++i) {
        const b2SensorEndTouchEvent& endEvent = sensorEvents.endEvents[i];
        if (!b2Shape_IsValid(endEvent.sensorShapeId) || !b2Shape_IsValid(endEvent.visitorShapeId)) continue;
        b2BodyId sensorBody = b2Shape_GetBody(endEvent.sensorShapeId);
        b2BodyId visitorBody = b2Shape_GetBody(endEvent.visitorShapeId);
        b2Vec2 visitorPos = b2Body_GetPosition(visitorBody);
        b2Vec2 visitorVel = b2Body_GetLinearVelocity(visitorBody);
        int sensorInternalId = findInternalBodyId(sensorBody);
        int visitorInternalId = findInternalBodyId(visitorBody);
        if (sensorInternalId >= 0 && visitorInternalId >= 0 && sensorCallback_) {
            SensorEvent event;
            event.sensorBodyId = sensorInternalId;
            event.visitorBodyId = visitorInternalId;
            event.visitorX = visitorPos.x;
            event.visitorY = visitorPos.y;
            event.visitorVelX = visitorVel.x;
            event.visitorVelY = visitorVel.y;
            event.isBegin = false;
            sensorCallback_(event);
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

void Box2DPhysics::enableBody(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_Enable(it->second);
}

void Box2DPhysics::disableBody(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Body_Disable(it->second);
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

bool Box2DPhysics::isBodyValid(int bodyId) const {
    return bodies_.find(bodyId) != bodies_.end();
}

void Box2DPhysics::addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density, float friction, float restitution) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Polygon box = b2MakeBox(halfWidth, halfHeight);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = density;
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;
    shapeDef.enableContactEvents = true;
    shapeDef.enableSensorEvents = true;

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
    shapeDef.enableContactEvents = true;
    shapeDef.enableSensorEvents = true;

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
    shapeDef.enableContactEvents = true;
    shapeDef.enableSensorEvents = true;

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
    shapeDef.enableContactEvents = true;

    b2CreateSegmentShape(it->second, &shapeDef, &segment);
}

void Box2DPhysics::addCircleSensor(int bodyId, float radius) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    b2Circle circle;
    circle.center = (b2Vec2){0.0f, 0.0f};
    circle.radius = radius;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.isSensor = true;
    shapeDef.enableSensorEvents = true;

    b2CreateCircleShape(it->second, &shapeDef, &circle);
}

void Box2DPhysics::addPolygonSensor(int bodyId, const float* vertices, int vertexCount) {
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
    shapeDef.isSensor = true;
    shapeDef.enableSensorEvents = true;

    b2CreatePolygonShape(it->second, &shapeDef, &polygon);
}

void Box2DPhysics::clearAllFixtures(int bodyId) {
    auto it = bodies_.find(bodyId);
    assert(it != bodies_.end());

    int shapeCount = b2Body_GetShapeCount(it->second);
    if (shapeCount > 0) {
        b2ShapeId shapes[16];
        int actualCount = b2Body_GetShapes(it->second, shapes, 16);
        for (int i = 0; i < actualCount; ++i) {
            b2DestroyShape(shapes[i], true);
        }
    }
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
        if (b2Joint_IsValid(it->second)) {
            b2DestroyJoint(it->second);
        }
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

    // Calculate bounding box for UV mapping
    float minX = vertices[0], maxX = vertices[0];
    float minY = vertices[1], maxY = vertices[1];

    for (int i = 0; i < vertexCount; ++i) {
        float x = vertices[i * 2];
        float y = vertices[i * 2 + 1];
        props.originalVertices[i * 2] = x;
        props.originalVertices[i * 2 + 1] = y;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
    }

    props.originalMinX = minX;
    props.originalMinY = minY;
    props.originalWidth = maxX - minX;
    props.originalHeight = maxY - minY;

    // Prevent division by zero in UV mapping
    if (props.originalWidth < MIN_DIMENSION_FOR_UV_MAPPING) props.originalWidth = MIN_DIMENSION_FOR_UV_MAPPING;
    if (props.originalHeight < MIN_DIMENSION_FOR_UV_MAPPING) props.originalHeight = MIN_DIMENSION_FOR_UV_MAPPING;

    // Default root bounds to same as original (not a fragment)
    props.hasRootBounds = false;
    props.rootMinX = props.originalMinX;
    props.rootMinY = props.originalMinY;
    props.rootWidth = props.originalWidth;
    props.rootHeight = props.originalHeight;

    // Default to no atlas
    props.usesAtlas = false;
    props.atlasU0 = 0.0f;
    props.atlasV0 = 0.0f;
    props.atlasU1 = 1.0f;
    props.atlasV1 = 1.0f;
    props.atlasTextureId = textureId;

    // Default to no normal map atlas
    props.usesNormalMapAtlas = false;
    props.normalAtlasU0 = 0.0f;
    props.normalAtlasV0 = 0.0f;
    props.normalAtlasU1 = 1.0f;
    props.normalAtlasV1 = 1.0f;
    props.atlasNormalMapId = normalMapId;

    destructibles_[bodyId] = props;
}

void Box2DPhysics::setBodyDestructibleAtlasUV(int bodyId, uint64_t atlasTextureId,
                                               float u0, float v0, float u1, float v1) {
    auto it = destructibles_.find(bodyId);
    if (it != destructibles_.end()) {
        it->second.usesAtlas = true;
        it->second.atlasU0 = u0;
        it->second.atlasV0 = v0;
        it->second.atlasU1 = u1;
        it->second.atlasV1 = v1;
        it->second.atlasTextureId = atlasTextureId;
    }
}

void Box2DPhysics::setBodyDestructibleNormalMapAtlasUV(int bodyId, uint64_t atlasNormalMapId,
                                                        float u0, float v0, float u1, float v1) {
    auto it = destructibles_.find(bodyId);
    if (it != destructibles_.end()) {
        it->second.usesNormalMapAtlas = true;
        it->second.normalAtlasU0 = u0;
        it->second.normalAtlasV0 = v0;
        it->second.normalAtlasU1 = u1;
        it->second.normalAtlasV1 = v1;
        it->second.atlasNormalMapId = atlasNormalMapId;
    }
}

void Box2DPhysics::setBodyDestructibleRootBounds(int bodyId, float minX, float minY, float width, float height) {
    auto it = destructibles_.find(bodyId);
    if (it != destructibles_.end()) {
        it->second.hasRootBounds = true;
        it->second.rootMinX = minX;
        it->second.rootMinY = minY;
        it->second.rootWidth = width;
        it->second.rootHeight = height;
    }
}

void Box2DPhysics::clearBodyDestructible(int bodyId) {
    destructibles_.erase(bodyId);
    destructibleBodyLayers_.erase(bodyId);
}

void Box2DPhysics::setBodyDestructibleLayer(int bodyId, int layerId) {
    destructibleBodyLayers_[bodyId] = layerId;
}

void Box2DPhysics::cleanupAllFragments() {
    // Destroy all fragment layers
    if (layerManager_) {
        for (int layerId : fragmentLayerIds_) {
            layerManager_->destroyLayer(layerId);
        }
    }
    fragmentLayerIds_.clear();

    // Destroy all fragment bodies
    for (int bodyId : fragmentBodyIds_) {
        clearBodyDestructible(bodyId);
        destroyBody(bodyId);
    }
    fragmentBodyIds_.clear();
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

// Convert a DestructiblePolygon to FragmentPolygon with UV coordinates
FragmentPolygon Box2DPhysics::createFragmentWithUVs(const DestructiblePolygon& poly,
                                                      const DestructibleProperties& props) {
    FragmentPolygon result;
    memset(&result, 0, sizeof(result));
    result.vertexCount = poly.vertexCount;
    result.area = poly.area;

    // Calculate centroid
    result.centroidX = 0.0f;
    result.centroidY = 0.0f;
    for (int i = 0; i < poly.vertexCount; ++i) {
        result.centroidX += poly.vertices[i * 2];
        result.centroidY += poly.vertices[i * 2 + 1];
    }
    result.centroidX /= poly.vertexCount;
    result.centroidY /= poly.vertexCount;

    // Use root bounds if available (for recursive fractures), otherwise use original bounds
    float boundsMinX = props.hasRootBounds ? props.rootMinX : props.originalMinX;
    float boundsMinY = props.hasRootBounds ? props.rootMinY : props.originalMinY;
    float boundsWidth = props.hasRootBounds ? props.rootWidth : props.originalWidth;
    float boundsHeight = props.hasRootBounds ? props.rootHeight : props.originalHeight;

    // Copy vertices and calculate UVs based on position within root bounding box
    for (int i = 0; i < poly.vertexCount; ++i) {
        float x = poly.vertices[i * 2];
        float y = poly.vertices[i * 2 + 1];

        // Store vertex (relative to centroid for proper local coordinates)
        result.vertices[i * 2] = x - result.centroidX;
        result.vertices[i * 2 + 1] = y - result.centroidY;

        // Calculate UV coordinates based on position in root bounding box
        // localU/localV are normalized 0-1 within the root polygon bounds
        float localU = (x - boundsMinX) / boundsWidth;
        float localV = (y - boundsMinY) / boundsHeight;

        // Clamp to valid range
        if (localU < 0.0f) localU = 0.0f;
        if (localU > 1.0f) localU = 1.0f;
        if (localV < 0.0f) localV = 0.0f;
        if (localV > 1.0f) localV = 1.0f;

        // Calculate texture UV
        float u, v;
        if (props.usesAtlas) {
            // Map from local UV (0-1) to atlas UV range
            u = props.atlasU0 + localU * (props.atlasU1 - props.atlasU0);
            v = props.atlasV0 + localV * (props.atlasV1 - props.atlasV0);
        } else {
            u = localU;
            v = localV;
        }
        result.uvs[i * 2] = u;
        result.uvs[i * 2 + 1] = v;

        // Calculate normal map UV (may be different atlas or no atlas)
        float nu, nv;
        if (props.usesNormalMapAtlas) {
            nu = props.normalAtlasU0 + localU * (props.normalAtlasU1 - props.normalAtlasU0);
            nv = props.normalAtlasV0 + localV * (props.normalAtlasV1 - props.normalAtlasV0);
        } else {
            nu = localU;
            nv = localV;
        }
        result.normalUvs[i * 2] = nu;
        result.normalUvs[i * 2 + 1] = nv;
    }

    return result;
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

// Calculate break force based on Mohs hardness scale
float Box2DPhysics::calculateBreakForce(float strength, float impactSpeed) const {
    // Mohs scale is roughly logarithmic - each level is ~1.3x harder than the previous
    float scaleFactor = powf(MOHS_SCALE_MULTIPLIER, strength - MOHS_REFERENCE_LEVEL);
    return MOHS_BASE_BREAK_SPEED * scaleFactor;
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
    memset(&result, 0, sizeof(result));
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
    memset(&currentPoly, 0, sizeof(currentPoly));
    currentPoly.vertexCount = props.originalVertexCount;
    for (int i = 0; i < props.originalVertexCount * 2; ++i) {
        currentPoly.vertices[i] = props.originalVertices[i];
    }
    currentPoly.area = calculatePolygonArea(currentPoly.vertices, currentPoly.vertexCount);

    // Split the polygon along the fracture line
    DestructiblePolygon poly1;
    DestructiblePolygon poly2;
    memset(&poly1, 0, sizeof(poly1));
    memset(&poly2, 0, sizeof(poly2));
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

            DestructiblePolygon sub1;
            DestructiblePolygon sub2;
            memset(&sub1, 0, sizeof(sub1));
            memset(&sub2, 0, sizeof(sub2));
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
    if (hull.count < 3) {
        // Invalid hull - destroy the body and return -1 to indicate failure
        // Bodies without shapes don't respond to gravity and float away
        b2DestroyBody(bodyId);
        SDL_UnlockMutex(physicsMutex_);
        return -1;
    }

    b2Polygon poly = b2MakePolygon(&hull, 0.0f);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    // Scale density by area ratio to maintain consistent mass behavior
    shapeDef.density = density;
    shapeDef.material.friction = friction;
    shapeDef.material.restitution = restitution;
    shapeDef.enableContactEvents = true;
    shapeDef.enableSensorEvents = true;

    b2CreatePolygonShape(bodyId, &shapeDef, &poly);

    int internalId = nextBodyId_++;
    bodies_[internalId] = bodyId;

    SDL_UnlockMutex(physicsMutex_);
    return internalId;
}

// Force field management
int Box2DPhysics::createForceField(const float* vertices, int vertexCount, float forceX, float forceY, float damping, bool isWater) {
    assert(vertexCount >= 3 && vertexCount <= 8);

    SDL_LockMutex(physicsMutex_);

    // Create a static body for the sensor
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_staticBody;

    // Calculate centroid of the polygon for body position
    float centroidX = 0.0f, centroidY = 0.0f;
    for (int i = 0; i < vertexCount; ++i) {
        centroidX += vertices[i * 2];
        centroidY += vertices[i * 2 + 1];
    }
    centroidX /= vertexCount;
    centroidY /= vertexCount;
    bodyDef.position = (b2Vec2){centroidX, centroidY};

    b2BodyId bodyId = b2CreateBody(worldId_, &bodyDef);
    assert(b2Body_IsValid(bodyId));

    // Convert vertices to local coordinates (relative to centroid)
    b2Vec2 points[8];
    for (int i = 0; i < vertexCount; ++i) {
        points[i] = (b2Vec2){
            vertices[i * 2] - centroidX,
            vertices[i * 2 + 1] - centroidY
        };
    }

    // Create polygon shape as sensor
    b2Hull hull = b2ComputeHull(points, vertexCount);
    b2Polygon polygon = b2MakePolygon(&hull, 0.0f);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.isSensor = true;
    shapeDef.enableSensorEvents = true;

    b2ShapeId shapeId = b2CreatePolygonShape(bodyId, &shapeDef, &polygon);

    // Store the body in bodies_ map
    int internalBodyId = nextBodyId_++;
    bodies_[internalBodyId] = bodyId;

    // Create force field entry
    int forceFieldId = nextForceFieldId_++;
    ForceField field;
    field.bodyId = internalBodyId;
    field.shapeId = shapeId;
    field.forceX = forceX;
    field.forceY = forceY;
    field.damping = damping;
    field.isWater = isWater;
    forceFields_[forceFieldId] = field;

    SDL_UnlockMutex(physicsMutex_);
    return forceFieldId;
}

void Box2DPhysics::destroyForceField(int forceFieldId) {
    SDL_LockMutex(physicsMutex_);

    auto it = forceFields_.find(forceFieldId);
    if (it != forceFields_.end()) {
        // Destroy the body (which also destroys all attached shapes)
        auto bodyIt = bodies_.find(it->second.bodyId);
        if (bodyIt != bodies_.end()) {
            b2DestroyBody(bodyIt->second);
            bodies_.erase(bodyIt);
        }
        forceFields_.erase(it);
    }

    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::setForceFieldDamping(int forceFieldId, float damping) {
    SDL_LockMutex(physicsMutex_);

    auto it = forceFields_.find(forceFieldId);
    if (it != forceFields_.end()) {
        it->second.damping = damping;
    }

    SDL_UnlockMutex(physicsMutex_);
}

// Radial force field management
int Box2DPhysics::createRadialForceField(float centerX, float centerY, float radius, float forceAtCenter, float forceAtEdge) {
    assert(radius > 0.0f);

    SDL_LockMutex(physicsMutex_);

    // Create a static body for the sensor at the center
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_staticBody;
    bodyDef.position = (b2Vec2){centerX, centerY};

    b2BodyId bodyId = b2CreateBody(worldId_, &bodyDef);
    assert(b2Body_IsValid(bodyId));

    // Create circle shape as sensor
    b2Circle circle;
    circle.center = (b2Vec2){0.0f, 0.0f};
    circle.radius = radius;

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.isSensor = true;
    shapeDef.enableSensorEvents = true;

    b2ShapeId shapeId = b2CreateCircleShape(bodyId, &shapeDef, &circle);

    // Store the body in bodies_ map
    int internalBodyId = nextBodyId_++;
    bodies_[internalBodyId] = bodyId;

    // Create radial force field entry
    int forceFieldId = nextForceFieldId_++;
    RadialForceField field;
    field.bodyId = internalBodyId;
    field.shapeId = shapeId;
    field.centerX = centerX;
    field.centerY = centerY;
    field.radius = radius;
    field.forceAtCenter = forceAtCenter;
    field.forceAtEdge = forceAtEdge;
    radialForceFields_[forceFieldId] = field;

    SDL_UnlockMutex(physicsMutex_);
    return forceFieldId;
}

void Box2DPhysics::destroyRadialForceField(int forceFieldId) {
    SDL_LockMutex(physicsMutex_);

    auto it = radialForceFields_.find(forceFieldId);
    if (it != radialForceFields_.end()) {
        // Destroy the body (which also destroys all attached shapes)
        auto bodyIt = bodies_.find(it->second.bodyId);
        if (bodyIt != bodies_.end()) {
            b2DestroyBody(bodyIt->second);
            bodies_.erase(bodyIt);
        }
        radialForceFields_.erase(it);
    }

    SDL_UnlockMutex(physicsMutex_);
}

// Maximum number of overlapping shapes to process per force field
static constexpr int MAX_FORCE_FIELD_OVERLAPS = 64;

void Box2DPhysics::applyForceFields() {
    // Stack-allocated buffer for sensor overlaps
    b2ShapeId overlaps[MAX_FORCE_FIELD_OVERLAPS];

    // Track bodies already processed to avoid applying force multiple times
    b2BodyId processedBodies[MAX_FORCE_FIELD_OVERLAPS];
    int processedCount = 0;

    // Apply force to all bodies overlapping with force field sensors
    for (auto& pair : forceFields_) {
        ForceField& field = pair.second;
        processedCount = 0;

        // Get the force field's own body to exclude it
        auto bodyIt = bodies_.find(field.bodyId);
        b2BodyId forceFieldBodyId = (bodyIt != bodies_.end()) ? bodyIt->second : b2_nullBodyId;

        // Get the force field's AABB for center-of-mass containment check
        b2AABB fieldAABB = b2Shape_GetAABB(field.shapeId);

        // Get overlapping shapes (capped at MAX_FORCE_FIELD_OVERLAPS)
        int overlapCount = b2Shape_GetSensorOverlaps(field.shapeId, overlaps, MAX_FORCE_FIELD_OVERLAPS);

        // Apply force to each overlapping body
        for (int i = 0; i < overlapCount; ++i) {
            b2BodyId overlappingBodyId = b2Shape_GetBody(overlaps[i]);

            // Skip the force field's own body
            if (B2_ID_EQUALS(overlappingBodyId, forceFieldBodyId)) continue;

            // Check if we already processed this body (handles multi-shape bodies)
            bool alreadyProcessed = false;
            for (int j = 0; j < processedCount; ++j) {
                if (B2_ID_EQUALS(processedBodies[j], overlappingBodyId)) {
                    alreadyProcessed = true;
                    break;
                }
            }
            if (alreadyProcessed) continue;

            // Only apply force to dynamic bodies
            b2BodyType bodyType = b2Body_GetType(overlappingBodyId);
            if (bodyType == b2_dynamicBody) {
                // Get the body's center of mass position
                b2Vec2 centerOfMass = b2Body_GetPosition(overlappingBodyId);

                // Only apply force if the center of mass is inside the force field
                bool centerInField = (centerOfMass.x >= fieldAABB.lowerBound.x &&
                                      centerOfMass.x <= fieldAABB.upperBound.x &&
                                      centerOfMass.y >= fieldAABB.lowerBound.y &&
                                      centerOfMass.y <= fieldAABB.upperBound.y);

                // Check if body is near the surface (within margin above water)
                // Large margin to catch objects that bounce above the surface
                const float surfaceMargin = 0.5f;
                bool nearSurface = (centerOfMass.x >= fieldAABB.lowerBound.x &&
                                    centerOfMass.x <= fieldAABB.upperBound.x &&
                                    centerOfMass.y > fieldAABB.upperBound.y &&
                                    centerOfMass.y <= fieldAABB.upperBound.y + surfaceMargin);

                if (centerInField) {
                    b2Vec2 vel = b2Body_GetLinearVelocity(overlappingBodyId);

                    float forceMultiplier = 1.0f;

                    if (field.isWater) {
                        int internalBodyId = findInternalBodyId(overlappingBodyId);
                        if (internalBodyId >= 0 && bodyHasType(internalBodyId, "heavy")) {
                            forceMultiplier = -0.5f;
                        }
                    }

                    // Apply force
                    vel.x += field.forceX * forceMultiplier * fixedTimestep_;
                    vel.y += field.forceY * forceMultiplier * fixedTimestep_;

                    // Apply velocity damping if set (simulates water drag)
                    // Use stronger damping factor (3x) for effective water resistance
                    if (field.damping > 0.0f) {
                        float effectiveDamping = field.damping * 3.0f;
                        float dampingFactor = 1.0f - effectiveDamping * fixedTimestep_;
                        if (dampingFactor < 0.0f) dampingFactor = 0.0f;
                        vel.x *= dampingFactor;
                        vel.y *= dampingFactor;

                        float angVel = b2Body_GetAngularVelocity(overlappingBodyId);
                        angVel *= dampingFactor;
                        b2Body_SetAngularVelocity(overlappingBodyId, angVel);
                    }

                    b2Body_SetLinearVelocity(overlappingBodyId, vel);
                } else if (nearSurface && field.damping > 0.0f) {
                    // Body is just above the water surface - apply damping to help settle
                    b2Vec2 vel = b2Body_GetLinearVelocity(overlappingBodyId);

                    // Apply damping above surface to stop bobbing
                    // Use 2x damping strength for air resistance near water
                    float effectiveDamping = field.damping * 2.0f;
                    float surfaceDampingFactor = 1.0f - effectiveDamping * fixedTimestep_;
                    if (surfaceDampingFactor < 0.0f) surfaceDampingFactor = 0.0f;
                    vel.x *= surfaceDampingFactor;
                    vel.y *= surfaceDampingFactor;
                    b2Body_SetLinearVelocity(overlappingBodyId, vel);

                    float angVel = b2Body_GetAngularVelocity(overlappingBodyId);
                    angVel *= surfaceDampingFactor;
                    b2Body_SetAngularVelocity(overlappingBodyId, angVel);
                }

                // Track this body as processed
                if (processedCount < MAX_FORCE_FIELD_OVERLAPS) {
                    processedBodies[processedCount++] = overlappingBodyId;
                }
            }
        }
    }
}

void Box2DPhysics::applyRadialForceFields() {
    // Stack-allocated buffer for sensor overlaps
    b2ShapeId overlaps[MAX_FORCE_FIELD_OVERLAPS];

    // Track bodies already processed to avoid applying force multiple times
    b2BodyId processedBodies[MAX_FORCE_FIELD_OVERLAPS];
    int processedCount = 0;

    // Apply force to all bodies overlapping with radial force field sensors
    for (auto& pair : radialForceFields_) {
        RadialForceField& field = pair.second;
        processedCount = 0;

        // Get the force field's own body to exclude it
        auto bodyIt = bodies_.find(field.bodyId);
        b2BodyId forceFieldBodyId = (bodyIt != bodies_.end()) ? bodyIt->second : b2_nullBodyId;

        // Get overlapping shapes (capped at MAX_FORCE_FIELD_OVERLAPS)
        int overlapCount = b2Shape_GetSensorOverlaps(field.shapeId, overlaps, MAX_FORCE_FIELD_OVERLAPS);

        // Apply force to each overlapping body
        for (int i = 0; i < overlapCount; ++i) {
            b2BodyId overlappingBodyId = b2Shape_GetBody(overlaps[i]);

            // Skip the force field's own body
            if (B2_ID_EQUALS(overlappingBodyId, forceFieldBodyId)) continue;

            // Check if we already processed this body (handles multi-shape bodies)
            bool alreadyProcessed = false;
            for (int j = 0; j < processedCount; ++j) {
                if (B2_ID_EQUALS(processedBodies[j], overlappingBodyId)) {
                    alreadyProcessed = true;
                    break;
                }
            }
            if (alreadyProcessed) continue;

            // Only apply force to dynamic bodies
            b2BodyType bodyType = b2Body_GetType(overlappingBodyId);
            if (bodyType == b2_dynamicBody) {
                // Get the body's center of mass position
                b2Vec2 bodyPos = b2Body_GetPosition(overlappingBodyId);

                // Calculate distance from center
                float dx = bodyPos.x - field.centerX;
                float dy = bodyPos.y - field.centerY;
                float distance = sqrtf(dx * dx + dy * dy);

                // Only apply force if the center of mass is inside the field
                if (distance <= field.radius) {
                    // Interpolate force based on distance (t=0 at center, t=1 at edge)
                    float t = distance / field.radius;
                    float forceMagnitude = field.forceAtCenter + t * (field.forceAtEdge - field.forceAtCenter);

                    // Calculate direction (radial, from center outward)
                    float dirX, dirY;
                    if (distance > 0.0001f) {
                        dirX = dx / distance;
                        dirY = dy / distance;
                    } else {
                        // At center, no direction - apply no force
                        dirX = 0.0f;
                        dirY = 0.0f;
                    }

                    // Apply acceleration directly to velocity (like gravity)
                    b2Vec2 vel = b2Body_GetLinearVelocity(overlappingBodyId);
                    vel.x += dirX * forceMagnitude * fixedTimestep_;
                    vel.y += dirY * forceMagnitude * fixedTimestep_;
                    b2Body_SetLinearVelocity(overlappingBodyId, vel);
                }

                // Track this body as processed
                if (processedCount < MAX_FORCE_FIELD_OVERLAPS) {
                    processedBodies[processedCount++] = overlappingBodyId;
                }
            }
        }
    }
}

// Process fractures for destructible bodies
void Box2DPhysics::processFractures() {
    fractureEvents_.clear();

    // Helper to process a single destructible body in a collision
    auto processDestructible = [this](int bodyId, const CollisionHitEvent& hit) {
        const DestructibleProperties* props = getDestructibleProperties(bodyId);
        if (!props) return;

        // Check if impact exceeds break threshold
        float breakForce = calculateBreakForce(props->strength, hit.approachSpeed);
        if (hit.approachSpeed < breakForce) return;

        // Check if already pending destruction
        for (int pending : pendingDestructions_) {
            if (pending == bodyId) return;
        }

        // Get body state
        auto bodyIt = bodies_.find(bodyId);
        if (bodyIt == bodies_.end()) return;

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

        if (fracture.fragmentCount < 2) return;

        // Create fracture event
        FractureEvent event;
        memset(&event, 0, sizeof(event));
        event.originalBodyId = bodyId;
        event.fragmentCount = 0;  // Count valid fragments
        event.impactPointX = hit.pointX;
        event.impactPointY = hit.pointY;
        event.impactNormalX = hit.normalX;
        event.impactNormalY = hit.normalY;
        event.impactSpeed = hit.approachSpeed;

        // Get and destroy the original layer if we know it
        auto layerIt = destructibleBodyLayers_.find(bodyId);
        if (layerIt != destructibleBodyLayers_.end()) {
            event.originalLayerId = layerIt->second;
            if (layerManager_) {
                layerManager_->destroyLayer(layerIt->second);
            }
            destructibleBodyLayers_.erase(layerIt);
        } else {
            event.originalLayerId = -1;
        }

        // Create fragment bodies (skip fragments that are too small)
        for (int i = 0; i < fracture.fragmentCount; ++i) {
            // Skip fragments that are too small - they "disappear" instead of infinitely shattering
            if (fracture.fragments[i].area < MIN_FRAGMENT_AREA) continue;

            int fragBodyId = createFragmentBody(pos.x, pos.y, angle,
                                                 fracture.fragments[i],
                                                 vel.x, vel.y, angularVel,
                                                 1.0f, 0.3f, 0.3f);

            // Skip fragments that failed to create (e.g., invalid hull)
            if (fragBodyId < 0) continue;

            int fragIdx = event.fragmentCount;
            event.newBodyIds[fragIdx] = fragBodyId;
            event.fragmentAreas[fragIdx] = fracture.fragments[i].area;

            // Create fragment polygon with UV coordinates for texture clipping
            event.fragmentPolygons[fragIdx] = createFragmentWithUVs(fracture.fragments[i], *props);

            // Create layer for fragment if layer manager is available
            int layerId = -1;
            if (layerManager_) {
                // Calculate layer size from fragment polygon area
                float fragSize = sqrtf(fracture.fragments[i].area) * 2.0f;
                if (fragSize < MIN_FRAGMENT_LAYER_SIZE) fragSize = MIN_FRAGMENT_LAYER_SIZE;

                // Create layer with atlas texture IDs if using atlas, otherwise original IDs
                // This ensures proper descriptor set lookup in the renderer
                uint64_t layerTexId = props->usesAtlas ? props->atlasTextureId : props->textureId;
                uint64_t layerNormId = props->usesNormalMapAtlas ? props->atlasNormalMapId : props->normalMapId;
                layerId = layerManager_->createLayer(layerTexId, fragSize, fragSize, layerNormId, props->pipelineId);
                layerManager_->attachLayerToBody(layerId, fragBodyId);

                // Set atlas UV coordinates if using atlas
                // This is important for proper texture batching
                if (props->usesAtlas) {
                    layerManager_->setLayerAtlasUV(layerId, props->atlasTextureId,
                                                    props->atlasU0, props->atlasV0,
                                                    props->atlasU1, props->atlasV1);
                }
                // Set normal map atlas UV coordinates if using normal map atlas
                if (props->usesNormalMapAtlas) {
                    layerManager_->setLayerNormalMapAtlasUV(layerId, props->atlasNormalMapId,
                                                             props->normalAtlasU0, props->normalAtlasV0,
                                                             props->normalAtlasU1, props->normalAtlasV1);
                }

                // Apply polygon vertices and UV coordinates for texture clipping
                const FragmentPolygon& fragPoly = event.fragmentPolygons[fragIdx];
                if (fragPoly.vertexCount >= 3) {
                    layerManager_->setLayerPolygon(layerId, fragPoly.vertices, fragPoly.uvs, fragPoly.normalUvs, fragPoly.vertexCount);
                }

                // Track fragment layer for cleanup
                fragmentLayerIds_.push_back(layerId);
            }
            event.newLayerIds[fragIdx] = layerId;

            // Track fragment body for cleanup
            fragmentBodyIds_.push_back(fragBodyId);

            // Make fragments also destructible if original was brittle enough and fragment is large enough
            if (props->brittleness > 0.5f && fracture.fragments[i].area >= MIN_FRAGMENT_AREA * MIN_REFRACTURE_AREA_MULTIPLIER) {
                setBodyDestructible(fragBodyId, props->strength, props->brittleness,
                                   fracture.fragments[i].vertices, fracture.fragments[i].vertexCount,
                                   props->textureId, props->normalMapId, props->pipelineId);

                // Copy root bounding box from parent for proper UV mapping
                // If parent has root bounds, use those; otherwise use parent's original bounds
                if (props->hasRootBounds) {
                    setBodyDestructibleRootBounds(fragBodyId,
                                                   props->rootMinX, props->rootMinY,
                                                   props->rootWidth, props->rootHeight);
                } else {
                    setBodyDestructibleRootBounds(fragBodyId,
                                                   props->originalMinX, props->originalMinY,
                                                   props->originalWidth, props->originalHeight);
                }

                // Copy texture atlas info to new fragment
                if (props->usesAtlas) {
                    setBodyDestructibleAtlasUV(fragBodyId, props->atlasTextureId,
                                                props->atlasU0, props->atlasV0, props->atlasU1, props->atlasV1);
                }
                // Copy normal map atlas info to new fragment
                if (props->usesNormalMapAtlas) {
                    setBodyDestructibleNormalMapAtlasUV(fragBodyId, props->atlasNormalMapId,
                                                         props->normalAtlasU0, props->normalAtlasV0,
                                                         props->normalAtlasU1, props->normalAtlasV1);
                }

                // Set layer for fragment so it can be destroyed if fragment breaks
                if (layerId >= 0) {
                    setBodyDestructibleLayer(fragBodyId, layerId);
                }
            }

            event.fragmentCount++;
        }

        // Only create event if we have valid fragments
        if (event.fragmentCount > 0) {
            fractureEvents_.push_back(event);
            pendingDestructions_.push_back(bodyId);

            // Call fracture callback if set
            if (fractureCallback_) {
                fractureCallback_(event);
            }
        }
    };

    // Process each collision event - check both bodies
    for (const auto& hit : collisionHitEvents_) {
        // Process body A if destructible
        if (isBodyDestructible(hit.bodyIdA)) {
            processDestructible(hit.bodyIdA, hit);
        }

        // Process body B if destructible (can happen in same collision)
        if (isBodyDestructible(hit.bodyIdB)) {
            processDestructible(hit.bodyIdB, hit);
        }
    }

    // Destroy joints attached to pending destruction bodies
    std::vector<int> jointsToDestroy;
    for (int bodyId : pendingDestructions_) {
        auto bodyIt = bodies_.find(bodyId);
        if (bodyIt != bodies_.end()) {
            for (auto& j : joints_) {
                b2JointId jointId = j.second;
                if (b2Joint_GetType(jointId) == b2_mouseJoint) {
                    b2BodyId bodyB = b2Joint_GetBodyB(jointId);
                    int attachedBodyId = findInternalBodyId(bodyB);
                    if (attachedBodyId == bodyId) {
                        jointsToDestroy.push_back(j.first);
                    }
                }
            }
        }
    }
    for (int jointId : jointsToDestroy) {
        destroyJoint(jointId);
    }

    // Destroy pending bodies
    for (int bodyId : pendingDestructions_) {
        clearBodyDestructible(bodyId);
        destroyBody(bodyId);
    }
    pendingDestructions_.clear();
}

void Box2DPhysics::clearAllForceFields() {
    SDL_LockMutex(physicsMutex_);

    std::vector<int> fieldIds;
    for (auto& pair : forceFields_) {
        fieldIds.push_back(pair.first);
    }

    SDL_UnlockMutex(physicsMutex_);

    for (int fieldId : fieldIds) {
        destroyForceField(fieldId);
    }
}

void Box2DPhysics::clearAllRadialForceFields() {
    SDL_LockMutex(physicsMutex_);

    std::vector<int> fieldIds;
    for (auto& pair : radialForceFields_) {
        fieldIds.push_back(pair.first);
    }

    SDL_UnlockMutex(physicsMutex_);

    for (int fieldId : fieldIds) {
        destroyRadialForceField(fieldId);
    }
}

void Box2DPhysics::reset() {
    SDL_LockMutex(physicsMutex_);

    // Destroy all force fields first (uses bodies)
    std::vector<int> forceFieldIds;
    for (auto& pair : forceFields_) {
        forceFieldIds.push_back(pair.first);
    }
    std::vector<int> radialForceFieldIds;
    for (auto& pair : radialForceFields_) {
        radialForceFieldIds.push_back(pair.first);
    }

    SDL_UnlockMutex(physicsMutex_);

    for (int fieldId : forceFieldIds) {
        destroyForceField(fieldId);
    }
    for (int fieldId : radialForceFieldIds) {
        destroyRadialForceField(fieldId);
    }

    SDL_LockMutex(physicsMutex_);

    // Destroy all joints
    std::vector<int> jointIds;
    for (auto& pair : joints_) {
        jointIds.push_back(pair.first);
    }

    SDL_UnlockMutex(physicsMutex_);

    for (int jointId : jointIds) {
        destroyJoint(jointId);
    }

    SDL_LockMutex(physicsMutex_);

    // Destroy all bodies
    std::vector<int> bodyIds;
    for (auto& pair : bodies_) {
        bodyIds.push_back(pair.first);
    }

    SDL_UnlockMutex(physicsMutex_);

    for (int bodyId : bodyIds) {
        clearBodyDestructible(bodyId);
        destroyBody(bodyId);
    }

    SDL_LockMutex(physicsMutex_);

    // Clear fragment tracking
    fragmentBodyIds_.clear();
    fragmentLayerIds_.clear();

    // Clear destructible body layers
    destructibleBodyLayers_.clear();

    // Clear collision events
    collisionHitEvents_.clear();
    fractureEvents_.clear();
    pendingDestructions_.clear();

    // Reset time accumulator
    timeAccumulator_ = 0.0f;

    // Reset mouse joint ground body
    mouseJointGroundBody_ = b2_nullBodyId;

    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::getAllDynamicBodyInfo(int* bodyIds, float* posX, float* posY, float* velY, int maxBodies, int* outCount) {
    SDL_LockMutex(physicsMutex_);

    int count = 0;
    for (const auto& pair : bodies_) {
        if (count >= maxBodies) break;

        b2BodyId bodyId = pair.second;
        if (!b2Body_IsValid(bodyId)) continue;

        b2BodyType bodyType = b2Body_GetType(bodyId);
        if (bodyType != b2_dynamicBody) continue;

        b2Vec2 pos = b2Body_GetPosition(bodyId);
        b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);

        bodyIds[count] = pair.first;
        posX[count] = pos.x;
        posY[count] = pos.y;
        velY[count] = vel.y;
        ++count;
    }

    *outCount = count;

    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::addBodyType(int bodyId, const std::string& type) {
    SDL_LockMutex(physicsMutex_);
    auto& types = bodyTypes_[bodyId];
    if (std::find(types.begin(), types.end(), type) == types.end()) {
        types.push_back(type);
    }
    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::removeBodyType(int bodyId, const std::string& type) {
    SDL_LockMutex(physicsMutex_);
    auto it = bodyTypes_.find(bodyId);
    if (it != bodyTypes_.end()) {
        auto& types = it->second;
        types.erase(std::remove(types.begin(), types.end(), type), types.end());
        if (types.empty()) {
            bodyTypes_.erase(it);
        }
    }
    SDL_UnlockMutex(physicsMutex_);
}

void Box2DPhysics::clearBodyTypes(int bodyId) {
    SDL_LockMutex(physicsMutex_);
    bodyTypes_.erase(bodyId);
    SDL_UnlockMutex(physicsMutex_);
}

bool Box2DPhysics::bodyHasType(int bodyId, const std::string& type) const {
    SDL_LockMutex(physicsMutex_);
    auto it = bodyTypes_.find(bodyId);
    bool result = false;
    if (it != bodyTypes_.end()) {
        const auto& types = it->second;
        result = std::find(types.begin(), types.end(), type) != types.end();
    }
    SDL_UnlockMutex(physicsMutex_);
    return result;
}

std::vector<std::string> Box2DPhysics::getBodyTypes(int bodyId) const {
    SDL_LockMutex(physicsMutex_);
    auto it = bodyTypes_.find(bodyId);
    std::vector<std::string> result;
    if (it != bodyTypes_.end()) {
        result = it->second;
    }
    SDL_UnlockMutex(physicsMutex_);
    return result;
}
