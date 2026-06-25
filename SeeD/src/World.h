#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#define SUBTASKWORLD(system) tf::Task system##Task = subflow.emplace([this, &system](){system->Update(this);}).name(#system)

class World;

struct assetID
{
    static const assetID Invalid;
    UINT64 hash;
    bool operator==(const assetID& other) const
    {
        return hash == other.hash;
    }
};
const assetID assetID::Invalid = { (UINT64)~0 };
namespace std
{
    template<>
    struct hash<assetID>
    {
        inline size_t operator()(const assetID& x) const
        {
            return x.hash;
        }
    };
}

struct EntityBase
{
    uint rev : 4 = 0b1111; // sync bit count with entitySlot rev
    uint id : 28 = 0b1111111111111111111111111111; // sync bit count with invalidEntity

    bool operator==(const EntityBase& other) const
    {
        return id == other.id && rev == other.rev;
    }
    bool IsValid();

    void FromUInt(uint obj)
    {
        *this = *(EntityBase*)&obj;
    }
    uint ToUInt()
    {
        uint res = *(uint*)this;
        return res;
    }
};
static constexpr EntityBase entityInvalid = { 0b1111, 0b1111111111111111111111111111 };

namespace Components
{
    static constexpr uint componentMaxCount = 64;
    typedef std::bitset<componentMaxCount> Mask;

    static uint masksIndex = 0;
    static std::array<String, componentMaxCount> names;
    static std::array<uint, componentMaxCount> strides;
    static std::array<bool, componentMaxCount> transient;

    using CallbackT = void(*)(EntityBase);
    static std::array<CallbackT, componentMaxCount> AddCallback;
    static std::array<CallbackT, componentMaxCount> RemoveCallback;

    template<typename T>
    static uint InitComponentStaticData()
    {
        AddCallback[masksIndex] = nullptr;
        RemoveCallback[masksIndex] = nullptr;
        transient[masksIndex] = T::transient;
        strides[masksIndex] = sizeof(T);
        String name = typeid(T).name();
        names[masksIndex] = name.substr(name.find_last_of("::") + 1);
        std::stringstream ss;
        ss << names[masksIndex] << " " << masksIndex << " " << sizeof(T) << "\n";
        std::string debugInfo(ss.str());
        OutputDebugStringA(debugInfo.c_str());
        masksIndex++;
        return masksIndex - 1;
    }

    static uint MaskToBucket(Mask mask)
    {
        unsigned long index;
        _BitScanForward64(&index, mask.to_ullong());
        return index;
    }

    template<typename T>
    struct ComponentBase
    {
        static uint bucketIndex;
        static Mask mask;
        static uint stride;
        static bool transient;
    };
    template<typename T>
    uint ComponentBase<T>::bucketIndex = InitComponentStaticData<T>();
    template<typename T>
    Mask ComponentBase<T>::mask = (unsigned long long) 1 << ComponentBase<T>::bucketIndex;
    template<typename T>
    uint ComponentBase<T>::stride = sizeof(T);
    template<typename T>
    bool ComponentBase<T>::transient = false;

    template<typename T>
    concept IsComponent = std::is_base_of<Components::ComponentBase<T>, T>::value;

    template<Components::IsComponent T>
    struct Handle : EntityBase
    {
        T& Get();
        T& GetPermanent();
        void Set(const EntityBase& other)
        {
            id = other.id;
            rev = other.rev;
        }
        bool IsValid() { return id != entityInvalid.id; }
    };

    // Be able to get the entity index from the entitySlot
    struct Entity : ComponentBase<Entity>, EntityBase
    {
        Entity(const EntityBase& other)
        {
            id = other.id;
            rev = other.rev;
        }
    };
}

// Reflection metadata types + generator (needs Components::Mask, defined above).
#include "ComponentMetaTypes.h"

#include "UIHelpers.h"

namespace Components
{
    struct Name : ComponentBase<Name>
    {
        #define ECS_NAME_LENGTH 256
        char name[ECS_NAME_LENGTH] = {};
    };

    struct Shader : ComponentBase<Shader>
    {
        assetID id;
    };

    struct Mesh : ComponentBase<Mesh>
    {
        assetID id;
    };

    struct Texture : ComponentBase<Texture>
    {
        assetID id;
    };

    struct Material : ComponentBase<Material>
    {
        Handle<Shader> shader;
        Handle<Texture> textures[HLSL::MaterialTextureCount]; //name:[albedo, roughness, metalness, normal]
        float parameters[HLSL::MaterialParametersCount]; //name:[albedo, roughness, metalness, normal, cutout]
    };
    static void MaterialPropertyDraw(char* mat)
    {
        Material* material = (Material*)mat;

        uint pushID = 0;
        ImGui::PushID(pushID++);
        UIHelpers::DrawHandle(*(EntityBase*)&material->shader, Shader::mask);
        ImGui::PopID();
        ImGui::Spacing();

        const char* names[] = { "albedo", "roughness", "metalness", "normal", "cutout" };
        for (uint i = 0; i < ARRAYSIZE(names); i++)
        {
            ImGui::Text(names[i]);
            ImGui::PushID(pushID++);
            UIHelpers::DrawHandle(*(EntityBase*)&material->textures[i], Texture::mask);
            ImGui::PopID();
            ImGui::SliderFloat(names[i], &material->parameters[i], 0, 1);
            ImGui::Spacing();
        }
    }

    struct Transform : ComponentBase<Transform>
    {
        float3 position;
        quaternion rotation;
        float3 scale;
    };

    struct WorldMatrix : ComponentBase<WorldMatrix>
    {
        float4x4 matrix;
    };

    struct Instance : ComponentBase<Instance>
    {
        Handle<Mesh> mesh;
        Handle<Mesh> meshRT;
        Handle<Material> material;
    };

    struct State : ComponentBase<State>
    {
        enum class Flags : uint
        {
            loaded = 1 << 0,
            dirty = 1 << 1,
            BLAS = 1 << 2
        };
        BitFlags<Flags> flags;
    };
    bool State::transient = true;

    struct Parent : ComponentBase<Parent>
    {
        Handle<Entity> entity;
    };

    struct Light : ComponentBase<Light>
    {
        float4 color;
        float size;
        float range;
        float angle;
        HLSL::LightType type;
        bool castShadow;
    };

    struct Camera : ComponentBase<Camera>
    {
        float fovY;
        float nearClip;
        float farClip;
    };

