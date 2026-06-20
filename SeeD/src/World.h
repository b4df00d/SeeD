#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>
#include <filesystem>
#include <unordered_map>
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
            // TODO : check that the pool have the right mask (debug assert ?)
            seedAssert(bucketIndex < Components::componentMaxCount);
            seedAssert(poolpool.data[bucketIndex] != nullptr);
            char* data = (char*)poolpool.data[bucketIndex];
            return &data[index * Components::strides[bucketIndex]];
        }

        template <Components::IsComponent T>
        T& Get()
        {
            T* data = (T*)Get(T::bucketIndex);
            return *data;
            /*
            auto& poolpool = World::instance->components[pool];
            // TODO : check that the pool have the right mask (debug assert ?)
            seedAssert(T::bucketIndex < Components::componentMaxCount);
            seedAssert(poolpool.data[T::bucketIndex] != nullptr);
            seedAssert((poolpool.mask & T::mask) != 0);
            T* data = (T*)poolpool.data[T::bucketIndex];
            auto& res = data[index];
            return res;
            */
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
            // TODO : check that the pool have the right mask (debug assert ?)
            auto& slot = World::instance->entitySlots[id];
            return slot.Has<T>();
        }

        char* Get(uint bucketIndex)
        {
            seedAssert(IsValid());
            // TODO : check that the pool have the right mask (debug assert ?)
            auto& slot = World::instance->entitySlots[id];
            return slot.Get(bucketIndex);
        }

        template <Components::IsComponent T>
        T& Get()
        {
            seedAssert(IsValid());
            // TODO : check that the pool have the right mask (debug assert ?)
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
            // TODO : consider removing the pool ...

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
            // TODO : consider removing the pool ...

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

    std::array<std::vector<EntitySlot>, 128> frameQueries;
    std::atomic<uint> frameQueriesIndex;

    std::vector<Systems::SystemBase*> systems;

    bool playing;

    void On()
    {
        instance = this;
        playing = false;
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
    // or fields stays loadable. Asset entities (Shader/Mesh/Texture) are deduped by assetID.
    void Load(String name)
    {
        ZoneScoped;
        InitKnownComponents();
        VerifyKnownComponents();

        std::ifstream fin((std::string)name, std::ios::binary);
        if (!fin.is_open()) { IOs::Log("Fail to open {}", name.c_str()); return; }

        char magic[4] = {};
        fin.read(magic, sizeof(magic));
        uint32_t version = 0;
        fin.read((char*)&version, sizeof(version));
        if (std::string(magic, 4) != "SEED" || version != 2)
        {
            IOs::Log("Unsupported world file {} (version {})", name.c_str(), version);
            return;
        }

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
            // derive each field's byte size from sorted offsets (last = stride - offset)
            std::vector<FField*> s; for (auto& f : c.fields) s.push_back(&f);
            std::sort(s.begin(), s.end(), [](FField* a, FField* b) { return a->offset < b->offset; });
            for (size_t i = 0; i < s.size(); i++) { uint end = (i + 1 < s.size()) ? s[i + 1]->offset : c.stride; s[i]->size = end - s[i]->offset; }
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
            bool isAsset = false;   // Shader/Mesh/Texture -> dedup by assetID
            uint assetIdSrcOffset = 0;
            std::vector<FieldCopy> copies;     // matched fields
            std::vector<HandleField> handles;  // ALL current handle fields (for remap + invalidation)
        };
        std::vector<Plan> plans((size_t)schemaCount);
        for (uint si = 0; si < schemaCount; si++)
        {
            FComp& sc = schema[si];
            Plan& pl = plans[si];
            pl.isEntity = (sc.name == "Entity");
            pl.isAsset = (sc.name == "Shader" || sc.name == "Mesh" || sc.name == "Texture");
            for (uint k = 0; k < knownComponents.size(); k++)
                if (knownComponents[k].name == sc.name) { pl.kcIndex = (int)k; break; }
            if (pl.isAsset)
                for (auto& f : sc.fields) if (f.name == "id") { pl.assetIdSrcOffset = f.offset; break; }
            if (pl.kcIndex < 0) continue;

            pl.bucket = Components::MaskToBucket(knownComponents[pl.kcIndex].mask);
            pl.curStride = Components::strides[pl.bucket];

            // current field offset/size table (size derived like the file side)
            struct CF { std::string name; uint offset; uint size; PropertyTypes type; uint count; };
            std::vector<CF> cf;
            for (auto& m : knownComponents[pl.kcIndex].members) cf.push_back({ (std::string)m.name, (uint)m.offset, 0u, m.dataType, (uint)m.dataCount });
            {
                std::vector<CF*> s; for (auto& x : cf) s.push_back(&x);
                std::sort(s.begin(), s.end(), [](CF* a, CF* b) { return a->offset < b->offset; });
                for (size_t i = 0; i < s.size(); i++) { uint end = (i + 1 < s.size()) ? s[i + 1]->offset : pl.curStride; s[i]->size = end - s[i]->offset; }
            }

            bool identical = (sc.stride == pl.curStride) && (sc.fields.size() == cf.size());
            for (auto& c : cf)
            {
                if (c.type == PropertyTypes::_Handle)
                    pl.handles.push_back({ c.offset, c.count });

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

        // ---- asset dedup: index existing asset entities by assetID ----
        std::unordered_map<assetID, EntityBase> assetDedup;
        uint entityBucket = Components::MaskToBucket(Components::Entity::mask);
        for (uint pi = 0; pi < components.size(); pi++)
        {
            Pool& pool = components[pi];
            for (uint k = 0; k < knownComponents.size(); k++)
            {
                auto& kc = knownComponents[k];
                if (kc.name != "Shader" && kc.name != "Mesh" && kc.name != "Texture") continue;
                uint bucket = Components::MaskToBucket(kc.mask);
                if (!pool.mask[bucket]) continue;
                for (uint j = 0; j < pool.count; j++)
                {
                    assetID id = *(assetID*)((char*)pool.data[bucket] + (size_t)j * Components::strides[bucket]);
                    EntityBase ent = *(EntityBase*)((char*)pool.data[entityBucket] + (size_t)j * Components::strides[entityBucket]);
                    assetDedup[id] = ent;
                }
            }
        }

        // ---- read pools, create/dedup entities, build savedId -> new entity map ----
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

                // dedup pure asset entities by assetID
                bool deduped = false;
                EntityBase mapped = entityInvalid;
                for (uint c = 0; c < compCount && !deduped; c++)
                {
                    Plan& pl = plans[archetype[c]];
                    if (!pl.isAsset) continue;
                    assetID id = *(assetID*)(blobs[c].data() + (size_t)s * schema[archetype[c]].stride + pl.assetIdSrcOffset);
                    auto it = assetDedup.find(id);
                    if (it != assetDedup.end()) { mapped = it->second; deduped = true; }
                }

                if (!deduped)
                {
                    Entity e; e.Make(mask);
                    mapped = e;

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

                    // register new asset entity so later slots/files dedup against it
                    for (uint c = 0; c < compCount; c++)
                    {
                        Plan& pl = plans[archetype[c]];
                        if (!pl.isAsset || pl.kcIndex < 0) continue;
                        assetID id = *(assetID*)(blobs[c].data() + (size_t)s * schema[archetype[c]].stride + pl.assetIdSrcOffset);
                        assetDedup[id] = mapped;
                    }

                    created.push_back({ mapped, archetypeRef });
                }

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

        IOs::Log("World loaded from {}", name.c_str());
    }

    // Near-raw bulk save. Self-describing: writes a schema table (components + fields by
    // name) followed by per-pool component blobs. Transient components are skipped.
    // No handle rewriting here; all remapping happens additively on Load.
    void Save(String name)
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
        writeU32(2);

        // ---- schema table = all knownComponents (all non-transient) ----
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

        // ---- pools (only non-empty) ----
        std::vector<uint> poolsToSave;
        for (uint p = 0; p < components.size(); p++) if (components[p].count > 0) poolsToSave.push_back(p);

        writeU32((uint32_t)poolsToSave.size());
        for (uint p : poolsToSave)
        {
            Pool& pool = components[p];
            std::vector<int> comps; // non-transient present buckets, as schema indices
            for (uint i = 0; i < Components::componentMaxCount; i++)
                if (pool.mask[i] && !Components::transient[i] && bucketToKC[i] >= 0) comps.push_back(bucketToKC[i]);

            writeU32((uint32_t)comps.size());
            for (int k : comps) writeU32((uint32_t)k);
            writeU32((uint32_t)pool.count);
            for (int k : comps)
            {
                uint bucket = Components::MaskToBucket(knownComponents[k].mask);
                size_t bytes = (size_t)Components::strides[bucket] * pool.count;
                fout.write((char*)pool.data[bucket], bytes);
            }
        }

        fout.close();
        IOs::Log("World saved to {}", name.c_str());
    }

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
            // TODO : consider removing the pool ...

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
        std::vector<EntitySlot>& queryResult = frameQueries[queryIndex];
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
                    queryResult.push_back({ 0, 0, i, j });
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
}