#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>

#define SUBTASKWORLD(system) tf::Task system##Task = subflow.emplace([this](){this->system.Update(this);}).name(#system)

typedef uint assetID;

namespace Components
{
    static constexpr uint componentMaxCount = 64;
    typedef std::bitset<componentMaxCount> Mask;

    static uint masksIndex = 0;
    static std::vector<uint> strides;

    template<typename T>
    static uint GetComponentStride()
    {
        strides.push_back(sizeof(T));
        masksIndex++;
        return masksIndex;
    }

    template<typename T>
    struct ComponentBase
    {
        static Mask mask;
        static uint bucketIndex;
        static uint stride;
    };
    template<typename T>
    Mask ComponentBase<T>::mask = 1 << masksIndex;
    template<typename T>
    uint ComponentBase<T>::bucketIndex = GetComponentStride<T>();
    template<typename T>
    uint ComponentBase<T>::stride = sizeof(T);

    template<typename T>
    concept IsComponent = std::is_base_of<Components::ComponentBase<T>, T>::value;

    template<Components::IsComponent T>
    struct PTR
    {
        uint index;
        T& Get();
    };

    struct Entity : ComponentBase<Entity>
    {
        uint index;
    };
    Entity entity;

    struct Shader : ComponentBase<Shader>
    {
        assetID id;
    };
    Shader shader;

    struct Mesh : ComponentBase<Mesh>
    {
        assetID id;
    };
    Mesh mesh;

    struct Material : ComponentBase<Material>
    {
        PTR<Shader> shader;
    };
    Material material;

    struct Instance : ComponentBase<Instance>
    {
        PTR<Mesh> mesh;
        PTR<Material> material;
    };
    Instance instance;
}

class World;

namespace Systems
{
    struct SystemBase
    {
        static Components::Mask mask;
        virtual void Update(World* world) = 0;
    };
    Components::Mask SystemBase::mask;

    struct Player : SystemBase
    {
        void Update(World* world)
        {
            IOs::Log("----------------");
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Entity).name(), Components::Entity::mask.to_string(), Components::Entity::bucketIndex, Components::Entity::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Shader).name(), Components::Shader::mask.to_string(), Components::Shader::bucketIndex, Components::Shader::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Mesh).name(), Components::Mesh::mask.to_string(), Components::Mesh::bucketIndex, Components::Mesh::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Material).name(), Components::Material::mask.to_string(), Components::Material::bucketIndex, Components::Material::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Instance).name(), Components::Instance::mask.to_string(), Components::Instance::bucketIndex, Components::Instance::stride);
            ZoneScoped;
        }
    };
}

static constexpr uint poolMaxSlots = 65535;
class World
{
public:
    struct Pool
    {
    public:
        Components::Mask mask;
        uint count;
        std::array<void*, Components::componentMaxCount> data;
        Slots freeslots;

        void Start()
        {
            count = 0;
            freeslots.Start(poolMaxSlots);
            for (uint i = 0; i < data.size(); i++)
            {
                data[i] = nullptr;
                if(mask[i])
                    data[i] = malloc(poolMaxSlots * Components::strides[i]);
            }
        }

        uint GetSlot()
        {
            return freeslots.Get();
        }

        void ReleaseSlot(uint index)
        {
            freeslots.Release(index);
        }
    };

    struct EntitySlot
    {
        uint pool : 16;
        uint index : 16;
    };

    struct Entity
    {
        uint id;

        Entity(Components::Mask mask)
        {
            EntitySlot slot;
            slot.pool = GetOrCreatePoolIndex(mask);
            slot.index = World::instance->components[slot.pool].GetSlot();

            if (World::instance->entityFreeSlots.size())
            {
                id = (uint)World::instance->entityFreeSlots.back();
                World::instance->entityFreeSlots.pop_back(); // pourquoi le popback avec la convertion en uint compile pas !??!?
                World::instance->entitySlots[id] = slot;
            }
            else
            {
                World::instance->entitySlots.push_back(slot);
                id = (uint)World::instance->entitySlots.size() - 1;
            }
        }

        ~Entity()
        {
            World::instance->entitySlots[id].index = poolMaxSlots;
            World::instance->entitySlots[id].pool = poolMaxSlots;
            World::instance->entityFreeSlots.push_back(id);
        }

        template <Components::IsComponent T>
        T& Get()
        {
            // TODO : check that the pool have the right mask (debug assert ?)
            auto& slot = World::instance->entitySlots[id];
            auto& pool = World::instance->components[slot.pool];
            auto& res = ((T*)pool.data[T::bucketIndex])[slot.index];
            return res;
        }

        template <Components::IsComponent T>
        void Set(T value)
        {
            auto& slot = World::instance->entitySlots[id];
            if (!(World::instance->components[slot.pool].mask & T::mask))
            {
                //TODO : make a temp copy Copy(from, to)
                slot.pool = GetOrCreatePoolIndex(World::instance->components[slot.pool].mask & T::mask);
                slot.index = World::instance->components[slot.pool].GetSlot();
            }
            auto& res = *(T*)World::instance->components[slot.pool].data[T::bucketIndex][slot.index * sizeof(T)];
            res = value;
        }

        void Copy(EntitySlot from, EntitySlot to)
        {

        }

        uint GetOrCreatePoolIndex(Components::Mask mask)
        {
            for (uint i = 0; i < World::instance->components.size(); i++)
            {
                auto& pool = World::instance->components[i];
                if (pool.mask == mask && pool.count < poolMaxSlots)
                    return i;
            }
            Pool newPool;
            newPool.mask = mask;
            newPool.Start();
            World::instance->components.push_back(newPool);
            return (uint)World::instance->components.size() - 1;
        }
        Pool& GetOrCreatePool(Components::Mask mask)
        {
            return World::instance->components[GetOrCreatePoolIndex(mask)];       
        }
    };

    static World* instance;

    std::vector<EntitySlot> entitySlots;
    std::vector<uint> entityFreeSlots;
    std::vector<Pool> components;

    Systems::Player player;

    void Start()
    {
        instance = this;
    }

    void Schedule(tf::Subflow& subflow)
    {
        ZoneScoped;
        //ECS systems
        SUBTASKWORLD(player);
    }
};
World* World::instance;

namespace Components
{
    template<Components::IsComponent T>
    T& PTR<T>::Get()
    {
        World::Entity entity{ index };
        return entity.Get<T>();
    }
}