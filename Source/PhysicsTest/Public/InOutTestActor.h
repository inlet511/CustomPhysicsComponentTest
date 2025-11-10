// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Spatial/FastWinding.h"

#include "InOutTestActor.generated.h"

/**
 * 
 */
UCLASS()
class PHYSICSTEST_API AInOutTestActor : public ADynamicMeshActor
{
	GENERATED_BODY()
public:
	AInOutTestActor();
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* SourceMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	AActor* TestActor;

	void AssignMesh(UStaticMesh* InMesh);
	
	std::unique_ptr<UE::Geometry::FDynamicMeshAABBTree3> Spatial;
	std::unique_ptr<UE::Geometry::TFastWindingTree<FDynamicMesh3>> Winding;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	
};
