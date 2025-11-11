// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicsTest/Public/SpringBackComponent.h"

#include "MovieSceneTracksComponentTypes.h"
#include "Engine/OverlapResult.h"



USpringBackComponent::USpringBackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


void USpringBackComponent::MoveTargetToParent(bool bUseSweep)
{
	if (!TargetComponent) return;
    
	FVector TargetPos = CalculateMoveTargetPosition();
	FHitResult HitResult;
	TargetComponent->SetWorldLocation(TargetPos,bUseSweep,&HitResult);
	if (bUseSweep && HitResult.bBlockingHit)
	{
		if (!bTargetIsUsingPhysics)
		{			
			SwitchToPhysicsSimulation();
		}
	}
	TargetComponent->SetWorldRotation(ParentComponent ? ParentComponent->GetComponentRotation() : FRotator::ZeroRotator);
	// 重置速度
	CurrentVelocity = FVector::ZeroVector;
}


void USpringBackComponent::SwitchToPhysicsSimulation()
{
	if (!TargetComponent) return;

	if (!bTargetIsUsingPhysics)
	{
		TargetComponent->SetSimulatePhysics(true);
		bTargetIsUsingPhysics = true;
		TargetComponent->SetPhysicsLinearVelocity(FVector(0,0,0));		
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 切换到物理模拟模式"));
	}
}

void USpringBackComponent::SwitchToSnapMode()
{
	if (!TargetComponent) return;
    
	if (bTargetIsUsingPhysics)
	{
		// 先停止物理模拟
		TargetComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TargetComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		TargetComponent->SetSimulatePhysics(false);
		bTargetIsUsingPhysics = false;		
		UE_LOG(LogTemp, Verbose, TEXT("SpringBackComponent: 切换到吸附模式"));

		// 立即吸附到目标位置
		MoveTargetToParent(false);
	}
}

// Called when the game starts
void USpringBackComponent::BeginPlay()
{
	Super::BeginPlay();
	
	// 查找并设置父组件（如果指定了名称）
	if (ParentComponentName != NAME_None)
	{
		FindAndSetParentComponentByName(ParentComponentName);
	}

	// 查找并设置目标组件（如果指定了名称）
	if (TargetComponentName != NAME_None)
	{
		FindAndSetTargetComponentByName(TargetComponentName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 未指定目标组件名称，请设置TargetComponentName属性"));
	}
	
	// 初始状态：吸附模式
	if (TargetComponent)
	{
		SwitchToSnapMode();
	}	
}


void USpringBackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
										 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetComponent) return;

	// 1. 状态切换
	// 当前在物理模拟，父级已经出来了
	if (bTargetIsUsingPhysics && !bCurrentlyParentInContact)
	{
		// 切出物理状态
		SwitchToSnapMode();
	}

	// 2. 不同状态下应用不同的函数
	if (bTargetIsUsingPhysics)
	{
		// 应用弹簧力
		ApplySpringForce(DeltaTime);
	}
	else
	{
		// 移动到父级位置
		MoveTargetToParent(true);
	}	
}

bool USpringBackComponent::FindAndSetTargetComponentByName(FName ComponentName)
{
	TargetComponent = FindPrimitiveComponentByName(ComponentName);
    
	if (TargetComponent)
	{
		// 启用物理模拟以确保碰撞检测和力作用
		TargetComponent->SetSimulatePhysics(true);
		TargetComponent->SetNotifyRigidBodyCollision(true);
		//TargetComponent->OnComponentHit.AddDynamic(this, &USpringBackComponent::OnComponentHit);

		if (NoBouncePhysicalMaterial)
		{
			TargetComponent->SetPhysMaterialOverride(NoBouncePhysicalMaterial);
		}
		else
		{
			UPhysicalMaterial* TempPhysMat = NewObject<UPhysicalMaterial>(this);
			TempPhysMat->Restitution = 0.0f;
			TempPhysMat->Friction = 0.8f;
			TargetComponent->SetPhysMaterialOverride(TempPhysMat);
		}


		// 记录质量
		ObjectMass = TargetComponent->GetMass();

		// 若未设置阻尼系数，自动计算临界阻尼值（c = 2√(m*k)）
		if (DampingCoefficient == 0.0f)
		{
			DampingCoefficient = 2 * FMath::Sqrt(ObjectMass * SpringStiffness);
		}

		UE_LOG(LogTemp, Log, TEXT("SpringBackComponent: 成功找到并设置目标组件 %s"), *ComponentName.ToString());
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 未找到名为 %s 的PrimitiveComponent"), *ComponentName.ToString());
		return false;
	}
}


