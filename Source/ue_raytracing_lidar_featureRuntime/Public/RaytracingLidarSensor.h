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
#include "RaytracingLidarTypes.h"
#include "Sensors/GemstarSensor.h"

#include "RaytracingLidarSensor.generated.h"

struct FRaytracingLidarRenderState;
class FRaytracingLidarViewExtension;

DECLARE_MULTICAST_DELEGATE_FourParams(
    FRaytracingLidarScanEvent, uint64, double, const FTransform&, TConstArrayView<FRaytracingLidarHit>);

UE_RAYTRACING_LIDAR_FEATURERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogRaytracingLidar, Log, All);

UCLASS()
class UE_RAYTRACING_LIDAR_FEATURERUNTIME_API ARaytracingLidarSensor : public AGemstarSensor
{
    GENERATED_BODY()
public:
    ARaytracingLidarSensor();
    void Configure(
        const FRaytracingLidarConfig& InConfig, const FSensorConfigData& InSensor);
    bool MatchesProducer(const FString& Producer) const;
    virtual void UpdateTransformAfterAttachment_Implementation() override;
    // Game-thread event; views are valid only during Broadcast. Includes fields absent in OSI 3.6.
    FRaytracingLidarScanEvent OnScanCompleted;
    uint64 GetDroppedScans() const
    {
        return Drops;
    }

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    FRaytracingLidarConfig Config;
    
    TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe> State;
    TSharedPtr<FRaytracingLidarViewExtension, ESPMode::ThreadSafe> Extension;
    FTransform PreviousPose;
    double Accumulator = 0;
    uint64 NextScan = 1, LastCompletedScan = 0, Drops = 0;
    bool Disabled = false;
    bool RayTracingAcquired = false;
};


