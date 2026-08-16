#pragma once

// windows.h (via Time.h) defines min/max as macros and SeeD relies on them, so NOMINMAX is not an
// option engine-wide -- but they break Jolt's JPH::min / JPH::max declarations. Hide them for the
// duration of the Jolt includes only.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#pragma pop_macro("max")
#pragma pop_macro("min")


// Authored floats arrive from raw malloc'd pool memory -- World::Pool::On allocates and GetSlot just
// bumps a counter, so a component's default member initializers never run and every value can be
// garbage until something writes it. These assign only when the source is usable, leaving whatever
// sane default the destination already holds otherwise.
#define PHYSICS_VALID(v)            (isfinite(v))
#define PHYSICS_VALID_NONNEG(v)     (isfinite(v) && (v) >= 0.0f)
#define PHYSICS_VALID_POSITIVE(v)   (isfinite(v) && (v) >  0.0f)

namespace Components
{
    struct RigidBody : ComponentBase<RigidBody>
    {
        uint  motionType = 0;          // 0 = Static, 1 = Kinematic, 2 = Dynamic (JPH::EMotionType order)
        uint  objectLayer = 0;         // broadphase/object layer index, resolved by the init system's ObjectLayerPairFilter
        float mass = 1.0f;             // kg, ignored when motionType != Dynamic
        float friction = 0.2f;         // Jolt default
        float restitution = 0.0f;      // bounciness, 0..1
        float linearDamping = 0.05f;   // Jolt default
        float angularDamping = 0.05f;  // Jolt default
        float gravityFactor = 1.0f;    // scales the world gravity for this body
        uint  allowSleeping = 1;
        uint  isSensor = 0;            // reports overlaps without solving contacts
    };

    struct CollisionShape : ComponentBase<CollisionShape>
    {
        uint   shapeType = 0;          // 0 = Box, 1 = Sphere, 2 = Capsule, 3 = Cylinder, 4 = ConvexHull, 5 = Mesh
        Handle<Mesh> mesh;             // shapeType 4/5 only
        float3 halfExtent;             // Box: half size on each axis. Sphere: .x = radius. Capsule/Cylinder: .x = radius, .y = half height of the cylindrical section (a capsule's total height is 2*(.y + .x)).
        float3 offset;                 // shape origin relative to the entity Transform (Jolt rotates around the shape's center of mass)
        float  convexRadius = 0.05f;   // Box/Cylinder bevel, keep small
    };

    struct Constraint : ComponentBase<Constraint>
    {
        uint   constraintType = 0;     // 0 = Fixed, 1 = Point, 2 = Hinge, 3 = Slider, 4 = Distance
        Handle<Entity> target;         // other body; invalid = attach to the world
        float3 anchorLocal;            // attachment point on this body, local space
        float3 anchorTarget;           // attachment point on the target body, target local space
        float3 axis;                   // hinge rotation axis / slider travel axis, local space
        float  limitMin = 0.0f;        // Hinge: radians. Slider/Distance: meters. min >= max = unlimited.
        float  limitMax = 0.0f;
    };

    // ---- car ---------------------------------------------------------------------------------
    // A car is a CHASSIS entity -- Transform + RigidBody (motionType Dynamic) + CollisionShape +
    // Vehicle -- with one CHILD entity per wheel carrying a Wheel component. The wheels are not
    // bodies: Jolt casts them against the world itself and drives the chassis body through a single
    // JPH::VehicleConstraint, so a wheel entity only needs a Transform (and whatever it renders).
    //
    // That child Transform is OUTPUT, rewritten every frame from the simulated wheel pose so the
    // mesh sits on its suspension, steers and spins on its own. Which is exactly why the suspension
    // attachment point is authored in Wheel::position and NOT in that Transform -- the writeback
    // would overwrite it on the first frame.
    struct Vehicle : ComponentBase<Vehicle>
    {
        float3 up;                          // chassis local space, treated as +Y while left at zero
        float3 forward;                     // chassis local space, treated as +Z while left at zero
        float  maxPitchRollAngle = 3.14159265f; // rad, half angle of the cone the chassis up stays in; pi = never righted
        uint   collisionTester = 0;         // 0 = ray (cheapest), 1 = sphere cast, 2 = cylinder cast (most accurate)
        float  maxSlopeAngle = 1.3962634f;  // rad (80 deg), wheel contacts steeper than this are ignored so the car does not climb walls. Ray/sphere testers only.
        float  engineMaxTorque = 500.0f;    // Nm
        float  engineMinRPM = 1000.0f;      // below this the engine would stall
        float  engineMaxRPM = 6000.0f;
        float  engineInertia = 0.5f;        // kg m^2
        float  shiftUpRPM = 4000.0f;        // the gearbox is always automatic, these two drive it
        float  shiftDownRPM = 2000.0f;
        float  clutchStrength = 10.0f;
        float  differentialRatio = 3.42f;   // gearbox -> wheel rotation ratio, same on every axle
        // Driver input, read every frame -- drive these from gameplay code, or by hand in the
        // property window to test a car without writing any.
        float  forwardInput = 0.0f;         // -1..1, the automatic gearbox picks reverse on its own
        float  rightInput = 0.0f;           // -1..1, 1 = steer right
        float  brakeInput = 0.0f;           // 0..1
        float  handBrakeInput = 0.0f;       // 0..1
    };
    // Defined at the end of this header (it reads the live Jolt state out of Physics::instance,
    // which is declared way below).
    static void VehiclePropertyDraw(char* p);

    // One per wheel, on a CHILD entity of the chassis -- the parent link is how a car finds its
    // wheels, there is no children list to walk (World.h). Every length is in the chassis body's
    // own space, which is the entity's local space with its scale taken out.
    struct Wheel : ComponentBase<Wheel>
    {
        float3 position;                    // suspension attachment (the hard point the strut hangs from)
        float3 suspensionDirection;         // treated as straight down (-Vehicle.up) while left at zero
        float  radius = 0.3f;               // m
        float  width = 0.1f;                // m
        float  suspensionMinLength = 0.3f;  // m below `position`, fully compressed
        float  suspensionMaxLength = 0.5f;  // m below `position`, fully extended. This is the rest pose.
        float  suspensionFrequency = 1.5f;  // Hz
        float  suspensionDamping = 0.5f;    // damping ratio, 1 = critically damped
        float  maxSteerAngle = 0.0f;        // rad, 0 = this wheel does not steer
        float  maxBrakeTorque = 1500.0f;    // Nm
        float  maxHandBrakeTorque = 0.0f;   // Nm, usually the rear wheels only
        float  inertia = 0.9f;              // kg m^2, 0.5 * wheelMass * radius^2
        float  angularDamping = 0.2f;
        uint   axle = 0;                    // wheels sharing an axle share a differential. Front = 0, rear = 1 by convention.
        uint   driven = 1;                  // 1 = the engine drives this wheel. A car with NO driven wheel drives all of them instead.
    };

    struct PhysicsBody : ComponentBase<PhysicsBody>
    {
        uint bodyID = 0xFFFFFFFF;      // JPH::BodyID::cInvalidBodyID; kept as a plain uint so the
                                       // metadata generator sees a known type and the editor can show it
    };
    bool PhysicsBody::transient = true;
}

// ============================================================================
// Jolt -> Tracy profiling bridge
// ============================================================================
// Jolt gives us a 16-byte-aligned 64-byte mUserData scratch buffer per measurement, so the Tracy
// zone is placement-new'd into it -- no allocation, and the lifetime matches the Jolt scope exactly.
// The name is a runtime string (JPH_FUNCTION_NAME), hence ScopedZone's runtime-source-location
// constructor rather than the usual static-srcloc ZoneScoped macro.
#ifdef JPH_EXTERNAL_PROFILE
JPH_NAMESPACE_BEGIN

ExternalProfileMeasurement::ExternalProfileMeasurement(const char* inName, JPH::uint32 inColor)
{
    static_assert(sizeof(tracy::ScopedZone) <= sizeof(mUserData), "Tracy zone no longer fits in Jolt's measurement scratch");
    new (mUserData) tracy::ScopedZone(0, nullptr, 0, nullptr, 0, inName, strlen(inName), inColor);
}

ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
    ((tracy::ScopedZone*)mUserData)->~ScopedZone();
}

JPH_NAMESPACE_END
#endif

// ============================================================================
// Jolt runtime
// ============================================================================
// Two object layers to start: static geometry never collides with static geometry, everything else
// collides with everything. the layer is derived from motionType instead until there is a real layer editor.

namespace PhysicsLayers
{
    static constexpr JPH::ObjectLayer nonMoving = 0;
    static constexpr JPH::ObjectLayer moving = 1;
    static constexpr JPH::ObjectLayer count = 2;
}
namespace PhysicsBroadPhaseLayers
{
    static constexpr JPH::BroadPhaseLayer nonMoving(0);
    static constexpr JPH::BroadPhaseLayer moving(1);
    static constexpr JPH::uint count = 2;
}

class PhysicsBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    PhysicsBroadPhaseLayerInterface()
    {
        layers[PhysicsLayers::nonMoving] = PhysicsBroadPhaseLayers::nonMoving;
        layers[PhysicsLayers::moving] = PhysicsBroadPhaseLayers::moving;
    }
    virtual JPH::uint GetNumBroadPhaseLayers() const override { return PhysicsBroadPhaseLayers::count; }
    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
    {
        return layers[inLayer < PhysicsLayers::count ? inLayer : PhysicsLayers::nonMoving];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
    {
        return inLayer == PhysicsBroadPhaseLayers::moving ? "moving" : "nonMoving";
    }
#endif
private:
    JPH::BroadPhaseLayer layers[PhysicsLayers::count] = { JPH::BroadPhaseLayer(0), JPH::BroadPhaseLayer(0) };
};

class PhysicsObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
    {
        if (inLayer1 == PhysicsLayers::nonMoving)
            return inLayer2 == PhysicsBroadPhaseLayers::moving;
        return true;
    }
};

class PhysicsObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::ObjectLayer inLayer2) const override
    {
        if (inLayer1 == PhysicsLayers::nonMoving)
            return inLayer2 == PhysicsLayers::moving;
        return true;
    }
};

// Jolt's own precondition checks, routed to the engine log. Without JPH_ENABLE_ASSERTS (both here
// and in the lib -- it changes struct layouts, so the two must match) JPH_ASSERT compiles out
// entirely and Jolt misuse surfaces as a raw crash somewhere deep in the library instead of a named
// condition with a file and line. Returning true lets the JPH_BREAKPOINT fire so the debugger stops
// at the call site with the message already in the log.
static bool PhysicsAssertFailed(const char* inExpression, const char* inMessage, const char* inFile, JPH::uint inLine)
{
    String expression = inExpression != nullptr ? inExpression : "";
    String message = inMessage != nullptr ? inMessage : "";
    String file = inFile != nullptr ? inFile : "";
    uint line = (uint)inLine;
    IOs::Log("Jolt assert: {} ({}) at {}:{}", expression.c_str(), message.c_str(), file.c_str(), line);
    return true;
}

// The same "the pool never ran a default initializer" problem as the PHYSICS_VALID macros, in the
// form the vehicle code needs it: a component the editor's add button just created is all ZEROES,
// which for most of these fields (a wheel radius, an engine's max rpm, a suspension direction) is
// not a usable value either. Falling back to the documented default keeps a freshly added Vehicle
// drivable instead of degenerate, and keeps the meaning of a field in one place.
static float PhysicsAuthored(float value, float fallback) { return PHYSICS_VALID(value) ? value : fallback; }
static float PhysicsAuthoredNonNeg(float value, float fallback) { return PHYSICS_VALID_NONNEG(value) ? value : fallback; }
static float PhysicsAuthoredPositive(float value, float fallback) { return PHYSICS_VALID_POSITIVE(value) ? value : fallback; }

static JPH::Vec3 PhysicsAuthoredPoint(float3 authored)
{
    float values[3];
    store(authored, values);
    for (uint axis = 0; axis < 3; axis++)
    {
        if (!PHYSICS_VALID(values[axis]))
            values[axis] = 0.0f;
    }
    return JPH::Vec3(values[0], values[1], values[2]);
}

