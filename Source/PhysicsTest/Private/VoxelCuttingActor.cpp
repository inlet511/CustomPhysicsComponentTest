// Fill out your copyright notice in the Description page of Project Settings.


#include "VoxelCuttingActor.h"

#include "DynamicMeshActor.h"


// Sets default values
AVoxelCuttingActor::AVoxelCuttingActor()
{
	PrimaryActorTick.bCanEverTick = true;
    
	// 创建根组件
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));    
    
	// 创建切削组件
	VoxelCutComponent = CreateDefaultSubobject<UVoxelCutComponent>(TEXT("VoxelCutComponent"));
}

void AVoxelCuttingActor::SetTargetActor(ADynamicMeshActor* InTargetActor)
{
	TargetActor = InTargetActor;
    
	if (TargetActor && VoxelCutComponent)
	{		
		TargetMeshComponent = TargetActor->GetDynamicMeshComponent();
        
		if (TargetMeshComponent)
		{
			VoxelCutComponent->SetTargetMesh(TargetMeshComponent);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AVoxelCuttingTool: Target actor has no DynamicMeshComponent"));
		}
	}
}

void AVoxelCuttingActor::SetCutToolActor(ADynamicMeshActor* InCutToolActor)
{
	CuttingToolActor = InCutToolActor;
	if (CuttingToolActor && VoxelCutComponent)
	{
		CutToolComponent = CuttingToolActor->GetDynamicMeshComponent();
		if (CutToolComponent)
		{
			VoxelCutComponent->SetCutToolMesh(CutToolComponent);		
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("AVoxelCuttingTool: Cut Tool actor has no DynamicMeshComponent"));
		}
	}
}

void AVoxelCuttingActor::StartCutting()
{
	if (VoxelCutComponent)
	{
		VoxelCutComponent->StartCutting();
	}
}

void AVoxelCuttingActor::StopCutting()
{
	if (VoxelCutComponent)
	{
		VoxelCutComponent->StopCutting();
	}
}

// Called when the game starts or when spawned
void AVoxelCuttingActor::BeginPlay()
{
	Super::BeginPlay();
    
	// 设置工具网格
	if (VoxelCutComponent)
	{
		VoxelCutComponent->SetCutToolMesh(CutToolComponent);
	}
	
}

// Called every frame
void AVoxelCuttingActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

