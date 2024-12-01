#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>

#define SUBTASKWORLD(system) tf::Task system##Task = subflow.emplace([this](){this->system.Update(this);}).name(#system)

typedef uint64_t assetID;

static constexpr uint entityInvalid = 65535;
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
    struct Handle
    {
        uint index = entityInvalid;
        T& Get();
    };

    // Be able to get the entity index from the entitySlot
    struct Entity : ComponentBase<Entity>
    {
        uint index = entityInvalid;
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

    struct Texture : ComponentBase<Texture>
    {
        assetID id;
    };
    Texture texture;

    struct __declspec(align(128)) Material : ComponentBase<Material>
    {
        Handle<Shader> shader;
        Handle<Texture> textures[16];
        float prameters[15]; // not 16 so that the struct is 128bytes
    };
    Material material;

    struct Instance : ComponentBase<Instance>
    {
        Handle<Mesh> mesh;
        Handle<Material> material;
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
            ZoneScoped;
            /*
            IOs::Log("----------------");
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Entity).name(), Components::Entity::mask.to_string(), Components::Entity::bucketIndex, Components::Entity::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Shader).name(), Components::Shader::mask.to_string(), Components::Shader::bucketIndex, Components::Shader::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Mesh).name(), Components::Mesh::mask.to_string(), Components::Mesh::bucketIndex, Components::Mesh::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Material).name(), Components::Material::mask.to_string(), Components::Material::bucketIndex, Components::Material::stride);
            IOs::Log("component {} mask {} bucketIndex {} size {}", typeid(Components::Instance).name(), Components::Instance::mask.to_string(), Components::Instance::bucketIndex, Components::Instance::stride);
            */
        }
    };
}

// Just one world. keep it simple.
static constexpr uint poolMaxSlots = 65535;
static constexpr uint poolInvalid = 65535;
class World
{
public:
    struct Pool
    {
    public:
        Components::Mask mask;
        uint count;
        Slots freeslots;
        std::array<void*, Components::componentMaxCount> data;

        void On()
        {
            count = 0;
            freeslots.On(poolMaxSlots);
            for (uint i = 0; i < data.size(); i++)
            {
                data[i] = nullptr;
                if(mask[i])
                    data[i] = malloc(poolMaxSlots * Components::strides[i]);
            }
        }

        uint GetSlot()
        {
            count++;
            return freeslots.Get();
        }

        void ReleaseSlot(uint index)
        {
            count--;
            freeslots.Release(index);
        }

        inline bool Satisfy(Components::Mask include, Components::Mask exclude)
        {
            return ((mask & include) == include) && (mask & exclude) == 0;
        }
    };

    struct EntitySlot
    {
        /*
        union
        {
            struct {
        */
                uint pool : 16;
                uint index : 16;
        /*
            };
            uint slot{};
        };
        */

        template <Components::IsComponent T>
        T& Get()
        {
            // TODO : check that the pool have the right mask (debug assert ?)
            auto& pool = World::instance->components[this->pool];
            auto& res = ((T*)pool.data[T::bucketIndex])[this->index];
            return res;
        }
    };

    struct Entity
    {
        uint id;

        Entity()
        {
            id = ~0;
        }

        inline void Make(uint i)
        {
            id = i;
        }

        void Make(Components::Mask mask)
        {
            mask |= Components::Entity::mask;

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

            Get<Components::Entity>().index = id;
        }

        void Release()
        {
            World::instance->entitySlots[id].index = poolInvalid;
            World::instance->entitySlots[id].pool = poolInvalid;
            World::instance->entityFreeSlots.push_back(id);
        }

        template <Components::IsComponent T>
        T& Get()
        {
            // TODO : check that the pool have the right mask (debug assert ?)
            auto& slot = World::instance->entitySlots[id];
            return slot.Get<T>();
        }

        template <Components::IsComponent T>
        void Set(T value)
        {
            auto& slot = World::instance->entitySlots[id];
            if (!(World::instance->components[slot.pool].mask & T::mask))
            {
                //TODO : make a temp copy Copy(from, to)
                slot.pool = GetOrCreatePoolIndex(World::instance->components[slot.pool].mask | T::mask);
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
            mask |= Components::Entity::mask;

            for (uint i = 0; i < World::instance->components.size(); i++)
            {
                auto& pool = World::instance->components[i];
                if (pool.mask == mask && pool.count < poolMaxSlots)
                    return i;
            }
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
    };

    static World* instance;

    std::vector<EntitySlot> entitySlots;
    std::vector<uint> entityFreeSlots;
    std::vector<Pool> components;

    std::array<std::vector<EntitySlot>, 128> frameQueries;
    uint frameQueriesIndex;

    Systems::Player player;

    void On()
    {
        instance = this;
    }

    void Off()
    {
        instance = nullptr;
    }

    void Schedule(tf::Subflow& subflow)
    {
        ZoneScoped;
        frameQueriesIndex = 0;
        //ECS systems
        SUBTASKWORLD(player);
    }

    uint Query(Components::Mask include, Components::Mask exclude)
    {
        uint queryIndex = frameQueriesIndex++;
        std::vector<EntitySlot>& queryResult = frameQueries[queryIndex];
        queryResult.clear();
        queryResult.reserve(components.size() * poolMaxSlots);
        for (uint i = 0; i < components.size(); i++)
        {
            Pool& pool = components[i];
            if (pool.Satisfy(include, exclude))
            {
                for (uint j = 0; j < pool.count; j++)
                {
                    queryResult.push_back({ i, j });
                }
            }
        }
        return queryIndex;
    }
};
World* World::instance;

namespace Components
{
    template<Components::IsComponent T>
    T& Handle<T>::Get()
    {
        World::Entity entity;
        if (index == entityInvalid)
        {
            entity.Make(T::mask);
        }
        else
        {
            entity.Make(index);
        }
        return entity.Get<T>();
    }
}