    // Streaming placeholder. An entity with Prefab + Transform additively loads `file`
    // (a .seed produced by World::SavePrefab) when the camera comes within loadDistance,
    // and unloads it when beyond. Handled by Systems::PrefabStreaming. The component is
    // plain POD so it serializes like any other; runtime load state lives in the system.
    struct Prefab : ComponentBase<Prefab>
    {
        #define ECS_PREFAB_PATH 256
        char file[ECS_PREFAB_PATH] = {};
        float loadDistance = 50.0f;
    };
    static void PrefabPropertyDraw(char* p)
    {
        Prefab* prefab = (Prefab*)p;
        ImGui::InputText("file", prefab->file, ECS_PREFAB_PATH);
        ImGui::DragFloat("loadDistance", &prefab->loadDistance, 0.5f, 0.0f, 100000.0f);
    }
}

// Autogenerated InitKnownComponents() — must come after the component structs (uses offsetof).
#include "ComponentMetaData.h"

class World;

namespace Systems
{
    struct SystemBase
    {
        Components::Mask mask;
        virtual void On() = 0;
        virtual void Off() = 0;
        virtual void Update(World* world) = 0;
    };
}

// Just one world. keep it simple.
static constexpr uint poolMaxSlots = 65535;
static constexpr uint poolInvalid = 0b11111111111;
class World
{
public:
    struct Pool
    {
    public:
        Components::Mask mask;
        uint count;
        std::array<void*, Components::componentMaxCount> data;

        void On()
        {
            count = 0;
            for (uint i = 0; i < data.size(); i++)
            {
                data[i] = nullptr;
                if(mask[i])
                    data[i] = (void*)malloc(poolMaxSlots * Components::strides[i]);
            }
        }

        uint GetSlot()
        {
            return count++;
        }

        inline bool Satisfy(Components::Mask include, Components::Mask exclude)
        {
            return ((mask & include) == include) && (mask & exclude) == 0;
        }
    };

    struct EntitySlot
    {
        uint permanent : 1;
        uint rev : 4; // sync bit count with EntityBase rev
        uint pool : 11; // sync bit count with the assert in GetOrCreatePoolIndex
        uint index : 16;

        template <Components::IsComponent T>
        bool Has()
        {
            auto& poolpool = World::instance->components[pool];
            return (poolpool.mask & T::mask) != 0;
        }
        
        char* Get(uint bucketIndex)
        {
            auto& poolpool = World::instance->components[pool];
            seedAssert(bucketIndex < Components::componentMaxCount);
            seedAssert(poolpool.data[bucketIndex] != nullptr);
            char* data = (char*)poolpool.data[bucketIndex];
            return &data[index * Components::strides[bucketIndex]];
        }

        template <Components::IsComponent T>
        T& Get()
        {
            auto& poolpool = World::instance->components[pool];
            seedAssert((poolpool.mask & T::mask) == T::mask); // the pool must own the whole component mask
            T* data = (T*)Get(T::bucketIndex);
            return *data;
        }
    };

    struct Entity : EntityBase
    {
        Entity()
        {
            id = entityInvalid.id;
            rev = entityInvalid.rev;
        }

        Entity(const EntityBase& other)
        {
            id = other.id;
            rev = other.rev;
        }

        ~Entity()
        {

        }

        bool operator==(const Entity& other) const
        {
            return id == other.id && rev == other.rev;
        }

        bool operator==(const EntityBase& other) const
        {
            return id == other.id && rev == other.rev;
        }

        Entity Make(Components::Mask mask, String name = "", bool permanent = false)
        {
            mask |= Components::Entity::mask;
            mask |= Components::State::mask; // remove this mandatory state ?

            if (!name.empty())
                mask |= Components::Name::mask;

            EntitySlot slot;
            slot.pool = World::instance->GetOrCreatePoolIndex(mask);
            slot.index = World::instance->components[slot.pool].GetSlot();
            slot.permanent = permanent;

            if (World::instance->entityFreeSlots.size())
            {
                id = (uint)World::instance->entityFreeSlots.back();
                World::instance->entityFreeSlots.pop_back(); // pourquoi le popback avec la convertion en uint compile pas !??!?
                slot.rev = World::instance->entitySlots[id].rev++ % 15; //sync witch rev count bits (leave the value 15 for invalid)
                World::instance->entitySlots[id] = slot;
            }
            else
            {
                slot.rev = 0;
                World::instance->entitySlots.push_back(slot);
                id = (uint)World::instance->entitySlots.size() - 1;
            }

            rev = slot.rev;
            Get<Components::Entity>() = *(EntityBase*)this;

            if (!name.empty())
            {
                strcpy(Get<Components::Name>().name, name.c_str());
            }

            auto& state = Get<Components::State>();
            state.flags.Clear();

            return *this;
        }

        Entity Clone(String name = "", bool permanent = false)
        {
            Entity newEntity;
            newEntity.Make(GetMask(), name, permanent);
            // copy all components
            auto& thisSlot = World::instance->entitySlots[id];
            auto& newSlot = World::instance->entitySlots[newEntity.id];
            Copy(thisSlot, newSlot);
            if (!name.empty())
            {
                strcpy(newEntity.Get<Components::Name>().name, name.c_str());
            }
            Get<Components::Entity>() = newEntity;
            return newEntity;
        }

        void Release()
        {
            seedAssert(IsValid());
            World::instance->deferredRelease.push_back(*this);

            auto mask = GetMask();
            for (uint i = 0; i < Components::componentMaxCount; i++)
            {
                if (mask[i])
                {
                    if(Components::RemoveCallback[i])
                    {
                        Components::RemoveCallback[i](*this);
                    }
                }
            }
        }

        Components::Mask GetMask()
        {
            seedAssert(IsValid());
            auto& slot = World::instance->entitySlots[id];
            return World::instance->components[slot.pool].mask;
        }

        template <Components::IsComponent T>
        bool Has()
        {
            seedAssert(IsValid());
            auto& slot = World::instance->entitySlots[id];
            return slot.Has<T>();
        }

        char* Get(uint bucketIndex)
        {
            seedAssert(IsValid());
            auto& slot = World::instance->entitySlots[id];
            return slot.Get(bucketIndex);
        }

        template <Components::IsComponent T>
        T& Get()
        {
            seedAssert(IsValid());
            auto& slot = World::instance->entitySlots[id];
            return slot.Get<T>();
        }

