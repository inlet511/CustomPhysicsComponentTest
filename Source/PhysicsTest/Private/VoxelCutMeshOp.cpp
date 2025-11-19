// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCutMeshOp.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshBoolean.h"
#include "Generators/MarchingCubes.h"

UE_DISABLE_OPTIMIZATION

using namespace UE::Geometry;

// FMaVoxelData 方法实现
void FMaVoxelData::Serialize(FArchive& Ar)
{
    Ar << Voxels;
    Ar << GridOrigin;
    Ar << GridSize;
    Ar << VoxelSize;
    SerializeAxisAlignedBox3d(Ar, WorldBounds);
}

void FMaVoxelData::Reset()
{
    Voxels.Reset();
    GridSize = 0;
    VoxelSize = 0.0;
    WorldBounds = FAxisAlignedBox3d::Empty();
}

int32 FMaVoxelData::GetVoxelIndex(int32 X, int32 Y, int32 Z) const
{
    return Z * GridSize * GridSize + Y * GridSize + X;
}

FVector3d FMaVoxelData::GetVoxelWorldPosition(int32 X, int32 Y, int32 Z) const
{
    return GridOrigin + FVector3d(X, Y, Z) * VoxelSize;
}

FIntVector FMaVoxelData::WorldToVoxel(const FVector3d& WorldPos) const
{
    FVector3d LocalPos = WorldPos - GridOrigin;
    return FIntVector(
        FMath::FloorToInt(LocalPos.X / VoxelSize),
        FMath::FloorToInt(LocalPos.Y / VoxelSize),
        FMath::FloorToInt(LocalPos.Z / VoxelSize)
    );
}


void FVoxelCutMeshOp::SetTransform(const FTransformSRT3d& Transform)
{
    ResultTransform = Transform;
}

void FVoxelCutMeshOp::CalculateResult(FProgressCancel* Progress)
{
    if (Progress && Progress->Cancelled())
    {
        return;
    }
    
    if (!bVoxelDataInitialized)
    {
        // 检查是否有持久化体素数据
        if (!PersistentVoxelData.IsValid() || !TargetMesh.IsValid())
        {        
            // 首次运行：初始化体素数据
            if (!InitializeVoxelData(Progress))
            {
                return;
            }
            bVoxelDataInitialized = true;
            UE_LOG(LogTemp, Warning, TEXT("VoxelData Initialized"));
        }
    }
    
    UE_LOG(LogTemp, Warning, TEXT("IncrementalCut Entry"));
    // 增量更新：基于现有体素数据进行切削
    if (!IncrementalCut(Progress))
    {
        return;
    }
    

    if (Progress && Progress->Cancelled())
    {
        return;
    }

    // 生成最终网格
    ConvertVoxelsToMesh(*PersistentVoxelData, Progress);
}

bool FVoxelCutMeshOp::InitializeVoxelData(FProgressCancel* Progress)
{
    if (!TargetMesh)
    {
        return false;
    }
    UE_LOG(LogTemp, Warning, TEXT("InitializeVoxelData"));
    
    // 创建新的体素数据容器
    if (!PersistentVoxelData.IsValid())
    {
        PersistentVoxelData = MakeShared<FMaVoxelData>();
        PersistentVoxelData->VoxelSize = VoxelSize;
    }

    // 变换目标网格到世界空间
    FDynamicMesh3 TransformedTargetMesh = *TargetMesh;
    MeshTransforms::ApplyTransform(TransformedTargetMesh, TargetTransform, true);

    // 计算平均平移作为结果变换的中心
    FVector3d AverageTranslation = TargetTransform.GetTranslation();
    ResultTransform = FTransformSRT3d(AverageTranslation);

    // 体素化目标网格
    return VoxelizeMesh(TransformedTargetMesh, FTransform::Identity, 
                       *PersistentVoxelData, Progress);
}

bool FVoxelCutMeshOp::IncrementalCut(FProgressCancel* Progress)
{
    if (!PersistentVoxelData.IsValid() || !CutToolMesh)
    {
        return false;
    }

    if (bIncrementalUpdate)
    {
        // 局部更新：只更新受刀具影响的区域
        UpdateLocalRegion(*PersistentVoxelData, *CutToolMesh, 
                         CutToolTransform, Progress);
    }
    else
    {
        // 全局更新：体素化刀具并执行布尔运算
        FMaVoxelData ToolVoxelData;
        if (VoxelizeMesh(*CutToolMesh, CutToolTransform, ToolVoxelData, Progress))
        {
            PerformBooleanCut(*PersistentVoxelData, ToolVoxelData, Progress);
        }
    }

    return !(Progress && Progress->Cancelled());
}

