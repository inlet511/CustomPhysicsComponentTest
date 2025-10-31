// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicsTest/Public/SpringBackComponent.h"
#include "Engine/OverlapResult.h"



USpringBackComponent::USpringBackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


void USpringBackComponent::SnapToTargetPosition()
{
	if (!TargetComponent) return;
    
	FVector TargetPos = CalculateTargetPosition();
	TargetComponent->SetWorldLocation(TargetPos);
	TargetComponent->SetWorldRotation(ParentComponent ? ParentComponent->GetComponentRotation() : FRotator::ZeroRotator);
    
	// 重置速度
	CurrentVelocity = FVector::ZeroVector;
}


bool USpringBackComponent::CheckForCollisions()
{
	if (!TargetComponent || !bEnableCollisionDetection) return false;
    
	float CurrentTime = GetWorld()->GetTimeSeconds();
    
	// 方法1：检查最近是否有碰撞事件
	bool bRecentlyCollided = (CurrentTime - LastCollisionTime) < CollisionCooldownTime;
    
	// 方法2：使用扫描检测当前是否有接触
	bool bCurrentlyInContact = CheckContactWithSweep();
    
	return bRecentlyCollided || bCurrentlyInContact;

}

bool USpringBackComponent::CheckContactWithSweep()
{
	if (!TargetComponent || !GetWorld()) return false;
    
	FVector Start = TargetComponent->GetComponentLocation();
    
	// 向多个方向发射短距离扫描检测接触
	TArray<FVector> Directions = {
		FVector(1, 0, 0), FVector(-1, 0, 0),
		FVector(0, 1, 0), FVector(0, -1, 0), 
		FVector(0, 0, 1), FVector(0, 0, -1)
	};
    
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
    
	float SweepDistance = ContactDetectionRadius;
    
	for (const FVector& Dir : Directions)
	{
		FVector End = Start + Dir * SweepDistance;
		FHitResult HitResult;
        
		bool bHit = GetWorld()->SweepSingleByChannel(
			HitResult,
			Start,
			End,
			FQuat::Identity,
			TargetComponent->GetCollisionObjectType(),
			FCollisionShape::MakeSphere(SweepDistance * 0.5f), // 使用小球体形状
			QueryParams
		);
        
		if (bHit && HitResult.GetComponent() && 
			HitResult.GetComponent()->GetOwner() != GetOwner())
		{
			return true;
		}
	}
    
	return false;
}

bool USpringBackComponent::CheckContactWithMultiSphere()
{
	if (!TargetComponent || !GetWorld()) return false;
    
	FVector Center = TargetComponent->GetComponentLocation();
	float Radius = ContactDetectionRadius;
    
	// 在多个位置检测重叠
	TArray<FVector> TestLocations = {
		Center,
		Center + FVector(Radius * 0.5f, 0, 0),
		Center + FVector(-Radius * 0.5f, 0, 0),
		Center + FVector(0, Radius * 0.5f, 0),
		Center + FVector(0, -Radius * 0.5f, 0),
		Center + FVector(0, 0, Radius * 0.5f),
		Center + FVector(0, 0, -Radius * 0.5f)
	};
    
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(GetOwner());
    
	for (const FVector& TestLocation : TestLocations)
	{
		TArray<FOverlapResult> OverlapResults;
		bool bHasOverlap = GetWorld()->OverlapMultiByChannel(
			OverlapResults,
			TestLocation,
			FQuat::Identity,
			TargetComponent->GetCollisionObjectType(),
			FCollisionShape::MakeSphere(Radius * 0.3f),
			QueryParams
		);
        
		if (bHasOverlap)
		{
			for (const FOverlapResult& Result : OverlapResults)
			{
				if (Result.GetComponent() && 
					Result.GetComponent() != TargetComponent &&
					Result.GetComponent()->GetOwner() != GetOwner())
				{
					return true;
				}
			}
		}
	}
    
	return false;
}


void USpringBackComponent::EnablePhysicsSimulation()
{
	if (!TargetComponent) return;
    
	if (!TargetComponent->IsSimulatingPhysics())
	{
		TargetComponent->SetSimulatePhysics(true);
		UE_LOG(LogTemp, Verbose, TEXT("SpringBackComponent: 切换到物理模拟模式"));
	}
}

void USpringBackComponent::EnableSnapMode()
{
	if (!TargetComponent) return;
    
	if (TargetComponent->IsSimulatingPhysics())
	{
		// 先停止物理模拟
		TargetComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		TargetComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		TargetComponent->SetSimulatePhysics(false);
	}
    
	// 立即吸附到目标位置
	SnapToTargetPosition();
	UE_LOG(LogTemp, Verbose, TEXT("SpringBackComponent: 切换到吸附模式"));
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
		EnableSnapMode();
	}
	
}

