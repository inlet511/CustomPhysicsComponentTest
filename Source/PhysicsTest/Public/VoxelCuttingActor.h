// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelCutComponent.h"
#include "VoxelCuttingActor.generated.h"

UCLASS()
class PHYSICSTEST_API AVoxelCuttingActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AVoxelCuttingActor();

	// 切削组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Cut")
	UVoxelCutComponent* VoxelCutComponent;

	// 设置目标Actor
	UFUNCTION(BlueprintCallable, Category = "Voxel Cut")
	void SetTargetActor(ADynamicMeshActor* InTargetActor);

	UFUNCTION(BlueprintCallable, Category = "Voxel Cut")
	void SetCutToolActor(ADynamicMeshActor* InCutToolActor);

	// 开始/停止切削
	UFUNCTION(BlueprintCallable, Category = "Voxel Cut")
	void StartCutting();
    
	UFUNCTION(BlueprintCallable, Category = "Voxel Cut")
	void StopCutting();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	
	// 切削工具
	UPROPERTY()
	ADynamicMeshActor* CuttingToolActor;
	
	// 切削工具网格组件
	UPROPERTY()
	UDynamicMeshComponent* CutToolComponent;
	
	// 切削对象
	UPROPERTY()
	ADynamicMeshActor* TargetActor;	
    
	// 切削对象网格组件
	UPROPERTY()
	UDynamicMeshComponent* TargetMeshComponent;
};
