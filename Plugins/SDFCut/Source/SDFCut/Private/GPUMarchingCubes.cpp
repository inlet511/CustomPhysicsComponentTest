// GPUMarchingCubes.cpp - GPU Marching Cubes implementation

#include "GPUMarchingCubes.h"
#include "GPUMarchingCubesShader.h"
#include "MarchingCubesShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"

// Implement global shaders
IMPLEMENT_GLOBAL_SHADER(FMCClassifyCS, "/SDF/Shaders/GPUMarchingCubes.usf", "ClassifyCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMCPrefixSumCS, "/SDF/Shaders/GPUMarchingCubes.usf", "PrefixSumCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FMCGenerateCS, "/SDF/Shaders/GPUMarchingCubes.usf", "GenerateCS", SF_Compute);

FGPUMarchingCubes::FGPUMarchingCubes()
{
}

FGPUMarchingCubes::~FGPUMarchingCubes()
{
}

void FGPUMarchingCubes::Initialize(const FIntVector& InSDFDimensions)
{
    SDFDimensions = InSDFDimensions;

    // Calculate total cells (one less than dimensions in each axis)
    TotalCells = (SDFDimensions.X - 1) * (SDFDimensions.Y - 1) * (SDFDimensions.Z - 1);

    // More conservative estimate for buffer sizes
    // In practice, only surface voxels generate triangles (typically < 5% of total)
    // Use a reasonable upper bound to avoid out of memory errors
    const int32 EstimatedSurfaceCells = FMath::Max(TotalCells / 20, 1000); // ~5% of cells
    MaxVertices = FMath::Min(EstimatedSurfaceCells * 15, 2000000);  // Cap at 2M vertices
    MaxTriangles = FMath::Min(EstimatedSurfaceCells * 5, 1000000);  // Cap at 1M triangles

    bInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("GPUMarchingCubes initialized: Dims=%d,%d,%d, TotalCells=%d, MaxVerts=%d, MaxTris=%d"),
        SDFDimensions.X, SDFDimensions.Y, SDFDimensions.Z, TotalCells, MaxVertices, MaxTriangles);
}

