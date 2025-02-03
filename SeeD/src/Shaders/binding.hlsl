#pragma once
#ifndef BINDINGS_HLSL
//#include "structs.hlsl"

//https://learn.microsoft.com/en-us/windows/win32/direct3d12/dynamic-indexing-using-hlsl-5-1
//https://devblogs.microsoft.com/directx/in-the-works-hlsl-shader-model-6-6/

#define GlobalRootSignature "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED), " \
            "CBV(b0, space = 0), "\
            "CBV(b1, space = 0), "\
            "CBV(b2, space = 0), "\
			"CBV(b3, space = 0), "\
            "RootConstants(b4, num32BitConstants = 1), "\
            "RootConstants(b5, num32BitConstants = 1), "\
            "StaticSampler(s0, "\
                            "filter         = FILTER_MIN_MAG_LINEAR_MIP_POINT, "\
                            "addressU       = TEXTURE_ADDRESS_WRAP, "\
                            "addressV       = TEXTURE_ADDRESS_WRAP, "\
                            "addressW       = TEXTURE_ADDRESS_WRAP, "\
                            "mipLodBias     = 0.0f, "\
                            "maxAnisotropy  = 0, "\
                            "comparisonFunc = COMPARISON_NEVER, "\
                            "borderColor    = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, "\
                            "minLOD         = 0.0f, "\
                            "maxLOD         = 3.402823466e+38f, "\
                            "space          = 0, "\
                            "visibility     = SHADER_VISIBILITY_ALL)," \
            "StaticSampler(s1, "\
                            "filter         = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "\
                            "addressU       = TEXTURE_ADDRESS_BORDER, "\
                            "addressV       = TEXTURE_ADDRESS_BORDER, "\
                            "addressW       = TEXTURE_ADDRESS_BORDER, "\
                            "mipLodBias     = 0.0f, "\
                            "maxAnisotropy  = 0, "\
                            "comparisonFunc = COMPARISON_LESS, "\
                            "borderColor    = STATIC_BORDER_COLOR_OPAQUE_WHITE, "\
                            "minLOD         = 0.0f, "\
                            "maxLOD         = 3.402823466e+38f, "\
                            "space          = 0, "\
                            "visibility     = SHADER_VISIBILITY_ALL)," \
			"StaticSampler(s2, "\
                            "filter         = FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "\
                            "addressU       = TEXTURE_ADDRESS_BORDER, "\
                            "addressV       = TEXTURE_ADDRESS_BORDER, "\
                            "addressW       = TEXTURE_ADDRESS_BORDER, "\
                            "mipLodBias     = 0.0f, "\
                            "maxAnisotropy  = 0, "\
                            "comparisonFunc = COMPARISON_GREATER, "\
                            "borderColor    = STATIC_BORDER_COLOR_OPAQUE_WHITE, "\
                            "minLOD         = 0.0f, "\
                            "maxLOD         = 3.402823466e+38f, "\
                            "space          = 0, "\
                            "visibility     = SHADER_VISIBILITY_ALL)," \
            "StaticSampler(s3, "\
                            "filter         = FILTER_MIN_MAG_MIP_POINT, "\
                            "addressU       = TEXTURE_ADDRESS_WRAP, "\
                            "addressV       = TEXTURE_ADDRESS_WRAP, "\
                            "addressW       = TEXTURE_ADDRESS_WRAP, "\
                            "mipLodBias     = 0.0f, "\
                            "maxAnisotropy  = 0, "\
                            "comparisonFunc = COMPARISON_NEVER, "\
                            "borderColor    = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, "\
                            "minLOD         = 0.0f, "\
                            "maxLOD         = 3.402823466e+38f, "\
                            "space          = 0, "\
                            "visibility     = SHADER_VISIBILITY_ALL),"\
            "StaticSampler(s4, "\
                            "filter         = FILTER_MIN_MAG_LINEAR_MIP_POINT, "\
                            "addressU       = TEXTURE_ADDRESS_CLAMP, "\
                            "addressV       = TEXTURE_ADDRESS_CLAMP, "\
                            "addressW       = TEXTURE_ADDRESS_CLAMP, "\
                            "mipLodBias     = 0.0f, "\
                            "maxAnisotropy  = 0, "\
                            "comparisonFunc = COMPARISON_NEVER, "\
                            "borderColor    = STATIC_BORDER_COLOR_TRANSPARENT_BLACK, "\
                            "minLOD         = 0.0f, "\
                            "maxLOD         = 3.402823466e+38f, "\
                            "space          = 0, "\
                            "visibility     = SHADER_VISIBILITY_ALL)"

//---------------------------------------------------------------------------------------------
cbuffer ViewParams : register(b0)
{
    HLSL::CommonResourcesIndices commonResourcesIndices;
};
//---------------------------------------------------------------------------------------------
cbuffer CullingContext : register(b1)
{
    HLSL::CullingContext cullingContext;
};
//---------------------------------------------------------------------------------------------
cbuffer Nop1 : register(b2)
{
    uint notUsedYet1;
};
//---------------------------------------------------------------------------------------------
cbuffer Nop2 : register(b3)
{
    uint notUsedYet2;
};
//---------------------------------------------------------------------------------------------
cbuffer InstanceIndex : register(b4)
{
    uint instanceIndexIndirect;
};
//---------------------------------------------------------------------------------------------
cbuffer MeshletIndex : register(b5)
{
    uint meshletIndexIndirect;
};

#define BINDINGS_HLSL
#endif