// Fill out your copyright notice in the Description page of Project Settings.


#include "PhysicsTest/Public/SpringBackComponent.h"


// Sets default values for this component's properties
USpringBackComponent::USpringBackComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
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
	
}

void USpringBackComponent::OnComponentHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	if (TargetComponent)
	{
		// 碰撞时更新速度估计，确保回弹响应及时
		CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();
	}
}

bool USpringBackComponent::FindAndSetTargetComponentByName(FName ComponentName)
{
	TargetComponent = FindPrimitiveComponentByName(ComponentName);
    
	if (TargetComponent)
	{
		// 启用物理模拟以确保碰撞检测和力作用
		TargetComponent->SetSimulatePhysics(true);
		// 启用碰撞事件触发
		TargetComponent->SetNotifyRigidBodyCollision(true);
		TargetComponent->OnComponentHit.AddDynamic(this, &USpringBackComponent::OnComponentHit);

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


UPrimitiveComponent* USpringBackComponent::FindComponentByName(FName NameToFind)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	// 方法1：获取所有组件并遍历查找[5](@ref)
	TArray<UActorComponent*> AllComponents;
	Owner->GetComponents(AllComponents);

	for (UActorComponent* Component : AllComponents)
	{
		if (Component && Component->GetFName() == NameToFind)
		{
			UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component);
			if (PrimComp)
			{
				return PrimComp;
			}
		}
	}

	// 方法2：如果方法1没找到，尝试通过递归查找子组件[5](@ref)
	USceneComponent* RootComp = Owner->GetRootComponent();
	if (RootComp)
	{
		TArray<USceneComponent*> ChildrenComponents;
		RootComp->GetChildrenComponents(true, ChildrenComponents); // true表示递归查找所有子级

		for (USceneComponent* ChildComp : ChildrenComponents)
		{
			if (ChildComp && ChildComp->GetFName() == NameToFind)
			{
				UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(ChildComp);
				if (PrimComp)
				{
					return PrimComp;
				}
			}
		}
	}

	return nullptr;
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
	if (!TargetComponent) return;

	FVector CurrentPosition = TargetComponent->GetComponentLocation();
	FVector TargetPosition = CalculateTargetPosition();
	FVector Offset = TargetPosition - CurrentPosition;
	float Distance = Offset.Size();

	// 限制运动范围：若超出范围，将位置强制拉回边界
	if (Distance > MovementRange)
	{
		Offset = Offset.GetSafeNormal() * MovementRange;
		CurrentPosition = TargetPosition - Offset;
		TargetComponent->SetWorldLocation(CurrentPosition);
		// 重置速度以防止惯性溢出
		TargetComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		CurrentVelocity = FVector::ZeroVector;
	}

	// 计算弹簧力 F = k*x - c*v （临界阻尼模型）[3](@ref)
	FVector SpringForce = SpringStiffness * Offset; // 朝向目标位置的弹性力
	FVector DampingForce = DampingCoefficient * CurrentVelocity; // 与速度反向的阻尼力
	FVector TotalForce = SpringForce - DampingForce; // 合力

	// 应用力到物理体
	TargetComponent->AddForce(TotalForce);

	// 更新速度估计（用于下一帧阻尼计算）
	CurrentVelocity = TargetComponent->GetPhysicsLinearVelocity();
}

// Called every frame
void USpringBackComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                         FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (TargetComponent && TargetComponent->IsSimulatingPhysics())
	{
		ApplySpringForce(DeltaTime);
	}

	// 调试绘制：显示运动范围球体
	if (bShowMovementRange && TargetComponent && ParentComponent)
	{
		FVector TargetPos = CalculateTargetPosition();
		DrawDebugSphere(GetWorld(), TargetPos, MovementRange, 12, FColor::Green, false, -1.0f, 0, 2.0f);
	}
}

bool USpringBackComponent::FindAndSetParentComponentByName(FName ComponentName)
{
	ParentComponent = FindSceneComponentByName(ComponentName);
    
	if (ParentComponent)
	{
		// 计算初始相对偏移
		if (TargetComponent)
		{
			FVector TargetWorldPos = TargetComponent->GetComponentLocation();
			FVector ParentWorldPos = ParentComponent->GetComponentLocation();
			InitialRelativeOffset = ParentComponent->GetComponentTransform().InverseTransformPosition(TargetWorldPos);
			RelativeOffset = InitialRelativeOffset;
		}
        
		UE_LOG(LogTemp, Log, TEXT("SpringBackComponent: 成功找到并设置父组件 %s"), *ComponentName.ToString());
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SpringBackComponent: 未找到名为 %s 的SceneComponent"), *ComponentName.ToString());
		return false;
	}
}

void USpringBackComponent::UpdateRelativeOffset()
{
	if (TargetComponent && ParentComponent)
	{
		// 更新相对偏移为当前位置相对于父组件的位置[4](@ref)
		FVector TargetWorldPos = TargetComponent->GetComponentLocation();
		RelativeOffset = ParentComponent->GetComponentTransform().InverseTransformPosition(TargetWorldPos);
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