        void Add(Components::Mask mask)
        {
            seedAssert(IsValid());
            auto& thisSlot = World::instance->entitySlots[id];
            EntitySlot newSlot;
            newSlot.pool = World::instance->GetOrCreatePoolIndex(World::instance->components[thisSlot.pool].mask | mask);
            newSlot.index = World::instance->components[newSlot.pool].GetSlot();
            newSlot.rev = thisSlot.rev;
            newSlot.permanent = thisSlot.permanent;

            Copy(thisSlot, newSlot);

            if (World::instance->components[thisSlot.pool].count > 1 && thisSlot.index < World::instance->components[thisSlot.pool].count - 1)
            {
                EntitySlot lastSlot = { 0, 0, thisSlot.pool, World::instance->components[thisSlot.pool].count - 1 };
                Entity entityOfLastSlot = lastSlot.Get<Components::Entity>();
                Copy(lastSlot, thisSlot);
                World::instance->entitySlots[entityOfLastSlot.id] = thisSlot; //crash here !
            }
            World::instance->components[thisSlot.pool].count--;
            //TODO : consider removing the pool ...

            thisSlot = newSlot;
        }

        void Remove(Components::Mask mask)
        {
            seedAssert(IsValid());
            auto& thisSlot = World::instance->entitySlots[id];
            EntitySlot newSlot;
            newSlot.pool = World::instance->GetOrCreatePoolIndex(World::instance->components[thisSlot.pool].mask & ~mask);
            newSlot.index = World::instance->components[newSlot.pool].GetSlot();

            Copy(thisSlot, newSlot);

            if (World::instance->components[thisSlot.pool].count > 1 && thisSlot.index < World::instance->components[thisSlot.pool].count - 1)
            {
                EntitySlot lastSlot = { 0, 0, thisSlot.pool, World::instance->components[thisSlot.pool].count - 1 };
                Entity entityOfLastSlot = lastSlot.Get<Components::Entity>();
                Copy(lastSlot, thisSlot);
                World::instance->entitySlots[entityOfLastSlot.id] = thisSlot; //crash here !
            }
            World::instance->components[thisSlot.pool].count--;
            //TODO : consider removing the pool ...

            thisSlot = newSlot;
        }

        template <Components::IsComponent T>
        void Set(T value)
        {
            seedAssert(IsValid());
            auto& thisSlot = World::instance->entitySlots[id];
            if ((World::instance->components[thisSlot.pool].mask & T::mask) == 0)
            {
                IOs::Log("Auto add of compoenent via .Set<T>()");
                Add(T::mask);
            }
            auto& res = *(T*)Get(T::bucketIndex);
            res = value;
        }

        static void Copy(EntitySlot& from, EntitySlot& to)
        {
            Pool poolFrom = World::instance->components[from.pool];
            Pool poolTo = World::instance->components[to.pool];
            for (uint i = 0; i < poolFrom.data.size(); i++)
            {
                if (poolFrom.data[i] != nullptr && poolTo.data[i] != nullptr)
                {
                    if(Components::transient[i] == false)
                        memcpy(to.Get(i), from.Get(i), Components::strides[i]);
                    else
                        memset(to.Get(i), 0, Components::strides[i]);
                }
            }
        }

        // if found set the calling entity to the found entity
        bool Find(String name)
        {
            for (uint i = 0; i < World::instance->components.size(); i++)
            {
                Pool& pool = World::instance->components[i];
                if (pool.Satisfy(Components::Name::mask, 0))
                {
                    for (uint j = 0; j < pool.count; j++)
                    {
                        EntitySlot slot;
                        slot.pool = i;
                        slot.index = j;
                        if (name == slot.Get<Components::Name>().name)
                        {
                            *this = slot.Get<Components::Entity>();
                            return true;
                        }
                    }
                }
            }
            return false;
        }
    };

    static World* instance;

    std::vector<EntitySlot> entitySlots;
    std::vector<uint> entityFreeSlots;
    std::vector<Pool> components;

    std::vector<Entity> deferredRelease;

    std::array<std::vector<Entity>, 128> frameQueries;
    std::atomic<uint> frameQueriesIndex;

    std::vector<Systems::SystemBase*> systems;

    bool playing;
    // Set by structural changes (currently prefab streaming load/unload), read by the editor
    // hierarchy window to refresh. Reset to false at frame start, before systems run.
    bool structureChanged = false;

    void On()
    {
        instance = this;
        playing = false;
        structureChanged = false;
        entitySlots.reserve(1024 * 1024);
        entityFreeSlots.reserve(entitySlots.size());
    }

    void Off()
    {
        instance = nullptr;
    }

    // Hard reset: free all pools and entity bookkeeping immediately.
    // Use before Load() when a full replace (rather than additive merge) is wanted.
    void ClearImmediate()
    {
        for (auto& pool : components)
            for (uint i = 0; i < Components::componentMaxCount; i++)
                if (pool.data[i] != nullptr) { free(pool.data[i]); pool.data[i] = nullptr; }
        components.clear();
        entitySlots.clear();
        entityFreeSlots.clear();
        deferredRelease.clear();
        frameQueriesIndex = 0;
        for (auto& fq : frameQueries) fq.clear();
    }

    // Load is ALWAYS additive: it creates fresh entities for everything in the file and
    // remaps handles, so the same file can be loaded as a prefab into a populated world,
    // and multiple files compose. For a full replace, call ClearImmediate() first.
    // Format is self-describing (schema by component name), so adding/removing components
    // or fields stays loadable; a v4 file also carries a schema GUID and when it matches this
    // build Load takes a fastpath that skips all schema/migration work. Every entity in the file
    // is created fresh (no asset dedup) --
    // a duplicate Shader/Mesh/Texture entity is harmless because the AssetLibrary keys the
    // actual GPU asset by assetID and loads it once regardless of how many entities reference it.
    struct LoadResult
    {
        std::vector<EntityBase> created;  // every entity freshly created by this load
        EntityBase root = entityInvalid;  // the prefab root, resolved to its new entity (SavePrefab files only)
    };