double GetDistanceToMesh(const FDynamicMeshAABBTree3& Spatial, const FVector3d& LocalPoint)
{
    double NearestDistSqr;

    
    
    int NearestTriID = Spatial.FindNearestTriangle(LocalPoint, NearestDistSqr);
    
    if (NearestTriID != IndexConstants::InvalidID)
    {
        return FMathd::Sqrt(NearestDistSqr);
    }
    
    return TNumericLimits<double>::Max();
}


bool FVoxelCutMeshOp::VoxelizeMesh(const FDynamicMesh3& Mesh, const FTransform& Transform, 
                                 FMaVoxelData& VoxelData, FProgressCancel* Progress)
{
    if (Mesh.TriangleCount() == 0)
    {
        return false;
    }

    // 计算网格边界框
    FAxisAlignedBox3d LocalBounds = Mesh.GetBounds();
    FAxisAlignedBox3d WorldBounds(LocalBounds,Transform);
    
    
    // 设置体素参数
    FVector3d Extent = WorldBounds.Max - WorldBounds.Min;
    double MaxExtent = Extent.GetMax();
    
    VoxelData.VoxelSize = VoxelSize;
    VoxelData.GridSize = FMath::CeilToInt(MaxExtent / VoxelSize) + 10;
    VoxelData.GridOrigin = WorldBounds.Min - FVector3d(5.0 * VoxelSize);
    VoxelData.WorldBounds = WorldBounds;
    
    // 初始化体素网格
    VoxelData.Voxels.SetNumZeroed(VoxelData.GridSize * VoxelData.GridSize * VoxelData.GridSize);
    
    // 创建AABB树用于快速查询
    FDynamicMesh3 TransformedMesh = Mesh;
    MeshTransforms::ApplyTransform(TransformedMesh, Transform, true);
    FDynamicMeshAABBTree3 Spatial(&TransformedMesh);
    TFastWindingTree<FDynamicMesh3> Winding(&Spatial);
    
    // 并行填充体素网格（提高性能）
    ParallelFor(VoxelData.GridSize, [&](int32 Z)
    {
        if (Progress && Progress->Cancelled())
        {
            return;
        }
        
        for (int32 Y = 0; Y < VoxelData.GridSize; Y++)
        {
            for (int32 X = 0; X < VoxelData.GridSize; X++)
            {
                FVector3d VoxelPos = VoxelData.GetVoxelWorldPosition(X, Y, Z);
                FVector3d LocalVoxelPos = Transform.InverseTransformPosition(VoxelPos);
                double Distance = GetDistanceToMesh(Spatial,LocalVoxelPos);
                bool bInside = Winding.IsInside(VoxelPos);
                
                int32 Index = VoxelData.GetVoxelIndex(X, Y, Z);
                VoxelData.Voxels[Index] = bInside ? -(float)Distance : (float)Distance;
            }
        }
    });
    
    return true;
}


float SampleToolVoxelAtPosition(const FMaVoxelData& ToolVoxels, const FVector3d& WorldPos)
{
    // 将世界坐标转换为工具体素网格的局部坐标
    FVector3d LocalPos = (WorldPos - ToolVoxels.GridOrigin) / ToolVoxels.VoxelSize;
    
    int32 X = FMath::FloorToInt(LocalPos.X);
    int32 Y = FMath::FloorToInt(LocalPos.Y);
    int32 Z = FMath::FloorToInt(LocalPos.Z);
    
    // 检查是否在工具体素网格范围内
    if (X < 0 || X >= ToolVoxels.GridSize || 
        Y < 0 || Y >= ToolVoxels.GridSize || 
        Z < 0 || Z >= ToolVoxels.GridSize)
    {
        return 1.0f; // 外部
    }
    
    int32 Index = ToolVoxels.GetVoxelIndex(X, Y, Z);
    return ToolVoxels.Voxels[Index];
}

