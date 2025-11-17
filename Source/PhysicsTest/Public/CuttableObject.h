// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Components/DynamicMeshComponent.h"
#include "GameFramework/Actor.h"
#include "CuttableObject.generated.h"

using namespace UE::Geometry;

UCLASS()
class PHYSICSTEST_API ACuttableObject : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ACuttableObject();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UStaticMesh* SourceMesh;

	void AssignMesh(UStaticMesh* InMesh);

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
};
