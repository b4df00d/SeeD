#include "structs.hlsl"

cbuffer Custom : register(b2)
{
    HLSL::DebugParameters debugParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

struct VS_OUTPUT
{
	float4 pos: SV_POSITION;
	float4 color: TEXCOORD0;
};

// define the output of shader before the call to shader so that the parser can know it before compiling
// the comment after the SV_target is important
struct PS_OUTPUT
{
    float4 albedo : SV_Target0; //DXGI_FORMAT_R8G8B8A8_UNORM
};

#pragma debug VertexMain PixelMain

[RootSignature(SeeDRootSignature)]
VS_OUTPUT VertexMain(in uint vertexID : SV_VertexID, in uint instanceID : SV_InstanceID)
{
    VS_OUTPUT o;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::IndirectCommand> indirectCommands = ResourceDescriptorHeap[commonResourcesIndices.debugBufferHeapIndex];
    HLSL::IndirectCommand indirectCommand = indirectCommands[0];
    
    StructuredBuffer<HLSL::Vertex> debugVertices = ResourceDescriptorHeap[commonResourcesIndices.debugVerticesHeapIndex];
    HLSL::Vertex vertex = debugVertices[vertexID];
    
    float4 worldPos = float4(vertex.pos.xyz, 1);
    o.pos = mul(camera.viewProj, worldPos);
    o.color = float4(vertex.normal, 0);
    
    return o;
}

[RootSignature(SeeDRootSignature)]
PS_OUTPUT PixelMain(VS_OUTPUT inVerts)
{
    PS_OUTPUT o;
    
    o.albedo = inVerts.color;
    
    return o;
}