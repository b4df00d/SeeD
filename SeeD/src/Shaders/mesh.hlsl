#include "structs.hlsl"
#include "binding.hlsl"

//#pragma gBuffer MeshMain PixelgBuffer
#pragma forward MeshMain PixelForward


[RootSignature(GlobalRootSignature)]
[outputtopology("triangle")]
[numthreads(12, 1, 1)]
void MeshMain(in uint groupThreadId : SV_GroupThreadID, out vertices HLSL::MSVert outVerts[8], out indices uint3 outIndices[12])
{
    SetMeshOutputCounts(8, 12);
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    //instance = instanceBuffer[0];
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[cameraIndex];
    
    if(groupThreadId < 8)
    {
        float3 v[8] =
        {
            float3(-0.2, 0.2, 0.2), float3(0.2, 0.2, 0.2), float3(-0.2, 0.2, 0.8), float3(0.2, 0.2, 0.8),
            float3(0.2, -0.2, 0.2), float3(0.2, -0.2, 0.2), float3(-0.2, -0.2, 0.2), float3(-0.2, -0.2, 0.8)
        };
        //StructuredBuffer<HLSL::Vertex> cubeVertices = ResourceDescriptorHeap[5];
        //float4 pos = float4(cubeVertices[groupThreadId].pos, 1);
        //outVerts[groupThreadId].pos = mul(drawCall.world, pos);
        float4 pos = float4(v[groupThreadId], 1);
        float4 worldPos = mul(instance.worldMatrix, pos);
        outVerts[groupThreadId].pos = mul(camera.viewProj, worldPos);
        outVerts[groupThreadId].color = worldPos.xyz * 0.5; // cubeColors[ groupThreadId];
    }
    
    uint3 i[12] =
    {
        uint3(0, 1, 2), uint3(3, 1, 2), uint3(3, 4, 5), uint3(6, 4, 5), uint3(6, 7, 8), uint3(9, 7, 8),
        uint3(10, 11, 2), uint3(1, 11, 2), uint3(10, 4, 7), uint3(3, 6, 2), uint3(8, 2, 6), uint3(5, 2, 9)
    };
    //StructuredBuffer<uint3> cubeIndices = ResourceDescriptorHeap[6];
    //outIndices[groupThreadId] = cubeIndices[groupThreadId];
    outIndices[groupThreadId] = i[groupThreadId];
}

struct PayloadData
{
    uint count;
};

[numthreads(32, 1, 1)]
void Amplification(in uint groupIndex : SV_GroupIndex)
{
    PayloadData data;
    data.count = 32;
    DispatchMesh(1, 1, 1, data);
}


struct PS_OUTPUT_FORWARD
{
    float4 albedo : SV_Target0;
    //uint entityID : SV_Target1;
};

PS_OUTPUT_FORWARD PixelForward(HLSL::MSVert inVerts)
{
    PS_OUTPUT_FORWARD o;
    
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];
    HLSL::Instance instance = instances[instanceIndex];
    o.albedo = float4(inVerts.color, 1);
    //o.entityID = 1;
    return o;
}

PS_OUTPUT_FORWARD PixelgBuffer()
{
    PS_OUTPUT_FORWARD o;
    o.albedo = 1;
    //o.entityID = 1;
    return o;
}