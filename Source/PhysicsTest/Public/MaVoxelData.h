// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

namespace UE
{
	namespace Geometry
	{
		// 体素数据容器，支持持久化存储和增量更新
		struct PHYSICSTEST_API FVoxelOctreeNode
		{
			// 节点的空间边界（世界坐标系）
			FAxisAlignedBox3d Bounds;
			// 节点层级（0=根节点，层级越高体素精度越高）
			int32 Level;
			// 最大层级（切削区域的最高精度，建议设为5-8，层级越高精度越高）
			static const int32 MaxLevel = 6;
			// 节点体素尺寸（Level越高，VoxelSize越小）
			double VoxelSize;
			// 是否为叶子节点（无细分）
			bool bIsLeaf;
			// 是否包含切削数据（剪枝关键：无数据则跳过计算）
			bool bHasCutData;
			// 叶子节点的体素数据（非叶子节点无数据）
			TArray<float> VoxelValues;
			// 子节点（8个，非叶子节点有效）
			TArray<TUniquePtr<FVoxelOctreeNode>> Children;
			// 构造函数
			FVoxelOctreeNode(const FAxisAlignedBox3d& InBounds, int32 InLevel, double BaseVoxelSize)
				: Bounds(InBounds), Level(InLevel), VoxelSize(BaseVoxelSize / FMath::Pow(2.0, InLevel))
				  , bIsLeaf(InLevel >= MaxLevel), bHasCutData(false)
			{
				// 叶子节点初始化体素数据（按当前节点尺寸计算体素数量）
				if (bIsLeaf)
				{
					int32 VoxelCountX = FMath::CeilToInt(Bounds.Extents().X / VoxelSize);
					int32 VoxelCountY = FMath::CeilToInt(Bounds.Extents().Y / VoxelSize);
					int32 VoxelCountZ = FMath::CeilToInt(Bounds.Extents().Z / VoxelSize);
					VoxelValues.SetNum(VoxelCountX * VoxelCountY * VoxelCountZ);
					FMemory::Memzero(VoxelValues.GetData(), VoxelValues.Num() * sizeof(float));
				}
				else
				{
					Children.Reserve(8);
				}
			}

			// 细分节点（拆分为8个子节点）
			void Subdivide()
			{
				if (bIsLeaf || Level >= MaxLevel) return;

				FVector3d Center = Bounds.Center();
				FVector3d Extent = Bounds.Extents() / 2.0;

				// 生成8个子节点的边界
				TArray<FAxisAlignedBox3d> ChildBounds;
				for (int32 x = 0; x < 2; x++)
				{
					for (int32 y = 0; y < 2; y++)
					{
						for (int32 z = 0; z < 2; z++)
						{
							FVector3d Min = Center - Extent + FVector3d(x * Extent.X * 2, y * Extent.Y * 2,
							                                            z * Extent.Z * 2);
							FVector3d Max = Center + Extent;
							ChildBounds.Add(FAxisAlignedBox3d(Min, Max));
						}
					}
				}

				// 创建子节点
				for (const auto& ChildBound : ChildBounds)
				{
					Children.Add(MakeUnique<FVoxelOctreeNode>(ChildBound, Level + 1, VoxelSize * 2));
				}

				bIsLeaf = false;
			}

			// 判断点是否在节点内
			bool ContainsPoint(const FVector3d& Point) const { return Bounds.Contains(Point); }

			// 设置体素值（递归找到对应叶子节点）
			void SetVoxelValue(const FVector3d& WorldPoint, float Value)
			{
				if (!ContainsPoint(WorldPoint)) return;

				// 叶子节点：直接设置体素值
				if (bIsLeaf)
				{
					int32 X = FMath::FloorToInt((WorldPoint.X - Bounds.Min.X) / VoxelSize);
					int32 Y = FMath::FloorToInt((WorldPoint.Y - Bounds.Min.Y) / VoxelSize);
					int32 Z = FMath::FloorToInt((WorldPoint.Z - Bounds.Min.Z) / VoxelSize);
					int32 VoxelCountX = FMath::CeilToInt(Bounds.Extents().X / VoxelSize);
					int32 VoxelCountY = FMath::CeilToInt(Bounds.Extents().Y / VoxelSize);
					int32 Index = X + Y * VoxelCountX + Z * VoxelCountX * VoxelCountY;

					if (Index >= 0 && Index < VoxelValues.Num())
					{
						VoxelValues[Index] = Value;
						bHasCutData = true; // 标记有切削数据
					}
					return;
				}

				// 非叶子节点：递归到子节点
				for (auto& Child : Children)
				{
					Child->SetVoxelValue(WorldPoint, Value);
					if (Child->bHasCutData)
					{
						bHasCutData = true; // 父节点继承“有数据”标记
					}
				}
			}

			// 遍历有切削数据的叶子节点（用于网格生成）
			void TraverseCutNodes(TFunction<void(FVoxelOctreeNode*)> Callback)
			{
				if (!bHasCutData) return; // 无切削数据，直接跳过（核心剪枝）

				if (bIsLeaf)
				{
					Callback(this); // 回调处理叶子节点
					return;
				}

				// 递归遍历子节点
				for (auto& Child : Children)
				{
					Child->TraverseCutNodes(Callback);
				}
			}
		};
	}
}