    LoadResult Load(String name)
    {
        //TODO : try to have the fewer allocations possible

        ZoneScoped;
        InitKnownComponents();
        VerifyKnownComponents();

        LoadResult result;

        std::ifstream fin((std::string)name, std::ios::binary);
        if (!fin.is_open()) { IOs::Log("Fail to open {}", name.c_str()); return result; }

        char magic[4] = {};
        fin.read(magic, sizeof(magic));
        uint32_t version = 0;
        fin.read((char*)&version, sizeof(version));
        if (std::string(magic, 4) != "SEED" || version < 2 || version > 4)
        {
            IOs::Log("Unsupported world file {} (version {})", name.c_str(), version);
            return result;
        }

        // v3+ stores an explicit prefab-root id right after the version (SavePrefab writes the
        // real root; whole-world Save writes invalid). v2 files have none -> root stays invalid.
        uint32_t rootRaw = 0; bool haveRoot = false;
        if (version >= 3) { fin.read((char*)&rootRaw, sizeof(rootRaw)); haveRoot = true; }

        // v4+ stores a schema GUID (fingerprint of knownComponents). If it matches this build the
        // file layout is byte-identical to ours -> skip schema verification and migration-plan
        // building (fastpath). Older files / a changed schema fall through to by-name migration.
        uint64_t fileGUID = 0; bool haveGUID = false;
        if (version >= 4) { fin.read((char*)&fileGUID, sizeof(fileGUID)); haveGUID = true; }
        bool schemaMatches = haveGUID && (fileGUID == SchemaGUID());
        if (haveGUID && !schemaMatches)
            IOs::Log("Schema GUID mismatch for {} -- saved by a different build, migrating by field name", name.c_str());

        auto readU32 = [&]() { uint32_t v = 0; fin.read((char*)&v, sizeof(v)); return v; };
        auto readStr = [&]() { uint32_t n = readU32(); std::string s; s.resize(n); if (n) fin.read(s.data(), n); return s; };

        // ---- file schema ----
        struct FField { std::string name; uint type; uint count; uint offset; uint size; };
        struct FComp { std::string name; uint stride; std::vector<FField> fields; };
        uint32_t schemaCount = readU32();
        std::vector<FComp> schema((size_t)schemaCount);
        for (auto& c : schema)
        {
            c.name = readStr();
            c.stride = readU32();
            uint32_t fc = readU32();
            c.fields.resize(fc);
            for (auto& f : c.fields) { f.name = readStr(); f.type = readU32(); f.count = readU32(); f.offset = readU32(); f.size = 0; }

            // Field sizes are only needed by the by-name migration path (the fastpath copies whole
            // strides). SaveEntities writes fields in declaration / ascending-offset order, so
            // derive each size from the next field's offset (last = stride - offset). The debug
            // assert traps a malformed/foreign unsorted file.
            if (!schemaMatches)
                for (size_t i = 0; i < c.fields.size(); i++)
                {
                    seedAssert(i == 0 || c.fields[i].offset > c.fields[i - 1].offset);
                    uint end = (i + 1 < c.fields.size()) ? c.fields[i + 1].offset : c.stride;
                    c.fields[i].size = end - c.fields[i].offset;
                }
        }

        // ---- per-file-component migration plan against the current build ----
        struct FieldCopy { uint srcOffset, dstOffset, size; };
        struct HandleField { uint dstOffset, count; };
        struct Plan
        {
            int kcIndex = -1;       // current knownComponents index, -1 if component removed in this build
            uint bucket = 0;
            uint curStride = 0;
            bool bulkCopy = false;  // file layout == current layout -> single memcpy
            bool isEntity = false;  // self-id component, never copied (set by Make)
            std::vector<FieldCopy> copies;     // matched fields
            std::vector<HandleField> handles;  // ALL current handle fields (for remap + invalidation)
        };
        std::vector<Plan> plans((size_t)schemaCount);
        for (uint si = 0; si < schemaCount; si++)
        {
            FComp& sc = schema[si];
            Plan& pl = plans[si];
            pl.isEntity = (sc.name == "Entity");
            for (uint k = 0; k < knownComponents.size(); k++)
                if (knownComponents[k].name == sc.name) { pl.kcIndex = (int)k; break; }
            if (pl.kcIndex < 0) continue;       // component removed in this build -> skipped on load

            pl.bucket = Components::MaskToBucket(knownComponents[pl.kcIndex].mask);
            pl.curStride = Components::strides[pl.bucket];

            // Handle fields come from the CURRENT schema on both paths (needed for handle remap).
            for (auto& m : knownComponents[pl.kcIndex].members)
                if (m.dataType == PropertyTypes::_Handle)
                    pl.handles.push_back({ (uint)m.offset, (uint)m.dataCount });

            // Fastpath: GUID matched, so this component's file layout == current layout. No field
            // matching, just copy the whole stride.
            if (schemaMatches) { pl.bulkCopy = true; continue; }

            // ---- by-name migration: match each current field to a file field, derive copies ----
            struct CF { std::string name; uint offset; uint size; };
            std::vector<CF> cf;
            for (auto& m : knownComponents[pl.kcIndex].members) cf.push_back({ (std::string)m.name, (uint)m.offset, 0u });
            // current member sizes: declaration order is ascending offset, derive from the next.
            for (size_t i = 0; i < cf.size(); i++)
            {
                seedAssert(i == 0 || cf[i].offset > cf[i - 1].offset);
                uint end = (i + 1 < cf.size()) ? cf[i + 1].offset : pl.curStride;
                cf[i].size = end - cf[i].offset;
            }

            bool identical = (sc.stride == pl.curStride) && (sc.fields.size() == cf.size());
            for (auto& c : cf)
            {
                FField* sf = nullptr;
                for (auto& f : sc.fields) if (f.name == c.name) { sf = &f; break; }
                if (sf)
                {
                    uint sz = std::min(sf->size, c.size);
                    pl.copies.push_back({ sf->offset, c.offset, sz });
                    if (sf->offset != c.offset || sf->size != c.size) identical = false;
                }
                else identical = false; // a current field is absent from the file
            }
            pl.bulkCopy = identical;
        }

        // ---- read pools, create entities, build savedId -> new entity map ----
        std::unordered_map<uint, EntityBase> idMap;
        struct Created { EntityBase entity; uint archetypeRef; };
        std::vector<Created> created;
        std::vector<std::vector<int>> archetypes; // per file-pool: list of schema indices

        uint32_t poolCount = readU32();
        for (uint p = 0; p < poolCount; p++)
        {
            uint32_t compCount = readU32();
            std::vector<int> archetype((size_t)compCount);
            for (auto& a : archetype) a = (int)readU32();
            uint32_t entityCount = readU32();

            std::vector<std::vector<char>> blobs((size_t)compCount);
            for (uint c = 0; c < compCount; c++)
            {
                FComp& sc = schema[archetype[c]];
                blobs[c].resize((size_t)entityCount * sc.stride);
                if (!blobs[c].empty()) fin.read(blobs[c].data(), blobs[c].size());
            }

            Components::Mask mask = 0;
            int entityComp = -1;
            for (uint c = 0; c < compCount; c++)
            {
                Plan& pl = plans[archetype[c]];
                if (pl.isEntity) entityComp = (int)c;
                if (pl.kcIndex >= 0) mask |= knownComponents[pl.kcIndex].mask;
            }

            uint archetypeRef = (uint)archetypes.size();
            archetypes.push_back(archetype);

            for (uint s = 0; s < entityCount; s++)
            {
                EntityBase saved = entityInvalid;
                if (entityComp >= 0)
                    saved = *(EntityBase*)(blobs[entityComp].data() + (size_t)s * schema[archetype[entityComp]].stride);

                Entity e; e.Make(mask);
                EntityBase mapped = e;

                for (uint c = 0; c < compCount; c++)
                {
                    Plan& pl = plans[archetype[c]];
                    if (pl.kcIndex < 0 || pl.isEntity) continue;        // removed comp / self-id
                    if (Components::transient[pl.bucket]) continue;
                    char* dst = e.Get(pl.bucket);
                    char* src = blobs[c].data() + (size_t)s * schema[archetype[c]].stride;
                    if (pl.bulkCopy)
                    {
                        memcpy(dst, src, pl.curStride);
                    }
                    else
                    {
                        // migrated layout: zero, set handles invalid, then copy matched fields
                        memset(dst, 0, pl.curStride);
                        for (auto& hf : pl.handles)
                            for (uint k = 0; k < hf.count; k++)
                                *(EntityBase*)(dst + hf.dstOffset + (size_t)k * sizeof(EntityBase)) = entityInvalid;
                        for (auto& fc : pl.copies)
                            memcpy(dst + fc.dstOffset, src + fc.srcOffset, fc.size);
                    }
                }

                created.push_back({ mapped, archetypeRef });

                if (saved.id != entityInvalid.id)
                    idMap[saved.id] = mapped;
            }
        }

        fin.close();

        // ---- remap handles in freshly created entities (forward refs now resolvable) ----
        for (auto& cr : created)
        {
            Entity e = cr.entity;
            std::vector<int>& archetype = archetypes[cr.archetypeRef];
            for (uint c = 0; c < archetype.size(); c++)
            {
                Plan& pl = plans[archetype[c]];
                if (pl.kcIndex < 0 || pl.handles.empty()) continue;
                char* data = e.Get(pl.bucket);
                for (auto& hf : pl.handles)
                {
                    for (uint k = 0; k < hf.count; k++)
                    {
                        EntityBase* h = (EntityBase*)(data + hf.dstOffset + (size_t)k * sizeof(EntityBase));
                        if (h->id == entityInvalid.id) continue;
                        auto it = idMap.find(h->id);
                        if (it != idMap.end()) *h = it->second;
                        else { h->id = entityInvalid.id; h->rev = entityInvalid.rev; }
                    }
                }
            }
        }

        result.created.reserve(created.size());
        for (auto& cr : created) result.created.push_back(cr.entity);

        if (haveRoot)
        {
            EntityBase rb; rb.FromUInt(rootRaw);
            if (rb.id != entityInvalid.id)
            {
                auto it = idMap.find(rb.id);
                if (it != idMap.end()) result.root = it->second;
            }
        }

        IOs::Log("World loaded from {} [{}]", name.c_str(), schemaMatches ? "schema fastpath" : "migration");
        return result;
    }

