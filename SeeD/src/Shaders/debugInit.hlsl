#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute DebugInit

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
