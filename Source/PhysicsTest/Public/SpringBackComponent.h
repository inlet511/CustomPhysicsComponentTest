// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "SpringBackComponent.generated.h"


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PHYSICSTEST_API USpringBackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	USpringBackComponent();

	// 目标组件名称（用户输入）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "目标组件名称"))
	FName TargetComponentName = NAME_None;

	// 父组件名称（用于跟随移动）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (DisplayName = "父组件名称"))
	FName ParentComponentName = NAME_None;

	// 相对偏移（相对于父组件的位置）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	FVector RelativeOffset = FVector::ZeroVector;

	// Called every frame
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;


	
	// 快速吸附速度系数（越大吸附越快）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float SnapSpeed = 10.0f;

	// 吸附距离阈值：距离小于此值时直接吸附
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float SnapThreshold = 5.0f;

	// 最大吸附力限制
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float MaxForce = 10000.0f;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;


	UFUNCTION()
	void OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);


	// 弹簧弹性系数（刚度），控制回弹力度
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float SpringStiffness = 100.0f;

	// 阻尼系数：若为0，则自动计算为临界阻尼值（c = 2√(m*k)）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float DampingCoefficient = 0.0f;

	// 最大运动范围（以初始位置为圆心的球体半径）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring")
	float MovementRange = 200.0f;

	// 是否显示调试运动范围
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowMovementRange = true;

	// 手动查找并设置目标组件（蓝图可调用）
	UFUNCTION(BlueprintCallable, Category = "Spring")
	bool FindAndSetTargetComponentByName(FName ComponentName);
	
	// 手动查找并设置父组件（蓝图可调用）
	UFUNCTION(BlueprintCallable, Category = "Spring")
	bool FindAndSetParentComponentByName(FName ComponentName);

	// 更新相对偏移为当前位置（蓝图可调用）
	UFUNCTION(BlueprintCallable, Category = "Spring")
	void UpdateRelativeOffset();

private:
	UPrimitiveComponent* TargetComponent; // 受控制的子物体组件（如StaticMesh）
	USceneComponent* ParentComponent;     // 父组件（用于跟随移动）
	FVector InitialRelativeOffset;         // 初始相对偏移
	FVector CurrentVelocity;               // 当前速度（用于阻尼计算）
	float ObjectMass;                      // 子物体质量（用于临界阻尼计算）


	UPrimitiveComponent* FindComponentByName(FName NameToFind);

	USceneComponent* FindSceneComponentByName(FName NameToFind);

	UPrimitiveComponent* FindPrimitiveComponentByName(FName NameToFind);


	// 计算当前的目标位置（基于父组件位置和相对偏移）
	FVector CalculateTargetPosition() const;	
	
	// 应用弹簧力：基于偏移和速度计算临界阻尼力
	void ApplySpringForce(float DeltaTime);
	

};