void FVoxelCutMeshOp::PerformBooleanCut(FMaVoxelData& TargetVoxels, const FMaVoxelData& ToolVoxels, 
                                       FProgressCancel* Progress)
{

    // 计算两个体素网格的世界空间重叠区域
    FAxisAlignedBox3d TargetWorldBounds(
        TargetVoxels.GridOrigin, 
        TargetVoxels.GridOrigin + FVector3d(TargetVoxels.GridSize) * TargetVoxels.VoxelSize
    );
    
    FAxisAlignedBox3d ToolWorldBounds(
        ToolVoxels.GridOrigin,
        ToolVoxels.GridOrigin + FVector3d(ToolVoxels.GridSize) * ToolVoxels.VoxelSize
    );
    
    FAxisAlignedBox3d OverlapBounds = TargetWorldBounds.Intersect(ToolWorldBounds);
    
    if (!OverlapBounds.IsEmpty())
    {
        // 计算重叠区域在目标体素网格中的体素范围
        FIntVector TargetMinVoxel = TargetVoxels.WorldToVoxel(OverlapBounds.Min);
        FIntVector TargetMaxVoxel = TargetVoxels.WorldToVoxel(OverlapBounds.Max);
    
        // 裁剪到有效范围
        TargetMinVoxel.X = FMath::Clamp(TargetMinVoxel.X, 0, TargetVoxels.GridSize - 1);
        TargetMinVoxel.Y = FMath::Clamp(TargetMinVoxel.Y, 0, TargetVoxels.GridSize - 1);
        TargetMinVoxel.Z = FMath::Clamp(TargetMinVoxel.Z, 0, TargetVoxels.GridSize - 1);
    
        TargetMaxVoxel.X = FMath::Clamp(TargetMaxVoxel.X, 0, TargetVoxels.GridSize - 1);
        TargetMaxVoxel.Y = FMath::Clamp(TargetMaxVoxel.Y, 0, TargetVoxels.GridSize - 1);
        TargetMaxVoxel.Z = FMath::Clamp(TargetMaxVoxel.Z, 0, TargetVoxels.GridSize - 1);
    
        ParallelFor(TargetMaxVoxel.Z - TargetMinVoxel.Z + 1, [&](int32 Dz)
        {
            if (Progress && Progress->Cancelled()) return;
        
            int32 Z = TargetMinVoxel.Z + Dz;
            for (int32 Y = TargetMinVoxel.Y; Y <= TargetMaxVoxel.Y; Y++)
            {
                for (int32 X = TargetMinVoxel.X; X <= TargetMaxVoxel.X; X++)
                {
                    FVector3d WorldPos = TargetVoxels.GetVoxelWorldPosition(X, Y, Z);
                
                    // 查询工具体素在该位置的值（需要坐标转换）
                    float ToolValue = SampleToolVoxelAtPosition(ToolVoxels, WorldPos);
                
                    int32 Index = TargetVoxels.GetVoxelIndex(X, Y, Z);
                
                    // 如果工具在当前位置为内部，则执行切削
                    if (ToolValue < 0)
                    {
                        TargetVoxels.Voxels[Index] = FMath::Abs(TargetVoxels.Voxels[Index]);
                    }
                }
            }
        });
    }
}

