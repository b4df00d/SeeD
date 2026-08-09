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
            JPH::BodyInterface& bodyInterface = physicsSystem.GetBodyInterface();
            for (auto& kv : bodies)
                DestroyBody(bodyInterface, kv.second.bodyID);
        }
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
        DrawDebugShapes();
    }

    void DrawDebugShapes()
    {
#ifdef JPH_DEBUG_RENDERER
        ZoneScopedN("PhysicsDebugShapes");

        if (debugRenderer == nullptr)
            return;
        debugRenderer->lines.clear();

        // With the toggle off, still draw whatever is selected -- that costs nothing when nothing
        // is selected, and it is the fastest way to see that a body failed to be created at all.
        uint selectedEntity = 0;
        bool drawSelectionOnly = !options.drawPhysicsShapes;
        if (drawSelectionOnly)
        {
            World::Entity selected = editorState.selectedObject;
            if (selected.id == entityInvalid.id || !selected.IsValid() || !selected.Has<Components::PhysicsBody>())
                return;
            selectedEntity = selected.ToUInt();
        }

        HLSL::Camera camera;
        if (GetMainViewCamera(camera))
            debugRenderer->SetCameraPos(JPH::RVec3(camera.worldPos.f32[0], camera.worldPos.f32[1], camera.worldPos.f32[2]));

        JPH::BodyManager::DrawSettings settings;
        settings.mDrawShape = true;
        settings.mDrawShapeWireframe = true;   // the debug pipeline draws lines, not solids
        settings.mDrawShapeColor = JPH::BodyManager::EShapeColor::MotionTypeColor;

        selectionDrawFilter.selectedEntity = selectedEntity;
        physicsSystem.DrawBodies(settings, debugRenderer, drawSelectionOnly ? &selectionDrawFilter : nullptr);
#endif
    }

    // Stage 2, a top-level task in Engine::Loop so it overlaps the editor + renderer tasks.
    // TODO : wiat until the DAG rework is done so that can be a system again and scheduled properly ?
    void Step()
    {
        ZoneScopedN("PhysicsStep");

        // TODO : do the same "is playing" test for the readback ?
        if (World::instance == nullptr || !World::instance->playing)
        {
            accumulator = 0.0f;
            return;
        }

        accumulator += Time::instance->deltaSeconds;
        if (accumulator > maxAccumulatedSeconds)
            accumulator = maxAccumulatedSeconds;

        // Just do a step every frame... I like it better to test
        // see if does really simulate unheavenly. If so, maybe subdivide the collisionSteps depending on deltatime?
        physicsSystem.Update(Time::instance->deltaSeconds, collisionSteps, tempAllocator, jobSystem);
        while (accumulator >= fixedDeltaSeconds)
        {
            //physicsSystem.Update(fixedDeltaSeconds, collisionSteps, tempAllocator, jobSystem);
            accumulator -= fixedDeltaSeconds;
        }

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

    void DestroyBody(JPH::BodyInterface& bodyInterface, JPH::BodyID bodyID)
    {
        if (bodyID.IsInvalid())
            return;
        if (bodyInterface.IsAdded(bodyID))
            bodyInterface.RemoveBody(bodyID);
        bodyInterface.DestroyBody(bodyID);
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
