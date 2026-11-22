#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Selection Selection

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Selection(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    // the objectID comes from the instance the visibility buffer recorded (common.hlsl PackVisibility)
    Texture2D<uint2> visibility = ResourceDescriptorHeap[viewContext.visibilityIndex];
    StructuredBuffer<HLSL::Instance> instances = ResourceDescriptorHeap[commonResourcesIndices.instancesHeapIndex];

    RWStructuredBuffer<HLSL::SelectionResult> selectionResult = ResourceDescriptorHeap[editorContext.selectionResultIndex];

    uint2 selectPixel = viewContext.mousePixel.xy / float2(viewContext.displayResolution.xy) * float2(viewContext.renderResolution.xy);

    // Pixels with no geometry (sky, background) never get a visibility write -- whatever the
    // render target holds there isn't a valid instance index, so bounds-check before using it as
    // one. 0xFFFFFFFF is World.h's entityInvalid as a raw uint (rev=0xF, id=0xFFFFFFF, all bits
    // set) -- matches what World::Entity::IsValid() (id != entityInvalid.id) treats as "no pick".
    HLSL::SelectionResult res;
    res.objectID = 0xFFFFFFFF;
    uint pickedInstance = visibility[selectPixel].x;
    if (pickedInstance < commonResourcesIndices.instanceCount)
        res.objectID = instances[pickedInstance].objectID;
    selectionResult[0] = res;
}
