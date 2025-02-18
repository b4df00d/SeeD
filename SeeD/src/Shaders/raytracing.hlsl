#define RT_SHADER
#include "structs.hlsl"
#include "binding.hlsl"
#include "common.hlsl"


GlobalRootSignature SeeDRootSignatureRT =
{
    SeeDRootSignature
};

LocalRootSignature MyLocalRootSignature =
{
    "RootConstants( num32BitConstants = 4, b4 )"           // Cube constants        
};

TriangleHitGroup MyHitGroup =
{
    "", // AnyHit
    "ClosestHit", // ClosestHit
};

SubobjectToExportsAssociation MyLocalRootSignatureAssociation =
{
    "MyLocalRootSignature", // subobject name
    "MyHitGroup"             // export association 
};

RaytracingShaderConfig MyShaderConfig =
{
    16, // max payload size
    8 // max attribute size
};

RaytracingPipelineConfig MyPipelineConfig =
{
    1 // max trace recursion depth
};

#pragma raytracing 


[shader("raygeneration")]
void RayGen()
{
}

[shader("miss")]
void Miss(inout HLSL::HitInfo payload : SV_RayPayload)
{
}

[shader("closesthit")]
void ClosestHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
}

[shader("anyhit")]
void AnyHit(inout HLSL::HitInfo payload : SV_RayPayload, HLSL::Attributes attrib)
{
}