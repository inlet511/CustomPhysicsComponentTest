// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MaVoxelData.generated.h"

namespace UE
{
	namespace Geometry
	{
		// 体素数据容器，支持持久化存储和增量更新
		struct PHYSICSTEST_API FMaVoxelData
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
	}
}