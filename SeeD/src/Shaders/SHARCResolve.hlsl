/*
 * Copyright (c) 2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "structs.hlsl"
#include "SharcCommon.h"

cbuffer CustomRT : register(b3)
{
    HLSL::RTParameters rtParameters;
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

#pragma compute sharcResolve sharcResolve

#define LINEAR_BLOCK_SIZE                   256
[RootSignature(SeeDRootSignature)]
[numthreads(LINEAR_BLOCK_SIZE, 1, 1)]
void sharcResolve(in uint2 did : SV_DispatchThreadID)
{
    
    StructuredBuffer<HLSL::Camera> cameras = ResourceDescriptorHeap[commonResourcesIndices.camerasHeapIndex];
    HLSL::Camera camera = cameras[0]; //viewContext.cameraIndex];
    
    SharcParameters sharcParameters;

    sharcParameters.gridParameters.cameraPosition = camera.worldPos.xyz;
    sharcParameters.gridParameters.sceneScale = rtParameters.SHARCSceneScale;
    sharcParameters.gridParameters.logarithmBase = SHARC_GRID_LOGARITHM_BASE;
    sharcParameters.gridParameters.levelBias = SHARC_GRID_LEVEL_BIAS;

    sharcParameters.hashMapData.capacity = rtParameters.SHARCEntriesNum;
    sharcParameters.hashMapData.hashEntriesBuffer = ResourceDescriptorHeap[rtParameters.SHARCHashEntriesBufferIndex];
    
    sharcParameters.accumulationBuffer = ResourceDescriptorHeap[rtParameters.SHARCAccumulationBufferIndex];
    sharcParameters.resolvedBuffer = ResourceDescriptorHeap[rtParameters.SHARCResolvedBufferIndex];
    sharcParameters.radianceScale = rtParameters.SHARCRadianceScale;

    SharcResolveParameters resolveParameters;
    resolveParameters.accumulationFrameNum = rtParameters.SHARCAccumulationFrameNum;
    resolveParameters.staleFrameNumMax = rtParameters.SHARCStaleFrameNum;
    resolveParameters.cameraPositionPrev = camera.previousWorldPos.xyz;
    resolveParameters.enableAntiFireflyFilter = rtParameters.SHARCEnableAntifirefly;

    SharcResolveEntry(did.x, sharcParameters, resolveParameters);
}