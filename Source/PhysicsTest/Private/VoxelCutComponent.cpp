// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCutComponent.h"
#include "Engine/Engine.h"


UVoxelCutComponent::UVoxelCutComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
    
	CutState = ECutState::Idle;
	bIsCutting = false;
	DistanceSinceLastUpdate = 0.0f;	
}

void UVoxelCutComponent::BeginPlay()
{
	Super::BeginPlay();
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
}

void UVoxelCutComponent::SetCutToolMesh(UDynamicMeshComponent* ToolMeshComp)
{
	CutToolMeshComponent = ToolMeshComp;
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
		if (TargetMeshComponent)
		{
			UDynamicMesh* DynamicMesh = TargetMeshComponent->GetDynamicMesh();
			if (DynamicMesh)
			{
				DynamicMesh->SetMesh(*ResultMesh);    
				TargetMeshComponent->NotifyMeshUpdated(); 
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
	CutOp->UpdateMargin = 5;
	CutOp->CutToolMesh = CopyToolMesh();
	
    
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

		// 体素化切割目标（只做一次）
		CutOp->InitializeVoxelData(nullptr);
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

void UVoxelCutComponent::UpdateStateMachine()
{
	FScopeLock Lock(&StateLock);
    
	switch (CutState)
	{
	case ECutState::Idle:		
		break;        
	case ECutState::RequestPending:
		StartAsyncCut();		
		break;        
	case ECutState::Processing:
		break;        
	case ECutState::Completed:
		CutState = ECutState::Idle;		
		break;
	}
}

void UVoxelCutComponent::RequestCut(const FTransform& ToolTransform)
{
	FScopeLock Lock(&StateLock);
    
	// 保存当前请求数据
	CurrentToolTransform = ToolTransform;
	
	//UE_LOG(LogTemp, Warning, TEXT("Cut State: %s"),*StaticEnum<ECutState>()->GetNameStringByValue((int64)CutState));
	
	// 只有在空闲状态或有新请求时才更新
	if (CutState == ECutState::Idle || CutState == ECutState::Completed)
	{		
		CutState = ECutState::RequestPending;
		UE_LOG(LogTemp, Warning, TEXT("Request Pending"));
	}
}

void UVoxelCutComponent::StartAsyncCut()
{
	FScopeLock Lock(&StateLock);
	// 如果已经在处理，则退出
	if (CutState==ECutState::Processing)
		return;
	CutState = ECutState::Processing;
    
    // 复制当前状态到局部变量（避免竞态条件）
    FTransform LocalToolTransform = CurrentToolTransform;
	
    CutOp->CutToolTransform = LocalToolTransform;
    
    // 在异步线程中执行实际切削计算
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