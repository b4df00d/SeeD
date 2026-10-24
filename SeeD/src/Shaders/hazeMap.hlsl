#include "structs.hlsl"

cbuffer CustomHaze : register(b3)
{
    uint4 hazeParams; // x: haze layer index, y / z: Clear only, the UAVs of the layer's two maps
};
#define CUSTOM_ROOT_BUFFER_1

#include "binding.hlsl"
#include "common.hlsl"

// Surface haze map resolve (structs.hlsl HazeLayer), dispatched per active layer by GBuffers::Render
// after the draws: folds this frame's splats (the GBuffer pixel shaders' InterlockedMax) into the
// layer's persistent map that FogMedium samples next frame. ClearSplat then empties the splats (a
// separate dispatch: the blur reads the neighbours' splats).
//
// The splats are sparse (one pixel per 4x4 tile per frame), so they are first averaged over the
// layer's blurRadius, which also fills the holes between them. A texel keeps the highest surface
// seen: a frame without a splat, or with only a lower one (the ground under a canopy whose leaves
// were not picked this frame), says nothing and the stored surface stays. It is replaced by a lower
// one only once it went HAZE_STALE_FRAMES without being seen again (the object moved or was removed).

#define HAZE_STALE_FRAMES       60      // frames without confirmation before a lower splat replaces the stored surface
#define HAZE_HEIGHT_TOLERANCE   0.05f   // m: a splat this close below the stored surface confirms it
#define HAZE_AGE_MAX            65535u

#pragma compute Resolve HazeMapResolve
#pragma compute ClearSplat HazeMapClearSplat
#pragma compute Clear HazeMapClear

// world cell held by a texel of the window [origin, origin + resolution)
int2 HazeWindowCell(int2 local, int2 origin, uint resolution)
{
    int res = int(resolution);
    return origin + (((local - origin) % res) + res) % res;
}

HLSL::HazeLayer GetHazeLayer()
{
    StructuredBuffer<HLSL::HazeLayer> layers = ResourceDescriptorHeap[viewContext.hazeMap.layersIndex];
    return layers[hazeParams.x];
}

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void HazeMapResolve(uint3 dtid : SV_DispatchThreadID)
{
    HLSL::HazeLayer layer = GetHazeLayer();
    if (dtid.x >= layer.resolution * HLSL::HazeMapLevels || dtid.y >= layer.resolution)
        return;
    uint level = dtid.x / layer.resolution;
    int2 local = int2(dtid.x - level * layer.resolution, dtid.y);
    int2 origin = HazeOrigin(layer.origin, level);
    int2 cell = HazeWindowCell(local, origin, layer.resolution);

    RWTexture2D<uint> splat = ResourceDescriptorHeap[layer.splatIndex];
    Texture2D<uint2> history = ResourceDescriptorHeap[layer.readIndex];
    RWTexture2D<uint2> output = ResourceDescriptorHeap[layer.writeIndex];

    // tent-weighted average of the splatted neighbours (in this frame's window only)
    int radius = int(layer.blurRadius);
    float heightSum = 0.0f;
    float weightSum = 0.0f;
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            uint2 texel;
            if (!HazeTexel(cell + int2(x, y), origin, level, layer.resolution, texel))
                continue;
            uint s = splat[texel];
            if (s == 0)
                continue;
            float w = float((radius + 1 - abs(x)) * (radius + 1 - abs(y)));
            heightSum += w * float(s);
            weightSum += w;
        }
    }
    uint current = weightSum > 0.0f ? max(uint(heightSum / weightSum + 0.5f), 1u) : 0;

    // the window scrolled: a texel that now holds another world cell starts empty
    bool sameCell = all(cell == HazeWindowCell(local, HazeOrigin(layer.previousOrigin, level), layer.resolution));
    uint2 previous = sameCell ? history[dtid.xy] : uint2(0, HAZE_AGE_MAX);

    const int tolerance = int(HAZE_HEIGHT_TOLERANCE / HLSL::HazeHeightRange * 16777215.0f);
    int currentHeight = int(current);
    int previousHeight = int(previous.x);
    uint2 result;
    if (current == 0)                                   // not seen this frame
        result = uint2(previous.x, min(previous.y + 1, HAZE_AGE_MAX));
    else if (previous.x == 0 || currentHeight >= previousHeight || previous.y >= HAZE_STALE_FRAMES)
        result = uint2(current, 0);                     // new, higher, or replacing a stale surface
    else if (currentHeight >= previousHeight - tolerance)
        result = uint2(previous.x, 0);                  // confirmed
    else                                                // a lower surface: wait for the stored one to go stale
        result = uint2(previous.x, min(previous.y + 1, HAZE_AGE_MAX));
    output[dtid.xy] = result;
}

[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void HazeMapClearSplat(uint3 dtid : SV_DispatchThreadID)
{
    HLSL::HazeLayer layer = GetHazeLayer();
    if (dtid.x >= layer.resolution * HLSL::HazeMapLevels || dtid.y >= layer.resolution)
        return;
    RWTexture2D<uint> splat = ResourceDescriptorHeap[layer.splatIndex];
    splat[dtid.xy] = 0;
}

// The layer's maps were just (re)created, uninitialized: clears the splats and both maps
[RootSignature(SeeDRootSignature)]
[numthreads(8, 8, 1)]
void HazeMapClear(uint3 dtid : SV_DispatchThreadID)
{
    HLSL::HazeLayer layer = GetHazeLayer();
    if (dtid.x >= layer.resolution * HLSL::HazeMapLevels || dtid.y >= layer.resolution)
        return;
    RWTexture2D<uint> splat = ResourceDescriptorHeap[layer.splatIndex];
    RWTexture2D<uint2> map0 = ResourceDescriptorHeap[hazeParams.y];
    RWTexture2D<uint2> map1 = ResourceDescriptorHeap[hazeParams.z];
    splat[dtid.xy] = 0;
    map0[dtid.xy] = uint2(0, HAZE_AGE_MAX);
    map1[dtid.xy] = uint2(0, HAZE_AGE_MAX);
}