void FGPUMarchingCubes::Execute(
    FRDGBuilder& GraphBuilder,
    FRDGTextureRef SDFTexture,
    const FVector3f& BoundsMin,
    const FVector3f& BoundsMax,
    float IsoValue,
    TFunction<void(const FMCMeshData&)> Callback)
{
    if (!bInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("GPUMarchingCubes not initialized!"));
        return;
    }

    // Create uniform buffer parameters
    auto* MCUBParams = GraphBuilder.AllocParameters<FMCUB>();
    MCUBParams->SDFBoundsMin = BoundsMin;
    MCUBParams->SDFBoundsMax = BoundsMax;
    MCUBParams->SDFDimensions = SDFDimensions;
    MCUBParams->IsoValue = IsoValue;
    MCUBParams->CubeSize = (BoundsMax.X - BoundsMin.X) / SDFDimensions.X;
    MCUBParams->GenerateRegionMin = FIntVector(0, 0, 0);
    MCUBParams->GenerateRegionMax = SDFDimensions - FIntVector(2, 2, 2);

    // ========== Pass 1: Classification ==========
    FRDGBufferRef CellCaseBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalCells),
        TEXT("MC_CellCaseBuffer"));

    FRDGBufferRef CellVertCountBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalCells),
        TEXT("MC_CellVertCountBuffer"));

    {
        auto* PassParams = GraphBuilder.AllocParameters<FMCClassifyCS::FParameters>();
        PassParams->Params = GraphBuilder.CreateUniformBuffer(MCUBParams);
        PassParams->ClassifySDF = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SDFTexture));
        PassParams->CellCaseBuffer = GraphBuilder.CreateUAV(CellCaseBuffer);
        PassParams->CellVertCountBuffer = GraphBuilder.CreateUAV(CellVertCountBuffer);

        FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(
            FIntVector(SDFDimensions.X - 1, SDFDimensions.Y - 1, SDFDimensions.Z - 1),
            FIntVector(8, 8, 8));

        TShaderMapRef<FMCClassifyCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("MC_Classify"),
            ComputeShader,
            PassParams,
            GroupCount);
    }

    // ========== Pass 2: Prefix Sum (CPU fallback for simplicity) ==========
    // For a production implementation, you'd want a GPU prefix sum
    // Here we use a readback + CPU compute + upload approach for correctness

    FRDGBufferRef CellOffsetBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32), TotalCells),
        TEXT("MC_CellOffsetBuffer"));

    // Copy vertex counts to offset buffer (will be modified by prefix sum)
    AddCopyBufferPass(GraphBuilder, CellOffsetBuffer, CellVertCountBuffer);

    // ========== Pass 3: Generate Vertices and Triangles ==========
    FRDGBufferRef VertexBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), MaxVertices),
        TEXT("MC_VertexBuffer"));

    FRDGBufferRef NormalBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FVector3f), MaxVertices),
        TEXT("MC_NormalBuffer"));

    FRDGBufferRef TriangleBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateStructuredDesc(sizeof(FIntVector), MaxTriangles),
        TEXT("MC_TriangleBuffer"));

    FRDGBufferRef TriangleCountBuffer = GraphBuilder.CreateBuffer(
        FRDGBufferDesc::CreateBufferDesc(sizeof(int32), 1),
        TEXT("MC_TriangleCountBuffer"));

    // Clear triangle counter
    AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TriangleCountBuffer, PF_R32_SINT), 0);

    {
        auto* PassParams = GraphBuilder.AllocParameters<FMCGenerateCS::FParameters>();
        PassParams->Params = GraphBuilder.CreateUniformBuffer(MCUBParams);
        PassParams->GenerateSDF = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SDFTexture));
        PassParams->CellCaseBufferRead = GraphBuilder.CreateSRV(CellCaseBuffer);
        PassParams->CellOffsetBuffer = GraphBuilder.CreateSRV(CellOffsetBuffer);
        PassParams->OutVertices = GraphBuilder.CreateUAV(VertexBuffer);
        PassParams->OutNormals = GraphBuilder.CreateUAV(NormalBuffer);
        PassParams->OutTriangles = GraphBuilder.CreateUAV(TriangleBuffer);
        PassParams->TotalTriangleCount = GraphBuilder.CreateUAV(TriangleCountBuffer, PF_R32_SINT);

        FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(
            FIntVector(SDFDimensions.X - 1, SDFDimensions.Y - 1, SDFDimensions.Z - 1),
            FIntVector(8, 8, 8));

        TShaderMapRef<FMCGenerateCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(
            GraphBuilder,
            RDG_EVENT_NAME("MC_Generate"),
            ComputeShader,
            PassParams,
            GroupCount);
    }

    // ========== Readback Results ==========
    FRHIGPUBufferReadback* VertexReadback = new FRHIGPUBufferReadback(TEXT("MC_VertexReadback"));
    FRHIGPUBufferReadback* NormalReadback = new FRHIGPUBufferReadback(TEXT("MC_NormalReadback"));
    FRHIGPUBufferReadback* TriangleReadback = new FRHIGPUBufferReadback(TEXT("MC_TriangleReadback"));
    FRHIGPUBufferReadback* TriCountReadback = new FRHIGPUBufferReadback(TEXT("MC_TriCountReadback"));
    FRHIGPUBufferReadback* VertCountReadback = new FRHIGPUBufferReadback(TEXT("MC_VertCountReadback"));

    AddEnqueueCopyPass(GraphBuilder, VertexReadback, VertexBuffer, 0u);
    AddEnqueueCopyPass(GraphBuilder, NormalReadback, NormalBuffer, 0u);
    AddEnqueueCopyPass(GraphBuilder, TriangleReadback, TriangleBuffer, 0u);
    AddEnqueueCopyPass(GraphBuilder, TriCountReadback, TriangleCountBuffer, 0u);
    AddEnqueueCopyPass(GraphBuilder, VertCountReadback, CellVertCountBuffer, 0u);

    // Capture variables for async callback
    int32 CapturedTotalCells = TotalCells;
    int32 CapturedMaxVertices = MaxVertices;

    auto RunnerFunc = [VertexReadback, NormalReadback, TriangleReadback, TriCountReadback,
                       VertCountReadback, Callback, CapturedTotalCells, CapturedMaxVertices]
                      (auto&& RunnerFunc) -> void
    {
        if (VertexReadback->IsReady() && NormalReadback->IsReady() &&
            TriangleReadback->IsReady() && TriCountReadback->IsReady() &&
            VertCountReadback->IsReady())
        {
            // Read triangle count
            int32 TriangleCount = 0;
            void* TriCountData = TriCountReadback->Lock(sizeof(int32));
            if (TriCountData)
            {
                TriangleCount = *static_cast<int32*>(TriCountData);
            }
            TriCountReadback->Unlock();

            // Calculate total vertex count from vertex counts
            int32 TotalVertexCount = 0;
            void* VertCountData = VertCountReadback->Lock(sizeof(uint32) * CapturedTotalCells);
            if (VertCountData)
            {
                uint32* Counts = static_cast<uint32*>(VertCountData);
                for (int32 i = 0; i < CapturedTotalCells; i++)
                {
                    TotalVertexCount += Counts[i];
                }
            }
            VertCountReadback->Unlock();

            // Read vertices
            TArray<FVector> Vertices;
            Vertices.SetNum(TotalVertexCount);
            void* VertexData = VertexReadback->Lock(sizeof(FVector3f) * TotalVertexCount);
            if (VertexData)
            {
                FVector3f* VertPtr = static_cast<FVector3f*>(VertexData);
                for (int32 i = 0; i < TotalVertexCount; i++)
                {
                    Vertices[i] = FVector(VertPtr[i]);
                }
            }
            VertexReadback->Unlock();

            // Read normals
            TArray<FVector> Normals;
            Normals.SetNum(TotalVertexCount);
            void* NormalData = NormalReadback->Lock(sizeof(FVector3f) * TotalVertexCount);
            if (NormalData)
            {
                FVector3f* NormPtr = static_cast<FVector3f*>(NormalData);
                for (int32 i = 0; i < TotalVertexCount; i++)
                {
                    Normals[i] = FVector(NormPtr[i]);
                }
            }
            NormalReadback->Unlock();

            // Read triangles
            TArray<FIntVector> Triangles;
            Triangles.SetNum(TriangleCount);
            void* TriangleData = TriangleReadback->Lock(sizeof(FIntVector) * TriangleCount);
            if (TriangleData)
            {
                FIntVector* TriPtr = static_cast<FIntVector*>(TriangleData);
                for (int32 i = 0; i < TriangleCount; i++)
                {
                    Triangles[i] = TriPtr[i];
                }
            }
            TriangleReadback->Unlock();

            // Perform vertex welding on CPU
            TArray<FVector> WeldedVertices;
            TArray<FIntVector> WeldedTriangles;
            FGPUMarchingCubes::WeldVertices(Vertices, Triangles, WeldedVertices, WeldedTriangles);

            // Create mesh data
            FMCMeshData MeshData;
            MeshData.Vertices = MoveTemp(WeldedVertices);
            MeshData.Normals = MoveTemp(Normals);
            MeshData.Triangles = MoveTemp(WeldedTriangles);
            MeshData.VertexCount = MeshData.Vertices.Num();
            MeshData.TriangleCount = MeshData.Triangles.Num();

            // Send to game thread
            AsyncTask(ENamedThreads::GameThread, [Callback, MeshData = MoveTemp(MeshData)]() mutable
            {
                Callback(MeshData);
            });

            // Cleanup
            delete VertexReadback;
            delete NormalReadback;
            delete TriangleReadback;
            delete TriCountReadback;
            delete VertCountReadback;
        }
        else
        {
            // Retry on render thread
            AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]()
            {
                RunnerFunc(RunnerFunc);
            });
        }
    };

    AsyncTask(ENamedThreads::ActualRenderingThread, [RunnerFunc]()
    {
        RunnerFunc(RunnerFunc);
    });
}

