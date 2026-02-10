// GPUMarchingCubes.h - GPU Marching Cubes management class

#pragma once
#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"

// Forward declarations
class FRDGBuilder;

// Mesh data output structure
struct FMCMeshData
{
    TArray<FVector> Vertices;
    TArray<FVector> Normals;
    TArray<FIntVector> Triangles;
    int32 VertexCount = 0;
    int32 TriangleCount = 0;
};

// GPU Marching Cubes manager class
class SDFCUT_API FGPUMarchingCubes
{
public:
    FGPUMarchingCubes();
    ~FGPUMarchingCubes();

    // Initialize with SDF dimensions
    void Initialize(const FIntVector& InSDFDimensions);

    // Execute 3-pass Marching Cubes algorithm
    void Execute(
        FRDGBuilder& GraphBuilder,
        FRDGTextureRef SDFTexture,
        const FVector3f& BoundsMin,
        const FVector3f& BoundsMax,
        float IsoValue,
        TFunction<void(const FMCMeshData&)> Callback);

    // CPU vertex welding to merge duplicate vertices
    static void WeldVertices(
        const TArray<FVector>& InVertices,
        const TArray<FIntVector>& InTriangles,
        TArray<FVector>& OutVertices,
        TArray<FIntVector>& OutTriangles,
        float WeldThreshold = 0.001f);

    // Check if initialized
    bool IsInitialized() const { return bInitialized; }

private:
    // SDF dimensions
    FIntVector SDFDimensions;
    int32 TotalCells = 0;

    // Maximum buffer sizes
    int32 MaxVertices = 0;
    int32 MaxTriangles = 0;

    // Initialization state
    bool bInitialized = false;

    // Helper to compute prefix sum on CPU (for small arrays)
    void ComputePrefixSumCPU(TArray<uint32>& Data);

    // Async readback handling
    void HandleReadback(
        FRHIGPUBufferReadback* VertexReadback,
        FRHIGPUBufferReadback* NormalReadback,
        FRHIGPUBufferReadback* TriangleReadback,
        FRHIGPUBufferReadback* TriCountReadback,
        int32 TotalVertexCount,
        TFunction<void(const FMCMeshData&)> Callback);
};
