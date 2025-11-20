// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCutMeshOp.h"

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMesh/MeshNormals.h"
#include "Operations/MeshBoolean.h"
#include "Generators/MarchingCubes.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "HAL/PlatformTime.h"

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
        UE_LOG(LogTemp, Error, TEXT("Initialize Voxel Data First! (Call InitializeVoxelData())"));
        return;
    }

    double CutStart = FPlatformTime::Seconds();  // 开始时间    
    // 增量更新：基于现有体素数据进行切削
    if (!IncrementalCut(Progress))
    {
        return;
    }
    double CutEnd = FPlatformTime::Seconds();    // 结束时间
    // 打印切削耗时
    double CutTimeMs = (CutEnd - CutStart) * 1000.0;
    UE_LOG(LogTemp, Log, TEXT("切削操作（IncrementalCut）耗时: %.2f 毫秒"), CutTimeMs);

    // 生成最终网格 - 修正计时
    double GenerateStart = FPlatformTime::Seconds();  // 开始时间
    // 生成最终网格
    ConvertVoxelsToMesh(*PersistentVoxelData, Progress);

    double GenerateEnd = FPlatformTime::Seconds();    // 结束时间
    
    // 打印模型生成耗时
    double GenerateTimeMs = (GenerateEnd - GenerateStart) * 1000.0;
    UE_LOG(LogTemp, Log, TEXT("模型生成（ConvertVoxelsToMesh）耗时: %.2f 毫秒"), GenerateTimeMs);
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
        PersistentVoxelData->VoxelSize = VoxelSize;
    }

    // 变换目标网格到世界空间
    FDynamicMesh3 TransformedTargetMesh = *TargetMesh;
    MeshTransforms::ApplyTransform(TransformedTargetMesh, TargetTransform, true);
    
    // 计算平均平移作为结果变换的中心
    FVector3d AverageTranslation = TargetTransform.GetTranslation();
    ResultTransform = FTransformSRT3d(AverageTranslation);
    

    // 体素化目标网格
    double VoxelizeStart = FPlatformTime::Seconds();  // 开始时间（秒）
    bool success = VoxelizeMesh(TransformedTargetMesh, FTransform::Identity,*PersistentVoxelData, Progress);
    double VoxelizeEnd = FPlatformTime::Seconds();
    // 转换为毫秒（1秒 = 1000毫秒）
    double VoxelizeTimeMs = (VoxelizeEnd - VoxelizeStart) * 1000.0;
    UE_LOG(LogTemp, Warning, TEXT("VoxelizeMesh 耗时: %.2f 毫秒"), VoxelizeTimeMs);    

    bVoxelDataInitialized = true;    
    return success;
}

bool FVoxelCutMeshOp::IncrementalCut(FProgressCancel* Progress)
{
    if (!PersistentVoxelData.IsValid() || !CutToolMesh)
    {
        return false;
    }

    // 局部更新：只更新受刀具影响的区域
    UpdateLocalRegion(*PersistentVoxelData, *CutToolMesh, 
                     CutToolTransform, Progress);
    

    return !(Progress && Progress->Cancelled());
}

double GetDistanceToMesh(const FDynamicMeshAABBTree3& Spatial, TFastWindingTree<FDynamicMesh3> Winding, const FVector3d& LocalPoint, const FVector3d& WorldPoint)
{
    double NearestDistSqr; 
    int NearestTriID = Spatial.FindNearestTriangle(LocalPoint, NearestDistSqr);
    
    if (NearestTriID == IndexConstants::InvalidID)
    {
        return TNumericLimits<double>::Max();
    }
    
    // 计算有符号距离（内部为负，外部为正）    
    bool bInSide = Winding.IsInside(WorldPoint);    

    double SignedDist =  FMathd::Sqrt(NearestDistSqr) * (bInSide? -1 : 1); 
    
    return SignedDist;
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
                double Distance = GetDistanceToMesh(Spatial,Winding,LocalVoxelPos, VoxelPos);
               
                int32 Index = VoxelData.GetVoxelIndex(X, Y, Z);
                VoxelData.Voxels[Index] = Distance;
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
    TFastWindingTree<FDynamicMesh3> ToolWinding(&ToolSpatial);
    
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
                FVector3d LocalPos = ToolTransform.InverseTransformPosition(VoxelPos);
                
                // 计算到刀具的距离
                double ToolDistance = GetDistanceToMesh(ToolSpatial,ToolWinding, LocalPos, VoxelPos);
                
                if (ToolDistance < 0)
                {
                    int32 Index = TargetVoxels.GetVoxelIndex(X, Y, Z);
                    TargetVoxels.Voxels[Index] = FMath::Abs(TargetVoxels.Voxels[Index]);
                }
            }
        }
    });

    // 切削后对局部区域进行高斯平滑（减少体素值突变）
    SmoothLocalVoxels(TargetVoxels, VoxelMin, VoxelMax, 1);
}


