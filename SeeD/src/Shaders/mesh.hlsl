#pragma forward MeshMain PixelForward
#pragma gBuffer MeshMain PixelgBuffer

#include "structs.hlsl"
#include "binding.hlsl"


[RootSignature(GlobalRootSignature)]
[outputtopology("triangle")]
[numthreads(12, 1, 1)]
void MeshMain(in uint groupThreadId : SV_GroupThreadID, out vertices HLSL::MSVert outVerts[8], out indices uint3 outIndices[12])
{
    SetMeshOutputCounts(8, 12);
    
    if(groupThreadId < 8)
    {
        StructuredBuffer<HLSL::Vertex> cubeVertices = ResourceDescriptorHeap[5];
        float4 pos = float4(cubeVertices[groupThreadId].pos, 1);
        outVerts[groupThreadId].pos = mul(drawCall.world, pos);
        outVerts[groupThreadId].color = float4(1, 0, 1, 1);// cubeColors[ groupThreadId];
    }
    
    StructuredBuffer<uint3> cubeIndices = ResourceDescriptorHeap[6];
    outIndices[groupThreadId] = cubeIndices[groupThreadId];
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

float4 PixelForward()
{
    return 1;
}