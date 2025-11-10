// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicsTest/Public/SpringBackComponent.h"

#include "MovieSceneTracksComponentTypes.h"
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
	if (!ParentComponent || !bEnableCollisionDetection) return false;
    
	float CurrentTime = GetWorld()->GetTimeSeconds();
    
	// 1：检查最近是否有碰撞事件
	bool bRecentlyCollided = (CurrentTime - LastCollisionTime) < CollisionCooldownTime;
    
	// 2：使用扫描检测当前是否有接触
	bool bCurrentlyInContact = CheckContactWithSweep();
    
	return bRecentlyCollided || bCurrentlyInContact;

}

bool USpringBackComponent::CheckContactWithSweep()
{
	if (!ParentComponent || !GetWorld()) return false;
    
	FVector Start = ParentComponent->GetComponentLocation();
    
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
		bTargetShouldUsePhysics = true;
		TargetComponent->SetPhysicsLinearVelocity(FVector(0,0,0));
		
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
		bTargetIsUsingPhysics = false;
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
	
	
	if (TargetComponent && ParentComponent)
	{
		// 设置最后有效位置
		LastValidPosition = TargetComponent->GetComponentLocation();
        
		// 确保初始位置不低于最小高度
		ClampToMinHeight();
	}
	
}

void USpringBackComponent::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bEnableCollisionDetection || !TargetComponent) return;
    
	// 忽略与自己的碰撞
	if (OtherActor == GetOwner()) return;    

	LastCollisionTime = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Warning, TEXT("Hit %f"),LastCollisionTime);

	bCurrentlyTargetHit = true;
	
	// 切换到物理模拟模式
	EnablePhysicsSimulation();
    
	// 碰撞时更新速度估计
	CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();
}


void USpringBackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
										 FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!TargetComponent) return;


	// 1. 状态切换
	if (!bCurrentlyParentInContact)
	{
		if (bCurrentlyTargetHit)
		{
			// 切入物理状态
			EnablePhysicsSimulation();
			bTargetShouldUsePhysics = true;
			bCurrentlyParentInContact = true;
		}
	}

	if (bTargetShouldUsePhysics)
	{
		if (!bCurrentlyParentInContact)
		{
			// 切出物理状态
			bTargetShouldUsePhysics = false;
			EnableSnapMode();
		}
	}

	// 2. 不同状态下应用不同的函数
	if (bTargetIsUsingPhysics)
	{        
		// 应用弹簧力
		ApplySpringForce(DeltaTime);
	}
	else
	{
		// 吸附到Parent
		SnapToTargetPosition();
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
		TargetComponent->OnComponentHit.AddDynamic(this, &USpringBackComponent::OnComponentHit);

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

	// 先应用高度限制
	ApplyHeightLimit(DeltaTime);

	// 如果低于最小高度，优先处理高度限制
	if (bIsBelowMinHeight && bEnableHeightLimit)
	{
		// 在高度限制期间，减弱水平方向的弹簧力
		Offset.Z = 0.0f; // 只处理水平方向
		Distance = Offset.Size();
	}
	
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

void USpringBackComponent::ApplyHeightLimit(float DeltaTime)
{
	if (!TargetComponent || !ParentComponent || !bEnableHeightLimit || !TargetComponent->IsSimulatingPhysics()) 
		return;
    
	float CurrentHeight = GetCurrentRelativeHeight();
    
	if (CurrentHeight < MinHeightRelative)
	{
		// 计算需要施加的力来推高球体
		float HeightDeficit = MinHeightRelative - CurrentHeight;
        
		// 将高度差转换为世界空间的偏移
		FVector WorldUp = ParentComponent->GetUpVector(); // 父组件的上方向
		FVector ForceDirection = WorldUp;
        
		// 计算力的大小（基于高度差和刚度）
		float ForceMagnitude = HeightLimitStiffness * HeightDeficit;
        
		// 应用力
		FVector CorrectiveForce = ForceDirection * ForceMagnitude;
		TargetComponent->AddForce(CorrectiveForce);
        
		bIsBelowMinHeight = true;
        
		// 调试输出
		if (bShowHeightLimit)
		{
			UE_LOG(LogTemp, Verbose, TEXT("Applying height limit force: %.2f, Deficit: %.2f"), ForceMagnitude, HeightDeficit);
		}
	}
	else
	{
		bIsBelowMinHeight = false;
	}
}

float USpringBackComponent::GetCurrentRelativeHeight() const
{
	if (!TargetComponent || !ParentComponent) 
		return 0.0f;
    
	// 将球体的世界坐标转换到父组件的局部空间
	FVector TargetWorldPos = TargetComponent->GetComponentLocation();
	FVector LocalPos = ParentComponent->GetComponentTransform().InverseTransformPosition(TargetWorldPos);
    
	// 返回局部空间的Z坐标（高度）
	return LocalPos.Z;
}

void USpringBackComponent::ClampToMinHeight()
{
	if (!TargetComponent || !ParentComponent || !bEnableHeightLimit) 
		return;
    
	float CurrentHeight = GetCurrentRelativeHeight();
    
	if (CurrentHeight < MinHeightRelative)
	{
		// 计算应该达到的最小高度位置（世界坐标）
		FVector DesiredLocalPos = ParentComponent->GetComponentTransform().InverseTransformPosition(TargetComponent->GetComponentLocation());
		DesiredLocalPos.Z = MinHeightRelative;
		FVector DesiredWorldPos = ParentComponent->GetComponentTransform().TransformPosition(DesiredLocalPos);
        
		// 设置位置
		TargetComponent->SetWorldLocation(DesiredWorldPos);
        
		// 重置Z轴速度
		FVector Velocity = TargetComponent->GetPhysicsLinearVelocity();
		Velocity.Z = 0.0f;
		TargetComponent->SetPhysicsLinearVelocity(Velocity);
        
		bIsBelowMinHeight = true;
		LastValidPosition = DesiredWorldPos;
	}
	else
	{
		bIsBelowMinHeight = false;
		LastValidPosition = TargetComponent->GetComponentLocation();
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
