#include "structs.hlsl"


[outputtopology("triangle")]
[numthread(12, 1, 1)]
void Mesh(in uint groupThreadId : SV_GroupThreadID, out vertices MSVert outVerts[8], out indices uint3 outIndices[12])
{
    SetMeshOutputCounts(8, 12);
    
    if(groupThreadId < 8)
    {
        float4 pos = cubeVertices[groupThreadId];
        outVerts[groupThreadId].pos = mul(worldMat, pos);
        outVerts[groupThreadId].color = cubeColors[groupThreadId];
    }
    
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