    // Near-raw bulk save of an ARBITRARY set of entities. Self-describing: writes a schema
    // table (all components + fields by name), then groups the requested entities by their
    // source pool and writes only those entities' component rows. Transient components are
    // skipped. No handle rewriting here; all remapping happens additively on Load -- a handle
    // pointing outside the saved set simply resolves to invalid on load.
    void SaveEntities(String name, const std::vector<EntityBase>& entities, EntityBase root = entityInvalid)
    {
        ZoneScoped;
        InitKnownComponents();
        VerifyKnownComponents();

        std::ofstream fout((std::string)name, std::ios::binary);
        if (!fout.is_open()) { IOs::Log("Fail to open {}", name.c_str()); return; }

        auto writeU32 = [&](uint32_t v) { fout.write((char*)&v, sizeof(v)); };
        auto writeStr = [&](const std::string& s) { writeU32((uint32_t)s.size()); if (!s.empty()) fout.write(s.data(), s.size()); };

        const char magic[4] = { 'S','E','E','D' };
        fout.write(magic, sizeof(magic));
        writeU32(4);
        writeU32(root.ToUInt()); // explicit prefab root (invalid for whole-world saves)
        uint64_t guid = SchemaGUID(); // schema fingerprint -> Load fastpath when it matches the loading build
        fout.write((char*)&guid, sizeof(guid));

        // ---- schema table = all knownComponents ----
        writeU32((uint32_t)knownComponents.size());
        for (auto& kc : knownComponents)
        {
            uint bucket = Components::MaskToBucket(kc.mask);
            writeStr((std::string)kc.name);
            writeU32(Components::strides[bucket]);
            writeU32((uint32_t)kc.members.size());
            for (auto& m : kc.members)
            {
                writeStr((std::string)m.name);
                writeU32((uint32_t)m.dataType);
                writeU32((uint32_t)m.dataCount);
                writeU32((uint32_t)m.offset);
            }
        }

        // bucket -> schema (knownComponents) index
        int bucketToKC[Components::componentMaxCount];
        for (uint i = 0; i < Components::componentMaxCount; i++) bucketToKC[i] = -1;
        for (uint k = 0; k < knownComponents.size(); k++) bucketToKC[Components::MaskToBucket(knownComponents[k].mask)] = (int)k;

        // ---- group requested entities by their pool (order preserved within a pool) ----
        std::vector<uint> poolOrder;                        // pools that contain >=1 requested entity
        std::unordered_map<uint, std::vector<uint>> byPool; // pool -> entity slot indices
        for (auto& eb : entities)
        {
            Entity e = eb;
            if (!e.IsValid()) continue;
            EntitySlot& slot = entitySlots[eb.id];
            if (byPool.find(slot.pool) == byPool.end()) poolOrder.push_back(slot.pool);
            byPool[slot.pool].push_back(slot.index);
        }

        writeU32((uint32_t)poolOrder.size());
        for (uint p : poolOrder)
        {
            Pool& pool = components[p];
            std::vector<uint>& indices = byPool[p];
            std::vector<int> comps; // non-transient present buckets, as schema indices
            for (uint i = 0; i < Components::componentMaxCount; i++)
                if (pool.mask[i] && !Components::transient[i] && bucketToKC[i] >= 0) comps.push_back(bucketToKC[i]);

            writeU32((uint32_t)comps.size());
            for (int k : comps) writeU32((uint32_t)k);
            writeU32((uint32_t)indices.size());
            for (int k : comps)
            {
                uint bucket = Components::MaskToBucket(knownComponents[k].mask);
                uint stride = Components::strides[bucket];
                char* base = (char*)pool.data[bucket];
                for (uint idx : indices) fout.write(base + (size_t)idx * stride, stride);
            }
        }

        fout.close();
        IOs::Log("Saved {} entities to {}", (uint)entities.size(), name.c_str());
    }

