#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Selection Selection

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Selection(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    // objectID gBuffer target was dropped: derive it from the instanceID target instead.
    Texture2D<uint> instanceID = ResourceDescriptorHeap[viewContext.instanceIDIndex];
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];

    RWStructuredBuffer<HLSL::SelectionResult> selectionResult = ResourceDescriptorHeap[editorContext.selectionResultIndex];

    uint2 selectPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);

    // Pixels with no geometry (sky, background) never get an instanceID write -- whatever the
    // render target holds there isn't a valid instance index, so bounds-check before using it as
    // one. 0xFFFFFFFF is World.h's entityInvalid as a raw uint (rev=0xF, id=0xFFFFFFF, all bits
    // set) -- matches what World::Entity::IsValid() (id != entityInvalid.id) treats as "no pick".
    HLSL::SelectionResult res;
    res.objectID = 0xFFFFFFFF;
    uint pickedInstance = instanceID[selectPixel];
    if (pickedInstance < commonResourcesIndices.instanceCount)
        res.objectID = instances[pickedInstance].objectID;
    selectionResult[0] = res;
}
