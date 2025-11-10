// Fill out your copyright notice in the Description page of Project Settings.


#include "InOutTestActor.h"
#include "GeometryScript/MeshAssetFunctions.h"

AInOutTestActor::AInOutTestActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AInOutTestActor::AssignMesh(UStaticMesh* InMesh)
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

void AInOutTestActor::BeginPlay()
{
	Super::BeginPlay();

	if (SourceMesh)
	{
		AssignMesh(SourceMesh);
		Spatial = std::make_unique<UE::Geometry::FDynamicMeshAABBTree3>(GetDynamicMeshComponent()->GetMesh());
		Winding = std::make_unique<UE::Geometry::TFastWindingTree<FDynamicMesh3>>(&(*Spatial));
		
	}
}

void AInOutTestActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!IsValid(TestActor))
		return;

	FVector WorldLoc = TestActor->GetActorLocation();
	FTransform MeshTransform = GetDynamicMeshComponent()->GetComponentTransform();
	FVector LocalLoc = MeshTransform.InverseTransformPosition(WorldLoc);
	
	bool bInside = Winding->IsInside(LocalLoc);
	UE_LOG(LogTemp, Warning, TEXT("%s"), bInside?TEXT("true"):TEXT("false"));
}
