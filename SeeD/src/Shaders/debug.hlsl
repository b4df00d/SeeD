#include "structs.hlsl"
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

#pragma debug Debug VertexMain PixelMain

[RootSignature(SeeDRootSignature)]
VS_OUTPUT VertexMain(in uint vertexID : SV_VertexID, in uint instanceID : SV_InstanceID)
{
    VS_OUTPUT o;
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[viewContext.cameraIndex];
    
    StructuredBuffer<HLSL::IndirectCommand> indirectCommands = ResourceDescriptorHeap[editorContext.debugBufferHeapIndex];
    HLSL::IndirectCommand indirectCommand = indirectCommands[0];
    
    StructuredBuffer<HLSL::Vertex> debugVertices = ResourceDescriptorHeap[editorContext.debugVerticesHeapIndex];
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
    
    o.albedo = float4(inVerts.color.xyz, 1);
    
    return o;
}

#pragma compute DebugInit DebugInit

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void DebugInit(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<uint> counter = ResourceDescriptorHeap[editorContext.debugVerticesCountHeapIndex];
    counter[0] = 0; // draw count
    counter[1] = 0; // vertex count
    
    RWStructuredBuffer<HLSL::IndirectCommand> debugIndirect = ResourceDescriptorHeap[editorContext.debugBufferHeapIndex];
    HLSL::IndirectCommand di = (HLSL::IndirectCommand) 0;
    
	//di.cbv = debugParameters.cbv;
	//di.index = 0;
	//di.stuff = 0;
	di.drawArguments.InstanceCount = 1;
	di.drawArguments.StartInstanceLocation = 0;
	//di.drawArguments.IndexCountPerInstance = 0;
	//di.drawArguments.StartIndexLocation = 0;
	//di.drawArguments.BaseVertexLocation = 0;
    di.drawArguments.VertexCountPerInstance = 0;
    di.drawArguments.StartVertexLocation = 0;
    
    debugIndirect[0] = di;
}
