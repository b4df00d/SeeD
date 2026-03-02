#include "structs.hlsl"

cbuffer CustomParamters : register(b3)
{
    HLSL::StructuredCommandBufferParameters updateParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute Update Update
#pragma compute Init Init

void CopyBufferData(ByteAddressBuffer srcBuffer, uint srcIndex, RWByteAddressBuffer dstBuffer, uint dstIndex, uint stride)
{
    for (uint i = 0; i < stride; i++)
    {
        dstBuffer.Store(dstIndex + i, srcBuffer.Load(srcIndex + i));
    }
}
void CopyBufferData(RWByteAddressBuffer srcBuffer, uint srcIndex, RWByteAddressBuffer dstBuffer, uint dstIndex, uint stride)
{
    for (uint i = 0; i < stride; i++)
    {
        dstBuffer.Store(dstIndex + i, srcBuffer.Load(srcIndex + i));
    }
}

[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Init(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    RWStructuredBuffer<uint> bufferCounter = ResourceDescriptorHeap[updateParameters.bufferCounterIndex];
    bufferCounter[0] = 0;
}


[RootSignature(SeeDRootSignature)]
[numthreads(1, 1, 1)]
void Update(uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint3 gid : SV_GroupID)
{
    
    RWByteAddressBuffer commandBuffer = ResourceDescriptorHeap[updateParameters.commandIndex]; // CANNOT BE NON RW BUFFER !! THIS WILL NOT INDEX THE SAME WAY. IDK ....
    RWByteAddressBuffer buffer = ResourceDescriptorHeap[updateParameters.bufferIndex];
    RWByteAddressBuffer bufferCounter = ResourceDescriptorHeap[updateParameters.bufferCounterIndex];
    
    for (uint i = 0; i < updateParameters.commandCount; i++)
    {
        uint commandAddress = i * updateParameters.commandStride;
        uint commandType = commandBuffer.Load(commandAddress);
        uint commandIndex = commandBuffer.Load(commandAddress + 4);
        if (commandType == 0) // add
        {
            uint lastIndex = 0;
            bufferCounter.InterlockedAdd(0, 1, lastIndex);
            uint bufferAddress = lastIndex * updateParameters.bufferStride;
            CopyBufferData(commandBuffer, commandAddress + 16, buffer, bufferAddress, updateParameters.bufferStride);
        }
        else if (commandType == 1) // medify
        {
            uint bufferAddress = commandIndex * updateParameters.bufferStride;
            CopyBufferData(commandBuffer, commandAddress + 16, buffer, bufferAddress, updateParameters.bufferStride);
        }
        else if (commandType == 2) // remove
        {
            uint lastIndex = 0;
            bufferCounter.InterlockedAdd(0, -1, lastIndex);
            if (commandIndex != lastIndex)
            {
                // move last into removed slot on CPU and upload that slot
                uint bufferAddress = commandIndex * updateParameters.bufferStride;
                uint bufferLastAddress = lastIndex * updateParameters.bufferStride;
                CopyBufferData(buffer, bufferLastAddress, buffer, bufferAddress, updateParameters.bufferStride);
            }
        }
    }
}
