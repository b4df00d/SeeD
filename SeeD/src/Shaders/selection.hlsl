#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Selection

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Selection(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    Texture2D<uint> objectID = ResourceDescriptorHeap[viewContext.objectIDIndex];
    
    RWStructuredBuffer<HLSL::SelectionResult> selectionResult = ResourceDescriptorHeap[editorContext.selectionResultIndex];
    
    uint2 selectPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);
    
    HLSL::SelectionResult res;
    res.objectID = objectID[selectPixel];
    selectionResult[0] = res;
}