void FVoxelCutMeshOp::UpdateLocalRegion(FMaVoxelData& TargetVoxels, const FDynamicMesh3& ToolMesh, 
                                       const FTransform& ToolTransform, FProgressCancel* Progress)
{
    UE_LOG(LogTemp, Warning, TEXT("UpdateLocal Region"));
    // 变换刀具网格到世界空间
    FDynamicMesh3 TransformedToolMesh = ToolMesh;
    MeshTransforms::ApplyTransform(TransformedToolMesh, ToolTransform, true);
    
    // 计算刀具的边界框
    FAxisAlignedBox3d ToolBounds = TransformedToolMesh.GetBounds();
    
    // 扩展边界（考虑更新边界）
    FVector3d ExpandedMin = ToolBounds.Min - FVector3d(UpdateMargin * TargetVoxels.VoxelSize);
    FVector3d ExpandedMax = ToolBounds.Max + FVector3d(UpdateMargin * TargetVoxels.VoxelSize);
    
    // 转换为体素坐标范围
    FIntVector VoxelMin = TargetVoxels.WorldToVoxel(ExpandedMin);
    FIntVector VoxelMax = TargetVoxels.WorldToVoxel(ExpandedMax);
    
    // 裁剪到有效范围
    VoxelMin.X = FMath::Clamp(VoxelMin.X, 0, TargetVoxels.GridSize - 1);
    VoxelMin.Y = FMath::Clamp(VoxelMin.Y, 0, TargetVoxels.GridSize - 1);
    VoxelMin.Z = FMath::Clamp(VoxelMin.Z, 0, TargetVoxels.GridSize - 1);
    
    VoxelMax.X = FMath::Clamp(VoxelMax.X, 0, TargetVoxels.GridSize - 1);
    VoxelMax.Y = FMath::Clamp(VoxelMax.Y, 0, TargetVoxels.GridSize - 1);
    VoxelMax.Z = FMath::Clamp(VoxelMax.Z, 0, TargetVoxels.GridSize - 1);
    
    // 创建刀具的AABB树
    FDynamicMeshAABBTree3 ToolSpatial(&TransformedToolMesh);
    
    // 只更新受影响区域
    ParallelFor(VoxelMax.Z - VoxelMin.Z + 1, [&](int32 Dz)
    {
        if (Progress && Progress->Cancelled())
        {
            return;
        }
        
        int32 Z = VoxelMin.Z + Dz;
        for (int32 Y = VoxelMin.Y; Y <= VoxelMax.Y; Y++)
        {
            for (int32 X = VoxelMin.X; X <= VoxelMax.X; X++)
            {
                FVector3d VoxelPos = TargetVoxels.GetVoxelWorldPosition(X, Y, Z);
                
                // 计算到刀具的距离
                double ToolDistance = GetDistanceToMesh(ToolSpatial, VoxelPos);
                
                if (ToolDistance < 0)
                {
                    int32 Index = TargetVoxels.GetVoxelIndex(X, Y, Z);
                    TargetVoxels.Voxels[Index] = FMath::Abs(TargetVoxels.Voxels[Index]);
                }
            }
        }
    });
}


void FVoxelCutMeshOp::ConvertVoxelsToMesh(const FMaVoxelData& Voxels, FProgressCancel* Progress)
{
    FMarchingCubes MarchingCubes;
    MarchingCubes.CubeSize = Voxels.VoxelSize;
    MarchingCubes.Bounds.Min = Voxels.GridOrigin;
    MarchingCubes.Bounds.Max = Voxels.GridOrigin + FVector3d(Voxels.GridSize) * Voxels.VoxelSize;
    
    MarchingCubes.Implicit = [&Voxels](const FVector3d& Pos) -> double
    {
        // 修正1: 正确的坐标转换
        FVector3d LocalPos = (Pos - Voxels.GridOrigin) / Voxels.VoxelSize;
        int32 X = FMath::FloorToInt(LocalPos.X);
        int32 Y = FMath::FloorToInt(LocalPos.Y);
        int32 Z = FMath::FloorToInt(LocalPos.Z);
        
        // 修正2: 使用 GridSize 进行边界检查
        if (X < 0 || X >= Voxels.GridSize ||
            Y < 0 || Y >= Voxels.GridSize ||
            Z < 0 || Z >= Voxels.GridSize )
        {
            return 1.0;
        }
        
        // 直接获取最近的体素值（不使用插值）
        int32 Index = X + Y * Voxels.GridSize + Z * Voxels.GridSize * Voxels.GridSize;
        
        if (Index >= Voxels.Voxels.Num() || Index < 0)
        {
            return 1.0;
        }
        
        return Voxels.Voxels[Index];
    };

    MarchingCubes.IsoValue = 0.0f;
    
    MarchingCubes.CancelF = [&Progress]()
    {
        return Progress && Progress->Cancelled();
    };
    
    // 调试：检查体素数据
    UE_LOG(LogTemp, Warning, TEXT("GridSize: %d, VoxelSize: %f, Voxels Num: %d"), 
           Voxels.GridSize, Voxels.VoxelSize, Voxels.Voxels.Num());
    
    ResultMesh->Copy(&MarchingCubes.Generate());
    
    UE_LOG(LogTemp, Warning, TEXT("Generated mesh triangle count: %d"), ResultMesh->TriangleCount());
    
    if (ResultMesh->TriangleCount() > 0)
    {
        FMeshNormals::QuickComputeVertexNormals(*ResultMesh);
    }
}

UE_ENABLE_OPTIMIZATION