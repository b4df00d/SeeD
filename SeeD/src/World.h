#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>
#include <filesystem>

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

    template<typename T>
    static uint GetComponentStride()
    {
        strides[masksIndex] = sizeof(T);
        String name = typeid(T).name();
        names[masksIndex] = name.substr(name.find_last_of("::") + 1);
        masksIndex++;
        std::stringstream ss;
        ss << names[masksIndex] << " " << masksIndex - 1 << " " << sizeof(T) << "\n";
        std::string debugInfo(ss.str());
        OutputDebugStringA(debugInfo.c_str());
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
    };
    template<typename T>
    uint ComponentBase<T>::bucketIndex = GetComponentStride<T>();
    template<typename T>
    Mask ComponentBase<T>::mask = 1 << ComponentBase<T>::bucketIndex;
    template<typename T>
    uint ComponentBase<T>::stride = sizeof(T);

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
        float parameters[HLSL::MaterialParametersCount]; //name:[albedo, roughness, metalness, normal]
    };
    static void MaterialPropertyDraw(char* mat)
    {
        Material* material = (Material*)mat;

        uint pushID = 0;
        ImGui::PushID(pushID++);
        UIHelpers::DrawHandle(*(EntityBase*)&material->shader, Shader::mask);
        ImGui::PopID();
        ImGui::Spacing();

        const char* names[] = { "albedo", "roughness", "metalness",  "normal" };
        for (uint i = 0; i < 4; i++)
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
        Handle<Material> material;
    };

    struct Parent : ComponentBase<Parent>
    {
        Handle<Entity> entity;
    };

    struct Light : ComponentBase<Light>
    {
        float angle;
        float range;
        float4 color;
        uint type;
    };

    struct Camera : ComponentBase<Camera>
    {
        float fovY;
        float nearClip;
        float farClip;
    };
}

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
        //Slots freeslots;
        std::array<void*, Components::componentMaxCount> data;

        void On()
        {
            count = 0;
            //freeslots.On(poolMaxSlots);
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
            //return freeslots.Get();
        }

        /*
        void ReleaseSlot(uint index)
        {
            count--;
            freeslots.Release(index);
        }
        */

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

            if (!name.empty())
                mask |= Components::Name::mask;

            EntitySlot slot;
            slot.pool = GetOrCreatePoolIndex(mask);
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

            return *this;
        }

        // not used yet. Is this the proper way ?
        void Release()
        {
            seedAssert(IsValid());
            World::instance->deferredRelease.push_back(*this);
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
            newSlot.pool = GetOrCreatePoolIndex(World::instance->components[thisSlot.pool].mask | mask);
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
            newSlot.pool = GetOrCreatePoolIndex(World::instance->components[thisSlot.pool].mask & ~mask);
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
                    memcpy(to.Get(i), from.Get(i), Components::strides[i]);
                }
            }
        }

        // not thread safe when need to create
        uint GetOrCreatePoolIndex(Components::Mask mask)
        {
            mask |= Components::Entity::mask;

            for (uint i = 0; i < World::instance->components.size(); i++)
            {
                auto& pool = World::instance->components[i];
                if (pool.mask == mask && pool.count < poolMaxSlots)
                    return i;
            }
            seedAssert(World::instance->components.size() < (1 << 11)); //sync with entitySlot.pool bit count
            Pool newPool;
            newPool.mask = mask;
            newPool.On();
            World::instance->components.push_back(newPool);
            return (uint)World::instance->components.size() - 1;
        }

        Pool& GetOrCreatePool(Components::Mask mask)
        {
            return World::instance->components[GetOrCreatePoolIndex(mask)];       
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
        entitySlots.reserve(1024);
        entityFreeSlots.reserve(entitySlots.size());
    }

    void Off()
    {
        instance = nullptr;
    }

    void Load(String name)
    {

    }

    void Save(String name)
    {

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