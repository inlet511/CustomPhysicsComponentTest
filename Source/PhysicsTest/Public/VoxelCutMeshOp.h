// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ModelingOperators.h"
#include "BaseOps/VoxelBaseOp.h"

namespace UE
{
	namespace Geometry
	{

		inline FArchive& SerializeAxisAlignedBox3d(FArchive& Ar, FAxisAlignedBox3d& Bounds)
		{
			FVector3d Min = Bounds.Min;
			FVector3d Max = Bounds.Max;
    
			Ar << Min;
			Ar << Max;
    
			if (Ar.IsLoading())
			{
				Bounds = FAxisAlignedBox3d(Min, Max);
			}
    
			return Ar;
		}
		
		// 体素数据容器，支持持久化存储和增量更新
		struct MODELINGOPERATORS_API FVoxelData
		{
			TArray<float> Voxels;           // 体素值（有符号距离场）
			FVector3d GridOrigin;           // 网格原点
			int32 GridSize;                 // 每个维度的体素数量
			double VoxelSize;               // 体素大小
			FAxisAlignedBox3d WorldBounds;  // 世界空间边界框
    
			// 序列化支持
			void Serialize(FArchive& Ar);
    
			// 清空数据
			void Reset();
    
			// 检查是否有效
			bool IsValid() const { return Voxels.Num() > 0; }
    
			// 获取体素索引
			int32 GetVoxelIndex(int32 X, int32 Y, int32 Z) const;
    
			// 获取体素世界位置
			FVector3d GetVoxelWorldPosition(int32 X, int32 Y, int32 Z) const;
    
			// 世界位置到体素坐标
			FIntVector WorldToVoxel(const FVector3d& WorldPos) const;
		};
		
		class PHYSICSTEST_API FVoxelCutMeshOp  : public FVoxelBaseOp
		{
		public:
			virtual ~FVoxelCutMeshOp() {}

			// 输入：目标网格和刀具网格
			TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe> TargetMesh;
			TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe> CutToolMesh;
    
			// 变换矩阵
			FTransform TargetTransform;
			FTransform CutToolTransform;
    
			// 持久化体素数据（输入/输出）
			TSharedPtr<FVoxelData> PersistentVoxelData;
    
			// 切削参数
			double CutOffset = 0.0;
			bool bFillCutHole = true;
			bool bKeepBothParts = false;
			double VoxelSize = 1.0;
			bool bSmoothCutEdges = true;
			double SmoothingStrength = 0.5;
    
			// 增量更新选项
			bool bIncrementalUpdate = true;  // 是否只更新受影响区域
			int32 UpdateMargin = 2;          // 更新边界扩展（体素单位）

			void SetTransform(const FTransformSRT3d& Transform);

			virtual void CalculateResult(FProgressCancel* Progress) override;
    
			// 初始化体素数据（首次使用）
			bool InitializeVoxelData(FProgressCancel* Progress);
    
			// 增量切削（基于现有体素数据）
			bool IncrementalCut(FProgressCancel* Progress);

		protected:
			// 体素化方法
			bool VoxelizeMesh(const FDynamicMesh3& Mesh, const FTransform& Transform, 
							 FVoxelData& VoxelData, FProgressCancel* Progress);
    
			// 布尔运算
			void PerformBooleanCut(FVoxelData& TargetVoxels, const FVoxelData& ToolVoxels, 
								  FProgressCancel* Progress);
    
			// 局部更新：只更新受刀具影响的区域
			void UpdateLocalRegion(FVoxelData& TargetVoxels, const FDynamicMesh3& ToolMesh, 
								  const FTransform& ToolTransform, FProgressCancel* Progress);
    
			// 网格生成
			void ConvertVoxelsToMesh(const FVoxelData& Voxels, FProgressCancel* Progress);
    
		private:
			// 内部状态
			bool bVoxelDataInitialized = false;
		};
	}
}