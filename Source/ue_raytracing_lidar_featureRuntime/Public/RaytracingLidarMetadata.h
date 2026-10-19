/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RaytracingLidarTypes.h"
#include "RaytracingLidarMetadata.generated.h"

// OpenMATERIAL adapters populate these values on the game thread, outside the shader.
USTRUCT(BlueprintType)
struct FRaytracingLidarMaterialProfile
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 ProfileId = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Reflectivity = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RetroReflectivity = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Roughness = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SpecularResponse = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Transmission = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Absorption = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WavelengthResponse = 1;
    bool ToGpu(FRaytracingLidarProfileGpu& Out) const;
};

UCLASS(ClassGroup = GEMSTAR, meta = (BlueprintSpawnableComponent))
class UE_RAYTRACING_LIDAR_FEATURERUNTIME_API URaytracingLidarMetadataComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    // Use the object's existing GEMSTAR/OSI ID. Never derive it from an RHI instance index.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiDAR")
    int64 OsiObjectId = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiDAR")
    int32 SemanticClass = 0;
    // Empty applies to all primitive components on the owner; otherwise exact component name.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiDAR")
    FName PrimitiveComponentName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LiDAR")
    TArray<FRaytracingLidarMaterialProfile> MaterialSlots;
};

void GatherRaytracingLidarMetadata(UWorld* World, TArray<FRaytracingLidarMetadata>& Metadata, TArray<FRaytracingLidarProfileGpu>& Profiles);
