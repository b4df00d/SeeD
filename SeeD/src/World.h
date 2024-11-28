#pragma once
#include <bitset>
#include <concepts>
#include <type_traits>

#define SUBTASKWORLD(system) tf::Task system##Task = subflow.emplace([this](){this->system.Update(this);}).name(#system)

typedef uint assetID;

namespace Components
{
    typedef std::bitset<64> Mask;

    static uint masksIndex = 0;

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
    uint ComponentBase<T>::bucketIndex = sizeof(masksIndex++);
    template<typename T>
    uint ComponentBase<T>::stride = sizeof(T);

    template<typename T>
    concept IsComponent = std::is_base_of<Components::ComponentBase<T>, T>::value;

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
        Shader* shader;
    };
    Material material;

    struct Instance : ComponentBase<Instance>
    {
        Mesh* mesh;
        Material* material;
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
            IOs::Log("component {} mask {} size {}", typeid(Components::Entity).name(), Components::Entity::mask.to_string(), Components::Entity::stride);
            IOs::Log("component {} mask {} size {}", typeid(Components::Shader).name(), Components::Shader::mask.to_string(), Components::Shader::stride);
            IOs::Log("component {} mask {} size {}", typeid(Components::Mesh).name(), Components::Mesh::mask.to_string(), Components::Mesh::stride);
            IOs::Log("component {} mask {} size {}", typeid(Components::Material).name(), Components::Material::mask.to_string(), Components::Material::stride);
            IOs::Log("component {} mask {} size {}", typeid(Components::Instance).name(), Components::Instance::mask.to_string(), Components::Instance::stride);
            ZoneScoped;
        }
    };
}

class World
{
    static constexpr uint maxScene = 8;
    static constexpr uint maxBucket = 12;
    static constexpr uint maxEntity = 12;
    struct Bucket
    {
        uint scene : maxScene;
        Components::Mask mask;
        std::vector<std::vector<void*>> components;

        Bucket()
        {
            components.resize(mask.size());
        }
    };

    struct EntitySpace
    {
        uint scene : maxScene;
        uint bucket : maxBucket;
        uint entity : maxEntity;
    };
    // an entity is just an index in this array
    // the array stores the real place the entity is living
    std::vector<EntitySpace> entities;

public:
    struct Query
    {
        Components::Mask required{ 0 };
        Components::Mask excluded{ 0 };
        uint entityPerThreadHint{ 10 };

        void Add(Components::Mask mask)
        {
            required &= mask;
        }
        void Exclude(Components::Mask mask)
        {
            excluded &= mask;
        }
    };
    struct Entity
    {
        uint id;

        template <Components::IsComponent T>
        T* Get()
        {
            auto& entitySpace = World::instance->entities[id];
            auto& bucket = World::instance->buckets[entitySpace.bucket];
            T* res = (T*)bucket.components[T::bucketIndex][entitySpace.entity];
            return res;
        }

        template <Components::IsComponent T>
        void Set()
        {
            T::mask;
            return nullptr;
        }
    };

    static World* instance;

    std::vector<Bucket> buckets;

    Systems::Player player;

    World()
    {
        instance = this;
    }

    void Add()
    {

    }

    void Schedule(tf::Subflow& subflow)
    {
        ZoneScoped;
        //ECS systems
        SUBTASKWORLD(player);
    }
    
    template <Components::IsComponent T>
    T* Get()
    {
        T::mask;
        return nullptr;
    }

    uint GetCount(Query query)
    {
        return 0;
    }

    typedef void (*Task)(void* data, Entity entity);
    template <Task task>
    void ParallelFor(Query query, void* data)
    {
        return;

        uint totalCount = GetCount(query);

        if (totalCount <= query.entityPerThreadHint)
        {
            Entity entity{};
            task(data, entity);
        }

        uint chunkCount = 0;
    }
};
World* World::instance;