// Zero length reads as "not authored" and takes the fallback -- for a direction that is the only
// sane reading, and it is what a zeroed component holds.
static JPH::Vec3 PhysicsAuthoredDirection(float3 authored, JPH::Vec3 fallback)
{
    float values[3];
    store(authored, values);
    for (uint axis = 0; axis < 3; axis++)
    {
        if (!PHYSICS_VALID(values[axis]))
            return fallback;
    }
    JPH::Vec3 direction(values[0], values[1], values[2]);
    return direction.LengthSq() > 1.0e-8f ? direction.Normalized() : fallback;
}

static JPH::EMotionType PhysicsMotionType(const Components::RigidBody& rigidBody)
{
    return rigidBody.motionType >= 2 ? JPH::EMotionType::Dynamic
        : rigidBody.motionType == 1 ? JPH::EMotionType::Kinematic
        : JPH::EMotionType::Static;
}

// ---- world-space pose <-> Components::Transform ----------------------------------------------
// Jolt works in world space; Components::Transform is LOCAL to Parent. Components::WorldMatrix is
// NOT a place to write the result -- MainView::UpdateInstances recomputes it from Transform via
// ComputeWorldMatrix every time its dirty/version gate opens (Renderer.h), so anything written
// there is discarded. The math below mirrors Guizmo::SetWorldPositionRotationScale (Editor.h),
// which solves the identical problem for the gizmo; that lives after this header, so it is
// repeated rather than shared. Row-vector convention throughout: world = mul(local, parent).
static void PhysicsGetWorldPose(World::Entity ent, float3& outPosition, quaternion& outRotation, float3& outScale)
{
    float4x4 worldMatrix = ComputeWorldMatrix(ent);
    float3x3 rotation = float3x3(worldMatrix);
    outScale = float3(length(rotation[0]), length(rotation[1]), length(rotation[2]));
    rotation[0] = normalize(rotation[0]);   // drop scale, Jolt takes a pure rotation
    rotation[1] = normalize(rotation[1]);
    rotation[2] = normalize(rotation[2]);
    outRotation = quaternion(rotation);
    outPosition = worldMatrix[3].xyz;
}

static void PhysicsGetWorldPose(World::Entity ent, float3& outPosition, quaternion& outRotation)
{
    float3 ignoredScale;
    PhysicsGetWorldPose(ent, outPosition, outRotation, ignoredScale);
}

static void PhysicsSetWorldPose(World::Entity ent, float3 position, quaternion rotation)
{
    auto& transform = ent.Get<Components::Transform>();

    if (ent.Has<Components::Parent>())
    {
        World::Entity parentEnt = World::Entity(ent.Get<Components::Parent>().entity.Get());
        if (parentEnt != entityInvalid && parentEnt.IsValid())
        {
            float4x4 parentMatrix = ComputeWorldMatrix(parentEnt);
            float4x4 invParentMatrix = inverse(parentMatrix);

            float3x3 localRotation = mul(float3x3(float4x4(normalize(rotation))), inverse(float3x3(parentMatrix)));
            localRotation[0] = normalize(localRotation[0]);
            localRotation[1] = normalize(localRotation[1]);
            localRotation[2] = normalize(localRotation[2]);
            rotation = quaternion(localRotation);

            position = mul(float4(position, 1.0f), invParentMatrix).xyz;
        }
    }

    transform.position = position;
    transform.rotation = rotation;
    // Not optional. MainView::UpdateInstances only re-derives a world matrix when
    // loaded/dirty/ComputeTransformVersion changed (Renderer.h) -- without this bump the bodies
    // simulate perfectly and nothing moves on screen.
    transform.version++;
}

// ---- debug shape visualization ----------------------------------------------------------------
struct PhysicsDebugVertex
{
    float px, py, pz;
    float cr, cg, cb;
};
static_assert(sizeof(PhysicsDebugVertex) == 24, "PhysicsDebugVertex must match the shader's DebugVertex layout");

#ifdef JPH_DEBUG_RENDERER
class PhysicsDebugRenderer final : public JPH::DebugRendererSimple
{
public:
    std::vector<PhysicsDebugVertex> lines;

    virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override
    {
        PhysicsDebugVertex vertex;
        vertex.cr = inColor.r / 255.0f;
        vertex.cg = inColor.g / 255.0f;
        vertex.cb = inColor.b / 255.0f;

        vertex.px = (float)inFrom.GetX(); vertex.py = (float)inFrom.GetY(); vertex.pz = (float)inFrom.GetZ();
        lines.push_back(vertex);
        vertex.px = (float)inTo.GetX();   vertex.py = (float)inTo.GetY();   vertex.pz = (float)inTo.GetZ();
        lines.push_back(vertex);
    }

    virtual void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override
    {
        // The debug line pipeline has no text path; body labels aren't worth a separate system yet.
    }
};

class PhysicsSelectionDrawFilter final : public JPH::BodyDrawFilter
{
public:
    uint selectedEntity = 0;
    virtual bool ShouldDraw(const JPH::Body& inBody) const override
    {
        return (uint)inBody.GetUserData() == selectedEntity;
    }
};
#endif

struct Physics
{
    static Physics* instance;

    static constexpr float fixedDeltaSeconds = 1.0f / 60.0f;
    static constexpr float maxAccumulatedSeconds = 0.25f;   // a hitch must not turn into a step spiral
    static constexpr int   collisionSteps = 1;
    static constexpr JPH::uint maxBodies = 65536;
    static constexpr JPH::uint numBodyMutexes = 0;          // 0 = let Jolt pick
    static constexpr JPH::uint maxBodyPairs = 65536;
    static constexpr JPH::uint maxContactConstraints = 10240;

    JPH::TempAllocatorImpl* tempAllocator = nullptr;
    JPH::JobSystemThreadPool* jobSystem = nullptr;
    PhysicsBroadPhaseLayerInterface broadPhaseLayerInterface;
    PhysicsObjectVsBroadPhaseLayerFilter objectVsBroadPhaseLayerFilter;
    PhysicsObjectLayerPairFilter objectLayerPairFilter;
    JPH::PhysicsSystem physicsSystem;

    struct BodyRecord
    {
        JPH::BodyID bodyID;
        uint rev;               // entity revision the body was created for, to survive entity id reuse
        uint seenFrame;         // last sync that still found the entity; older than current = destroy
        uint transformVersion;  // ComputeTransformVersion as of our last read/write; a mismatch means
                                // something OUTSIDE physics moved the entity -> teleport the body
        float worldScale[3];    // world scale the shape was built at; Jolt shapes are immutable and
                                // carry their own scale, so a change here means rebuilding the shape
    };
    std::unordered_map<uint, BodyRecord> bodies;    // key = entity id
    // Mesh-derived shapes are immutable and shared, so one per asset is enough. Without this every
    // instance of the same mesh would re-read the .mesh off disk and rebuild the hull/BVH.
    std::unordered_map<assetID, JPH::ShapeRefC> convexHullShapeCache;
    std::unordered_map<assetID, JPH::ShapeRefC> meshShapeCache;
    // Entities already warned about a not-yet-available collision mesh, so the retry does not log
    // every frame. Cleared per entity once its body finally gets created.
    std::unordered_set<uint> pendingMeshWarned;

    struct ConstraintRecord
    {
        JPH::Ref<JPH::Constraint> constraint;
        uint rev;
        uint seenFrame;
        JPH::BodyID bodyA;
        JPH::BodyID bodyB;      // invalid when anchored to the world
    };
    std::unordered_map<uint, ConstraintRecord> constraints;   // key = entity id
    std::unordered_set<uint> pendingConstraintWarned;

    // A car. Kept apart from `constraints` even though JPH::VehicleConstraint IS a JPH::Constraint:
    // it is also a PhysicsStepListener (that is what actually casts the wheels and turns the
    // engine), it owns a collision tester, and it is rebuilt when its WHEEL entities change, none of
    // which the generic constraint path knows anything about.
    struct VehicleRecord
    {
        JPH::Ref<JPH::VehicleConstraint> constraint;
        uint rev;
        uint seenFrame;
        JPH::BodyID body;                       // chassis body, to catch a body recreated under us
        std::vector<World::Entity> wheels;      // wheel entity per Jolt wheel index, in build order
    };
    std::unordered_map<uint, VehicleRecord> vehicles;   // key = chassis entity id
    std::unordered_set<uint> pendingVehicleWarned;
    // Rebuilt every SyncVehicles: wheel entities bucketed by the chassis they hang under. A member
    // rather than a local so the buckets keep their capacity across frames.
    std::unordered_map<uint, std::vector<World::Entity>> wheelsByChassis;

    uint syncFrame = 0;
    float accumulator = 0.0f;

#ifdef JPH_DEBUG_RENDERER
    PhysicsDebugRenderer* debugRenderer = nullptr;
    PhysicsSelectionDrawFilter selectionDrawFilter;
#endif

    void On()
    {
        ZoneScoped;

        JPH::RegisterDefaultAllocator();
#ifdef JPH_ENABLE_ASSERTS
        JPH::AssertFailed = PhysicsAssertFailed;
#endif
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        tempAllocator = new JPH::TempAllocatorImpl(32 * 1024 * 1024);

        // Deliberately below hardware_concurrency: Taskflow's executor already owns a pool that
        // size, and Step() runs concurrently with the render tasks, so a full-width Jolt pool would
        // just fight it for cores.
        uint cores = (uint)std::thread::hardware_concurrency();
        //int workers = (int)(cores > 2 ? cores / 2 : 1);
        int workers = (int)(cores > 2 ? cores - 2 : 1);
        jobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, workers);

        /*
        // TODO : I would prefere to see a tracy row per logical core of the CPU and have JPH mixed with the other engine threads

        // Give Jolt's threads names so they show up as identifiable rows in Tracy instead of
        // anonymous ones. Names are in static storage on purpose: Tracy's fallback naming paths
        // keep the pointer rather than copying it. JPH_PROFILE_THREAD_START is a no-op under
        // JPH_EXTERNAL_PROFILE, so this hook is the only way to label them.
        static char threadNames[64][24] = {};
        jobSystem->SetThreadInitFunction([](int threadIndex)
        {
            if (threadIndex >= 0 && threadIndex < 64)
            {
                sprintf_s(threadNames[threadIndex], "Jolt Worker %d", threadIndex);
                tracy::SetThreadName(threadNames[threadIndex]);
            }
            else
            {
                tracy::SetThreadName("Jolt Worker");
            }
        });
        */
        
        physicsSystem.Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints, broadPhaseLayerInterface, objectVsBroadPhaseLayerFilter, objectLayerPairFilter);

#ifdef JPH_DEBUG_RENDERER
        debugRenderer = new PhysicsDebugRenderer();
#endif

        instance = this;
        IOs::Log("Physics: Jolt up, {} worker threads", workers);
    }

    void Off()
    {
        ZoneScoped;

        if (tempAllocator != nullptr)
        {
            // Cars first: a VehicleConstraint is also a step listener, and leaving one registered
            // while the bodies it drives are destroyed is a crash in the next Update.
            for (auto it = vehicles.begin(); it != vehicles.end(); ++it)
                DestroyVehicleRecord(it);
            vehicles.clear();

            for (auto it = constraints.begin(); it != constraints.end(); ++it)
                DestroyConstraintRecord(it);
            constraints.clear();

            JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
            for (auto& kv : bodies)
                DestroyBody(bodyInterface, kv.second.bodyID);
        }
        vehicles.clear();
        wheelsByChassis.clear();
        constraints.clear();
        bodies.clear();

#ifdef JPH_DEBUG_RENDERER
        delete debugRenderer;
        debugRenderer = nullptr;
#endif

        delete jobSystem;
        jobSystem = nullptr;
        delete tempAllocator;
        tempAllocator = nullptr;

        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        instance = nullptr;
    }

    // Stage 1, single-threaded, scheduled before DeferredRelease. Drains LAST frame's simulation
    // into the ECS, then pushes ECS changes into Jolt.
    void SyncAndReadback(World* world)
    {
        ZoneScopedN("PhysicsSyncReadback");
        Readback(world);
        Sync(world);
        SyncConstraints(world);
        SyncVehicles(world);
        DrawDebug();
    }

    // Single entry point for every physics overlay: they all feed the one line list, so it is
    // cleared here once, and DebugRendererSimple's camera position (it picks shape LOD from it) only
    // has to be pushed once as well.
    void DrawDebug()
    {
#ifdef JPH_DEBUG_RENDERER
        ZoneScopedN("PhysicsDebugDraw");

        if (debugRenderer == nullptr)
            return;
        debugRenderer->lines.clear();

        HLSL::Camera camera;
        if (GetMainViewCamera(camera))
            debugRenderer->SetCameraPos(JPH::RVec3(camera.worldPos.f32[0], camera.worldPos.f32[1], camera.worldPos.f32[2]));

        // Normalized once so the two draws below only have to test the id: a stale selection (the
        // entity was released) reads as "nothing selected" rather than as a live entity.
        World::Entity selected = editorState.selectedObject;
        if (!selected.IsValid())
            selected = World::Entity();

        DrawDebugShapes(selected);
        DrawDebugConstraints(selected);
        DrawDebugVehicles(selected);
#endif
    }