    // Whole-world save: every live entity.
    void Save(String name)
    {
        ZoneScoped;
        InitKnownComponents();
        uint entityBucket = Components::MaskToBucket(Components::Entity::mask);
        std::vector<EntityBase> all;
        for (uint p = 0; p < components.size(); p++)
        {
            Pool& pool = components[p];
            if (!pool.mask[entityBucket]) continue;
            for (uint j = 0; j < pool.count; j++)
            {
                EntitySlot slot{ 0, 0, p, j };
                all.push_back(slot.Get<Components::Entity>());
            }
        }
        SaveEntities(name, all);
    }

    // Transitive dependency closure of one entity: every Handle field (mesh / material /
    // shader / textures ...) followed forward, plus children found by reverse Parent scan.
    // The Parent link is never followed UPWARD, so the root's own parent (and the rest of the
    // scene) stays out. Asset entities are pulled in by their assetID handle but carry only
    // that id (the heavy blob lives in AssetLibrary), so the result is small and shareable.
    std::vector<EntityBase> CollectClosure(Entity root)
    {
        InitKnownComponents();

        int bucketToKC[Components::componentMaxCount];
        for (uint i = 0; i < Components::componentMaxCount; i++) bucketToKC[i] = -1;
        for (uint k = 0; k < knownComponents.size(); k++) bucketToKC[Components::MaskToBucket(knownComponents[k].mask)] = (int)k;

        uint parentBucket = Components::MaskToBucket(Components::Parent::mask);

        std::unordered_set<uint> visited;
        std::vector<EntityBase> order;
        std::vector<EntityBase> stack;
        stack.push_back(root);

        while (!stack.empty())
        {
            Entity e = stack.back(); stack.pop_back();
            if (!e.IsValid() || visited.count(e.id)) continue;
            visited.insert(e.id);
            order.push_back(e);

            Components::Mask mask = e.GetMask();
            // forward deps: follow every Handle field EXCEPT Parent (Parent points UP)
            for (uint b = 0; b < Components::componentMaxCount; b++)
            {
                if (!mask[b] || bucketToKC[b] < 0 || b == parentBucket) continue;
                char* data = e.Get(b);
                for (auto& m : knownComponents[bucketToKC[b]].members)
                {
                    if (m.dataType != PropertyTypes::_Handle) continue;
                    for (int k = 0; k < m.dataCount; k++)
                    {
                        EntityBase h = *(EntityBase*)(data + m.offset + (size_t)k * sizeof(EntityBase));
                        if (h.id != entityInvalid.id) stack.push_back(h);
                    }
                }
            }

            // children: reverse Parent scan (walk DOWN the hierarchy)
            for (uint pi = 0; pi < components.size(); pi++)
            {
                Pool& pool = components[pi];
                if (!pool.mask[parentBucket]) continue;
                for (uint j = 0; j < pool.count; j++)
                {
                    EntitySlot slot{ 0, 0, pi, j };
                    if (slot.Get<Components::Parent>().entity.id == e.id)
                        stack.push_back(slot.Get<Components::Entity>());
                }
            }
        }
        return order;
    }

    // Save one entity (and its whole dependency closure) as a standalone prefab .seed.
    void SavePrefab(Entity root, String name)
    {
        SaveEntities(name, CollectClosure(root), root);
    }

    //TODO : add a validation and conversion function to convert old version of the world to the new one (for example if a component is removed or added)
    //and add a UI function to start the conversion

    void Clear()
    {
        std::vector<Systems::SystemBase*> systemsStopped;
        systemsStopped.reserve(systems.size());
        for (uint i = 0; i < systems.size(); i++)
        {
            auto sys = systems[i];
            sys->Off();
            systemsStopped.push_back(sys);
        }
        for (uint i = 0; i < entitySlots.size(); i++)
        {
            if (!entitySlots[i].permanent && entitySlots[i].pool != poolInvalid)
            {
                Entity ent;
                ent.rev = entitySlots[i].rev;
                ent.id = i;
                ent.Release();
            }
        }
        for (uint i = 0; i < systemsStopped.size(); i++)
        {
            auto sys = systemsStopped[i];
            sys->On();
        }
    }

    void Schedule(tf::Subflow& subflow)
    {
        ZoneScoped;
        frameQueriesIndex = 0;
        //ECS systems
        for (uint i = 0; i < systems.size(); i++)
        {
            auto sys = systems[i];
            //SUBTASKWORLD(sys);
            tf::Task t = subflow.emplace([this, i]() {this->systems[i]->Update(this); }).name("#system");
        }
    }

    // not thread safe when need to create
    uint GetOrCreatePoolIndex(Components::Mask mask)
    {
        mask |= Components::Entity::mask;

        for (uint i = 0; i < components.size(); i++)
        {
            auto& pool = components[i];
            if (pool.mask == mask && pool.count < poolMaxSlots)
                return i;
        }
        seedAssert(components.size() < (1 << 11)); //sync with entitySlot.pool bit count
        Pool newPool;
        newPool.mask = mask;
        newPool.On();
        components.push_back(newPool);
        return (uint)components.size() - 1;
    }

    Pool& GetOrCreatePool(Components::Mask mask)
    {
        return components[GetOrCreatePoolIndex(mask)];
    }

