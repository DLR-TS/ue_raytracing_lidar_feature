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

#include "RaytracingLidarLoader.generated.h"

class ARaytracingLidarManager;

UCLASS(ClassGroup = GEMSTAR, meta = (BlueprintSpawnableComponent))
class UE_RAYTRACING_LIDAR_FEATURERUNTIME_API URaytracingLidarLoader : public UActorComponent
{
    GENERATED_BODY()
public:
    URaytracingLidarLoader();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<ARaytracingLidarManager> Manager;
};