void FVoxelCutMeshOp::ConvertVoxelsToMesh(const FMaVoxelData& Voxels, FProgressCancel* Progress)
{
    FMarchingCubes MarchingCubes;
    MarchingCubes.CubeSize = Voxels.VoxelSize;
    MarchingCubes.Bounds.Min = Voxels.GridOrigin;
    MarchingCubes.Bounds.Max = Voxels.GridOrigin + FVector3d(Voxels.GridSize) * Voxels.VoxelSize;
    
    MarchingCubes.Implicit = [&Voxels](const FVector3d& Pos) -> double
    {
        // 将世界坐标转换为体素网格的局部坐标（浮点）
        FVector3d LocalPos = (Pos - Voxels.GridOrigin) / Voxels.VoxelSize;
        
        // 计算周围8个体素的整数坐标和插值权重
        int32 X = FMath::FloorToInt(LocalPos.X);
        int32 Y = FMath::FloorToInt(LocalPos.Y);
        int32 Z = FMath::FloorToInt(LocalPos.Z);
        
        // 检查是否在有效体素范围内（预留边界，避免越界）
        if (X < 1 || X >= Voxels.GridSize - 2 ||
            Y < 1 || Y >= Voxels.GridSize - 2 ||
            Z < 1 || Z >= Voxels.GridSize - 2)
        {
            return 1.0; // 超出范围视为外部
        }
        
        // 计算插值权重（0~1之间）
        double u = FMath::Clamp(LocalPos.X - X, 0.0, 1.0);
        double v = FMath::Clamp(LocalPos.Y - Y, 0.0, 1.0);
        double w = FMath::Clamp(LocalPos.Z - Z, 0.0, 1.0);
        
        // 获取周围8个顶点的体素值（带越界保护）
        auto GetVoxel = [&](int32 dx, int32 dy, int32 dz) -> float
        {
            int32 Tx = X + dx;
            int32 Ty = Y + dy;
            int32 Tz = Z + dz;
            if (Tx < 0 || Tx >= Voxels.GridSize || Ty < 0 || Ty >= Voxels.GridSize || Tz < 0 || Tz >= Voxels.GridSize)
            {
                return 1.0f;
            }
            int32 Index = Voxels.GetVoxelIndex(Tx, Ty, Tz);
            return (Index >= 0 && Index < Voxels.Voxels.Num()) ? Voxels.Voxels[Index] : 1.0f;
        };
        
        float v000 = GetVoxel(0, 0, 0);
        float v100 = GetVoxel(1, 0, 0);
        float v010 = GetVoxel(0, 1, 0);
        float v110 = GetVoxel(1, 1, 0);
        float v001 = GetVoxel(0, 0, 1);
        float v101 = GetVoxel(1, 0, 1);
        float v011 = GetVoxel(0, 1, 1);
        float v111 = GetVoxel(1, 1, 1);
        
        // 三线性插值计算
        // 1. X方向插值
        float x00 = FMath::Lerp(v000, v100, u);
        float x10 = FMath::Lerp(v010, v110, u);
        float x01 = FMath::Lerp(v001, v101, u);
        float x11 = FMath::Lerp(v011, v111, u);
        
        // 2. Y方向插值
        float y0 = FMath::Lerp(x00, x10, v);
        float y1 = FMath::Lerp(x01, x11, v);
        
        // 3. Z方向插值
        return FMath::Lerp(y0, y1, w);
    };

    MarchingCubes.IsoValue = 0.0f;
    
    MarchingCubes.CancelF = [&Progress]()
    {
        return Progress && Progress->Cancelled();
    };
    
    // 调试信息
    UE_LOG(LogTemp, Warning, TEXT("GridSize: %d, VoxelSize: %f, Voxels Num: %d"), 
           Voxels.GridSize, Voxels.VoxelSize, Voxels.Voxels.Num());
    
    ResultMesh->Copy(&MarchingCubes.Generate());
    
    UE_LOG(LogTemp, Warning, TEXT("Generated mesh triangle count: %d"), ResultMesh->TriangleCount());
    
    if (ResultMesh->TriangleCount() > 0)
    {
        ResultMesh->ReverseOrientation(true);
        FMeshNormals::QuickComputeVertexNormals(*ResultMesh);
        
        // 复原位置
        FTransform InverseTargetTransform = TargetTransform.Inverse();
        MeshTransforms::ApplyTransform(*ResultMesh, InverseTargetTransform, true);
    }
}

void FVoxelCutMeshOp::SmoothLocalVoxels(FMaVoxelData& Voxels, const FIntVector& Min, const FIntVector& Max,
    int32 Iterations)
{
    const int32 GridSize = Voxels.GridSize;
    TArray<float> TempVoxels = Voxels.Voxels; // 临时数组保存原始值

    for (int32 It = 0; It < Iterations; It++)
    {
        for (int32 Z = Min.Z; Z <= Max.Z; Z++)
        {
            for (int32 Y = Min.Y; Y <= Max.Y; Y++)
            {
                for (int32 X = Min.X; X <= Max.X; X++)
                {
                    // 3x3x3邻域采样（中心权重更高）
                    float Sum = 0.0f;
                    float Weight = 0.0f;
                    for (int32 dz = -1; dz <= 1; dz++)
                    {
                        for (int32 dy = -1; dy <= 1; dy++)
                        {
                            for (int32 dx = -1; dx <= 1; dx++)
                            {
                                int32 Tx = X + dx;
                                int32 Ty = Y + dy;
                                int32 Tz = Z + dz;
                                if (Tx < 0 || Tx >= GridSize || Ty < 0 || Ty >= GridSize || Tz < 0 || Tz >= GridSize)
                                    continue;

                                // 中心体素权重为2，周围为1（简单高斯近似）
                                float W = (dx == 0 && dy == 0 && dz == 0) ? 2.0f : 1.0f;
                                Sum += TempVoxels[Voxels.GetVoxelIndex(Tx, Ty, Tz)] * W;
                                Weight += W;
                            }
                        }
                    }
                    // 更新当前体素为邻域平均值
                    Voxels.Voxels[Voxels.GetVoxelIndex(X, Y, Z)] = Sum / Weight;
                }
            }
        }
        TempVoxels = Voxels.Voxels; // 迭代更新临时数组
    }
}



UE_ENABLE_OPTIMIZATION