    void DeferredRelease()
    {
        ZoneScoped;
        for (uint i = 0; i < deferredRelease.size(); i++)
        {
            Entity ent = deferredRelease[i];

            EntitySlot& thisSlot = entitySlots[ent.id];

            if (components[thisSlot.pool].count > 1 && thisSlot.index < components[thisSlot.pool].count - 1)
            {
                ZoneScoped;
                EntitySlot tmpSlot = thisSlot; // only interested in pool and index, permanent and rev are not relevent right now
                tmpSlot.index = components[thisSlot.pool].count - 1;
                Entity entityOfLastSlot = tmpSlot.Get<Components::Entity>();
                EntitySlot& lastSlot = entitySlots[entityOfLastSlot.id]; // now the permanent and rev are correct
                Entity::Copy(lastSlot, thisSlot);
                thisSlot.permanent = lastSlot.permanent;
                thisSlot.rev = lastSlot.rev;
                seedAssert(entityOfLastSlot.rev == thisSlot.rev);
                entitySlots[entityOfLastSlot.id] = thisSlot;
            }
            components[thisSlot.pool].count--;
            //TODO : consider removing the pool ...

            //thisSlot.index = poolInvalid;
            thisSlot.pool = poolInvalid;
            entityFreeSlots.push_back(ent.id);
        }
        deferredRelease.clear();
    }

    uint Query(Components::Mask include, Components::Mask exclude)
    {
        ZoneScoped;
        uint queryIndex = frameQueriesIndex++;
        std::vector<Entity>& queryResult = frameQueries[queryIndex];
        queryResult.clear();
        //queryResult.reserve(components.size() * poolMaxSlots);
        queryResult.reserve(poolMaxSlots * 2);
        for (uint i = 0; i < components.size(); i++)
        {
            Pool& pool = components[i];
            if (pool.Satisfy(include, exclude))
            {
                for (uint j = 0; j < pool.count; j++)
                {
                    World::EntitySlot slot{ 0, 0, i, j };
                    queryResult.push_back(slot.Get<Components::Entity>());
                }
            }
        }
        return queryIndex;
    }

    uint CountQuery(Components::Mask include, Components::Mask exclude)
    {
        ZoneScoped;
        uint countQuery = 0;
        for (uint i = 0; i < components.size(); i++)
        {
            Pool& pool = components[i];
            if (pool.Satisfy(include, exclude))
            {
                countQuery += pool.count;
            }
        }
        return countQuery;
    }
};
World* World::instance;
namespace std 
{
    template<>
    struct hash<World::Entity> 
    {
        inline size_t operator()(const World::Entity& x) const 
        {
            return x.id;
        }
    };
}


bool EntityBase::IsValid()
{
    return id != entityInvalid.id && rev == World::instance->entitySlots[id].rev && World::instance->entitySlots[id].pool != poolInvalid;
}

namespace Components
{
    template<Components::IsComponent T>
    T& Handle<T>::Get()
    {
        World::Entity entity = *this;
        if (!entity.IsValid())
        {
            IOs::Log("Should not be adding something if in multitrhead");
            entity.Make(T::mask, "", false);
            id = entity.id;
            rev = entity.rev;
        }
        return entity.Get<T>();
    }
    template<Components::IsComponent T>
    T& Handle<T>::GetPermanent()
    {
        World::Entity entity = *this;
        if (!entity.IsValid())
        {
            IOs::Log("Should not be adding something if in multitrhead");
            entity.Make(T::mask, "", true);
            id = entity.id;
            rev = entity.rev;
        }
        return entity.Get<T>();
    }
}

float4x4 ComputeLocalMatrix(World::Entity ent)
{
    if (ent.Has<Components::Transform>())
    {
        auto& trans = ent.Get<Components::Transform>();

        float4x4 rotationMat;
        float4x4 translationMat;
        float4x4 scaleMat;

        rotationMat = float4x4(trans.rotation);
        translationMat = float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, trans.position.x, trans.position.y, trans.position.z, 1);
        scaleMat = float4x4(trans.scale.x, 0, 0, 0, 0, trans.scale.y, 0, 0, 0, 0, trans.scale.z, 0, 0, 0, 0, 1);

        float4x4 matrix = mul(scaleMat, mul(rotationMat, translationMat));
        return matrix;
    }
    return float4x4::identity();
}

float4x4 ComputeWorldMatrix(World::Entity ent)
{
    float4x4 matrix = ComputeLocalMatrix(ent);

    if (ent.Has<Components::Parent>())
    {
        auto& parentCmp = ent.Get<Components::Parent>();
        auto parentEnt = World::Entity(parentCmp.entity.Get());
        if (parentEnt != entityInvalid)
        {
            float4x4 parentMatrix = ComputeWorldMatrix(parentEnt);
            matrix = mul(matrix, parentMatrix);
        }
    }

    return matrix;
}

uint Rand(uint max)
{
    return uint((float(std::rand()) / float(RAND_MAX)) * max * 0.99999f);
}

float Rand01()
{
    return float(std::rand()) / float(RAND_MAX);
}

namespace Systems
{
    struct Player : SystemBase
    {
        World::Entity camera;
        float sensibility = 1.0f;
        float sensibilityLook = 0.003f;

        void On() override
        {
            auto& cam = camera.Make(Components::Transform::mask | Components::WorldMatrix::mask | Components::Camera::mask).Get<Components::Camera>();
            cam.fovY = 90;
            cam.nearClip = 0.02;
            cam.farClip = 8000.0f;
            auto& trans = camera.Get<Components::Transform>();
            trans.position = float3(0, 1, -2);
            trans.rotation = quaternion::identity();
            trans.scale = 1;

            World::instance->systems.push_back(this);
        }

        void Off() override
        {
            auto item = std::find(World::instance->systems.begin(), World::instance->systems.end(), this);
            if(item != World::instance->systems.end())
                World::instance->systems.erase(item);
        }