#ifdef JPH_DEBUG_RENDERER
    void DrawDebugShapes(World::Entity selected)
    {
        ZoneScopedN("PhysicsDebugShapes");

        // With the toggle off, still draw whatever is selected -- that costs nothing when nothing
        // is selected, and it is the fastest way to see that a body failed to be created at all.
        uint selectedEntity = 0;
        bool drawSelectionOnly = !options.drawPhysicsShapes;
        if (drawSelectionOnly)
        {
            if (selected.id == entityInvalid.id || !selected.Has<Components::PhysicsBody>())
                return;
            selectedEntity = selected.ToUInt();
        }

        JPH::BodyManager::DrawSettings settings;
        settings.mDrawShape = true;
        settings.mDrawShapeWireframe = true;   // the debug pipeline draws lines, not solids
        settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::MotionTypeColor;

        selectionDrawFilter.selectedEntity = selectedEntity;
        physicsSystem.DrawBodies(settings, debugRenderer, drawSelectionOnly ? &selectionDrawFilter : nullptr);
    }

    // Walks OUR record map instead of calling physicsSystem.DrawConstraints(), because that one
    // draws every constraint in the system with no filter -- there is no constraint equivalent of
    // BodyDrawFilter -- and the selection-only mode below is the half of this that gets used most.
    void DrawDebugConstraints(World::Entity selected)
    {
        ZoneScopedN("PhysicsDebugConstraints");

        if (constraints.empty())
            return;

        // The selection test is done on the BODY, not on the constraint's own entity, so that
        // picking EITHER end of a pair lights the constraint up -- including the target entity,
        // which carries no Constraint component of its own and would otherwise never match.
        JPH::BodyID selectedBody;
        bool drawSelectionOnly = !options.drawPhysicsConstraints;
        if (drawSelectionOnly)
        {
            if (selected.id == entityInvalid.id)
                return;
            auto body = bodies.find(selected.id);
            if (body == bodies.end() || body->second.rev != selected.rev)
                return;
            selectedBody = body->second.bodyID;
        }

        for (auto& kv : constraints)
        {
            JPH::Constraint* constraint = kv.second.constraint.GetPtr();
            if (constraint == nullptr)
                continue;
            if (drawSelectionOnly && kv.second.bodyA != selectedBody && kv.second.bodyB != selectedBody)
                continue;

            bool anchoredToWorld = kv.second.bodyB.IsInvalid();

            // FixedConstraint::DrawConstraint draws body-centre -> anchor for BOTH of its bodies.
            // Anchored to the world, body 2 IS Body::sFixedToWorld at the origin, so that second
            // line shoots across the whole level from 0,0,0. The anchor marker below says the same
            // thing without the stray line; every other type draws from its anchors, not the centre.
            if (!(anchoredToWorld && constraint->GetSubType() == JPH::EConstraintSubType::Fixed))
                constraint->DrawConstraint(debugRenderer);
            constraint->DrawConstraintLimits(debugRenderer);   // no-op on the types that have none

            // Everything we build in BuildConstraint is two-body, but GetType() is the cheap way to
            // make the downcast safe if a one-body type is ever added.
            if (constraint->GetType() != JPH::EConstraintType::TwoBodyConstraint)
                continue;
            JPH::TwoBodyConstraint* twoBody = static_cast<JPH::TwoBodyConstraint*>(constraint);
            // Matches what every JPH ...Constraint::DrawConstraint does internally: the constraint
            // matrices are relative to the body's CENTER OF MASS, not its origin.
            JPH::RVec3 anchorA = twoBody->GetBody1()->GetCenterOfMassTransform() * twoBody->GetConstraintToBody1Matrix().GetTranslation();

            if (anchoredToWorld)
            {
                // Nothing to connect to, so mark the pinned point itself.
                debugRenderer->DrawMarker(anchorA, JPH::Color::sYellow, 0.25f);
                continue;
            }

            // The one thing Jolt's per-type drawing never shows: WHICH two bodies a constraint ties
            // together. Fixed and Point in particular are otherwise just two loose markers.
            JPH::RVec3 anchorB = twoBody->GetBody2()->GetCenterOfMassTransform() * twoBody->GetConstraintToBody2Matrix().GetTranslation();
            debugRenderer->DrawLine(anchorA, anchorB, constraint->GetEnabled() ? JPH::Color::sYellow : JPH::Color::sGrey);
        }
    }

    // Cars, from the vehicle map for the same reason as above: DrawConstraints() has no filter.
    // A VehicleConstraint draws the thing that actually matters here -- where each wheel ended up on
    // its suspension, its contact point and the rpm meter -- none of which the collider wireframe
    // shows, because the wheels are not bodies and have no collider.
    void DrawDebugVehicles(World::Entity selected)
    {
        ZoneScopedN("PhysicsDebugVehicles");

        if (vehicles.empty())
            return;

        // Selecting a WHEEL lights up the car it belongs to, not just selecting the chassis: a
        // wheel is where you look when suspension travel is wrong, and it carries no body of its own.
        uint selectedEntity = 0;
        bool drawSelectionOnly = !options.drawPhysicsConstraints;
        if (drawSelectionOnly)
        {
            if (selected.id == entityInvalid.id)
                return;
            selectedEntity = selected.id;
            if (selected.Has<Components::Wheel>() && selected.Has<Components::Parent>())
            {
                World::Entity chassis = World::Entity(selected.Get<Components::Parent>().entity.Get());
                if (chassis != entityInvalid && chassis.IsValid())
                    selectedEntity = chassis.id;
            }
        }

        for (auto& kv : vehicles)
        {
            if (drawSelectionOnly && kv.first != selectedEntity)
                continue;
            JPH::VehicleConstraint* constraint = kv.second.constraint.GetPtr();
            if (constraint == nullptr)
                continue;
            constraint->DrawConstraint(debugRenderer);
            constraint->DrawConstraintLimits(debugRenderer);
        }
    }
#endif

    // Stage 2, a top-level task in Engine::Loop so it overlaps the editor + renderer tasks.
    // TODO : wiat until the DAG rework is done so that can be a system again and scheduled properly ?
    void Step()
    {
        ZoneScopedN("PhysicsStep");

        // TODO : do the same "is playing" test for the readback ?
        if (World::instance == nullptr || !engineState.playing)
        {
            accumulator = 0.0f;
            return;
        }

        accumulator += Time::instance->deltaSeconds;
        if (accumulator > maxAccumulatedSeconds)
            accumulator = maxAccumulatedSeconds;

        // Just do a step every frame... I like it better to test
        // see if does really simulate unheavenly. If so, maybe subdivide the collisionSteps depending on deltatime?
        //physicsSystem.Update(Time::instance->deltaSeconds, collisionSteps, tempAllocator, jobSystem);
        while (accumulator >= fixedDeltaSeconds)
        {
            physicsSystem.Update(fixedDeltaSeconds, collisionSteps, tempAllocator, jobSystem);
            accumulator -= fixedDeltaSeconds;
        }

    }

    // Speed along the car's OWN forward axis in m/s, signed -- negative means it is rolling
    // backwards. Signed is the whole point: it is what tells "the driver wants to reverse" from
    // "the driver wants to brake" (Systems::Driver), and it is what a speedometer would read.
    // False when the entity is not a built car.
    bool GetVehicleForwardSpeed(World::Entity ent, float& outSpeed)
    {
        auto record = vehicles.find(ent.id);
        if (record == vehicles.end() || record->second.rev != ent.rev || record->second.constraint == nullptr)
            return false;

        JPH::VehicleConstraint* constraint = record->second.constraint.GetPtr();
        const JPH::Body* body = constraint->GetVehicleBody();
        if (body == nullptr)
            return false;

        JPH::Vec3 localVelocity = body->GetRotation().Conjugated() * body->GetLinearVelocity();
        outSpeed = localVelocity.Dot(constraint->GetLocalForward());
        return true;
    }

    // Kill a body's linear and angular velocity. Anything that repositions an entity from outside
    // physics has to come here too: the teleport Sync does for an outside Transform edit is a
    // SetPositionAndRotation, which does NOT touch velocity, so a car flipped onto its roof and only
    // rotated upright keeps the spin that put it there and rolls straight back over.
    // Safe from a system task -- PhysicsStep succeeds the whole world subflow (Engine.h), so nothing
    // is stepping Jolt while systems run.
    bool StopBody(World::Entity ent)
    {
        auto record = bodies.find(ent.id);
        if (record == bodies.end() || record->second.rev != ent.rev)
            return false;

        JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
        if (bodyInterface.GetMotionType(record->second.bodyID) == JPH::EMotionType::Static)
            return false;

        bodyInterface.SetLinearAndAngularVelocity(record->second.bodyID, JPH::Vec3::sZero(), JPH::Vec3::sZero());
        return true;
    }