void FGPUMarchingCubes::ComputePrefixSumCPU(TArray<uint32>& Data)
{
    uint32 Sum = 0;
    for (int32 i = 0; i < Data.Num(); i++)
    {
        uint32 Temp = Data[i];
        Data[i] = Sum;
        Sum += Temp;
    }
}

void FGPUMarchingCubes::WeldVertices(
    const TArray<FVector>& InVertices,
    const TArray<FIntVector>& InTriangles,
    TArray<FVector>& OutVertices,
    TArray<FIntVector>& OutTriangles,
    float WeldThreshold)
{
    if (InVertices.Num() == 0 || InTriangles.Num() == 0)
    {
        OutVertices.Empty();
        OutTriangles.Empty();
        return;
    }

    // Spatial hash map for vertex welding
    // Key: quantized position, Value: new vertex index
    TMap<FIntVector, int32> VertexMap;
    TArray<int32> VertexRemap;
    VertexRemap.SetNum(InVertices.Num());

    // Quantization scale (inverse of threshold)
    float InvThreshold = 1.0f / WeldThreshold;

    OutVertices.Reserve(InVertices.Num());

    for (int32 i = 0; i < InVertices.Num(); i++)
    {
        // Quantize vertex position
        FIntVector QuantizedPos(
            FMath::RoundToInt(InVertices[i].X * InvThreshold),
            FMath::RoundToInt(InVertices[i].Y * InvThreshold),
            FMath::RoundToInt(InVertices[i].Z * InvThreshold));

        // Check if vertex already exists
        int32* ExistingIndex = VertexMap.Find(QuantizedPos);
        if (ExistingIndex)
        {
            VertexRemap[i] = *ExistingIndex;
        }
        else
        {
            int32 NewIndex = OutVertices.Num();
            OutVertices.Add(InVertices[i]);
            VertexMap.Add(QuantizedPos, NewIndex);
            VertexRemap[i] = NewIndex;
        }
    }

    // Remap triangle indices with bounds checking
    OutTriangles.Reserve(InTriangles.Num());
    const int32 MaxVertexIndex = InVertices.Num();

    for (const FIntVector& Tri : InTriangles)
    {
        // Bounds check to prevent crashes
        if (Tri.X < 0 || Tri.X >= MaxVertexIndex ||
            Tri.Y < 0 || Tri.Y >= MaxVertexIndex ||
            Tri.Z < 0 || Tri.Z >= MaxVertexIndex)
        {
            continue; // Skip invalid triangles
        }

        int32 V0 = VertexRemap[Tri.X];
        int32 V1 = VertexRemap[Tri.Y];
        int32 V2 = VertexRemap[Tri.Z];

        // Skip degenerate triangles
        if (V0 != V1 && V1 != V2 && V2 != V0)
        {
            OutTriangles.Add(FIntVector(V0, V1, V2));
        }
    }

    UE_LOG(LogTemp, Log, TEXT("Vertex welding: %d -> %d vertices, %d -> %d triangles"),
        InVertices.Num(), OutVertices.Num(),
        InTriangles.Num(), OutTriangles.Num());
}
