// Fill out your copyright notice in the Description page of Project Settings.


#include "CuttableObject.h"
#include "GeometryScript/MeshAssetFunctions.h"
#include "DynamicMesh/MeshNormals.h"

// Sets default values
ACuttableObject::ACuttableObject()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// 创建根组件
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));   
	
    
	// 启用碰撞
	GetDynamicMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}


void ACuttableObject::AssignMesh(UStaticMesh* InMesh)
{
	if (!IsValid(InMesh))
		return;
	
	UDynamicMeshComponent* MeshComp = this->GetDynamicMeshComponent();
	UDynamicMesh* NewMesh = this->AllocateComputeMesh();
	
	FGeometryScriptCopyMeshFromAssetOptions Options;
	Options.bApplyBuildSettings = true;
	FGeometryScriptMeshReadLOD Lodsettings;
	Lodsettings.LODIndex = 0;
	EGeometryScriptOutcomePins Outcome;
	UGeometryScriptLibrary_StaticMeshFunctions::CopyMeshFromStaticMeshV2(InMesh, NewMesh, Options, Lodsettings, Outcome);
	
	if (Outcome == EGeometryScriptOutcomePins::Success)
	{
		MeshComp->SetMesh(MoveTemp(NewMesh->GetMeshRef()));
		MeshComp->UpdateBounds();
		MeshComp->UpdateCollision();
		// 计算法线
		UE::Geometry::FMeshNormals::QuickComputeVertexNormals(*MeshComp->GetMesh());
	
		MeshComp->SetSimulatePhysics(false);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		MeshComp->SetCollisionObjectType(ECC_WorldDynamic);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Overlap);
		MeshComp->RecreatePhysicsState();
	
		// 同步到渲染系统
		MeshComp->NotifyMeshUpdated();
		MeshComp->MarkRenderStateDirty();
	}
	else
	{
		// 释放Mesh
		this->ReleaseComputeMesh(NewMesh);
	}
}

// Called when the game starts or when spawned
void ACuttableObject::BeginPlay()
{
	Super::BeginPlay();
	if (SourceMesh)
	{
		AssignMesh(SourceMesh);		
	}
	
}

// Called every frame
void ACuttableObject::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

