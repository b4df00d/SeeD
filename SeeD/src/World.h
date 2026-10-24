#pragma once
#include <bitset>
#include <atomic>
#include <concepts>
#include <type_traits>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <deque>
#include <vector>
#include <cmath>
#include <cfloat>
#include <functional>

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

    using CallbackT = std::function<void(EntityBase)>;
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
        #define ECS_SHADER_PATH 256
        char path[ECS_SHADER_PATH] = {};
    };
    // Defined in UI.h: the picker lists the "#pragma <stage> <PublicName> ..." variations of a
    // .hlsl, which only ShaderLoader (Loading.h) knows how to read.
    static void ShaderPropertyDraw(char* p);

    struct Mesh : ComponentBase<Mesh>
    {
        assetID id;
        bool noRayTracing; // instances of this mesh are left out of the TLAS (see also MaterialNoRayTracingParam)
    };

    struct Texture : ComponentBase<Texture>
    {
        assetID id;
    };

    struct Material : ComponentBase<Material>
    {
        Handle<Shader> shader;
        Handle<Texture> textures[HLSL::MaterialTextureCount]; //name:[albedo, roughness, metalness, normal, occlusion, emissive]
        // 0,1,2 albedo RGB | 3 roughness | 4 metalness | 5 normal scale |
        // 6 cutout threshold (0 = opaque) | 7,8,9 emissive RGB | 10 double sided | 11 haze layer
        float parameters[HLSL::MaterialParametersCount];
    };
    static void MaterialPropertyDraw(char* mat)
    {
        Material* material = (Material*)mat;

        uint pushID = 0;
        ImGui::PushID(pushID++);
        UIHelpers::DrawHandle(*(EntityBase*)&material->shader, Shader::mask);
        ImGui::PopID();
        ImGui::Spacing();

        // Texture slot -> the parameter it scales. Albedo and emissive are colours
        // (3 floats each), the rest are single values, so they cannot share one loop.
        ImGui::Text("albedo");
        ImGui::PushID(pushID++);
        UIHelpers::DrawHandle(*(EntityBase*)&material->textures[0], Texture::mask);
        ImGui::PopID();
        ImGui::ColorEdit3("albedo", &material->parameters[0]);
        ImGui::Spacing();

        const char* scalarNames[] = { "roughness", "metalness", "normal", "cutout" };
        const uint scalarParam[] = { 3, 4, 5, 6 };
        const uint scalarTexture[] = { 1, 2, 3, ~0u }; // cutout has no texture of its own
        for (uint i = 0; i < ARRAYSIZE(scalarNames); i++)
        {
            ImGui::Text(scalarNames[i]);
            if (scalarTexture[i] != ~0u)
            {
                ImGui::PushID(pushID++);
                UIHelpers::DrawHandle(*(EntityBase*)&material->textures[scalarTexture[i]], Texture::mask);
                ImGui::PopID();
            }
            ImGui::SliderFloat(scalarNames[i], &material->parameters[scalarParam[i]], 0, 1);
            ImGui::Spacing();
        }

        ImGui::Text("emissive");
        ImGui::PushID(pushID++);
        UIHelpers::DrawHandle(*(EntityBase*)&material->textures[5], Texture::mask);
        ImGui::PopID();
        ImGui::ColorEdit3("emissive", &material->parameters[7], ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        ImGui::Spacing();

        // Re-buckets the material's meshlets (sidedness picks the PSO cull mode): instances already
        // loaded keep their old bucket's capacity, so they only draw correctly after a reload.
        bool doubleSided = material->parameters[HLSL::MaterialDoubleSidedParam] != 0.0f;
        if (ImGui::Checkbox("double sided", &doubleSided))
            material->parameters[HLSL::MaterialDoubleSidedParam] = doubleSided ? 1.0f : 0.0f;
        ImGui::Spacing();

        // HazeLayer component index this material's surfaces emit (0 = none)
        int hazeLayer = (int)material->parameters[HLSL::MaterialHazeLayerParam];
        if (ImGui::SliderInt("haze layer", &hazeLayer, 0, HLSL::HazeLayerCount - 1))
            material->parameters[HLSL::MaterialHazeLayerParam] = (float)hazeLayer;
        ImGui::Spacing();

        // Read by culling.hlsl every frame: instances using this material get a null BLAS
        bool noRayTracing = material->parameters[HLSL::MaterialNoRayTracingParam] != 0.0f;
        if (ImGui::Checkbox("no ray tracing", &noRayTracing))
            material->parameters[HLSL::MaterialNoRayTracingParam] = noRayTracing ? 1.0f : 0.0f;
        ImGui::Spacing();
    }

    struct Transform : ComponentBase<Transform>
    {
        float3 position;
        quaternion rotation;
        float3 scale;
        uint version;
    };

    struct WorldMatrix : ComponentBase<WorldMatrix>
    {
        float4x4 matrix;
        uint versionCache;
    };

    struct Instance : ComponentBase<Instance>
    {
        Handle<Mesh> mesh;
        Handle<Mesh> meshRT;
        Handle<Material> material;
        float4 boundingSphereOverride;
    };
    // Defined in Loading.h, right after OMMBaker (whose RequestBake this calls) is declared.
    static void InstancePropertyDraw(char* p);

    struct State : ComponentBase<State>
    {
        enum class Flags : uint
        {
            loaded = 1 << 0,
            // Each dirty bit names the consumer that clears it -- they are NOT interchangeable, and
            // sharing one between two consumers means whichever runs first steals it from the other.
            // instanceDirty : cleared by MainView::UpdateInstances (Renderer.h)
            // physicsDirty  : cleared by Physics::Sync (Physic.h)
            // constraintDirty : cleared by Physics::SyncConstraints, which runs AFTER Sync -- it
            //   cannot share physicsDirty for exactly that reason.
            // All are set together when the property window edits an entity (Editor.h).
            instanceDirty = 1 << 1,
            BLAS = 1 << 2,
            transient = 1 << 3,
            physicsDirty = 1 << 4,
            constraintDirty = 1 << 5,
            vehicleDirty = 1 << 6,
        };
        BitFlags<Flags> flags;
    };
    bool State::transient = true;
    // `flags` is a BitFlags<Flags>, which the metadata can only describe as _raw -- so
    // DefaultPropertyDraw shows nothing at all for this component. One checkbox per bit instead.
    //
    // Two things this window cannot change, both by design:
    //  - State is a transient COMPONENT (State::transient above), so nothing edited here is written
    //    to the .seed -- these bits are runtime scratch, they always come back at their default.
    //  - a dirty bit unchecked here is ORed straight back on: the property window sets
    //    instanceDirty|physicsDirty|constraintDirty whenever a drawer changed its component's bytes
    //    (Editor.h, right after the PropertyDraw call), and unchecking one IS such a change. So the
    //    dirty checkboxes are for RAISING a bit (forcing a re-sync); clearing is the consumer's job
    //    and happens on its own next frame.
    static void StatePropertyDraw(char* p)
    {
        State* state = (State*)p;

        struct FlagRow { State::Flags bit; const char* name; const char* tooltip; };
        const FlagRow rows[] =
        {
            { State::Flags::loaded,          "loaded",          "Set by MainView::UpdateInstances (Renderer.h) the first frame the instance is fully uploaded (mesh, material and shader all resolved)." },
            { State::Flags::instanceDirty,   "instanceDirty",   "Re-upload the instance. Cleared by MainView::UpdateInstances (Renderer.h), its only reader." },
            { State::Flags::BLAS,            "BLAS",            "Declared but unused -- nothing in the engine reads it today." },
            { State::Flags::transient,       "transient",       "Regenerated every frame by a system (terrain quadtree instances, editor camera): SaveEntities skips the entity entirely, so it is never written to a .seed." },
            { State::Flags::physicsDirty,    "physicsDirty",    "Re-apply the body properties, recreating the body when motionType changed. Cleared by Physics::Sync (Physic.h)." },
            { State::Flags::constraintDirty, "constraintDirty", "Rebuild the constraint. Cleared by Physics::SyncConstraints, which runs AFTER Sync -- which is why it cannot share physicsDirty." },
        };

        for (uint i = 0; i < ARRAYSIZE(rows); i++)
        {
            bool on = state->flags.IsSet(rows[i].bit);
            if (ImGui::Checkbox(rows[i].name, &on))
            {
                if (on) state->flags.Set(rows[i].bit);
                else    state->flags.Unset(rows[i].bit);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", rows[i].tooltip);
        }

        ImGui::Spacing();
        // The same three bits the property window raises on any edit -- here without having to
        // actually change something, to re-push an entity through the instance and physics syncs.
        if (ImGui::Button("Force re-sync"))
        {
            state->flags.Set(State::Flags::instanceDirty);
            state->flags.Set(State::Flags::physicsDirty);
            state->flags.Set(State::Flags::constraintDirty);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("raw 0x%02X", state->flags.ToRaw());
    }

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

    enum RenderSettingsOverride : uint
    {
        RSO_fogScattering      = 1u << 0,
        RSO_fogAbsorption      = 1u << 1,
        RSO_specialNear        = 1u << 2,
        RSO_heightFalloff      = 1u << 3,
        RSO_noiseFrequency     = 1u << 4,
        RSO_noiseThresholdLow  = 1u << 5,
        RSO_noiseThresholdHigh = 1u << 6,
        RSO_animationSpeed     = 1u << 7,
        RSO_expoMul            = 1u << 8,
        RSO_expoAdd            = 1u << 9,
        RSO_P                  = 1u << 10,
        RSO_a                  = 1u << 11,
        RSO_m                  = 1u << 12,
        RSO_l                  = 1u << 13,
        RSO_c                  = 1u << 14,
        RSO_b                  = 1u << 15,
        RSO_tonemapper         = 1u << 16,
        RSO_planetRadius        = 1u << 17,
        RSO_atmosphereHeight    = 1u << 18,
        RSO_rayleighScattering  = 1u << 19,
        RSO_rayleighScaleHeight = 1u << 20,
        RSO_mieScattering       = 1u << 21,
        RSO_mieAbsorption       = 1u << 22,
        RSO_mieScaleHeight      = 1u << 23,
        RSO_mieAnisotropy       = 1u << 24,
        RSO_ozoneAbsorption     = 1u << 25,
        RSO_groundAlbedo        = 1u << 26,
        RSO_fogAnisotropy       = 1u << 27,
        RSO_fogHeight           = 1u << 28,
        RSO_fogDensity          = 1u << 29,
    };

    struct RenderSettingsVolume : ComponentBase<RenderSettingsVolume>
    {
        uint  shape = 2;             // 0=Box (OBB from Transform), 1=Sphere, 2=Global/unbound
        float radius = 10.0f;        // sphere radius (shape==Sphere)
        float blendDistance = 5.0f;  // outward falloff band in world units (Box/Sphere)
        float priority = 0.0f;       // higher = applied last = wins on overlap
        uint  overrides = 0;         // RenderSettingsOverride bitmask
        // local fog (froxels), coefficients in 1/m at density 1
        float fogDensity = 1.0f;     // multiplies the height/noise density
        float3 fogScattering = float3(0.002f, 0.002f, 0.002f);
        float3 fogAbsorption = float3(0.0f, 0.0f, 0.0f);
        float fogAnisotropy = 0.6f;  // Henyey-Greenstein g
        float fogHeight = 0.0f;      // world y (m) below which the density is full
        float specialNear = 0.25f;   // view distance (m) of the first froxel slice
        float heightFalloff = 0.02f; // 1/m above fogHeight
        float noiseFrequency = 0.2f;
        float noiseThresholdLow = 0.5f;
        float noiseThresholdHigh = 0.6f;
        float animationSpeed = 0.00000002f;
        // exposure / tonemapping
        float expoMul = 1.0f;
        float expoAdd = 0.0f;
        float P = 1.0f;
        float a = 1.0f;
        float m = 0.22f;
        float l = 0.4f;
        float c = 1.33f;
        float b = 0.0f;
        uint  tonemapper = 3;        // HLSL::Tonemapper* (0=None 1=GT 2=ACES 3=AgX 4=AgX Punchy 5=PBR Neutral); not blended, the strongest-weighted volume wins
        // sky atmosphere (Hillaire 2020), Earth defaults; lengths in km, coefficients in 1/km
        float  planetRadius = 6360.0f;
        float  atmosphereHeight = 100.0f;
        float3 rayleighScattering = float3(0.005802f, 0.013558f, 0.0331f);
        float  rayleighScaleHeight = 8.0f;
        float3 mieScattering = float3(0.003996f, 0.003996f, 0.003996f);
        float3 mieAbsorption = float3(0.000444f, 0.000444f, 0.000444f);
        float  mieScaleHeight = 1.2f;
        float  mieAnisotropy = 0.8f;
        float3 ozoneAbsorption = float3(0.00065f, 0.001881f, 0.000085f);
        float3 groundAlbedo = float3(0.3f, 0.3f, 0.3f);
    };
    static void RenderSettingsVolumePropertyDraw(char* p)
    {
        RenderSettingsVolume* v = (RenderSettingsVolume*)p;

        const char* shapes[] = { "Box", "Sphere", "Global" };
        ImGui::Combo("shape", (int*)&v->shape, shapes, IM_ARRAYSIZE(shapes));
        if (v->shape == 1) ImGui::DragFloat("radius", &v->radius, 0.1f, 0.0f, 100000.0f);
        if (v->shape != 2) ImGui::DragFloat("blendDistance", &v->blendDistance, 0.1f, 0.0f, 100000.0f);
        ImGui::DragFloat("priority", &v->priority, 0.1f);

        // checkbox toggles the override bit; slider is only editable while overridden.
        auto row = [&](uint bit, const char* label, float* val, float lo, float hi, ImGuiSliderFlags flags)
        {
            bool on = (v->overrides & bit) != 0;
            ImGui::PushID(label);
            if (ImGui::Checkbox("##override", &on))
                v->overrides = on ? (v->overrides | bit) : (v->overrides & ~bit);
            ImGui::SameLine();
            ImGui::BeginDisabled(!on);
            ImGui::SliderFloat(label, val, lo, hi, "%.6f", flags);
            ImGui::EndDisabled();
            ImGui::PopID();
        };
        auto row3 = [&](uint bit, const char* label, float3* val, float speed, float lo, float hi)
        {
            bool on = (v->overrides & bit) != 0;
            ImGui::PushID(label);
            if (ImGui::Checkbox("##override", &on))
                v->overrides = on ? (v->overrides | bit) : (v->overrides & ~bit);
            ImGui::SameLine();
            ImGui::BeginDisabled(!on);
            float f[3];
            store(*val, f);
            if (ImGui::DragFloat3(label, f, speed, lo, hi, "%.6f"))
                *val = float3(f[0], f[1], f[2]);
            ImGui::EndDisabled();
            ImGui::PopID();
        };

        if (ImGui::CollapsingHeader("Sky Atmosphere", ImGuiTreeNodeFlags_DefaultOpen))
        {
            row(RSO_planetRadius,         "planetRadius (km)",         &v->planetRadius,        100,    100000, ImGuiSliderFlags_Logarithmic);
            row(RSO_atmosphereHeight,     "atmosphereHeight (km)",     &v->atmosphereHeight,    1,      1000,   ImGuiSliderFlags_Logarithmic);
            row3(RSO_rayleighScattering,  "rayleighScattering (1/km)", &v->rayleighScattering,  0.0001f, 0,     1);
            row(RSO_rayleighScaleHeight,  "rayleighScaleHeight (km)",  &v->rayleighScaleHeight, 0.1f,   100,    ImGuiSliderFlags_Logarithmic);
            row3(RSO_mieScattering,       "mieScattering (1/km)",      &v->mieScattering,       0.0001f, 0,     1);
            row3(RSO_mieAbsorption,       "mieAbsorption (1/km)",      &v->mieAbsorption,       0.0001f, 0,     1);
            row(RSO_mieScaleHeight,       "mieScaleHeight (km)",       &v->mieScaleHeight,      0.1f,   100,    ImGuiSliderFlags_Logarithmic);
            row(RSO_mieAnisotropy,        "mieAnisotropy",             &v->mieAnisotropy,       -0.99f, 0.99f,  0);
            row3(RSO_ozoneAbsorption,     "ozoneAbsorption (1/km)",    &v->ozoneAbsorption,     0.0001f, 0,     1);
            row3(RSO_groundAlbedo,        "groundAlbedo",              &v->groundAlbedo,        0.01f,  0,      1);
        }
        if (ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen))
        {
            row(RSO_fogDensity,         "fogDensity",         &v->fogDensity,         0.0f,  1000.0f, ImGuiSliderFlags_None);
            row3(RSO_fogScattering,     "fogScattering (1/m)", &v->fogScattering,     0.0001f, 0,     1);
            row3(RSO_fogAbsorption,     "fogAbsorption (1/m)", &v->fogAbsorption,     0.0001f, 0,     1);
            row(RSO_fogAnisotropy,      "fogAnisotropy",      &v->fogAnisotropy,      -0.99f, 0.99f,  0);
            row(RSO_fogHeight,          "fogHeight (m)",      &v->fogHeight,          -1000,  10000,  0);
            row(RSO_specialNear,        "specialNear",        &v->specialNear,        0.1f,   1,      ImGuiSliderFlags_Logarithmic);
            row(RSO_heightFalloff,      "heightFalloff",      &v->heightFalloff,      0,      0.2f,   ImGuiSliderFlags_Logarithmic);
            row(RSO_noiseFrequency,     "noiseFrequency",     &v->noiseFrequency,     0.001f, 2,      ImGuiSliderFlags_Logarithmic);
            row(RSO_noiseThresholdLow,  "noiseThresholdLow",  &v->noiseThresholdLow,  0,      1,      0);
            row(RSO_noiseThresholdHigh, "noiseThresholdHigh", &v->noiseThresholdHigh, 0,      1,      0);
            row(RSO_animationSpeed,     "animationSpeed",     &v->animationSpeed,     0,      0.0001f, ImGuiSliderFlags_Logarithmic);
        }
        if (ImGui::CollapsingHeader("Exposition", ImGuiTreeNodeFlags_DefaultOpen))
        {
            row(RSO_expoMul, "expoMul", &v->expoMul, 0,  16, 0);
            row(RSO_expoAdd, "expoAdd", &v->expoAdd, -1, 1, 0);

            // same checkbox + disabled-widget pattern as row(), with a combo instead of a slider
            {
                const char* tonemappers[] = { "None (clamp)", "Gran Turismo", "ACES (Narkowicz)", "AgX", "AgX Punchy", "Khronos PBR Neutral" };
                bool on = (v->overrides & RSO_tonemapper) != 0;
                ImGui::PushID("tonemapper");
                if (ImGui::Checkbox("##override", &on))
                    v->overrides = on ? (v->overrides | RSO_tonemapper) : (v->overrides & ~RSO_tonemapper);
                ImGui::SameLine();
                ImGui::BeginDisabled(!on);
                ImGui::Combo("tonemapper", (int*)&v->tonemapper, tonemappers, IM_ARRAYSIZE(tonemappers));
                ImGui::EndDisabled();
                ImGui::PopID();
            }

            // Gran Turismo curve parameters, only meaningful for that tonemapper
            if (v->tonemapper == 1)
            {
                row(RSO_P,       "P",       &v->P,       0,  2, 0);
                row(RSO_a,       "a",       &v->a,       0,  2, 0);
                row(RSO_m,       "m",       &v->m,       0,  2, 0);
                row(RSO_l,       "l",       &v->l,       0,  2, 0);
                row(RSO_c,       "c",       &v->c,       0,  3, 0);
                row(RSO_b,       "b",       &v->b,       0,  1, 0);
            }
        }
    }

    // Soft sphere of fog added to the froxel fog (FogMedium in AtmosphericScattering.hlsl), centered on
    // the entity, radius scaled by its largest world axis. Density falls off as (1 - d^2/r^2)^2
    struct FogVolume : ComponentBase<FogVolume>
    {
        float3 color = float3(1.0f, 1.0f, 1.0f); // single-scattering albedo: scatters color, absorbs 1 - color
        float density = 0.05f;                   // extinction at the center, 1/m
        float radius = 5.0f;                     // m
    };
    static void FogVolumePropertyDraw(char* p)
    {
        FogVolume* v = (FogVolume*)p;
        float c[3];
        store(v->color, c);
        if (ImGui::ColorEdit3("color (albedo)", c))
            v->color = float3(c[0], c[1], c[2]);
        ImGui::DragFloat("density (1/m)", &v->density, 0.001f, 0.0f, 100.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("radius (m)", &v->radius, 0.1f, 0.0f, 100000.0f);
    }

    // A surface haze type (water vapor, dust, pollen...): materials whose haze layer index matches
    // `layer` splat their surfaces into the haze map, and FogMedium adds this medium above them
    struct HazeLayer : ComponentBase<HazeLayer>
    {
        uint layer = 1;                                  // 1 .. HLSL::HazeLayerCount - 1, matched by the material's haze layer
        float3 color = float3(0.9f, 0.9f, 0.9f);         // single-scattering albedo: scatters color, absorbs 1 - color
        float density = 0.2f;                            // extinction at the surface, 1/m
        float height = 0.5f;                             // m, falls off as exp(-h / height) above the surface
        float slab = 0.0f;                               // m below the top surface still inside the medium (canopy depth)
        float anisotropy = 0.7f;                         // Henyey-Greenstein g
        float3 wind = float3(0.4f, 0.05f, 0.2f);         // noise scroll, m/s
        float noiseFrequency = 0.5f;                     // 1/m
        float noiseThresholdLow = 0.3f;
        float noiseThresholdHigh = 0.8f;
        uint resolution = 256;                           // haze map texels per side, per level (windows of 128 m and 1 km)
        float upThreshold = 0.5f;                        // minimum surface normal y to splat; 0 for foliage
        uint blurRadius = 1;                             // texels, averages the sparse splats when accumulated
        float coverage = 1.0f;                           // 0..1, multiplies each map texel's coverage
    };
    static void HazeLayerPropertyDraw(char* p)
    {
        HazeLayer* v = (HazeLayer*)p;
        int layer = (int)v->layer;
        if (ImGui::SliderInt("layer", &layer, 1, HLSL::HazeLayerCount - 1))
            v->layer = (uint)layer;
        float c[3];
        store(v->color, c);
        if (ImGui::ColorEdit3("color (albedo)", c))
            v->color = float3(c[0], c[1], c[2]);
        ImGui::DragFloat("density (1/m)", &v->density, 0.001f, 0.0f, 100.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("height (m)", &v->height, 0.01f, 0.01f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::DragFloat("slab (m)", &v->slab, 0.01f, 0.0f, 100.0f);
        ImGui::SliderFloat("anisotropy", &v->anisotropy, -0.99f, 0.99f);
        store(v->wind, c);
        if (ImGui::DragFloat3("wind (m/s)", c, 0.01f))
            v->wind = float3(c[0], c[1], c[2]);
        ImGui::DragFloat("noise frequency", &v->noiseFrequency, 0.01f, 0.0f, 100.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("noise threshold low", &v->noiseThresholdLow, 0.0f, 1.0f);
        ImGui::SliderFloat("noise threshold high", &v->noiseThresholdHigh, 0.0f, 1.0f);

        const uint resolutions[] = { 64, 128, 256, 512, 1024 };
        const char* resolutionNames[] = { "64", "128", "256", "512", "1024" };
        int current = 2;
        for (int i = 0; i < ARRAYSIZE(resolutions); i++)
            if (resolutions[i] == v->resolution)
                current = i;
        if (ImGui::Combo("map resolution", &current, resolutionNames, ARRAYSIZE(resolutionNames)))
            v->resolution = resolutions[current];
        ImGui::SliderFloat("up threshold", &v->upThreshold, 0.0f, 1.0f);
        int blur = (int)v->blurRadius;
        if (ImGui::SliderInt("blur radius (texels)", &blur, 0, 4))
            v->blurRadius = (uint)blur;
        ImGui::SliderFloat("coverage", &v->coverage, 0.0f, 1.0f);
    }

    struct MeshOverride : ComponentBase<MeshOverride>
    {
        uint overrideMeshIndex; // MeshStorage meshIndex namespace (shared with normal Mesh::storageIndex)
        uint sourceMeshIndex;   // the override's source mesh's storageIndex -- needed to free it back into MeshStorage's per-source recycling list
        uint dirty;             // needs a vertex re-bake; the owning baking system sets/clears this
    };
}

// The terrain components (Terrain / TerrainBaking) live in Terrain.h, next to the systems that
// drive them -- the metadata generator picks up components from any header.

// The autogenerated InitKnownComponents() is NOT included here: components may be declared in any
// header (next to the system that owns them), so the generated table needs every one of them
// declared first and is included once from Main.cpp, after the last engine header. The forward
// declaration in ComponentMetaTypes.h covers the callers below.

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

        void ReleaseImmediately()
        {
            seedAssert(IsValid());

            auto mask = GetMask();
            for (uint i = 0; i < Components::componentMaxCount; i++)
            {
                if (mask[i])
                {
                    if (Components::RemoveCallback[i])
                    {
                        Components::RemoveCallback[i](*this);
                    }
                }
            }

            EntitySlot& thisSlot = World::instance->entitySlots[id];
            Pool& pool = World::instance->components[thisSlot.pool];
            if (pool.count > 1 && thisSlot.index < pool.count - 1)
            {
                EntitySlot tmpSlot = thisSlot; // only interested in pool and index, permanent and rev are not relevant right now
                tmpSlot.index = pool.count - 1;
                Entity entityOfLastSlot = tmpSlot.Get<Components::Entity>();
                EntitySlot& lastSlot = World::instance->entitySlots[entityOfLastSlot.id]; // now the permanent and rev are correct
                Entity::Copy(lastSlot, thisSlot);
                thisSlot.permanent = lastSlot.permanent;
                thisSlot.rev = lastSlot.rev;
                seedAssert(entityOfLastSlot.rev == thisSlot.rev);
                World::instance->entitySlots[entityOfLastSlot.id] = thisSlot;
            }
            pool.count--;
            thisSlot.pool = poolInvalid;
            World::instance->entityFreeSlots.push_back(id);
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
                    memcpy(to.Get(i), from.Get(i), Components::strides[i]);
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

    bool structureChanged = false;

    void On()
    {
        instance = this;
        structureChanged = false;
        entitySlots.reserve(1024 * 1024);
        entityFreeSlots.reserve(entitySlots.size());
    }

    void Off()
    {
        instance = nullptr;
    }

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

    struct LoadResult
    {
        std::vector<EntityBase> created;  // every entity freshly created by this load
        EntityBase root = entityInvalid;  // the prefab root, resolved to its new entity (SavePrefab files only)
        bool imported = false;            // went through the asset importer, so `created` is not filled
    };

    // .seed is the native snapshot; anything else (gltf/glb/fbx/...) is a source
    // asset for the importer.
    static bool IsWorldFile(String name)
    {
        std::string s = (std::string)name;
        size_t dot = s.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext = s.substr(dot + 1);
        for (auto& c : ext) c = (char)tolower((unsigned char)c);
        return ext == "seed";
    }

    LoadResult Load(String name)
    {
        //TODO : try to have the fewer allocations possible

        ZoneScoped;

        LoadResult result;

        // TODO : not good, not terrible... think about it.
        // Everything that is not a world snapshot goes to the importer -- the same
        // path a file drop takes. Routed here so callers have one way to load a
        // scene rather than each one repeating the extension test.
        if (!IsWorldFile(name))
        {
            result.imported = true;
            ImportSceneFile(name);
            return result;
        }

        InitKnownComponents();
        VerifyKnownComponents();

        std::ifstream fin((std::string)name, std::ios::binary);
        if (!fin.is_open()) { IOs::Log("Fail to open {}", name.c_str()); return result; }

        char magic[4] = {};
        fin.read(magic, sizeof(magic));
        uint32_t version = 0;
        fin.read((char*)&version, sizeof(version));
        if (std::string(magic, 4) != "SEED" || version < 2 || version > 5)
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
            // Current member sizes come straight from the metadata (the generator emits sizeof per
            // member), so a field the table happens to be missing can no longer be swallowed by
            // the size of the one before it. VerifyKnownComponents catches such a gap.
            struct CF { std::string name; uint offset; uint size; };
            std::vector<CF> cf;
            for (auto& m : knownComponents[pl.kcIndex].members) cf.push_back({ (std::string)m.name, (uint)m.offset, (uint)m.size });

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
        // v5 stores the total entity count up front so the containers below reserve exactly
        // (older files just grow). result.created doubles as the created-entity list.
        uint32_t totalEntities = (version >= 5) ? readU32() : 0;

        std::unordered_map<uint, EntityBase> idMap;
        if (totalEntities) { idMap.reserve(totalEntities); result.created.reserve(totalEntities); }

        struct PoolBatch { uint createdBegin; uint count; uint archetypeRef; };
        std::vector<PoolBatch> batches;
        std::vector<std::vector<int>> archetypes; // per file-pool: list of schema indices
        std::vector<char> staging;                // ONE reusable buffer, holds the current component array

        uint32_t poolCount = readU32();
        batches.reserve(poolCount);
        archetypes.reserve(poolCount);             // reserved so the `arche` refs below stay valid
        for (uint p = 0; p < poolCount; p++)
        {
            uint32_t compCount = readU32();
            std::vector<int> archetype((size_t)compCount);
            for (auto& a : archetype) a = (int)readU32();
            uint32_t entityCount = readU32();

            Components::Mask mask = 0;
            for (uint c = 0; c < compCount; c++)
            {
                Plan& pl = plans[archetype[c]];
                if (pl.kcIndex >= 0) mask |= knownComponents[pl.kcIndex].mask;
            }

            uint archetypeRef = (uint)archetypes.size();
            archetypes.push_back(std::move(archetype));
            std::vector<int>& arche = archetypes.back();  // archetype was moved-from; use arche below

            // Make every entity for this pool up front so the file can be processed COMPONENT-major:
            // read one component's whole array into the single reusable `staging` buffer and scatter
            // it across the rows, then reuse that same buffer for the next component. (The old code
            // allocated a separate blob per component and held them all at once.)
            uint createdBegin = (uint)result.created.size();
            for (uint s = 0; s < entityCount; s++) { Entity e; e.Make(mask); result.created.push_back(e); }
            batches.push_back({ createdBegin, entityCount, archetypeRef });

            for (uint c = 0; c < compCount; c++)
            {
                FComp& sc = schema[arche[c]];
                size_t arraySize = (size_t)entityCount * sc.stride;
                if (staging.size() < arraySize) staging.resize(arraySize);
                if (arraySize) fin.read(staging.data(), arraySize); // consume even when skipped below

                Plan& pl = plans[arche[c]];
                if (pl.isEntity)
                {
                    // not scattered (Make set new ids) -- the saved ids only feed the remap map
                    for (uint s = 0; s < entityCount; s++)
                    {
                        EntityBase saved = *(EntityBase*)(staging.data() + (size_t)s * sc.stride);
                        if (saved.id != entityInvalid.id)
                            idMap[saved.id] = result.created[createdBegin + s];
                    }
                    continue;
                }
                if (pl.kcIndex < 0 || Components::transient[pl.bucket]) continue; // removed/transient: consumed, not scattered

                for (uint s = 0; s < entityCount; s++)
                {
                    Entity e = result.created[createdBegin + s];
                    char* dst = e.Get(pl.bucket);
                    char* src = staging.data() + (size_t)s * sc.stride;
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
            }
        }

        fin.close();

        // ---- remap handles in freshly created entities (forward refs now resolvable) ----
        for (auto& b : batches)
        {
            std::vector<int>& arche = archetypes[b.archetypeRef];
            for (uint c = 0; c < arche.size(); c++)
            {
                Plan& pl = plans[arche[c]];
                if (pl.kcIndex < 0 || pl.handles.empty()) continue;
                for (uint s = 0; s < b.count; s++)
                {
                    Entity e = result.created[b.createdBegin + s];
                    char* data = e.Get(pl.bucket);
                    for (auto& hf : pl.handles)
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
        writeU32(5);
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
        uint32_t totalEntities = 0;                         // valid entities actually written
        for (auto& eb : entities)
        {
            Entity e = eb;
            if (!e.IsValid()) continue;
            EntitySlot& slot = entitySlots[eb.id];
            if (byPool.find(slot.pool) == byPool.end()) poolOrder.push_back(slot.pool);
            byPool[slot.pool].push_back(slot.index);
            totalEntities++;
        }

        writeU32(totalEntities);                            // v5: total entity count -> exact reserves on Load
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
                // skip entities regenerated every frame by a system (e.g. TerrainStreaming's
                // quadtree instances) -- they carry nothing worth persisting, they're rebuilt from
                // their owning Terrain component on load.
                if (pool.mask[Components::State::bucketIndex]
                    && (slot.Get<Components::State>().flags & Components::State::Flags::transient))
                    continue;
                all.push_back(slot.Get<Components::Entity>());
            }
        }
        SaveEntities(name, all);
    }

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

    void Clear()
    {
        // Snapshot first: SystemBase::Off() erases the system from `systems`, so
        // walking the live vector by index skips every other entry -- half the
        // systems would keep running against a world whose entities just died.
        std::vector<Systems::SystemBase*> systemsStopped = systems;
        for (auto sys : systemsStopped)
            sys->Off();
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

    // predecessor: if non-empty, every system task succeeds it (e.g. prefab/terrain streaming +
    // DeferredRelease must land before the ECS systems read the world this frame).
    void Schedule(tf::Subflow& subflow, tf::Task predecessor = tf::Task())
    {
        ZoneScoped;
        frameQueriesIndex = 0;
        //ECS systems
        for (uint i = 0; i < systems.size(); i++)
        {
            auto sys = systems[i];
            //SUBTASKWORLD(sys);
            tf::Task t = subflow.emplace([this, i]() {this->systems[i]->Update(this); }).name("#system");
            if (!predecessor.empty())
                t.succeed(predecessor);
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

    uint Query(Components::Mask include, Components::Mask exclude, bool includeTransient)
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
                    if (!includeTransient && slot.Get<Components::State>().flags & Components::State::Flags::transient)
                        continue;
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

uint ComputeTransformVersion(World::Entity ent)
{
    uint version = ent.Has<Components::Transform>() ? ent.Get<Components::Transform>().version : 0;

    if (ent.Has<Components::Parent>())
    {
        auto& parentCmp = ent.Get<Components::Parent>();
        auto parentEnt = World::Entity(parentCmp.entity.Get());
        if (parentEnt != entityInvalid)
            version += ComputeTransformVersion(parentEnt);
    }

    return version;
}

uint Rand(uint max)
{
    return uint((float(std::rand()) / float(RAND_MAX)) * max * 0.99999f);
}

float Rand01()
{
    return float(std::rand()) / float(RAND_MAX);
}

// What Systems::Driver (Physic.h) hands to Systems::Player's chase camera. It is the only state
// that crosses between two ECS systems, and World::Schedule emplaces them as independent tasks with
// no edges between them -- they run in parallel, in no defined order -- so it is atomic. Reading a
// value one frame old is fine here: it only ever names WHICH car to follow, never where it is. The
// car's pose is read from its Transform, which PhysicsSyncReadback wrote in stage 1, before any
// system task could start.
static constexpr uint chaseCameraNoTarget = 0xFFFFFFFFu;   // entityInvalid, packed as ToUInt packs it
struct ChaseCameraTarget
{
    std::atomic<uint> entity{ chaseCameraNoTarget };
    std::atomic<float> scale{ 1.0f };   // chassis half length; the rig's distances are multiples of it
};
static ChaseCameraTarget chaseCameraTarget;

namespace Systems
{
    struct Player : SystemBase
    {
        World::Entity camera;
        float sensibility = 1.0f;
        float sensibilityLook = 0.003f;

        float3 position = float3(0, 1, -2);
        quaternion rotation = quaternion::identity();

        void On() override
        {
            auto& cam = camera.Make(Components::Transform::mask | Components::WorldMatrix::mask | Components::Camera::mask).Get<Components::Camera>();
            cam.fovY = 90;
            cam.nearClip = 0.02;
            cam.farClip = 8000.0f;
            auto& trans = camera.Get<Components::Transform>();
            trans.position = position;
            trans.rotation = rotation;
            trans.scale = 1;
            camera.Get<Components::State>().flags |= Components::State::Flags::transient;

            World::instance->systems.push_back(this);
        }

        void Off() override
        {
            auto item = std::find(World::instance->systems.begin(), World::instance->systems.end(), this);
            if(item != World::instance->systems.end())
                World::instance->systems.erase(item);
        }

        // Chase rig state, in world space. The velocities belong to the springs, not to the camera.
        float3 chasePosition = float3(0);
        float3 chasePositionVelocity = float3(0);
        float3 chaseLookAt = float3(0);
        float3 chaseLookAtVelocity = float3(0);
        uint chaseEntity = chaseCameraNoTarget;   // what the rig is locked onto, to know when to snap

        // Critically damped spring: closes most of the gap in `smoothTime` seconds and never
        // overshoots, so the camera lags into a corner and settles instead of ringing. The rational
        // approximation of exp(-x) is what keeps it stable at any frame time -- integrating a spring
        // as `velocity += -k * offset * dt` blows up the moment k * dt approaches 1, which on a
        // camera means a hitch throws you across the level.
        static void SmoothDamp(float3& current, float3& velocity, float3 target, float smoothTime, float dt)
        {
            smoothTime = smoothTime > 1.0e-4f ? smoothTime : 1.0e-4f;
            float omega = 2.0f / smoothTime;
            float x = omega * dt;
            float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            float3 offset = current - target;
            float3 temp = (velocity + offset * omega) * dt;
            velocity = (velocity - temp * omega) * decay;
            current = target + (offset + temp) * decay;
        }

        // Sits behind and above the car Systems::Driver published, on the springs above. Returns
        // false when there is nothing to follow, which is what hands the camera back to the free-fly
        // controls in Update.
        bool UpdateChaseCamera(World* world)
        {
            if (!options.followVehicle || !engineState.playing)
            {
                chaseEntity = chaseCameraNoTarget;
                return false;
            }

            uint packed = chaseCameraTarget.entity.load(std::memory_order_relaxed);
            World::Entity target;
            target.FromUInt(packed);
            if (packed == chaseCameraNoTarget || target.id >= (uint)world->entitySlots.size()
                || !target.IsValid() || !target.Has<Components::Transform>())
            {
                chaseEntity = chaseCameraNoTarget;
                return false;
            }

            float4x4 carMatrix = ComputeWorldMatrix(target);
            float3 carPosition = carMatrix[3].xyz;

            // Yaw only. Letting the rig inherit the car's pitch and roll is what turns a chase
            // camera into a washing machine the first time the car lands on its side.
            float3 flat = float3((float)carMatrix[2].x, 0.0f, (float)carMatrix[2].z);
            float flatLength = (float)length(flat);
            float3 carForward = flatLength > 1.0e-4f ? flat * (1.0f / flatLength) : float3(0, 0, 1);

            float scale = chaseCameraTarget.scale.load(std::memory_order_relaxed);
            float3 desiredPosition = carPosition - carForward * (options.followDistance * scale)
                                                 + float3(0, options.followHeight * scale, 0);
            float3 desiredLookAt = carPosition + float3(0, options.followLookAtHeight * scale, 0);

            // A different car (or the first frame of following one) is a cut, not a move: springing
            // across the level from wherever the free camera was left would take seconds.
            if (chaseEntity != packed)
            {
                chaseEntity = packed;
                chasePosition = desiredPosition;
                chaseLookAt = desiredLookAt;
                chasePositionVelocity = float3(0);
                chaseLookAtVelocity = float3(0);
            }
            else
            {
                float dt = Time::instance->deltaSeconds;
                SmoothDamp(chasePosition, chasePositionVelocity, desiredPosition, options.followSmoothTime, dt);
                // Half the smooth time: the aim has to settle before the position does, or the car
                // slides around the frame while the rig catches up behind it.
                SmoothDamp(chaseLookAt, chaseLookAtVelocity, desiredLookAt, options.followSmoothTime * 0.5f, dt);
            }

            auto& trans = camera.Get<Components::Transform>();
            trans.position = chasePosition;
            trans.scale = float3(1);

            float3 toTarget = chaseLookAt - chasePosition;
            float toTargetLength = (float)length(toTarget);
            if (toTargetLength < 1.0e-4f)
                return true;   // sitting on the car; keep last frame's rotation rather than NaN it

            // Rows are right / up / forward, the same left-handed basis the free camera rebuilds at
            // the end of Update, so right = up x forward and up = forward x right.
            float3 forward = toTarget * (1.0f / toTargetLength);
            float3 right = cross(float3(0, 1, 0), forward);
            float rightLength = (float)length(right);
            right = rightLength > 1.0e-4f ? right * (1.0f / rightLength) : float3(1, 0, 0);
            float3 up = cross(forward, right);

            float4x4 basis = float4x4(
                (float)right.x, (float)right.y, (float)right.z, 0,
                (float)up.x, (float)up.y, (float)up.z, 0,
                (float)forward.x, (float)forward.y, (float)forward.z, 0,
                0, 0, 0, 1);
            trans.rotation = MatrixToQuaternion(float3x3(basis));
            return true;
        }

        void Update(World* world) override
        {
            ZoneScoped;

            if (UpdateChaseCamera(world))
                return;

            auto& cam = camera.Get<Components::Camera>();
            auto& trans = camera.Get<Components::Transform>();
            Components::WorldMatrix mat;
            mat.matrix = Matrix(trans.position, trans.rotation, trans.scale);

            sensibility += IOs::instance->mouse.mouseWheel * .001f * sensibility;
            sensibility = clamp(float1(sensibility), 0.0025f, 1000.0f);

            float sensDt = sensibility * Time::instance->deltaSeconds;
            float sensLookDt = sensibilityLook;
            if (IOs::instance->keys.down[VK_W])// || IOs::instance->keys.down[VK_UP])
            {
                trans.position = trans.position + (float3(mat.matrix[2].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_S])// || IOs::instance->keys.down[VK_DOWN])
            {
                trans.position = trans.position - (float3(mat.matrix[2].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_D])// || IOs::instance->keys.down[VK_RIGHT])
            {
                trans.position = trans.position + (float3(mat.matrix[0].xyz) * sensDt);
            }
            if (IOs::instance->keys.down[VK_A])// || IOs::instance->keys.down[VK_LEFT])
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

        static void ParentUnder(World::Entity child, World::Entity parent)
        {
            if (!child.IsValid() || !child.Has<Components::Transform>()) return;
            if (!child.Has<Components::Parent>()) child.Add(Components::Parent::mask);
            child.Get<Components::Parent>().entity.Set(parent);
        }

        void Update(World* world) override
        {
            ZoneScoped;
            if (!engineState.playing) { UnloadAll(); return; }
            if (!FindCamera(world)) return;

            uint prefabBucket = Components::MaskToBucket(Components::Prefab::mask);
            uint trBucket = Components::MaskToBucket(Components::Transform::mask);

            uint instanceQueryIndex = world->Query(Components::Prefab::mask, 0, true);
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
                    World::Entity root = r.root;
                    if (root.IsValid())
                    {
                        ParentUnder(root, e);
                    }
                    else
                    {
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