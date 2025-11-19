// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCutComponent.h"
#include "Engine/Engine.h"


UVoxelCutComponent::UVoxelCutComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
    
	CutState = ECutState::Idle;
	bIsCutting = false;
	bProcessing = false;
	DistanceSinceLastUpdate = 0.0f;
    
	PersistentVoxelData = MakeShared<FMaVoxelData>();
}

void UVoxelCutComponent::BeginPlay()
{
	Super::BeginPlay();

	CreateResultMeshComponent();	
}


void UVoxelCutComponent::TickComponent(float DeltaTime, ELevelTick TickType,
									   FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsCutting || !CutToolMeshComponent || !TargetMeshComponent)
		return;

	// 获取当前工具位置
	FTransform CurrentTransform = CutToolMeshComponent->GetComponentTransform();
    
	// 检查是否需要切削更新
	if (NeedsCutUpdate(CurrentTransform))
	{
		RequestCut(CurrentTransform);
        
		// 重置距离计数
		DistanceSinceLastUpdate = 0.0f;
		LastToolPosition = CurrentTransform.GetLocation();
		LastToolRotation = CurrentTransform.GetRotation().Rotator();
	}
	else
	{
		DistanceSinceLastUpdate += FVector::Distance(LastToolPosition, CurrentTransform.GetLocation());
	}
    
	// 更新状态机
	UpdateStateMachine();
}

void UVoxelCutComponent::SetTargetMesh(UDynamicMeshComponent* TargetMeshComp)
{	
	TargetMeshComponent = TargetMeshComp;
    
	if (!bSystemInitialized && TargetMeshComponent && CutToolMeshComponent)
	{
		InitializeCutSystem();
	}
}

void UVoxelCutComponent::SetCutToolMesh(UDynamicMeshComponent* ToolMeshComp)
{
	CutToolMeshComponent = ToolMeshComp;
	if (!bSystemInitialized && TargetMeshComponent && CutToolMeshComponent)
	{
		InitializeCutSystem();
	}
}

void UVoxelCutComponent::StartCutting()
{
	bIsCutting = true;
    
	// 记录初始工具位置
	if (CutToolMeshComponent)
	{
		LastToolPosition = CutToolMeshComponent->GetComponentLocation();
		LastToolRotation = CutToolMeshComponent->GetComponentRotation();
	}
    
	DistanceSinceLastUpdate = 0.0f;
}

void UVoxelCutComponent::StopCutting()
{
	bIsCutting = false;
}



void UVoxelCutComponent::OnCutComplete(FDynamicMesh3* ResultMesh)
{
	if (ResultMesh && ResultMesh->TriangleCount() > 0)
	{
		// 更新结果网格
		if (ResultMeshComponent)
		{
			UDynamicMesh* DynamicMesh = ResultMeshComponent->GetDynamicMesh();
			if (DynamicMesh)
			{
				DynamicMesh->SetMesh(*ResultMesh);                
				ResultMeshComponent->NotifyMeshUpdated();
                
				// 复制材质
				if (TargetMeshComponent)
				{
					for (int32 i = 0; i < TargetMeshComponent->GetNumMaterials(); i++)
					{
						UMaterialInterface* Material = TargetMeshComponent->GetMaterial(i);
						if (Material)
						{
							ResultMeshComponent->SetMaterial(i, Material);
						}
					}
				}
			}
		}
	}
    
	// 更新状态
	FScopeLock Lock(&StateLock);
	CutState = ECutState::Completed;
}

void UVoxelCutComponent::InitializeCutSystem()
{
	if (bSystemInitialized || !TargetMeshComponent || !CutToolMeshComponent)
		return;
    
	// 创建切削操作器（只创建一次）
	if (!CutOp.IsValid())
	{
		CutOp = MakeShared<FVoxelCutMeshOp>();
	}
    
	// 设置基础参数
	CutOp->VoxelSize = VoxelSize;
	CutOp->bSmoothCutEdges = bSmoothEdges;
	CutOp->SmoothingStrength = SmoothingStrength;
	CutOp->bFillCutHole = bFillHoles;
	CutOp->bIncrementalUpdate = true;
	CutOp->UpdateMargin = 3;
    
	// 设置持久化数据
	CutOp->PersistentVoxelData = PersistentVoxelData;
    
	// 获取目标网格数据
	UDynamicMesh* TargetDynamicMesh = TargetMeshComponent->GetDynamicMesh();
	if (TargetDynamicMesh)
	{
		// 创建目标网格的副本（只做一次）
		CutOp->TargetMesh = MakeShared<FDynamicMesh3>();
		TargetDynamicMesh->ProcessMesh([this](const FDynamicMesh3& SourceMesh)
		{
			CutOp->TargetMesh->Copy(SourceMesh);
		});
        
		// 设置目标变换
		CutOp->TargetTransform = TargetMeshComponent->GetComponentTransform();
	}

	bSystemInitialized = true;
}