void USpringBackComponent::ApplySpringForce(float DeltaTime)
{
	if (!TargetComponent || !TargetComponent->IsSimulatingPhysics()) return;

	FVector CurrentPosition = TargetComponent->GetComponentLocation();
	FVector TargetPosition = CalculateMoveTargetPosition();
	FVector Offset = TargetPosition - CurrentPosition;
	float Distance = Offset.Size();
	
	// 限制运动范围
	if (Distance > MovementRange)
	{
		Offset = Offset.GetSafeNormal() * MovementRange;
		CurrentPosition = TargetPosition - Offset;
		TargetComponent->SetWorldLocation(CurrentPosition);
		TargetComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CurrentVelocity = FVector::ZeroVector;
	}
	
	FVector SpringForce = SpringStiffness * Offset;
	FVector DampingForce = DampingCoefficient * CurrentVelocity;
	FVector TotalForce = SpringForce - DampingForce;

	// 限制最大力防止过度振荡
	TotalForce = TotalForce.GetClampedToMaxSize(10000.0f);
    
	TargetComponent->AddForce(TotalForce);
	CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();

	// 最后确保位置不低于最小高度
	ClampToMinHeight();
	
}



bool USpringBackComponent::FindAndSetParentComponentByName(FName ComponentName)
{
	ParentComponent = FindPrimitiveComponentByName(ComponentName);
    
	if (ParentComponent)
	{
		ParentComponent->SetGenerateOverlapEvents(true);
		ParentComponent->OnComponentBeginOverlap.AddDynamic(this,&USpringBackComponent::ParentBeginOverlap);
		ParentComponent->OnComponentEndOverlap.AddDynamic(this,  &USpringBackComponent::ParentEndOverlap);
		UE_LOG(LogTemp, Log, TEXT("SpringBackComponent: 成功找到并设置父组件 %s"), *ComponentName.ToString());
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 未找到名为 %s 的SceneComponent"), *ComponentName.ToString());
		return false;
	}
}

void USpringBackComponent::ParentBeginOverlap(UPrimitiveComponent* OverlappedComponent,AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// 忽略掉TargetComponent
	if(OverlappedComponent == TargetComponent)
		return;
	UE_LOG(LogTemp, Warning, TEXT("Begin Overlap"));
	bCurrentlyParentInContact = true;
}

void USpringBackComponent::ParentEndOverlap(UPrimitiveComponent* OverlappedComponent,AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	// 忽略掉TargetComponent
	if(OverlappedComponent == TargetComponent)
		return;
	UE_LOG(LogTemp, Warning, TEXT("End Overlap"));
	bCurrentlyParentInContact = false;
}


UPrimitiveComponent* USpringBackComponent::FindPrimitiveComponentByName(FName NameToFind)
{
	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	TArray<UActorComponent*> AllComponents;
	Owner->GetComponents(AllComponents);

	for (UActorComponent* Component : AllComponents)
	{
		if (Component && Component->GetFName() == NameToFind)
		{
			return Cast<UPrimitiveComponent>(Component);
		}
	}

	// 递归查找子组件
	USceneComponent* RootComp = Owner->GetRootComponent();
	if (RootComp)
	{
		TArray<USceneComponent*> ChildrenComponents;
		RootComp->GetChildrenComponents(true, ChildrenComponents);

		for (USceneComponent* ChildComp : ChildrenComponents)
		{
			if (ChildComp && ChildComp->GetFName() == NameToFind)
			{
				return Cast<UPrimitiveComponent>(ChildComp);
			}
		}
	}

	return nullptr;
}

FVector USpringBackComponent::CalculateMoveTargetPosition() const
{
	if (ParentComponent)
	{
		// 基于父组件的当前位置和相对偏移计算目标位置[2](@ref)
		return ParentComponent->GetComponentTransform().TransformPosition(RelativeOffset);
	}
	else
	{
		// 如果没有父组件，使用目标组件的初始位置（回退行为）
		return TargetComponent ? TargetComponent->GetComponentLocation() : FVector::ZeroVector;
	}
}
