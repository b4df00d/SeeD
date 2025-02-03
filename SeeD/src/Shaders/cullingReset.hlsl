//#pragma once

#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Reset

[RootSignature(GlobalRootSignature)]
[numthreads(1, 1, 1)]
void Reset(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID, uint gid : SV_GroupID)
{
    uint instanceIndex = dtid;
    
    RWStructuredBuffer<uint> isntancesCounters = ResourceDescriptorHeap[cullingContext.instancesCounterIndex];
    isntancesCounters[0] = 0;
    
    RWStructuredBuffer<uint> meshletsCounters = ResourceDescriptorHeap[cullingContext.meshletsCounterIndex];
    meshletsCounters[0] = 0;
}