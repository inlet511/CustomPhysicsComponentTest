// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCutMeshOp.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshBoolean.h"
#include "Generators/MarchingCubes.h"

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

    // 检查是否有持久化体素数据
    if (!PersistentVoxelData.IsValid() || !PersistentVoxelData->IsValid())
    {
        // 首次运行：初始化体素数据
        if (!InitializeVoxelData(Progress))
        {
            return;
        }
        bVoxelDataInitialized = true;
    }
    else
    {
        // 增量更新：基于现有体素数据进行切削
        if (!IncrementalCut(Progress))
        {
            return;
        }
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

    // 创建新的体素数据容器
    if (!PersistentVoxelData.IsValid())
    {
        PersistentVoxelData = MakeShared<FMaVoxelData>();
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

void FVoxelCutMeshOp::PerformBooleanCut(FMaVoxelData& TargetVoxels, const FMaVoxelData& ToolVoxels, 
                                       FProgressCancel* Progress)
{
    // 确保两个体素网格参数匹配
    if (TargetVoxels.GridSize != ToolVoxels.GridSize || 
        TargetVoxels.GridOrigin != ToolVoxels.GridOrigin)
    {
        return;
    }

    // 并行执行布尔减操作
    ParallelFor(TargetVoxels.GridSize, [&](int32 Z)
    {
        if (Progress && Progress->Cancelled())
        {
            return;
        }
        
        for (int32 Y = 0; Y < TargetVoxels.GridSize; Y++)
        {
            for (int32 X = 0; X < TargetVoxels.GridSize; X++)
            {
                int32 Index = TargetVoxels.GetVoxelIndex(X, Y, Z);                
                
                if (ToolVoxels.Voxels[Index] < 0)
                {
                    TargetVoxels.Voxels[Index] = FMath::Abs(TargetVoxels.Voxels[Index]);
                }
            }
        }
    });
}

void FVoxelCutMeshOp::UpdateLocalRegion(FMaVoxelData& TargetVoxels, const FDynamicMesh3& ToolMesh, 
                                       const FTransform& ToolTransform, FProgressCancel* Progress)
{
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
                // double ToolDistance = ToolSpatial.GetDistance(VoxelPos);
                double ToolDistance = GetDistanceToMesh(ToolSpatial, VoxelPos);
                
                // 如果刀具内部，标记为外部
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
        // 将世界坐标转换为体素索引
        FVector3d LocalPos = Pos / Voxels.VoxelSize;
        int32 X = FMath::FloorToInt(LocalPos.X);
        int32 Y = FMath::FloorToInt(LocalPos.Y);
        int32 Z = FMath::FloorToInt(LocalPos.Z);
        
        // 检查边界
        if (X < 0 || X >= Voxels.VoxelSize - 1 ||
            Y < 0 || Y >= Voxels.VoxelSize - 1 ||
            Z < 0 || Z >= Voxels.VoxelSize - 1)
        {
            return -1.0; // 边界外返回负值
        }
        
        // 三线性插值获取体素值
        double FracX = LocalPos.X - X;
        double FracY = LocalPos.Y - Y;
        double FracZ = LocalPos.Z - Z;
        
        int32 Index000 = X + Y * Voxels.VoxelSize + Z * Voxels.VoxelSize * Voxels.VoxelSize;
        int32 Index100 = Index000 + 1;
        int32 Index010 = Index000 + Voxels.VoxelSize;
        int32 Index110 = Index010 + 1;
        int32 Index001 = Index000 + Voxels.VoxelSize * Voxels.VoxelSize;
        int32 Index101 = Index001 + 1;
        int32 Index011 = Index001 + Voxels.VoxelSize;
        int32 Index111 = Index011 + 1;
        
        // 确保索引有效
        if (Index111 >= Voxels.Voxels.Num()) return -1.0;
        
        // 三线性插值
        double V00 = FMath::Lerp(Voxels.Voxels[Index000], Voxels.Voxels[Index100], FracX);
        double V01 = FMath::Lerp(Voxels.Voxels[Index010], Voxels.Voxels[Index110], FracX);
        double V0 = FMath::Lerp(V00, V01, FracY);
        
        double V10 = FMath::Lerp(Voxels.Voxels[Index001], Voxels.Voxels[Index101], FracX);
        double V11 = FMath::Lerp(Voxels.Voxels[Index011], Voxels.Voxels[Index111], FracX);
        double V1 = FMath::Lerp(V10, V11, FracY);
        
        return FMath::Lerp(V0, V1, FracZ);
    };

    MarchingCubes.IsoValue = 0.0f;
    
    MarchingCubes.CancelF = [&Progress]()
    {
        return Progress && Progress->Cancelled();
    };
    
    ResultMesh->Copy(&MarchingCubes.Generate());
    
    if (ResultMesh->TriangleCount() > 0)
    {
        FMeshNormals::QuickComputeVertexNormals(*ResultMesh);
    }
}