#pragma once

#include <box2d/box2d.h>
#include <SDL3/SDL.h>
#include <functional>
#include "../core/String.h"
#include "../core/Vector.h"
#include "../core/HashTable.h"
#include "../memory/MemoryAllocator.h"

#define LENGTH_UNITS_PER_METER 0.05f  // Define this smaller so box2d doesn't join polygon vertices

// Forward declarations
class SceneLayerManager;
class ConsoleBuffer;
class TrigLookup;

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

// Sensor event for force field enter/exit
struct SensorEvent {
    int sensorBodyId;  // The sensor body ID (force field)
    int visitorBodyId; // The body entering/exiting
    float visitorX, visitorY;  // Position of visitor body
    float visitorVelX, visitorVelY;  // Velocity of visitor body
    float surfaceY;  // Water surface Y for splash position
    bool isBegin;  // true for begin touch, false for end touch
};

// 2D polygon for destructible objects
struct DestructiblePolygon {
    float vertices[16];  // Max 8 vertices, x/y pairs
    int vertexCount;
    float area;  // Calculated polygon area
};

// Fragment polygon with UV coordinates for texture clipping
struct FragmentPolygon {
    float vertices[16];  // Max 8 vertices, x/y pairs (local coordinates)
    float uvs[16];       // Max 8 UV pairs, u/v for each vertex (texture)
    float normalUvs[16]; // Max 8 UV pairs, u/v for each vertex (normal map)
    int vertexCount;
    float area;
    float centroidX, centroidY;  // Centroid for positioning
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
    float originalWidth;   // Bounding box width for UV calculation
    float originalHeight;  // Bounding box height for UV calculation
    float originalMinX, originalMinY;  // Bounding box min for UV calculation
    // Root bounding box - preserved from original parent for proper UV mapping in recursive fractures
    float rootMinX, rootMinY;
    float rootWidth, rootHeight;
    bool hasRootBounds;  // True if root bounds are set (for fragments)
    uint64_t textureId;      // Texture for rendering fragments
    uint64_t normalMapId;    // Normal map for fragments
    int pipelineId;          // Shader pipeline for fragments
    // Atlas UV coordinates for texture (if using texture atlas)
    bool usesAtlas;
    float atlasU0, atlasV0;  // Top-left UV in atlas for texture
    float atlasU1, atlasV1;  // Bottom-right UV in atlas for texture
    uint64_t atlasTextureId;      // Atlas texture ID (if using atlas)
    // Atlas UV coordinates for normal map (separate, may be different)
    bool usesNormalMapAtlas;
    float normalAtlasU0, normalAtlasV0;  // Top-left UV in atlas for normal map
    float normalAtlasU1, normalAtlasV1;  // Bottom-right UV in atlas for normal map
    uint64_t atlasNormalMapId;    // Atlas normal map ID (if using atlas)
};

// Callback for when a body is fractured (returns new fragment body IDs and layer IDs)
struct FractureEvent {
    int originalBodyId;
    int originalLayerId;
    int newBodyIds[8];
    int newLayerIds[8];
    float fragmentAreas[8];  // Area of each fragment for sizing layers
    FragmentPolygon fragmentPolygons[8];  // Fragment polygons with UV coordinates
    int fragmentCount;
    float impactPointX, impactPointY;
    float impactNormalX, impactNormalY;
    float impactSpeed;
};

// Force field that applies a constant force to all bodies inside it
struct ForceField {
    int bodyId;           // The static body holding the sensor shape
    b2ShapeId shapeId;    // The sensor shape ID
    float forceX, forceY; // Force vector to apply
    float damping;        // Velocity damping factor (0 = no damping, higher = more damping)
    bool isWater;         // True if this is a water force field
    float waterSurfaceY;  // Water surface Y (only valid if isWater=true)
};

// Radial force field that applies force based on distance from center
struct RadialForceField {
    int bodyId;           // The static body holding the sensor shape
    b2ShapeId shapeId;    // The sensor shape ID
    float centerX, centerY; // Center position of the field
    float radius;         // Radius of the field
    float forceAtCenter;  // Force magnitude at center (positive = outward, negative = inward)
    float forceAtEdge;    // Force magnitude at edge
};

class Box2DPhysics {
public:
    Box2DPhysics(MemoryAllocator* smallAllocator, MemoryAllocator* largeAllocator, SceneLayerManager* layerManager, ConsoleBuffer* consoleBuffer, TrigLookup* trigLookup);
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
    void enableBody(int bodyId);
    void disableBody(int bodyId);
    void applyForce(int bodyId, float fx, float fy, float px, float py);
    void applyTorque(int bodyId, float torque);

    // Body queries
    float getBodyPositionX(int bodyId);
    float getBodyPositionY(int bodyId);
    float getBodyAngle(int bodyId);
    float getBodyLinearVelocityX(int bodyId);
    float getBodyLinearVelocityY(int bodyId);
    float getBodyAngularVelocity(int bodyId);
    bool isBodyValid(int bodyId) const;

