#include "structs.hlsl"
#include "SharcCommon.h"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Lighting Lighting

// number of overdraws that maps to the hot end (red) of the heatmap
#define OVERDRAW_MAX 16.0

// blue -> green -> red ramp for t in [0,1]
float3 OverdrawColor(float t)
{
    t = saturate(t);
    float3 cold = float3(0, 0, 1);
    float3 mid = float3(0, 1, 0);
    float3 hot = float3(1, 0, 0);
    return t < 0.5 ? lerp(cold, mid, t * 2) : lerp(mid, hot, t * 2 - 1);
}

[RootSignature(SeeDRootSignature)]
[numthreads(16, 16, 1)]
void Lighting(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    if (dtid.x > viewContext.renderResolution.x || dtid.y > viewContext.renderResolution.y) return;
    
    RWTexture2D<float4> lighted = ResourceDescriptorHeap[rtParameters.lightedIndex];
    
    GBufferCameraData cd = GetGBufferCameraData(dtid.xy);
    
    SurfaceData s = GetSurfaceData(cd.pixel.xy);
        
    if (editorContext.albedo)
    {
        //s.albedo = saturate(cd.viewDistDiff * 2 - 0.5);
        lighted[dtid.xy] = s.albedo;
    }
    else if (editorContext.normals)
    {
        lighted[dtid.xy].xyz = normalize(s.normal.xyz) * 0.5 + 0.5;
    }
    else if (editorContext.overdraw)
    {
        RWTexture2D<uint> overdraw = ResourceDescriptorHeap[viewContext.overdrawIndex];
        uint count = overdraw[dtid.xy];
        float3 color = count == 0 ? float3(0, 0, 0) : OverdrawColor(count / OVERDRAW_MAX);
        lighted[dtid.xy] = float4(color, 1);
    }
    else if (editorContext.lighting)
    {
    }
    else if (editorContext.GIprobes)
    {
        HashGridParameters gridParameters;
        gridParameters.cameraPosition = cd.camera.worldPos.xyz;
        gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
        gridParameters.sceneScale = rtParameters.SHARCSceneScale;
        gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

        float3 color = HashGridDebugColoredHash(cd.worldPos, cd.worldNorm, gridParameters);
        //lighted[dtid.xy] = float4(color, 1);
            
        // Initialize SHARC parameters
        SharcParameters sharcParameters;
        {
            sharcParameters.gridParameters.cameraPosition = cd.camera.worldPos.xyz;
            sharcParameters.gridParameters.sceneScale = rtParameters.SHARCSceneScale;
            sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
            sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

            sharcParameters.hashMapData.capacity = rtParameters.SHARCEntriesNum;
            sharcParameters.hashMapData.hashEntriesBuffer = ResourceDescriptorHeap[rtParameters.SHARCHashEntriesBufferIndex];

        #if !SHARC_ENABLE_64_BIT_ATOMICS
            sharcParameters.hashMapData.lockBuffer = u_SharcLockBuffer;
        #endif // !SHARC_ENABLE_64_BIT_ATOMICS

            sharcParameters.accumulationBuffer = ResourceDescriptorHeap[rtParameters.SHARCAccumulationBufferIndex];
            sharcParameters.resolvedBuffer = ResourceDescriptorHeap[rtParameters.SHARCResolvedBufferIndex];
            sharcParameters.radianceScale = rtParameters.SHARCRadianceScale;
            sharcParameters.enableAntiFireflyFilter = rtParameters.SHARCEnableAntifirefly;
        }
        // Construct SharcHitData structure needed for creating a query point at this hit location
        SharcHitData sharcHitData;
        sharcHitData.positionWorld = cd.worldPos;
        sharcHitData.normalWorld = cd.worldNorm;
#if SHARC_MATERIAL_DEMODULATION
        sharcHitData.materialDemodulation = float3(1.0f, 1.0f, 1.0f);
        if (g_Global.sharcMaterialDemodulation)
        {
            float3 specularFAvg = s.specularF0 + (1.0f - s.specularF0) * 0.047619; // 1/21
            sharcHitData.materialDemodulation = max(s.diffuseAlbedo, 0.05f) + max(s.specularF0, 0.02f) * luminance(specularFAvg);
        }
#endif // SHARC_MATERIAL_DEMODULATION
#if SHARC_SEPARATE_EMISSIVE
        sharcHitData.emissive = s.emissiveColor;
#endif // SHARC_SEPARATE_EMISSIVE
            
            uint gridLevel = HashGridGetLevel(cd.worldPos, sharcParameters.gridParameters);
        float voxelSize = HashGridGetVoxelSize(gridLevel, sharcParameters.gridParameters);

        float3 sharcRadiance;
        if (SharcGetCachedRadiance(sharcParameters, sharcHitData, sharcRadiance, false))
        {
            lighted[dtid.xy] = float4(sharcRadiance, 1);
        }
    }
   
    if (editorContext.boundingVolumes) // daw boundingbox
    {
        int2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
        bool inRange = abs(length(debugPixel - int2(dtid.xy))) <= 0;
        if (inRange)
        {
            
            Texture2D<uint> instanceID = ResourceDescriptorHeap[viewContext.instanceIDIndex];
            uint sampleInstanceID = instanceID[debugPixel];
    
            StructuredBuffer<HLSL:: Instance > instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
            HLSL::Instance instance = instances[sampleInstanceID];
        
            StructuredBuffer<HLSL:: Mesh > meshes = ResourceDescriptorHeap[commonResourcesIndices.meshesHeapIndex];
            HLSL::Mesh mesh = meshes[instance.meshIndex];
        
            float4x4 worldMatrix = instance.unpack(instance.current);
            float3 center = mul(worldMatrix, float4(mesh.boundingSphere.xyz, 1)).xyz;
            float radius = instance.GetScale() * mesh.boundingSphere.w;
            DrawSphere(center, radius);
        }
    }
    if (editorContext.rays) // daw ray
    {
        RWStructuredBuffer<HLSL::GIReservoirCompressed> giReservoir = ResourceDescriptorHeap[rtParameters.giReservoirIndex];
        HLSL::GIReservoir r = UnpackGIReservoir(giReservoir[dtid.x + dtid.y * viewContext.renderResolution.x]);
        uint2 debugPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
        if (abs(length(debugPixel - dtid.xy)) < 6)
        {
            DrawLine(cd.offsetedWorldPos, cd.offsetedWorldPos + r.dir.xyz * min(r.dist, 2));
        }
    }
}
