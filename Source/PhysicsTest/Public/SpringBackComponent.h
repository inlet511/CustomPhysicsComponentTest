// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "SpringBackComponent.generated.h"

/**
 *	ParentComponent指的是笔尖
 *	TargetComponent指的是跟随笔尖运动的组件，会受到碰撞阻挡
 */

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PHYSICSTEST_API USpringBackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USpringBackComponent();	

	// 父组件名称
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "父组件名称"))
	FName ParentComponentName = NAME_None;

	// 目标组件名称（用户输入）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "目标组件名称"))
	FName TargetComponentName = NAME_None;
	
	// 相对偏移（相对于父组件的位置）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	FVector RelativeOffset = FVector::ZeroVector;
	
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;	
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	bool bEnableCollisionDetection = true;
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	float CollisionCooldownTime = 0.5f; // 碰撞后保持物理模拟的时间    
	
	/** 移动到父级位置
	 *  bUseSweep: 移动过程中是否进行扫描
	 */
	UFUNCTION(BlueprintCallable, Category = "Spring")
	void MoveTargetToParent(bool bUseSweep);
	

	// 高度限制参数-----------------------------------------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Limit")
	bool bEnableHeightLimit = true; // 是否启用高度限制
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Limit")
	float MinHeightRelative = 0.0f; // 相对于父物体的最小高度（相对空间）
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Height Limit")
	float HeightLimitStiffness = 1000.0f; // 高度限制的刚度系数
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowHeightLimit = true; // 是否显示高度限制调试

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Physics")
	UPhysicalMaterial* NoBouncePhysicalMaterial;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	
	// 弹簧弹性系数（刚度），控制回弹力度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float SpringStiffness = 100.0f;

	// 阻尼系数：若为0，则自动计算为临界阻尼值（c = 2√(m*k)）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float DampingCoefficient = 0.0f;

	// 最大运动范围（以初始位置为圆心的球体半径）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float MovementRange = 200.0f;

	// 查找并设置目标组件
	UFUNCTION(BlueprintCallable, Category = "Spring")
	bool FindAndSetTargetComponentByName(FName ComponentName);
	
	// 查找并设置父组件
	UFUNCTION(BlueprintCallable, Category = "Spring")
	bool FindAndSetParentComponentByName(FName ComponentName);

	UFUNCTION()
	void ParentBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult & SweepResult);

	UFUNCTION()
	void ParentEndOverlap(UPrimitiveComponent* OverlappedComponent,AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
	// 应用高度限制-------------------------------------------------
	void ApplyHeightLimit(float DeltaTime);
    
	// 计算球体在父物体相对空间中的高度
	float GetCurrentRelativeHeight() const;


private:
	UPrimitiveComponent* TargetComponent; // 受控制的子物体组件（如StaticMesh）
	UPrimitiveComponent* ParentComponent;     // 父组件（用于跟随移动）
	FVector CurrentVelocity;               // 当前速度（用于阻尼计算）
	float ObjectMass;                      // 子物体质量（用于临界阻尼计算）
	
	UPrimitiveComponent* FindPrimitiveComponentByName(FName NameToFind);

	// 计算当前的目标位置
	FVector CalculateMoveTargetPosition() const;	
	
	// 应用弹簧力：基于偏移和速度计算临界阻尼力
	void ApplySpringForce(float DeltaTime);

	// 当前父级物体和切削对象是否有接触
	bool bCurrentlyParentInContact = false;

	// 当前目标物体是否启用了物理模拟
	bool bTargetIsUsingPhysics = false;
	
	// 切换到物理模拟模式
	void SwitchToPhysicsSimulation();
    
	// 切换到吸附模式
	void SwitchToSnapMode();	

};