    // Shape management
    void addBoxFixture(int bodyId, float halfWidth, float halfHeight, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addCircleFixture(int bodyId, float radius, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addPolygonFixture(int bodyId, const float* vertices, int vertexCount, float density = 1.0f, float friction = 0.3f, float restitution = 0.0f);
    void addSegmentFixture(int bodyId, float x1, float y1, float x2, float y2, float friction = 0.3f, float restitution = 0.0f);
    void clearAllFixtures(int bodyId);

    // Sensor shape management (for nodes)
    void addCircleSensor(int bodyId, float radius);
    void addPolygonSensor(int bodyId, const float* vertices, int vertexCount);

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
    const Vector<DebugVertex>& getDebugLineVertices();
    const Vector<DebugVertex>& getDebugTriangleVertices();

    // Collision events - returns hit events from last physics step
    const Vector<CollisionHitEvent>& getCollisionHitEvents() const { return collisionHitEvents_; }

    // Destructible object management
    void setBodyDestructible(int bodyId, float strength, float brittleness,
                             const float* vertices, int vertexCount,
                             uint64_t textureId, uint64_t normalMapId, int pipelineId);

    // Set atlas UV coordinates for a destructible body's texture (call after setBodyDestructible)
    void setBodyDestructibleAtlasUV(int bodyId, uint64_t atlasTextureId,
                                     float u0, float v0, float u1, float v1);

    // Set atlas UV coordinates for a destructible body's normal map (call after setBodyDestructible)
    void setBodyDestructibleNormalMapAtlasUV(int bodyId, uint64_t atlasNormalMapId,
                                              float u0, float v0, float u1, float v1);

    // Set root bounding box for a destructible fragment (for proper UV mapping in recursive fractures)
    void setBodyDestructibleRootBounds(int bodyId, float minX, float minY, float width, float height);

    // Set the layer ID associated with a destructible body (for cleanup when fractured)
    void setBodyDestructibleLayer(int bodyId, int layerId);

    void clearBodyDestructible(int bodyId);
    bool isBodyDestructible(int bodyId) const;
    const DestructibleProperties* getDestructibleProperties(int bodyId) const;

    // Get fracture events from last physics step
    const Vector<FractureEvent>& getFractureEvents() const { return fractureEvents_; }

    // Clean up all fragment bodies and layers created during fractures
    // Call this before recreating destructible objects (e.g., on scene reset)
    void cleanupAllFragments();

    // Get fragment body IDs (for debugging/tracking)
    const Vector<int>& getFragmentBodyIds() const { return fragmentBodyIds_; }

    // Process destructible collisions and generate fractures
    // Returns body/layer IDs that should be created (caller must create layers)
    // This is called automatically during step() but results can be queried
    void processFractures();

    // Calculate fracture based on impact point and properties
    static FractureResult calculateFracture(const DestructibleProperties& props,
                                            float impactX, float impactY,
                                            float normalX, float normalY,
                                            float impactSpeed,
                                            float bodyX, float bodyY, float bodyAngle,
                                            TrigLookup* trigLookup);

    // Calculate polygon area using shoelace formula
    static float calculatePolygonArea(const float* vertices, int vertexCount);

    // Split polygon along a line, returns two polygons
    static void splitPolygon(const float* vertices, int vertexCount,
                             float lineX, float lineY, float lineDirX, float lineDirY,
                             DestructiblePolygon& poly1, DestructiblePolygon& poly2);

    // Convert a DestructiblePolygon to FragmentPolygon with UV coordinates
    static FragmentPolygon createFragmentWithUVs(const DestructiblePolygon& poly,
                                                  const DestructibleProperties& props);

    // Create a fragment body with proper physics properties
    int createFragmentBody(float x, float y, float angle,
                          const DestructiblePolygon& polygon,
                          float vx, float vy, float angularVel,
                          float density, float friction, float restitution);

    // Fracture callback - set to receive notifications when objects fracture
    using FractureCallback = std::function<void(const FractureEvent&)>;
    void setFractureCallback(FractureCallback callback) { fractureCallback_ = callback; }

    // Sensor callback - set to receive notifications when bodies enter/exit sensors
    using SensorCallback = std::function<void(const SensorEvent&)>;
    void setSensorCallback(SensorCallback callback) { sensorCallback_ = callback; }

    // Force field management
    // Creates a force field sensor with a polygon shape that applies force to overlapping bodies
    // vertices: array of x,y pairs defining the polygon (3-8 vertices)
    // forceX, forceY: force vector to apply to bodies inside the field
    // damping: velocity damping factor (0 = no damping, higher = more drag)
    // isWater: true if this is a water force field (for internal tracking)
    // Returns the force field ID
    int createForceField(const float* vertices, int vertexCount, float forceX, float forceY, float damping = 0.0f, bool isWater = false);
    void destroyForceField(int forceFieldId);

    // Set the damping factor for an existing force field
    void setForceFieldDamping(int forceFieldId, float damping);

    // Set the water surface Y for a water force field
    void setForceFieldWaterSurface(int forceFieldId, float surfaceY);

    // Set the rotation angle for a force field body (in radians)
    void setForceFieldRotation(int forceFieldId, float rotation);

    // Radial force field management
    // Creates a circular force field that applies radial force based on distance from center
    // centerX, centerY: center position of the field
    // radius: radius of the field
    // forceAtCenter: force magnitude at center (positive = outward, negative = inward)
    // forceAtEdge: force magnitude at edge
    // Returns the radial force field ID
    int createRadialForceField(float centerX, float centerY, float radius, float forceAtCenter, float forceAtEdge);
    void destroyRadialForceField(int forceFieldId);

    // Clear all force fields (for scene cleanup)
    void clearAllForceFields();
    void clearAllRadialForceFields();

    // Accessors for force fields (for callbacks)
    const HashTable<int, ForceField>& getForceFields() const { return forceFields_; }
    const ForceField* getForceField(int id) const {
        return forceFields_.find(id);
    }

    // Get all dynamic body IDs for splash detection
    void getAllDynamicBodyInfo(int* bodyIds, float* posX, float* posY, float* velY, int maxBodies, int* outCount);

    // Reset physics world (for scene cleanup)
    void reset();

    // Type system for object interactions
    void addBodyType(int bodyId, const char* type);
    void removeBodyType(int bodyId, const char* type);
    void clearBodyTypes(int bodyId);
    bool bodyHasType(int bodyId, const char* type) const;
    Vector<String> getBodyTypes(int bodyId) const;

    // Collision callback for type-based interactions
    using CollisionCallback = std::function<void(int bodyIdA, int bodyIdB, float pointX, float pointY, float normalX, float normalY, float approachSpeed)>;
    void setCollisionCallback(CollisionCallback callback) { collisionCallback_ = callback; }

    // Trigger the collision callback manually (e.g., for sensor events)
    void triggerCollisionCallback(int bodyIdA, int bodyIdB, float pointX, float pointY, float normalX, float normalY, float approachSpeed) {
        if (collisionCallback_) {
            collisionCallback_(bodyIdA, bodyIdB, pointX, pointY, normalX, normalY, approachSpeed);
        }
    }

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
    HashTable<int, b2BodyId> bodies_;
    HashTable<int, b2JointId> joints_;
    HashTable<int, DestructibleProperties> destructibles_;  // Destructible properties per body
    int nextBodyId_;
    int nextJointId_;
    bool debugDrawEnabled_;
    Vector<DebugVertex> debugLineVertices_;
    Vector<DebugVertex> debugTriangleVertices_;

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
    Vector<CollisionHitEvent> collisionHitEvents_;

    // Fracture events from last physics step
    Vector<FractureEvent> fractureEvents_;

    // Layer manager for creating fragment layers (not owned)
    SceneLayerManager* layerManager_ = nullptr;

    // Fracture callback
    FractureCallback fractureCallback_;

    // Sensor callback
    SensorCallback sensorCallback_;

    // Bodies pending destruction after fracture (processed after step)
    Vector<int> pendingDestructions_;

    // Fragment tracking for cleanup
    Vector<int> fragmentBodyIds_;   // All fragment body IDs
    Vector<int> fragmentLayerIds_;  // All fragment layer IDs

    // Map from destructible body ID to its layer ID (for destroying layer when body fractures)
    HashTable<int, int> destructibleBodyLayers_;

    // Force field tracking
    HashTable<int, ForceField> forceFields_;
    HashTable<int, RadialForceField> radialForceFields_;
    int nextForceFieldId_;

    // Type system for object interactions
    HashTable<int, Vector<String>*> bodyTypes_;

    // Memory allocator for string operations
    MemoryAllocator* stringAllocator_;

    // Console buffer for logging (optional, may be nullptr)
    ConsoleBuffer* consoleBuffer_;

    // Trig lookup table for fast sin/cos calculations
    TrigLookup* trigLookup_;

    // Collision callback
    CollisionCallback collisionCallback_;

    // Helper to convert b2BodyId to internal ID
    int findInternalBodyId(b2BodyId bodyId);

    // Apply force fields to all overlapping bodies
    void applyForceFields();

    // Apply radial force fields to all overlapping bodies
    void applyRadialForceFields();

    // Calculate required force to break based on Moh's hardness
    float calculateBreakForce(float strength, float impactSpeed) const;

    // Determine number of fracture pieces based on brittleness and impact
    int calculateFragmentCount(float brittleness, float impactSpeed, float strength) const;
};
