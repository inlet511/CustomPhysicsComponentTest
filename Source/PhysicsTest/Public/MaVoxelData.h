// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"

using namespace UE::Geometry;

// 八叉树节点
struct PHYSICSTEST_API FOctreeNode
{
	FAxisAlignedBox3d Bounds;
	TArray<FOctreeNode> Children;
	TArray<float> Voxels; // 叶子节点存储体素数据
	int32 Depth = 0;
	bool bIsLeaf = true;
	bool bIsEmpty = true; // 标记节点是否为空（优化用）

	// 平均体素值（用于非叶子节点）
	float AverageValue = 0.0f;

	void Subdivide(double MinVoxelSize);
	bool ContainsPoint(const FVector3d& Point) const;
	bool IntersectsBounds(const FAxisAlignedBox3d& OtherBounds) const;
};

// 体素数据容器，支持持久化存储和增量更新
struct PHYSICSTEST_API FMaVoxelData
{
	// 传统均匀网格数据（向后兼容）
	TArray<float> Voxels;
	FVector3d GridOrigin;
	int32 GridSize = 0;
	double VoxelSize = 1.0;

	// 八叉树优化
	FOctreeNode OctreeRoot;
	bool bUseOctree = true;
	int32 MaxOctreeDepth = 6; // 最大深度，控制精度
	double MinVoxelSize = 0.5; // 最小体素大小

	void Reset();
	bool IsValid() const { return Voxels.Num() > 0 || (bUseOctree && OctreeRoot.Bounds.IsEmpty() == false); }

	// 均匀网格方法（保持兼容性）
	int32 GetVoxelIndex(int32 X, int32 Y, int32 Z) const;
	FVector3d GetVoxelWorldPosition(int32 X, int32 Y, int32 Z) const;
	FIntVector WorldToVoxel(const FVector3d& WorldPos) const;

	// 八叉树方法
	void BuildOctreeFromMesh(const FDynamicMesh3& Mesh, const FTransform& Transform);
	float GetValueAtPosition(const FVector3d& WorldPos) const;
	void UpdateRegion(const FAxisAlignedBox3d& UpdateBounds, const TFunctionRef<float(const FVector3d&)>& UpdateFunction);

	// 调试
	void DebugLogOctreeStats() const;

	// 获取用于Marching Cubes的边界
	FAxisAlignedBox3d GetOctreeBounds() const { return OctreeRoot.Bounds; }

private:
	// 内部辅助方法
	float CalculateDistanceToMesh(const FDynamicMeshAABBTree3& Spatial, 
								TFastWindingTree<FDynamicMesh3>& Winding,
								const FVector3d& WorldPos,
								const FTransform& MeshTransform) const;
};
