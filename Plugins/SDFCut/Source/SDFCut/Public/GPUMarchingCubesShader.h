// GPUMarchingCubesShader.h - Shader bindings for 3-pass GPU Marching Cubes

#pragma once
#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphResources.h"
#include "MarchingCubesShader.h"

// ============================================================================
// Classification Shader (Pass 1)
// ============================================================================

class FMCClassifyCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMCClassifyCS);
    SHADER_USE_PARAMETER_STRUCT(FMCClassifyCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMCUB, Params)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, ClassifySDF)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellCaseBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, CellVertCountBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

// ============================================================================
// Prefix Sum Shader (Pass 2)
// ============================================================================

class FMCPrefixSumCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMCPrefixSumCS);
    SHADER_USE_PARAMETER_STRUCT(FMCPrefixSumCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>, PrefixSumBuffer)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};

// ============================================================================
// Generate Shader (Pass 3)
// ============================================================================

class FMCGenerateCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FMCGenerateCS);
    SHADER_USE_PARAMETER_STRUCT(FMCGenerateCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMCUB, Params)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, GenerateSDF)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellCaseBufferRead)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, CellOffsetBuffer)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, OutVertices)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float3>, OutNormals)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<int3>, OutTriangles)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<int>, TotalTriangleCount)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }
};