bool UVoxelCutComponent::NeedsCutUpdate(const FTransform& InCurrentToolTransform)
{
	float Distance = FVector::Distance(LastToolPosition, InCurrentToolTransform.GetLocation());
	float AngleDiff = FQuat::Error(LastToolRotation.Quaternion(), InCurrentToolTransform.GetRotation());
	return (DistanceSinceLastUpdate + Distance >= UpdateThreshold) || 
		   (AngleDiff > FMath::DegreesToRadians(5.0f));
}

void UVoxelCutComponent::CreateResultMeshComponent()
{
	AActor* Owner = GetOwner();
	if (!Owner)
		return;
    
	ResultMeshComponent = NewObject<UDynamicMeshComponent>(Owner, TEXT("ResultMeshComponent"));
	ResultMeshComponent->SetupAttachment(Owner->GetRootComponent());
	ResultMeshComponent->RegisterComponent();
	ResultMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void UVoxelCutComponent::UpdateStateMachine()
{
	FScopeLock Lock(&StateLock);
    
	switch (CutState.load())
	{
	case ECutState::Idle:
		// 空闲状态，等待请求
		break;
        
	case ECutState::RequestPending:
		// 有请求待处理，开始异步切削
		if (!bProcessing)
		{
			StartAsyncCut();
			CutState = ECutState::Processing;
		}
		break;
        
	case ECutState::Processing:
		// 正在处理，等待完成
		break;
        
	case ECutState::Completed:
		// 切削完成，准备处理下一个请求
		bProcessing = false;
        
		// 检查是否有新的请求
		if (CutState == ECutState::Completed) // 双重检查，避免竞态条件
		{
			CutState = ECutState::Idle;
		}
		break;
	}
}

void UVoxelCutComponent::RequestCut(const FTransform& ToolTransform)
{
	FScopeLock Lock(&StateLock);
    
	// 保存当前请求数据
	CurrentToolTransform = ToolTransform;

	ECutState currentState = CutState.load();
	
	UE_LOG(LogTemp, Warning, TEXT("Cut State: %s"),*StaticEnum<ECutState>()->GetNameStringByValue((int64)currentState));
	ECutState S = currentState;

	FString Msg = FString::Printf(TEXT("Cut State: %s"), *StaticEnum<ECutState>()->GetNameStringByValue((int64)S));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, Msg);
	}
	
	// 只有在空闲状态或有新请求时才更新
	if (currentState == ECutState::Idle || currentState == ECutState::Completed)
	{		
		CutState = ECutState::RequestPending;		
	}
	// 如果正在处理，新的请求会覆盖当前等待的请求
	// 这样确保我们总是处理最新的工具位置
}

void UVoxelCutComponent::StartAsyncCut()
{
	if (bProcessing)
        return;
        
    bProcessing = true;
    
    // 复制当前状态到局部变量（避免竞态条件）
    FTransform LocalToolTransform = CurrentToolTransform;
    
    // 在异步线程中复制工具网格（这是必要的，因为工具网格可能变化）
    Async(EAsyncExecution::ThreadPool, [this, LocalToolTransform]()
    {
        // 复制工具网格（在异步线程中执行，避免阻塞主线程）
        TSharedPtr<FDynamicMesh3> LocalToolMesh = CopyToolMesh();
        
        // 回到主线程设置操作器参数（确保线程安全）
        Async(EAsyncExecution::TaskGraphMainThread, [this, LocalToolTransform, LocalToolMesh]()
        {
            // 设置操作器参数
            CutOp->CutToolMesh = LocalToolMesh;
            CutOp->CutToolTransform = LocalToolTransform;
            
            // 在另一个异步线程中执行实际切削计算
            Async(EAsyncExecution::ThreadPool, [this]()
            {
                try
                {
                    CutOp->CalculateResult(nullptr);
                    
                    // 切削完成，回到主线程
                    Async(EAsyncExecution::TaskGraphMainThread, [this]()
                    {
                        FDynamicMesh3* ResultMesh = CutOp->GetResultMesh();                        
                        
                        OnCutComplete(ResultMesh);
                    });
                }
                catch (const std::exception& e)
                {
                    UE_LOG(LogTemp, Error, TEXT("Cut operation failed: %s"), UTF8_TO_TCHAR(e.what()));
                    
                    Async(EAsyncExecution::TaskGraphMainThread, [this]()
                    {
                        // 即使失败也标记为完成
                        FScopeLock Lock(&StateLock);
                        CutState = ECutState::Completed;
                    });
                }
            });
        });
    });
}

TSharedPtr<FDynamicMesh3> UVoxelCutComponent::CopyToolMesh()
{
	if (!CutToolMeshComponent)
		return nullptr;
    
	TSharedPtr<FDynamicMesh3> CopiedMesh = MakeShared<FDynamicMesh3>();
    
	UDynamicMesh* SourceMesh = CutToolMeshComponent->GetDynamicMesh();
	if (SourceMesh)
	{
		SourceMesh->ProcessMesh([&CopiedMesh](const FDynamicMesh3& SourceMeshData)
		{
			CopiedMesh->Copy(SourceMeshData);
		});
	}
    
	return CopiedMesh;
}