        void Update(World* world) override
        {
            ZoneScoped;

            auto& cam = camera.Get<Components::Camera>();
            auto& trans = camera.Get<Components::Transform>();
            Components::WorldMatrix mat;
            mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

            sensibility += IOs::instance->mouse.mouseWheel * .001f * sensibility;
            sensibility = clamp(float1(sensibility), 0.0025f, 1000.0f);

            float sensDt = sensibility * Time::instance->deltaSeconds;
            float sensLookDt = sensibilityLook;
            if (IOs::instance->keys.down[VK_W] || IOs::instance->keys.down[VK_UP])
            {
                trans.position = trans.position + (float3(mat.matrix[2].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_S] || IOs::instance->keys.down[VK_DOWN])
            {
                trans.position = trans.position - (float3(mat.matrix[2].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_D] || IOs::instance->keys.down[VK_RIGHT])
            {
                trans.position = trans.position + (float3(mat.matrix[0].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_A] || IOs::instance->keys.down[VK_LEFT])
            {
                trans.position = trans.position - (float3(mat.matrix[0].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_SPACE])
            {
                trans.position = trans.position + (float3(mat.matrix[1].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_SHIFT])
            {
                trans.position = trans.position - (float3(mat.matrix[1].xyz) * sensDt);
            }
            mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

            float3 forward = float3(0, 0, 1);
            if (IOs::instance->mouse.mouseButtonLeft)
            {
                trans.rotation = mul(quaternion::rotation_axis(float3(mat.matrix[0].xyz), (float)IOs::instance->mouse.mouseDelta.y * sensLookDt), trans.rotation);
                mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);
                trans.rotation = mul(quaternion::rotation_axis(float3(mat.matrix[1].xyz), (float)IOs::instance->mouse.mouseDelta.x * sensLookDt), trans.rotation);
                mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);
            }

            //reset the up for the cam
            float3 right = mat.matrix[0].xyz;
            right.y = 0;
            right = normalize(right);
            float3 up = cross(mat.matrix[2].xyz, right);
            mat.matrix[0].xyz = right;
            mat.matrix[1].xyz = up;
            trans.rotation = MatrixToQuaternion(float3x3(mat.matrix));
            mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

        }
    };

    // Distance-based prefab streaming. NOT registered in World::systems (those run in parallel):
    // it mutates world structure via Load / Release, so it must run single-threaded -- call
    // Update() once per frame from the main loop, after DeferredRelease(). Only runs while
    // playing, so placeholders stay empty in edit mode and authored saves don't capture
    // streamed-in entities.
    struct PrefabStreaming : SystemBase
    {
        struct Instance { std::vector<EntityBase> created; bool loaded = false; };
        std::unordered_map<uint, Instance> instances; // key = placeholder entity id
        float3 cameraPos = float3(0);

        void On() override {}
        void Off() override { UnloadAll(); }

        bool FindCamera(World* world)
        {
            uint camBucket = Components::MaskToBucket(Components::Camera::mask);
            uint trBucket = Components::MaskToBucket(Components::Transform::mask);
            for (uint pi = 0; pi < world->components.size(); pi++)
            {
                World::Pool& pool = world->components[pi];
                if (pool.count == 0 || !pool.mask[camBucket] || !pool.mask[trBucket]) continue;
                World::EntitySlot slot{ 0, 0, pi, 0 };
                cameraPos = slot.Get<Components::Transform>().position;
                return true;
            }
            return false;
        }

        void Unload(Instance& inst)
        {
            // Release everything this instance loaded, asset entities included: Load no longer
            // dedups, so each instance owns its own Shader/Mesh/Texture entities and nothing else
            // points at them. Releasing them does not unload the underlying GPU asset -- the
            // AssetLibrary keeps that alive by assetID for as long as any entity references it.
            for (auto& eb : inst.created)
            {
                World::Entity e = eb;
                if (!e.IsValid()) continue;
                e.Release();
            }
            inst.created.clear();
            inst.loaded = false;
            World::instance->structureChanged = true;
        }

        void UnloadAll()
        {
            for (auto& kv : instances)
                if (kv.second.loaded) Unload(kv.second);
        }

        // Re-parent child under parent (its transform becomes relative to parent). Adds a Parent
        // component if missing. No-op for entities without a Transform (nothing to position).
        static void ParentUnder(World::Entity child, World::Entity parent)
        {
            if (!child.IsValid() || !child.Has<Components::Transform>()) return;
            if (!child.Has<Components::Parent>()) child.Add(Components::Parent::mask);
            child.Get<Components::Parent>().entity.Set(parent);
        }

        void Update(World* world) override
        {
            ZoneScoped;
            if (!world->playing) { UnloadAll(); return; }
            if (!FindCamera(world)) return;

            // snapshot placeholders first: Load() mutates pools, so we must not iterate them live
            uint prefabBucket = Components::MaskToBucket(Components::Prefab::mask);
            uint trBucket = Components::MaskToBucket(Components::Transform::mask);

            uint instanceQueryIndex = world->Query(Components::Prefab::mask, 0);
            auto& placeholders = world->frameQueries[instanceQueryIndex];
            for (auto& eb : placeholders)
            {
                World::Entity e = eb;
                if (!e.IsValid()) continue;
                auto& pf = e.Get<Components::Prefab>();
                auto& tr = e.Get<Components::Transform>();
                float d = length(tr.position - cameraPos);
                Instance& inst = instances[e.id];
                if (!inst.loaded && pf.file[0] != 0 && d < pf.loadDistance)
                {
                    World::LoadResult r = world->Load(String(pf.file));
                    inst.created = std::move(r.created);
                    // Parent the prefab under the placeholder so it inherits the placeholder's
                    // transform (its world position). Prefer the explicit root recorded by
                    // SavePrefab; only that one entity needs re-parenting -- its children keep
                    // their (remapped) in-prefab parents and ride along.
                    World::Entity root = r.root;
                    if (root.IsValid())
                    {
                        ParentUnder(root, e);
                    }
                    else
                    {
                        // fallback for whole-world / legacy files with no recorded root: parent
                        // every created entity whose parent isn't itself part of this load.
                        std::unordered_set<uint> createdIds;
                        for (auto& ceb : inst.created) createdIds.insert(ceb.id);
                        for (auto& ceb : inst.created)
                        {
                            World::Entity child = ceb;
                            if (!child.IsValid() || !child.Has<Components::Transform>()) continue;
                            if (child.Has<Components::Parent>())
                            {
                                auto& pe = child.Get<Components::Parent>().entity;
                                if (pe.IsValid() && createdIds.count(pe.id)) continue; // child of another loaded entity
                            }
                            ParentUnder(child, e);
                        }
                    }
                    inst.loaded = true;
                    world->structureChanged = true;
                }
                else if (inst.loaded && d > pf.loadDistance * 1.1f)
                {
                    Unload(inst);
                }
            }
        }
    };
}