//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Reset

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Reset(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    uint instanceIndex = dtid.x;
    
    RWStructuredBuffer<uint> isntancesCounters = ResourceDescriptorHeap[viewContext.instancesCounterIndex];
    isntancesCounters[0] = 0;
    
    RWStructuredBuffer<uint> meshletsCounters = ResourceDescriptorHeap[viewContext.meshletsCounterIndex];
    meshletsCounters[0] = 0;
}