private:
    bool ReadCollisionGeometry(assetID meshAssetId, JPH::VertexList& outVertices, JPH::IndexedTriangleList& outTriangles)
    {
        ZoneScopedN("PhysicsReadCollisionGeometry");

        if (AssetLibrary::instance == nullptr || MeshLoader::instance == nullptr)
            return false;

        if (!AssetLibrary::instance->map.contains(meshAssetId) && !AssetLibrary::instance->ProbeCache(meshAssetId))
            return false;

        String meshPath = AssetLibrary::instance->map[meshAssetId].path;
        if (meshPath.empty())
            return false;

        MeshData meshData = MeshLoader::instance->Read(meshPath);
        if (meshData.vertices.empty() || meshData.LODs.empty())
            return false;

        outVertices.resize(meshData.vertices.size());

        // dont forget to unpack the vertex positions from the mesh's SNORM quantization !
        for (uint i = 0; i < (uint)meshData.vertices.size(); i++)
        {
            const VertexPacked& packed = meshData.vertices[i];
            // packedPos0 = [x | y<<16], packedPos1 = [z | handedness<<16] -- the high half of
            // packedPos1 is the TBN handedness sign, not geometry.
            int16_t quantized[3] =
            {
                (int16_t)(uint16_t)(packed.packedPos0 & 0xFFFF),
                (int16_t)(uint16_t)(packed.packedPos0 >> 16),
                (int16_t)(uint16_t)(packed.packedPos1 & 0xFFFF)
            };
            float decoded[3];
            for (uint axis = 0; axis < 3; axis++)
            {
                float normalized = (float)quantized[axis] / 32767.0f;   // hardware SNORM decode
                if (normalized < -1.0f)
                    normalized = -1.0f;
                decoded[axis] = meshData.aabbMin[axis] + (normalized * 0.5f + 0.5f) * meshData.aabbExtent[axis];
            }
            outVertices[i] = JPH::Float3(decoded[0], decoded[1], decoded[2]);
        }

        // LOD 0 is the highest detail. A coarser LOD would make a cheaper collider; correctness first.
        const std::vector<unsigned int>& indices = meshData.LODs[0].indices;
        uint vertexCount = (uint)outVertices.size();
        outTriangles.clear();
        outTriangles.reserve(indices.size() / 3);
        for (uint i = 0; i + 2 < (uint)indices.size(); i += 3)
        {
            seedAssert(indices[i] < vertexCount && indices[i + 1] < vertexCount && indices[i + 2] < vertexCount);
            if (indices[i] >= vertexCount || indices[i + 1] >= vertexCount || indices[i + 2] >= vertexCount)
                continue;   // still skipped in Release rather than handing Jolt an out-of-range index
            outTriangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
        }
        return !outTriangles.empty();
    }

    JPH::ShapeRefC GetOrCreateMeshBasedShape(Components::CollisionShape& collisionShape, uint entityId, bool wantsConvexHull, float convexRadius, bool& outRetry)
    {
        if (!collisionShape.mesh.IsValid())
        {
            IOs::Log("Physics: entity {} uses a mesh-based CollisionShape but its mesh handle is not set", entityId);
            return nullptr;
        }
        assetID meshAssetId = collisionShape.mesh.Get().id;

        auto& cache = wantsConvexHull ? convexHullShapeCache : meshShapeCache;
        auto cached = cache.find(meshAssetId);
        if (cached != cache.end())
            return cached->second;

        JPH::VertexList vertices;
        JPH::IndexedTriangleList triangles;
        if (!ReadCollisionGeometry(meshAssetId, vertices, triangles))
        {
            if (pendingMeshWarned.insert(entityId).second)
                IOs::Log("Physics: entity {} is waiting on its collision mesh asset (not registered/readable yet), retrying", entityId);
            outRetry = true;
            return nullptr;
        }

        JPH::ShapeSettings::ShapeResult result;
        if (wantsConvexHull)
        {
            JPH::Array<JPH::Vec3> points;
            points.reserve(vertices.size());
            for (uint i = 0; i < (uint)vertices.size(); i++)
                points.push_back(JPH::Vec3(vertices[i].x, vertices[i].y, vertices[i].z));
            result = JPH::ConvexHullShapeSettings(points, convexRadius).Create();
        }
        else
        {
            result = JPH::MeshShapeSettings(vertices, triangles).Create();
        }
        if (result.HasError())
        {
            IOs::Log("Physics: entity {} mesh-based shape build failed: {}", entityId, result.GetError().c_str());
            return nullptr;
        }

        uint vertexCount = (uint)vertices.size();
        uint triangleCount = (uint)triangles.size();
        IOs::Log("Physics: built {} collider from mesh ({} verts, {} tris), cached for reuse",
            wantsConvexHull ? "convex hull" : "mesh", vertexCount, triangleCount);

        cache[meshAssetId] = result.Get();
        return result.Get();
    }

    void DestroyConstraintRecord(std::unordered_map<uint, ConstraintRecord>::iterator it)
    {
        if (it->second.constraint != nullptr)
            physicsSystem.RemoveConstraint(it->second.constraint);
        pendingConstraintWarned.erase(it->first);
    }

    void RemoveConstraintsForBody(JPH::BodyID bodyID)
    {
        if (bodyID.IsInvalid() || constraints.empty())
            return;
        for (auto it = constraints.begin(); it != constraints.end();)
        {
            if (it->second.bodyA == bodyID || it->second.bodyB == bodyID)
            {
                DestroyConstraintRecord(it);
                it = constraints.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // Order matters: the step listener is what runs the vehicle every Update, so it has to be
    // unregistered before the constraint (and, above all, before the chassis body) goes away.
    void DestroyVehicleRecord(std::unordered_map<uint, VehicleRecord>::iterator it)
    {
        if (it->second.constraint != nullptr)
        {
            physicsSystem.RemoveStepListener(it->second.constraint);
            physicsSystem.RemoveConstraint(it->second.constraint);
        }
        pendingVehicleWarned.erase(it->first);
    }

    void RemoveVehiclesForBody(JPH::BodyID bodyID)
    {
        if (bodyID.IsInvalid() || vehicles.empty())
            return;
        for (auto it = vehicles.begin(); it != vehicles.end();)
        {
            if (it->second.body == bodyID)
            {
                DestroyVehicleRecord(it);
                it = vehicles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void DestroyBody(JPH::BodyInterface& bodyInterface, JPH::BodyID bodyID)
    {
        // Before the constraints: a VehicleConstraint keeps a raw Body* and is registered as a step
        // listener, so it is the one thing that must not outlive the body for even one frame.
        RemoveVehiclesForBody(bodyID);
        RemoveConstraintsForBody(bodyID);

        if (bodyID.IsInvalid())
            return;
        if (bodyInterface.IsAdded(bodyID))
            bodyInterface.RemoveBody(bodyID);
        bodyInterface.DestroyBody(bodyID);
    }

    // Anchors and axes are authored in the entity's LOCAL space, but the constraint is built in
    // EConstraintSpace::WorldSpace on purpose: Jolt's local space is relative to the body's CENTER OF
    // MASS, not its origin ("you need to subtract Shape::GetCenterOfMass()"), which is not what
    // Components::Constraint means. Converting here with the body's own transform is exact and needs
    // no COM correction.
    static JPH::Vec3 LocalPointToWorld(const JPH::Body& body, float3 localPoint)
    {
        float values[3];
        store(localPoint, values);
        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID(values[axis]))
                values[axis] = 0.0f;
        }
        return JPH::Vec3(body.GetWorldTransform() * JPH::Vec3(values[0], values[1], values[2]));
    }

    static JPH::Vec3 LocalAxisToWorld(const JPH::Body& body, float3 localAxis)
    {
        float values[3];
        store(localAxis, values);
        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID(values[axis]))
                values[axis] = 0.0f;
        }
        JPH::Vec3 result = body.GetWorldTransform().Multiply3x3(JPH::Vec3(values[0], values[1], values[2]));
        return result.LengthSq() > 1.0e-8f ? result.Normalized() : JPH::Vec3::sAxisY();
    }

    JPH::Ref<JPH::Constraint> BuildConstraint(World::Entity ent, JPH::BodyID idA, JPH::BodyID idB, bool anchoredToWorld, bool& outRetry)
    {
        outRetry = false;
        uint entityId = ent.id;

        Components::Constraint authored = ent.Get<Components::Constraint>();

        JPH::BodyID lockIds[2] = { idA, idB };
        JPH::BodyLockMultiWrite lock(physicsSystem.GetBodyLockInterface(), lockIds, anchoredToWorld ? 1 : 2);
        JPH::Body* bodyA = lock.GetBody(0);
        JPH::Body* bodyB = anchoredToWorld ? &JPH::Body::sFixedToWorld : lock.GetBody(1);
        if (bodyA == nullptr || bodyB == nullptr)
        {
            outRetry = true;
            return nullptr;
        }

        JPH::Vec3 pointA = LocalPointToWorld(*bodyA, authored.anchorLocal);
        // World-anchored: the second attachment point is the same world position, so the body starts
        // pinned where it already is rather than being yanked to the origin.
        JPH::Vec3 pointB = anchoredToWorld ? pointA : LocalPointToWorld(*bodyB, authored.anchorTarget);
        JPH::Vec3 axis = LocalAxisToWorld(*bodyA, authored.axis);
        JPH::Vec3 normalAxis = axis.GetNormalizedPerpendicular();

        bool hasLimits = PHYSICS_VALID(authored.limitMin) && PHYSICS_VALID(authored.limitMax) && authored.limitMin < authored.limitMax;

        JPH::Ref<JPH::Constraint> created;
        switch (authored.constraintType)
        {
        case 0:
        {
            JPH::FixedConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mAutoDetectPoint = false;
            settings.mPoint1 = pointA;
            settings.mPoint2 = pointB;
            created = settings.Create(*bodyA, *bodyB);
            break;
        }
        case 1:
        {
            JPH::PointConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = pointA;
            settings.mPoint2 = pointB;
            created = settings.Create(*bodyA, *bodyB);
            break;
        }
        case 2:
        {
            JPH::HingeConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = pointA;
            settings.mPoint2 = pointB;
            settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
            settings.mNormalAxis1 = settings.mNormalAxis2 = normalAxis;
            if (hasLimits)
            {
                // Jolt requires min in [-pi, 0] and max in [0, pi].
                settings.mLimitsMin = JPH::Clamp(authored.limitMin, -JPH::JPH_PI, 0.0f);
                settings.mLimitsMax = JPH::Clamp(authored.limitMax, 0.0f, JPH::JPH_PI);
            }
            created = settings.Create(*bodyA, *bodyB);
            break;
        }
        case 3:
        {
            JPH::SliderConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = pointA;
            settings.mPoint2 = pointB;
            settings.mSliderAxis1 = settings.mSliderAxis2 = axis;
            settings.mNormalAxis1 = settings.mNormalAxis2 = normalAxis;
            if (hasLimits)
            {
                settings.mLimitsMin = authored.limitMin;
                settings.mLimitsMax = authored.limitMax;
            }
            created = settings.Create(*bodyA, *bodyB);
            break;
        }
        case 4:
        {
            JPH::DistanceConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = pointA;
            settings.mPoint2 = pointB;
            if (hasLimits)
            {
                settings.mMinDistance = authored.limitMin;
                settings.mMaxDistance = authored.limitMax;
            }
            // else left at -1/-1, which tells Jolt to keep the current separation
            created = settings.Create(*bodyA, *bodyB);
            break;
        }
        default:
        {
            uint constraintType = authored.constraintType;
            IOs::Log("Physics: entity {} has unknown Constraint.constraintType {}, no constraint created", entityId, constraintType);
            return nullptr;
        }
        }

        if (created == nullptr)
        {
            IOs::Log("Physics: entity {} constraint creation failed", entityId);
            return nullptr;
        }

        physicsSystem.AddConstraint(created);
        pendingConstraintWarned.erase(entityId);
        return created;
    }

    void SyncConstraints(World* world)
    {
        ZoneScopedN("PhysicsSyncConstraints");

        uint queryIndex = world->Query(Components::Constraint::mask, 0, true);
        auto& queryResult = world->frameQueries[queryIndex];
        for (uint i = 0; i < (uint)queryResult.size(); i++)
        {
            World::Entity ent = queryResult[i];

            auto record = constraints.find(ent.id);
            bool constraintDirty = ent.Has<Components::State>() && (ent.Get<Components::State>().flags & Components::State::Flags::constraintDirty);

            if (record != constraints.end() && (record->second.rev != ent.rev || constraintDirty))
            {
                DestroyConstraintRecord(record);
                constraints.erase(record);
                record = constraints.end();
            }
            if (record != constraints.end())
            {
                record->second.seenFrame = syncFrame;
                if (constraintDirty)
                    ent.Get<Components::State>().flags.Unset(Components::State::Flags::constraintDirty);
                continue;
            }

            auto bodyA = bodies.find(ent.id);
            if (bodyA == bodies.end() || bodyA->second.rev != ent.rev)
                continue;

            auto& authored = ent.Get<Components::Constraint>();
            World::Entity target = World::Entity(authored.target);
            bool anchoredToWorld = target.id == entityInvalid.id || !target.IsValid();

            JPH::BodyID idB;
            if (!anchoredToWorld)
            {
                auto bodyB = bodies.find(target.id);
                if (bodyB == bodies.end() || bodyB->second.rev != target.rev)
                {
                    uint entityId = ent.id;
                    if (pendingConstraintWarned.insert(entityId).second)
                        IOs::Log("Physics: entity {} constraint is waiting on its target's body, retrying", entityId);
                    continue;
                }
                idB = bodyB->second.bodyID;
            }

            bool retry = false;
            JPH::Ref<JPH::Constraint> created = BuildConstraint(ent, bodyA->second.bodyID, idB, anchoredToWorld, retry);
            if (created == nullptr)
            {
                if (!retry && ent.Has<Components::State>())
                    ent.Get<Components::State>().flags.Unset(Components::State::Flags::constraintDirty);
                continue;
            }

            constraints[ent.id] = { created, ent.rev, syncFrame, bodyA->second.bodyID, idB };
            if (ent.Has<Components::State>())
                ent.Get<Components::State>().flags.Unset(Components::State::Flags::constraintDirty);
        }

        for (auto it = constraints.begin(); it != constraints.end();)
        {
            if (it->second.seenFrame != syncFrame)
            {
                DestroyConstraintRecord(it);
                it = constraints.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    static bool SameWheels(const std::vector<World::Entity>& a, const std::vector<World::Entity>& b)
    {
        if (a.size() != b.size())
            return false;
        for (uint i = 0; i < (uint)a.size(); i++)
        {
            if (a[i].id != b[i].id || a[i].rev != b[i].rev)
                return false;
        }
        return true;
    }

    // Turns the chassis' Vehicle component plus its children's Wheel components into one Jolt
    // vehicle. Everything below is read defensively for the reason at the top of this header -- and
    // more so here than anywhere else, because a car has ~20 numbers and Jolt asserts on a good half
    // of them (an inverted suspension range, an rpm band that is not a band, ...).
    // The wheel list is taken by non-const reference only because World::Entity::Get is non-const.
    JPH::Ref<JPH::VehicleConstraint> BuildVehicle(World::Entity ent, JPH::BodyID bodyID, std::vector<World::Entity>& wheelEntities, bool& outRetry)
    {
        outRetry = false;
        uint entityId = ent.id;

        Components::Vehicle authored = ent.Get<Components::Vehicle>();

        JPH::Vec3 up = PhysicsAuthoredDirection(authored.up, JPH::Vec3::sAxisY());
        JPH::Vec3 forward = PhysicsAuthoredDirection(authored.forward, JPH::Vec3::sAxisZ());
        // up x forward, which in SeeD's LEFT-handed frame (+X right, +Y up, +Z forward -- the axes
        // the camera's WASD keys move along, World.h) makes right +X. Jolt's own sample derives it
        // the other way round because Jolt is right-handed, and copying that is what used to put a
        // car's "left" wheels on its right side.
        JPH::Vec3 right = up.Cross(forward);
        if (right.LengthSq() < 1.0e-6f)
        {
            IOs::Log("Physics: entity {} has a Vehicle whose up and forward are parallel, falling back to +Y up / +Z forward", entityId);
            up = JPH::Vec3::sAxisY();
            forward = JPH::Vec3::sAxisZ();
            right = up.Cross(forward);
        }
        right = right.Normalized();

        JPH::VehicleConstraintSettings settings;
        settings.mUp = up;
        settings.mForward = forward;
        settings.mMaxPitchRollAngle = JPH::Clamp(PhysicsAuthoredNonNeg(authored.maxPitchRollAngle, JPH::JPH_PI), 0.0f, JPH::JPH_PI);
        settings.mDrawConstraintSize = 0.1f;

        float smallestRadius = 0.0f;
        for (uint i = 0; i < (uint)wheelEntities.size(); i++)
        {
            Components::Wheel wheel = wheelEntities[i].Get<Components::Wheel>();

            // Raw pointer, handed to the Array<Ref<>> straight away so it owns it from here on.
            JPH::WheelSettingsWV* wheelSettings = new JPH::WheelSettingsWV();
            settings.mWheels.push_back(wheelSettings);

            wheelSettings->mPosition = PhysicsAuthoredPoint(wheel.position);
            wheelSettings->mSuspensionDirection = PhysicsAuthoredDirection(wheel.suspensionDirection, -up);
            wheelSettings->mSteeringAxis = up;
            wheelSettings->mWheelUp = up;
            wheelSettings->mWheelForward = forward;
            wheelSettings->mRadius = PhysicsAuthoredPositive(wheel.radius, 0.3f);
            wheelSettings->mWidth = PhysicsAuthoredPositive(wheel.width, 0.1f);
            wheelSettings->mSuspensionMinLength = PhysicsAuthoredNonNeg(wheel.suspensionMinLength, 0.3f);
            wheelSettings->mSuspensionMaxLength = PhysicsAuthoredNonNeg(wheel.suspensionMaxLength, 0.5f);
            // An inverted travel range is a Jolt assert, and it is what a half-authored wheel holds.
            if (wheelSettings->mSuspensionMaxLength < wheelSettings->mSuspensionMinLength)
                wheelSettings->mSuspensionMaxLength = wheelSettings->mSuspensionMinLength;
            wheelSettings->mSuspensionSpring.mMode = JPH::ESpringMode::FrequencyAndDamping;
            wheelSettings->mSuspensionSpring.mFrequency = PhysicsAuthoredPositive(wheel.suspensionFrequency, 1.5f);
            wheelSettings->mSuspensionSpring.mDamping = PhysicsAuthoredNonNeg(wheel.suspensionDamping, 0.5f);
            wheelSettings->mMaxSteerAngle = JPH::Clamp(PhysicsAuthoredNonNeg(wheel.maxSteerAngle, 0.0f), 0.0f, 0.5f * JPH::JPH_PI);
            wheelSettings->mMaxBrakeTorque = PhysicsAuthoredNonNeg(wheel.maxBrakeTorque, 1500.0f);
            wheelSettings->mMaxHandBrakeTorque = PhysicsAuthoredNonNeg(wheel.maxHandBrakeTorque, 0.0f);
            wheelSettings->mInertia = PhysicsAuthoredPositive(wheel.inertia, 0.9f);
            wheelSettings->mAngularDamping = PhysicsAuthoredNonNeg(wheel.angularDamping, 0.2f);

            smallestRadius = (i == 0) ? wheelSettings->mRadius : min(smallestRadius, wheelSettings->mRadius);
        }

        JPH::WheeledVehicleControllerSettings* controller = new JPH::WheeledVehicleControllerSettings();
        settings.mController = controller;   // same ownership handover as the wheels above

        controller->mEngine.mMaxTorque = PhysicsAuthoredPositive(authored.engineMaxTorque, 500.0f);
        controller->mEngine.mMinRPM = PhysicsAuthoredPositive(authored.engineMinRPM, 1000.0f);
        controller->mEngine.mMaxRPM = PhysicsAuthoredPositive(authored.engineMaxRPM, 6000.0f);
        if (controller->mEngine.mMaxRPM <= controller->mEngine.mMinRPM)
            controller->mEngine.mMaxRPM = controller->mEngine.mMinRPM + 1000.0f;
        controller->mEngine.mInertia = PhysicsAuthoredPositive(authored.engineInertia, 0.5f);
        // The shift points have to sit inside the rev band, and down strictly below up, or the
        // gearbox oscillates between two gears every step.
        controller->mTransmission.mShiftUpRPM = JPH::Clamp(PhysicsAuthoredPositive(authored.shiftUpRPM, 4000.0f), controller->mEngine.mMinRPM + 1.0f, controller->mEngine.mMaxRPM);
        controller->mTransmission.mShiftDownRPM = JPH::Clamp(PhysicsAuthoredPositive(authored.shiftDownRPM, 2000.0f), controller->mEngine.mMinRPM, controller->mTransmission.mShiftUpRPM - 1.0f);
        controller->mTransmission.mClutchStrength = PhysicsAuthoredPositive(authored.clutchStrength, 10.0f);

        // One differential per axle, splitting the engine torque between the wheel on the left of
        // that axle and the one on its right -- which side a wheel is on comes from the sign of its
        // position along the car's own right axis, so authoring only has to say WHICH axle it is on.
        bool anyDriven = false;
        for (uint i = 0; i < (uint)wheelEntities.size(); i++)
            anyDriven |= wheelEntities[i].Get<Components::Wheel>().driven != 0;
        if (!anyDriven)
            IOs::Log("Physics: entity {} is a car with no wheel marked driven -- driving all {} of them instead", entityId, (uint)wheelEntities.size());

        std::vector<uint> axleIds;
        std::vector<std::vector<int>> axleLeft;
        std::vector<std::vector<int>> axleRight;
        for (uint i = 0; i < (uint)wheelEntities.size(); i++)
        {
            if (anyDriven && wheelEntities[i].Get<Components::Wheel>().driven == 0)
                continue;

            uint axle = wheelEntities[i].Get<Components::Wheel>().axle;
            uint group = 0;
            while (group < (uint)axleIds.size() && axleIds[group] != axle)
                group++;
            if (group == (uint)axleIds.size())
            {
                axleIds.push_back(axle);
                axleLeft.push_back(std::vector<int>());
                axleRight.push_back(std::vector<int>());
            }

            if (settings.mWheels[i]->mPosition.Dot(right) > 0.0f)
                axleRight[group].push_back((int)i);
            else
                axleLeft[group].push_back((int)i);
        }

        // Paired inside the axle, so an odd wheel count or a six-wheeler still gets working
        // differentials -- Jolt takes -1 for a missing side.
        for (uint group = 0; group < (uint)axleIds.size(); group++)
        {
            uint pairs = max((uint)axleLeft[group].size(), (uint)axleRight[group].size());
            for (uint pair = 0; pair < pairs; pair++)
            {
                JPH::VehicleDifferentialSettings differential;
                differential.mLeftWheel = pair < (uint)axleLeft[group].size() ? axleLeft[group][pair] : -1;
                differential.mRightWheel = pair < (uint)axleRight[group].size() ? axleRight[group][pair] : -1;
                differential.mDifferentialRatio = PhysicsAuthoredPositive(authored.differentialRatio, 3.42f);
                controller->mDifferentials.push_back(differential);
            }
        }
        // Jolt wants the ratios to sum to 1; without this every axle would get the FULL engine
        // torque and a 4WD car would be twice as fast as the same car in 2WD.
        if (!controller->mDifferentials.empty())
        {
            float share = 1.0f / (float)controller->mDifferentials.size();
            for (uint i = 0; i < (uint)controller->mDifferentials.size(); i++)
                controller->mDifferentials[i].mEngineTorqueRatio = share;
        }

        JPH::Ref<JPH::VehicleConstraint> created;
        {
            JPH::BodyLockWrite lock(physicsSystem.GetBodyLockInterface(), bodyID);
            if (!lock.Succeeded())
            {
                outRetry = true;
                return nullptr;
            }
            JPH::Body& body = lock.GetBody();
            if (body.GetMotionType() != JPH::EMotionType::Dynamic)
            {
                // Warn once: SyncVehicles keeps retrying a car it could not build (that is what lets
                // a car come up once its wheels are added), so this would otherwise log every frame.
                if (pendingVehicleWarned.insert(entityId).second)
                    IOs::Log("Physics: entity {} has a Vehicle but its RigidBody.motionType is not Dynamic -- a car has to be a dynamic body, no car created", entityId);
                return nullptr;
            }
            created = new JPH::VehicleConstraint(body, settings);
        }

        // WORLD up, not the chassis' own: this is the vector that stops a wheel from finding a wall
        // and treating it as ground, so it has to follow gravity rather than the car's attitude.
        JPH::Vec3 gravity = physicsSystem.GetGravity();
        JPH::Vec3 worldUp = gravity.LengthSq() > 1.0e-8f ? -gravity.Normalized() : JPH::Vec3::sAxisY();
        float maxSlopeAngle = JPH::Clamp(PhysicsAuthoredNonNeg(authored.maxSlopeAngle, JPH::DegreesToRadians(80.0f)), 0.0f, 0.5f * JPH::JPH_PI);
        // PhysicsLayers::moving, so the wheels see the static world AND other moving bodies
        // (PhysicsObjectLayerPairFilter lets moving collide with everything).
        switch (authored.collisionTester)
        {
        case 1:
            created->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastSphere(PhysicsLayers::moving, 0.5f * smallestRadius, worldUp, maxSlopeAngle));
            break;
        case 2:
            // The cylinder tester has no slope cutoff -- it tests the real wheel shape instead.
            created->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastCylinder(PhysicsLayers::moving));
            break;
        default:
            if (authored.collisionTester > 2)
            {
                uint collisionTester = authored.collisionTester;
                IOs::Log("Physics: entity {} has unknown Vehicle.collisionTester {}, using the ray tester", entityId, collisionTester);
            }
            created->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterRay(PhysicsLayers::moving, worldUp, maxSlopeAngle));
            break;
        }

        // Both lists, and both matter: the constraint solves the suspension, the step listener is
        // what casts the wheels and turns the engine. Registered as one, a car just sags and sits.
        physicsSystem.AddConstraint(created);
        physicsSystem.AddStepListener(created);
        pendingVehicleWarned.erase(entityId);
        return created;
    }

    // Every frame, for a car that already exists. Deliberately NOT part of the rebuild path: driver
    // input changes constantly and rebuilding the constraint for it would reset the engine rpm, the
    // current gear and every wheel's angular velocity on every touch of the accelerator.
    void UpdateVehicle(World::Entity ent, VehicleRecord& record)
    {
        Components::Vehicle& authored = ent.Get<Components::Vehicle>();
        JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(record.constraint->GetController());

        float forwardInput = JPH::Clamp(PhysicsAuthored(authored.forwardInput, 0.0f), -1.0f, 1.0f);
        float rightInput = JPH::Clamp(PhysicsAuthored(authored.rightInput, 0.0f), -1.0f, 1.0f);
        float brakeInput = JPH::Clamp(PhysicsAuthored(authored.brakeInput, 0.0f), 0.0f, 1.0f);
        float handBrakeInput = JPH::Clamp(PhysicsAuthored(authored.handBrakeInput, 0.0f), 0.0f, 1.0f);
        controller->SetDriverInput(forwardInput, rightInput, brakeInput, handBrakeInput);

        // A parked car falls asleep, and a sleeping body's step listener does nothing -- so without
        // this the gas pedal would be dead until something else happened to wake the car up.
        if (forwardInput != 0.0f || rightInput != 0.0f || brakeInput != 0.0f || handBrakeInput != 0.0f)
            physicsSystem.GetBodyInterface().ActivateBody(record.body);

        WriteBackWheelTransforms(ent, record);
    }

    // The wheel entities' Transform is OUTPUT: rewritten from the simulated pose so the mesh sits on
    // its suspension, steers and spins. Jolt returns that pose in the chassis BODY's space, which is
    // the wheel entity's local space (the wheels are its children) once the chassis' world scale --
    // which the body does not carry, its shape does -- is divided back out.
    void WriteBackWheelTransforms(World::Entity ent, VehicleRecord& record)
    {
        float scale[3] = { 1.0f, 1.0f, 1.0f };
        auto bodyRecord = bodies.find(ent.id);
        if (bodyRecord != bodies.end())
        {
            for (uint axis = 0; axis < 3; axis++)
            {
                if (PHYSICS_VALID(bodyRecord->second.worldScale[axis]) && fabsf(bodyRecord->second.worldScale[axis]) > 1.0e-6f)
                    scale[axis] = bodyRecord->second.worldScale[axis];
            }
        }

        uint wheelCount = min((uint)record.wheels.size(), (uint)record.constraint->GetWheels().size());
        for (uint i = 0; i < wheelCount; i++)
        {
            World::Entity wheelEnt = record.wheels[i];
            if (!wheelEnt.IsValid() || !wheelEnt.Has<Components::Transform>())
                continue;

            // The wheel MESH convention: its +X is the axle (pointing right) and its +Y is up. A
            // wheel modelled along another axis has to be rotated in the asset; there is no
            // per-wheel override, one convention keeps the four wheels of a car interchangeable.
            JPH::Mat44 wheelMatrix = record.constraint->GetWheelLocalTransform(i, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
            JPH::Vec3 wheelPosition = wheelMatrix.GetTranslation();
            JPH::Quat wheelRotation = wheelMatrix.GetQuaternion().Normalized();

            float3 position = float3(wheelPosition.GetX() / scale[0], wheelPosition.GetY() / scale[1], wheelPosition.GetZ() / scale[2]);
            quaternion rotation = quaternion(wheelRotation.GetX(), wheelRotation.GetY(), wheelRotation.GetZ(), wheelRotation.GetW());

            auto& transform = wheelEnt.Get<Components::Transform>();

            // Only on a real change: transform.version is what makes the renderer recompute a world
            // matrix (Renderer.h), and a parked car would otherwise redo all four wheels every frame.
            float current[3];
            float next[3];
            store(transform.position, current);
            store(position, next);
            bool changed = false;
            for (uint axis = 0; axis < 3; axis++)
                changed |= !PHYSICS_VALID(current[axis]) || fabsf(current[axis] - next[axis]) > 1.0e-5f;
            for (uint k = 0; k < 4; k++)
                changed |= !PHYSICS_VALID(transform.rotation.f32[k]) || fabsf(transform.rotation.f32[k] - rotation.f32[k]) > 1.0e-5f;
            if (!changed)
                continue;

            transform.position = position;
            transform.rotation = rotation;
            transform.version++;
        }
    }

    // Stage 1c, after SyncConstraints: the chassis body has to exist first, and a car may also carry
    // an ordinary Constraint of its own (a towbar, say) which is built there.
    void SyncVehicles(World* world)
    {
        ZoneScopedN("PhysicsSyncVehicles");

        // Wheels are found through Parent -- there is no children list (World.h) -- so one pass over
        // every Wheel entity buckets them by chassis before any car is visited.
        wheelsByChassis.clear();
        uint wheelQueryIndex = world->Query(Components::Wheel::mask, 0, true);
        auto& wheelQueryResult = world->frameQueries[wheelQueryIndex];
        for (uint i = 0; i < (uint)wheelQueryResult.size(); i++)
        {
            World::Entity wheelEnt = wheelQueryResult[i];

            World::Entity chassis = entityInvalid;
            if (wheelEnt.Has<Components::Parent>())
                chassis = World::Entity(wheelEnt.Get<Components::Parent>().entity);
            if (chassis.id == entityInvalid.id || !chassis.IsValid() || !chassis.Has<Components::Vehicle>())
            {
                uint wheelEntityId = wheelEnt.id;
                if (pendingVehicleWarned.insert(wheelEntityId).second)
                    IOs::Log("Physics: entity {} has a Wheel but is not a child of an entity with a Vehicle component -- it belongs to no car and is ignored", wheelEntityId);
                continue;
            }
            wheelsByChassis[chassis.id].push_back(wheelEnt);
        }

        uint queryIndex = world->Query(Components::Vehicle::mask, 0, true);
        auto& queryResult = world->frameQueries[queryIndex];
        for (uint i = 0; i < (uint)queryResult.size(); i++)
        {
            World::Entity ent = queryResult[i];
            std::vector<World::Entity>& wheels = wheelsByChassis[ent.id];

            // A car is rebuilt whole. An edit to a WHEEL raises the bit on that wheel's entity, not
            // on the chassis, so both ends are read here -- and both cleared at the bottom.
            bool vehicleDirty = ent.Has<Components::State>() && (ent.Get<Components::State>().flags & Components::State::Flags::vehicleDirty);
            for (uint w = 0; w < (uint)wheels.size(); w++)
                vehicleDirty |= wheels[w].Has<Components::State>() && (wheels[w].Get<Components::State>().flags & Components::State::Flags::vehicleDirty);

            auto bodyRecord = bodies.find(ent.id);
            bool hasBody = bodyRecord != bodies.end() && bodyRecord->second.rev == ent.rev;

            auto record = vehicles.find(ent.id);
            if (record != vehicles.end())
            {
                // A recreated body (Sync does that on a motionType change) leaves the constraint
                // holding a dangling Body* -- the bodyID test below is what catches it.
                bool stale = record->second.rev != ent.rev
                    || vehicleDirty
                    || !hasBody
                    || record->second.body != bodyRecord->second.bodyID
                    || !SameWheels(record->second.wheels, wheels);
                if (stale)
                {
                    DestroyVehicleRecord(record);
                    vehicles.erase(record);
                    record = vehicles.end();
                }
                else
                {
                    record->second.seenFrame = syncFrame;
                }
            }

            if (record == vehicles.end())
            {
                if (wheels.empty())
                {
                    uint entityId = ent.id;
                    if (pendingVehicleWarned.insert(entityId).second)
                        IOs::Log("Physics: entity {} has a Vehicle but no child entity with a Wheel component -- a car needs at least one wheel", entityId);
                }
                else if (hasBody)
                {
                    bool retry = false;
                    JPH::Ref<JPH::VehicleConstraint> created = BuildVehicle(ent, bodyRecord->second.bodyID, wheels, retry);
                    if (created == nullptr && retry)
                        continue;   // keeps the dirty bit, so the next frame has another go
                    if (created != nullptr)
                    {
                        VehicleRecord& fresh = vehicles[ent.id];
                        fresh.constraint = created;
                        fresh.rev = ent.rev;
                        fresh.seenFrame = syncFrame;
                        fresh.body = bodyRecord->second.bodyID;
                        fresh.wheels = wheels;
                        record = vehicles.find(ent.id);
                    }
                }
            }

            // Cleared whether or not a car came out of it: a permanent failure (no wheels, a static
            // chassis) must not retry every frame. The retry path above continues before this.
            if (ent.Has<Components::State>())
                ent.Get<Components::State>().flags.Unset(Components::State::Flags::vehicleDirty);
            for (uint w = 0; w < (uint)wheels.size(); w++)
            {
                if (wheels[w].Has<Components::State>())
                    wheels[w].Get<Components::State>().flags.Unset(Components::State::Flags::vehicleDirty);
            }

            if (record != vehicles.end())
                UpdateVehicle(ent, record->second);
        }

        // Same sweep as the bodies and constraints: anything the query stopped returning is gone.
        for (auto it = vehicles.begin(); it != vehicles.end();)
        {
            if (it->second.seenFrame != syncFrame)
            {
                DestroyVehicleRecord(it);
                it = vehicles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void Readback(World* world)
    {
        ZoneScopedN("PhysicsReadback");

        JPH::BodyIDVector active;
        physicsSystem.GetActiveBodies(JPH::EBodyType::RigidBody, active);
        if (active.empty())
            return;

        const JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterfaceNoLock();
        for (uint i = 0; i < (uint)active.size(); i++)
        {
            World::Entity ent;
            ent.FromUInt((uint)bodyInterface.GetUserData(active[i]));
            if (ent.id >= (uint)world->entitySlots.size() || !ent.IsValid())
                continue;

            if (!ent.Has<Components::Transform>())
                continue;

            auto record = bodies.find(ent.id);
            bool tracked = record != bodies.end() && record->second.rev == ent.rev;

            // Something outside physics (gizmo, a moved ancestor, a loader) wrote this Transform
            // since we last touched it. Do NOT overwrite it with the body's stale pose: leave it
            // alone and let Sync -- which runs straight after -- push it into Jolt as a teleport.
            // Writing first is what made a gizmo drag snap back, because absorbing the version below
            // then hid the edit from Sync's teleport check entirely.
            if (tracked && ComputeTransformVersion(ent) != record->second.transformVersion)
                continue;

            JPH::RVec3 position;
            JPH::Quat rotation;
            bodyInterface.GetPositionAndRotation(active[i], position, rotation);

            PhysicsSetWorldPose(ent,
                float3((float)position.GetX(), (float)position.GetY(), (float)position.GetZ()),
                quaternion(rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()));

            // Absorb the version bump we just caused, so Sync sees only outside changes. Without
            // this every simulated body would teleport itself back to where it already is, forever.
            if (tracked)
                record->second.transformVersion = ComputeTransformVersion(ent);
        }
    }

    void Sync(World* world)
    {
        ZoneScopedN("PhysicsSync");

        JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
        syncFrame++;

        uint queryIndex = world->Query(Components::RigidBody::mask | Components::CollisionShape::mask, 0, true);
        auto& queryResult = world->frameQueries[queryIndex];
        for (uint i = 0; i < (uint)queryResult.size(); i++)
        {
            World::Entity ent = queryResult[i];

            auto record = bodies.find(ent.id);
            if (record != bodies.end() && (record->second.rev != ent.rev || (uint)bodyInterface.GetUserData(record->second.bodyID) != ent.ToUInt()))
            {
                // The entity id was recycled by a different entity while our record survived (a
                // release only frees the slot at DeferredRelease, so the sweep below sees it a frame
                // late). Kill the orphaned body now -- otherwise CreateBody overwrites the record
                // below and the old Jolt body leaks for the rest of the session.
                DestroyBody(bodyInterface, record->second.bodyID);
                bodies.erase(record);
                record = bodies.end();
            }

            bool physicsDirty = ent.Has<Components::State>() && (ent.Get<Components::State>().flags & Components::State::Flags::physicsDirty);

            if (record != bodies.end())
            {
                record->second.seenFrame = syncFrame;
                if (physicsDirty)
                {
                    if (bodyInterface.GetMotionType(record->second.bodyID) != PhysicsMotionType(ent.Get<Components::RigidBody>()))
                    {
                        DestroyBody(bodyInterface, record->second.bodyID);
                        bodies.erase(record);
                        ent.Get<Components::PhysicsBody>().bodyID = JPH::BodyID::cInvalidBodyID;
                        ent.Get<Components::State>().flags.Unset(Components::State::Flags::physicsDirty);
                        CreateBody(ent, bodyInterface);
                        continue;
                    }
                    ApplyBodyProperties(ent, record->second.bodyID, bodyInterface);
                    ent.Get<Components::State>().flags.Unset(Components::State::Flags::physicsDirty);
                }
                // Anything that moved the entity from outside physics (gizmo, a moved ancestor,
                // a loader) shows up as a version change. Readback refreshes this cache after each
                // write, so a body's own simulated motion never looks like a teleport here.
                uint transformVersion = ComputeTransformVersion(ent);
                if (transformVersion != record->second.transformVersion && ent.Has<Components::Transform>())
                {
                    float3 position;
                    quaternion rotation;
                    float3 worldScale;
                    PhysicsGetWorldPose(ent, position, rotation, worldScale);

                    // A Transform edit that changed the world SCALE (this entity's or any
                    // ancestor's) needs the shape rebuilt -- Jolt shapes are immutable and carry
                    // their own scale, so there is nothing to update in place.
                    bool scaleChanged = false;
                    for (uint axis = 0; axis < 3; axis++)
                        scaleChanged |= fabsf(worldScale[axis] - record->second.worldScale[axis]) > 1.0e-4f;
                    if (scaleChanged && ent.Has<Components::CollisionShape>())
                    {
                        uint entityId = ent.id;
                        JPH::EMotionType motionType = PhysicsMotionType(ent.Get<Components::RigidBody>());
                        bool ignoredRetry = false;
                        JPH::ShapeRefC shape = BuildShape(ent.Get<Components::CollisionShape>(), motionType, entityId, worldScale, ignoredRetry);
                        if (shape != nullptr)
                        {
                            bodyInterface.SetShape(record->second.bodyID, shape, true, motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
                            record->second.worldScale[0] = worldScale[0];
                            record->second.worldScale[1] = worldScale[1];
                            record->second.worldScale[2] = worldScale[2];
                        }
                    }

                    bodyInterface.SetPositionAndRotation(record->second.bodyID,
                        JPH::RVec3(position[0], position[1], position[2]),
                        JPH::Quat(rotation.f32[0], rotation.f32[1], rotation.f32[2], rotation.f32[3]).Normalized(),
                        bodyInterface.GetMotionType(record->second.bodyID) == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
                    record->second.transformVersion = transformVersion;
                }
                continue;
            }

            bool alreadyFailed = ent.Has<Components::PhysicsBody>() && ent.Get<Components::PhysicsBody>().bodyID == JPH::BodyID::cInvalidBodyID;
            if (!alreadyFailed || physicsDirty)
            {
                if (physicsDirty)
                    ent.Get<Components::State>().flags.Unset(Components::State::Flags::physicsDirty);
                CreateBody(ent, bodyInterface);
            }
        }

        // Anything the query no longer returns is gone (released, or its RigidBody was removed).
        // Runs before DeferredRelease, so the Jolt body dies before the entity slot is recycled.
        for (auto it = bodies.begin(); it != bodies.end();)
        {
            if (it->second.seenFrame != syncFrame)
            {
                DestroyBody(bodyInterface, it->second.bodyID);
                it = bodies.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ApplyBodyProperties(World::Entity ent, JPH::BodyID bodyID, JPH::BodyInterface& bodyInterface)
    {
        if (bodyID.IsInvalid() || !ent.Has<Components::RigidBody>())
            return;
        auto& rigidBody = ent.Get<Components::RigidBody>();

        JPH::EMotionType motionType = PhysicsMotionType(rigidBody);

        if (ent.Has<Components::CollisionShape>())
        {
            float3 worldScale = float3(1, 1, 1);
            if (ent.Has<Components::Transform>())
            {
                float3 ignoredPosition;
                quaternion ignoredRotation;
                PhysicsGetWorldPose(ent, ignoredPosition, ignoredRotation, worldScale);
            }
            bool ignoredRetry = false;
            JPH::ShapeRefC shape = BuildShape(ent.Get<Components::CollisionShape>(), motionType, ent.id, worldScale, ignoredRetry);
            if (shape != nullptr && shape != bodyInterface.GetShape(bodyID))
            {
                bodyInterface.SetShape(bodyID, shape, true, motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
                auto record = bodies.find(ent.id);
                if (record != bodies.end())
                    store(worldScale, record->second.worldScale);
            }
        }

        if (PHYSICS_VALID_NONNEG(rigidBody.friction))
            bodyInterface.SetFriction(bodyID, rigidBody.friction);
        if (PHYSICS_VALID_NONNEG(rigidBody.restitution))
            bodyInterface.SetRestitution(bodyID, rigidBody.restitution);
        if (PHYSICS_VALID(rigidBody.gravityFactor))
            bodyInterface.SetGravityFactor(bodyID, rigidBody.gravityFactor);

        // Damping, mass and the sensor flag have no BodyInterface setters, so go through the body.
        JPH::BodyLockWrite lock(physicsSystem.GetBodyLockInterface(), bodyID);
        if (!lock.Succeeded())
            return;
        JPH::Body& body = lock.GetBody();
        body.SetIsSensor(rigidBody.isSensor != 0);

        JPH::MotionProperties* motionProperties = body.GetMotionPropertiesUnchecked();
        if (motionProperties == nullptr)
            return;
        if (PHYSICS_VALID_NONNEG(rigidBody.linearDamping))
            motionProperties->SetLinearDamping(rigidBody.linearDamping);
        if (PHYSICS_VALID_NONNEG(rigidBody.angularDamping))
            motionProperties->SetAngularDamping(rigidBody.angularDamping);
        if (motionType == JPH::EMotionType::Dynamic && PHYSICS_VALID_POSITIVE(rigidBody.mass))
        {
            JPH::MassProperties massProperties = body.GetShape()->GetMassProperties();
            massProperties.ScaleToMass(rigidBody.mass);
            motionProperties->SetMassProperties(motionProperties->GetAllowedDOFs(), massProperties);
        }
    }

    JPH::ShapeRefC BuildShape(Components::CollisionShape collisionShape, JPH::EMotionType motionType, uint entityId, float3 worldScale, bool& outRetry)
    {
        outRetry = false;
        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID_POSITIVE(collisionShape.halfExtent[axis]))
                collisionShape.halfExtent[axis] = 0.5f;
        }
        float smallest = min(min(collisionShape.halfExtent[0], collisionShape.halfExtent[1]), collisionShape.halfExtent[2]);
        float convexRadius = collisionShape.convexRadius;
        if (!PHYSICS_VALID_NONNEG(convexRadius))
            convexRadius = 0.0f;
        if (convexRadius > smallest * 0.5f)
            convexRadius = smallest * 0.5f;

        // halfExtent means: 
        // Box = half size per axis. 
        // Sphere = .x radius. 
        // Capsule/Cylinder = .x radius, .y half height of the CYLINDRICAL section (Jolt's convention -- a capsule's total height is 2 * (halfExtent.y + halfExtent.x)).
        JPH::ShapeRefC shape;
        JPH::ShapeSettings::ShapeResult shapeResult;
        switch (collisionShape.shapeType)
        {
        case 0:
            shapeResult = JPH::BoxShapeSettings(JPH::Vec3(collisionShape.halfExtent[0], collisionShape.halfExtent[1], collisionShape.halfExtent[2]), convexRadius).Create();
            break;
        case 1:
            shapeResult = JPH::SphereShapeSettings(collisionShape.halfExtent[0]).Create();
            break;
        case 2:
            shapeResult = JPH::CapsuleShapeSettings(collisionShape.halfExtent[1], collisionShape.halfExtent[0]).Create();
            break;
        case 3:
            shapeResult = JPH::CylinderShapeSettings(collisionShape.halfExtent[1], collisionShape.halfExtent[0], convexRadius > collisionShape.halfExtent[0] ? collisionShape.halfExtent[0] : convexRadius).Create();
            break;
        case 4:
        case 5:
        {
            // Convex hull is the default; a Mesh collider is the special case, and only on a static
            // body -- Jolt cannot simulate a concave triangle mesh as a moving one. Falling back to a
            // hull keeps the entity simulating instead of silently vanishing from the world.
            bool wantsConvexHull = collisionShape.shapeType == 4;
            if (!wantsConvexHull && motionType != JPH::EMotionType::Static)
            {
                IOs::Log("Physics: entity {} uses a Mesh collider on a non-static body, which Jolt does not support -- falling back to a convex hull", entityId);
                wantsConvexHull = true;
            }
            shape = GetOrCreateMeshBasedShape(collisionShape, entityId, wantsConvexHull, convexRadius, outRetry);
            if (shape == nullptr)
                return nullptr;
            break;
        }
        default:
        {
            uint shapeType = collisionShape.shapeType;
            IOs::Log("Physics: entity {} has unknown CollisionShape.shapeType {}, no shape built", entityId, shapeType);
            return nullptr;
        }
        }
        // Read the error only if not a mesh
        if (shape == nullptr)
        {
            if (shapeResult.HasError())
            {
                uint shapeType = collisionShape.shapeType;
                IOs::Log("Physics: entity {} shape type {} failed: {}", entityId, shapeType, shapeResult.GetError().c_str());
                return nullptr;
            }
            shape = shapeResult.Get();
        }

        bool hasOffset = false;
        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID(collisionShape.offset[axis]))
                collisionShape.offset[axis] = 0.0f;
            hasOffset |= collisionShape.offset[axis] != 0.0f;
        }
        if (hasOffset)
        {
            JPH::ShapeSettings::ShapeResult offsetResult = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(collisionShape.offset[0], collisionShape.offset[1], collisionShape.offset[2]), JPH::Quat::sIdentity(), shape).Create();
            if (offsetResult.HasError())
            {
                IOs::Log("Physics: entity {} shape offset failed: {}", entityId, offsetResult.GetError().c_str());
                return nullptr;
            }
            shape = offsetResult.Get();
        }

        bool scaled = false;
        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID(worldScale[axis]) || worldScale[axis] == 0.0f)
                worldScale[axis] = 1.0f;
            scaled |= fabsf(worldScale[axis] - 1.0f) > 1.0e-4f;
        }
        if (scaled)
        {
            // Several shapes reject non-uniform scale outright (a sphere cannot become an ellipsoid,
            // a capsule cannot be squashed on one axis). MakeScaleValid returns the closest scale
            // the shape does accept, so the collider degrades predictably instead of asserting.
            JPH::Vec3 requestedScale(worldScale[0], worldScale[1], worldScale[2]);
            JPH::Vec3 validScale = shape->MakeScaleValid(requestedScale);
            if (validScale != requestedScale)
            {
                IOs::Log("Physics: entity {} world scale ({}, {}, {}) is not supported by its shape type, using ({}, {}, {}) instead",
                    entityId, worldScale[0], worldScale[1], worldScale[2], (float)validScale.GetX(), (float)validScale.GetY(), (float)validScale.GetZ());
            }

            JPH::ShapeSettings::ShapeResult scaledResult = JPH::ScaledShapeSettings(shape, validScale).Create();
            if (scaledResult.HasError())
            {
                IOs::Log("Physics: entity {} shape scaling failed: {}", entityId, scaledResult.GetError().c_str());
                return nullptr;
            }
            shape = scaledResult.Get();
        }

        return shape;
    }

    void CreateBody(World::Entity ent, JPH::BodyInterface& bodyInterface)
    {
        // The get by copy is deliberate: adding a component (PhysicsBody)  invalidates the reference
        Components::RigidBody rigidBody = ent.Get<Components::RigidBody>();
        Components::CollisionShape collisionShape = ent.Get<Components::CollisionShape>();
        float3 position = float3(0, 0, 0);
        quaternion rotation = quaternion::identity();
        float3 scale = float3(1, 1, 1);

        seedAssert(ent.Has<Components::Transform>());
        if (ent.Has<Components::Transform>())
        {
            PhysicsGetWorldPose(ent, position, rotation, scale);
        }
        uint entityId = ent.id;
        uint entityRev = ent.rev;

        JPH::EMotionType motionType = PhysicsMotionType(rigidBody);

        bool retry = false;
        JPH::ShapeRefC shape = BuildShape(collisionShape, motionType, entityId, scale, retry);
        if (shape == nullptr && retry)
            return;

        // Guarded: the component can already be there when a prefab clone copied it, and re-Adding
        // a mask the entity already has would shuffle it within its pool for no reason.
        if (!ent.Has<Components::PhysicsBody>())
            ent.Add(Components::PhysicsBody::mask);
        Components::PhysicsBody& physicBody = ent.Get<Components::PhysicsBody>();
        physicBody.bodyID = JPH::BodyID::cInvalidBodyID;

        if (shape == nullptr)
            return;

        JPH::ObjectLayer objectLayer = motionType == JPH::EMotionType::Static ? PhysicsLayers::nonMoving : PhysicsLayers::moving;

        for (uint axis = 0; axis < 3; axis++)
        {
            if (!PHYSICS_VALID(position[axis]))
                position[axis] = 0.0f;
        }
        // Same garbage-memory concern: an unnormalized quaternion trips a Jolt assert.
        float qx = rotation.f32[0], qy = rotation.f32[1], qz = rotation.f32[2], qw = rotation.f32[3];
        float lengthSq = qx * qx + qy * qy + qz * qz + qw * qw;
        JPH::Quat jphRotation = (PHYSICS_VALID(lengthSq) && lengthSq > 1.0e-6f) ? JPH::Quat(qx, qy, qz, qw).Normalized() : JPH::Quat::sIdentity();
        JPH::RVec3 jphPosition(position[0], position[1], position[2]);

        JPH::BodyCreationSettings settings(shape, jphPosition, jphRotation, motionType, objectLayer);
        settings.mUserData = (JPH::uint64)ent.ToUInt();
        if (PHYSICS_VALID_NONNEG(rigidBody.friction))       settings.mFriction = rigidBody.friction;
        if (PHYSICS_VALID_NONNEG(rigidBody.restitution))    settings.mRestitution = rigidBody.restitution;
        if (PHYSICS_VALID_NONNEG(rigidBody.linearDamping))  settings.mLinearDamping = rigidBody.linearDamping;
        if (PHYSICS_VALID_NONNEG(rigidBody.angularDamping)) settings.mAngularDamping = rigidBody.angularDamping;
        if (PHYSICS_VALID(rigidBody.gravityFactor))         settings.mGravityFactor = rigidBody.gravityFactor;
        settings.mAllowSleeping = rigidBody.allowSleeping != 0;
        settings.mIsSensor = rigidBody.isSensor != 0;
        if (motionType == JPH::EMotionType::Dynamic && PHYSICS_VALID_POSITIVE(rigidBody.mass))
        {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = rigidBody.mass;
        }

        JPH::Body* body = bodyInterface.CreateBody(settings);
        if (body == nullptr)
        {
            IOs::Log("Physics: entity {} body creation failed -- max body count ({}) reached", entityId, (uint)maxBodies);
            return;
        }
        bodyInterface.AddBody(body->GetID(), motionType == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);

        pendingMeshWarned.erase(entityId);
        physicBody.bodyID = body->GetID().GetIndexAndSequenceNumber();
        bodies[entityId] = { body->GetID(), entityRev, syncFrame, ComputeTransformVersion(ent), { scale[0], scale[1], scale[2] } };
    }
};
Physics* Physics::instance = nullptr;

namespace Components
{
    // Declared next to Vehicle above. Everything a running car tells you -- rpm, gear, which wheels
    // are actually touching the ground -- is read LIVE out of Jolt here rather than mirrored into
    // the component: the property window snapshots a component's bytes before drawing it and diffs
    // them after (Editor.h), so a field the simulation rewrites every frame would read as a user
    // edit and re-dirty (and so rebuild) the car forever.
    static void VehiclePropertyDraw(char* p)
    {
        DefaultPropertyDraw(Vehicle::mask, p);

        ImGui::Spacing();
        ImGui::SeparatorText("live");

        // The drawer is handed the component's bytes only, and the property window draws exactly one
        // entity -- the selected one -- so that is where the car is looked up.
        World::Entity ent = editorState.selectedObject;
        if (Physics::instance == nullptr || !ent.IsValid())
            return;

        auto record = Physics::instance->vehicles.find(ent.id);
        if (record == Physics::instance->vehicles.end() || record->second.rev != ent.rev || record->second.constraint == nullptr)
        {
            ImGui::TextDisabled("no car built");
            ImGui::SetItemTooltip("A car needs a Transform, a RigidBody with motionType Dynamic, a CollisionShape,\nand at least one CHILD entity carrying a Wheel component. The log says which one is missing.");
            return;
        }

        JPH::VehicleConstraint* constraint = record->second.constraint.GetPtr();
        JPH::WheeledVehicleController* controller = static_cast<JPH::WheeledVehicleController*>(constraint->GetController());

        ImGui::Text("engine %.0f rpm", controller->GetEngine().GetCurrentRPM());
        ImGui::Text("gear %d, clutch %.2f", controller->GetTransmission().GetCurrentGear(), controller->GetTransmission().GetClutchFriction());
        ImGui::Text("%u driven wheel pair(s)", (uint)controller->GetDifferentials().size());
        ImGui::SetItemTooltip("One differential per axle that has driven wheels. Set Wheel.axle to say which\nwheels share one, and Wheel.driven to say whether the engine reaches them.");

        for (uint i = 0; i < (uint)constraint->GetWheels().size(); i++)
        {
            const JPH::Wheel* wheel = constraint->GetWheel(i);
            ImGui::Text("wheel %u: %s, suspension %.3f m, steer %.1f deg", i,
                wheel->HasContact() ? "on ground" : "in the air",
                wheel->GetSuspensionLength(),
                JPH::RadiansToDegrees(wheel->GetSteerAngle()));
        }

        float speed = 0.0f;
        if (Physics::instance->GetVehicleForwardSpeed(ent, speed))
            ImGui::Text("speed %.1f km/h", speed * 3.6f);
    }
}

namespace Systems
{
    // Arrow keys -> one car's driver input, while playing. Up accelerates, Down brakes to a stop and
    // only then reverses, Left / Right steer.
    //
    // While playing this is the ONLY writer of Vehicle's four input fields, and it writes them every
    // frame, zeros included -- letting go of a key has to mean "off". So the property window's input
    // sliders are effectively read-only for as long as the game runs; stopping hands them back, at
    // zero, so a car does not drive off by itself the next time play starts.
    //
    // One frame of latency by construction, and that is fine at 16 ms: the system tasks succeed
    // DeferredRelease, which succeeds PhysicsSyncReadback (Engine.h), so SyncVehicles has already
    // pushed this frame's component values into Jolt by the time this runs. What is written here is
    // consumed by the NEXT frame's sync and step.
    struct Driver : SystemBase
    {
        static Driver* instance;

        // Which way the driver last committed to going. Starts forward, like Jolt's own sample: it
        // is what makes the first press of Down read as "brake" rather than "slam it into reverse".
        float previousForward = 1.0f;
        bool wasPlaying = false;

        void On() override
        {
            instance = this;
            World::instance->systems.push_back(this);
        }

        void Off() override
        {
            auto item = std::find(World::instance->systems.begin(), World::instance->systems.end(), this);
            if (item != World::instance->systems.end())
                World::instance->systems.erase(item);
            instance = nullptr;
        }

        // The car under the wheel: whatever is selected in the editor when that IS a car (or one of
        // its wheels, which is what you have selected while tuning suspension), so a scene with more
        // than one car is drivable at all. Otherwise simply the first car in the world.
        static World::Entity FindVehicle(World* world)
        {
            World::Entity selected = editorState.selectedObject;
            if (selected.IsValid())
            {
                if (selected.Has<Components::Vehicle>())
                    return selected;
                if (selected.Has<Components::Wheel>() && selected.Has<Components::Parent>())
                {
                    World::Entity chassis = World::Entity(selected.Get<Components::Parent>().entity);
                    if (chassis.IsValid() && chassis.Has<Components::Vehicle>())
                        return chassis;
                }
            }

            uint queryIndex = world->Query(Components::Vehicle::mask, 0, true);
            auto& queryResult = world->frameQueries[queryIndex];
            return queryResult.empty() ? World::Entity() : queryResult[0];
        }

        static void SetInput(World::Entity vehicle, float forward, float right, float brake)
        {
            auto& authored = vehicle.Get<Components::Vehicle>();
            authored.forwardInput = forward;
            authored.rightInput = right;
            authored.brakeInput = brake;
            authored.handBrakeInput = 0.0f;
        }

        // Hand the chase camera (Systems::Player, World.h) the car to sit behind, and the length to
        // size its rig by -- the same rig numbers then frame a 4 m road car and a 400 mm buggy.
        // Cleared whenever there is nothing to drive, which is what gives the free camera back.
        static void PublishChaseTarget(World::Entity vehicle)
        {
            if (!vehicle.IsValid())
            {
                chaseCameraTarget.entity.store(chaseCameraNoTarget, std::memory_order_relaxed);
                return;
            }

            float halfLength = 1.0f;
            if (vehicle.Has<Components::CollisionShape>())
            {
                float authored = (float)vehicle.Get<Components::CollisionShape>().halfExtent[2];
                if (authored > 1.0e-3f)     // also rejects a NaN out of a half-authored shape
                    halfLength = authored;
            }
            // Scale first: a camera that reads the new car with the old scale for one frame is
            // better than one that reads the old car with the new scale and jumps.
            chaseCameraTarget.scale.store(halfLength, std::memory_order_relaxed);
            chaseCameraTarget.entity.store(vehicle.ToUInt(), std::memory_order_relaxed);
        }

        // R: set the car back on its wheels where it stands. Yaw is kept -- you want to carry on the
        // way you were pointing -- pitch and roll go, and it lifts 0.1 m so it does not have to
        // resolve its way out of the ground it was leaning against. Zeroing the velocities is not
        // optional here: see Physics::StopBody.
        static void ResetVehicle(World::Entity vehicle)
        {
            if (!vehicle.Has<Components::Transform>())
                return;

            float3 position;
            quaternion rotation;
            PhysicsGetWorldPose(vehicle, position, rotation);

            float4x4 matrix = float4x4(normalize(rotation));
            float3 flat = float3((float)matrix[2].x, 0.0f, (float)matrix[2].z);
            float flatLength = (float)length(flat);
            // Standing exactly on its nose or its tail: no yaw survives the flattening, so face +Z.
            float3 forward = flatLength > 1.0e-4f ? flat * (1.0f / flatLength) : float3(0, 0, 1);
            float3 right = cross(float3(0, 1, 0), forward);   // left-handed, as everywhere else here

            float4x4 upright = float4x4(
                (float)right.x, (float)right.y, (float)right.z, 0,
                0, 1, 0, 0,
                (float)forward.x, (float)forward.y, (float)forward.z, 0,
                0, 0, 0, 1);

            // Goes through the Transform, so Physics::Sync picks it up as an outside edit next frame
            // and teleports the body -- the one supported way of moving something under simulation.
            PhysicsSetWorldPose(vehicle, position + float3(0, 0.1f, 0), MatrixToQuaternion(float3x3(upright)));

            if (Physics::instance != nullptr)
                Physics::instance->StopBody(vehicle);
        }

        void Update(World* world) override
        {
            ZoneScoped;

            if (!engineState.playing)
            {
                // Once, on the way out -- so a value typed into the property window while stopped
                // stays typed instead of being wiped every frame.
                if (wasPlaying)
                {
                    World::Entity vehicle = FindVehicle(world);
                    if (vehicle.IsValid())
                        SetInput(vehicle, 0.0f, 0.0f, 0.0f);
                    previousForward = 1.0f;
                    wasPlaying = false;
                }
                PublishChaseTarget(World::Entity());
                return;
            }
            wasPlaying = true;

            World::Entity vehicle = FindVehicle(world);
            PublishChaseTarget(vehicle);
            if (!vehicle.IsValid())
                return;

            IOs::Keys& keys = IOs::instance->keys;

            if (keys.pressed[VK_R])
                ResetVehicle(vehicle);

            float forward = 0.0f;
            if (keys.down[VK_UP])    forward += 1.0f;
            if (keys.down[VK_DOWN])  forward -= 1.0f;
            float right = 0.0f;
            if (keys.down[VK_RIGHT]) right -= 1.0f;
            if (keys.down[VK_LEFT])  right += 1.0f;

            // Asking for the opposite direction to the one we committed to is the brake until the
            // car has actually stopped, and only then a gear change -- the same rule as Jolt's
            // vehicle sample. Feeding the gearbox a reverse request at speed snaps the car around.
            float brake = 0.0f;
            if (previousForward * forward < 0.0f)
            {
                float speed = 0.0f;
                bool known = Physics::instance != nullptr && Physics::instance->GetVehicleForwardSpeed(vehicle, speed);
                if (known && ((forward > 0.0f && speed < -0.1f) || (forward < 0.0f && speed > 0.1f)))
                {
                    forward = 0.0f;
                    brake = 1.0f;
                }
                else
                {
                    previousForward = forward;
                }
            }

            SetInput(vehicle, forward, right, brake);
        }
    };
    Driver* Driver::instance = nullptr;
}
