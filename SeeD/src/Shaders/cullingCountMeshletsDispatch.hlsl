//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute CountMeshletsDispatch

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void CountMeshletsDispatch(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    
    RWStructuredBuffer<uint> instancesCounters = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    RWStructuredBuffer<HLSL::InstanceCullingDispatch> instancesCulledArgs = ResourceDescriptorHeap[viewContext.instancesCulledArgsIndex];
    
    instancesCounters[0] = 1;
    HLSL::InstanceCullingDispatch idc = (HLSL::InstanceCullingDispatch) 0;
    idc.instanceIndex = 0;
    idc.meshletIndex = 0;
    idc.ThreadGroupCountX = ceil(instancesCounters[1] / (GROUPED_CULLING_THREADS * 1.0f));
    idc.ThreadGroupCountY = 1;
    idc.ThreadGroupCountZ = 1;
    instancesCulledArgs[0] = idc;
}