void USpringBackComponent::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bEnableCollisionDetection || !TargetComponent) return;
    
	// 忽略与自己的碰撞
	if (OtherActor == GetOwner()) return;    

	LastCollisionTime = GetWorld()->GetTimeSeconds();
    
	// 切换到物理模拟模式
	EnablePhysicsSimulation();
    
	// 碰撞时更新速度估计
	CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();
}

bool USpringBackComponent::FindAndSetTargetComponentByName(FName ComponentName)
{
	TargetComponent = FindPrimitiveComponentByName(ComponentName);
    
	if (TargetComponent)
	{
		// 启用物理模拟以确保碰撞检测和力作用
		TargetComponent->SetSimulatePhysics(true);
		TargetComponent->SetNotifyRigidBodyCollision(true);
		TargetComponent->OnComponentHit.AddDynamic(this, &USpringBackComponent::OnComponentHit);
		// TargetComponent->OnComponentBeginOverlap.AddDynamic(this, &USpringBackComponent::OnComponentBeginOverlap);
		// TargetComponent->OnComponentEndOverlap.AddDynamic(this, &USpringBackComponent::OnComponentEndOverlap);		
		// TargetComponent->SetGenerateOverlapEvents(true);


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


USceneComponent* USpringBackComponent::FindSceneComponentByName(FName NameToFind)
{
	AActor* Owner = GetOwner();
	if (!Owner) return nullptr;

	TArray<UActorComponent*> AllComponents;
	Owner->GetComponents(AllComponents);

	for (UActorComponent* Component : AllComponents)
	{
		if (Component && Component->GetFName() == NameToFind)
		{
			return Cast<USceneComponent>(Component);
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
				return ChildComp;
			}
		}
	}

	return nullptr;
}

void USpringBackComponent::ApplySpringForce(float DeltaTime)
{
	if (!TargetComponent || !TargetComponent->IsSimulatingPhysics()) return;

	FVector CurrentPosition = TargetComponent->GetComponentLocation();
	FVector TargetPosition = CalculateTargetPosition();
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

	// 使用更强的弹簧力确保快速回归
	FVector SpringForce = SpringStiffness * Offset;
	FVector DampingForce = DampingCoefficient * CurrentVelocity;
	FVector TotalForce = SpringForce - DampingForce;

	// 限制最大力防止过度振荡
	TotalForce = TotalForce.GetClampedToMaxSize(10000.0f);
    
	TargetComponent->AddForce(TotalForce);
	CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();
	
}

// Called every frame
void USpringBackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetComponent) return;

	bool bShouldUsePhysics = CheckForCollisions();
	
	// 状态切换
	if (CheckForCollisions())
	{
		// 确保启用物理模拟
		if (!TargetComponent->IsSimulatingPhysics())
		{
			EnablePhysicsSimulation();
		}
        
		// 应用弹簧力
		ApplySpringForce(DeltaTime);
	}
	else
	{
		// 切换到吸附模式
		if (TargetComponent->IsSimulatingPhysics())
		{
			// 添加小延迟避免抖动
			float CurrentTime = GetWorld()->GetTimeSeconds();
			if ((CurrentTime - LastCollisionTime) > CollisionCooldownTime)
			{
				EnableSnapMode();
			}
		}
		else
		{
			// 确保位置正确跟随
			SnapToTargetPosition();
		}
	}

	// 调试绘制
	if (bShowMovementRange && TargetComponent && ParentComponent)
	{
		FVector TargetPos = CalculateTargetPosition();
		DrawDebugSphere(GetWorld(), TargetPos, MovementRange, 12, FColor::Green, false, -1.0f, 0, 2.0f);
        
		// 显示接触检测范围
		DrawDebugSphere(GetWorld(), TargetPos, ContactDetectionRadius, 8, FColor::Yellow, false, -1.0f, 0, 1.0f);
        
		FString StatusText = FString::Printf(TEXT("物理模式: %s"), 
			bShouldUsePhysics ? TEXT("是") : TEXT("否"));
		DrawDebugString(GetWorld(), TargetPos + FVector(0,0,60), StatusText, nullptr, 
					   bShouldUsePhysics ? FColor::Red : FColor::Green, 0, true);
	}
}

bool USpringBackComponent::FindAndSetParentComponentByName(FName ComponentName)
{
	ParentComponent = FindSceneComponentByName(ComponentName);
    
	if (ParentComponent)
	{        
		UE_LOG(LogTemp, Log, TEXT("SpringBackComponent: 成功找到并设置父组件 %s"), *ComponentName.ToString());
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 未找到名为 %s 的SceneComponent"), *ComponentName.ToString());
		return false;
	}
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

FVector USpringBackComponent::CalculateTargetPosition() const
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
