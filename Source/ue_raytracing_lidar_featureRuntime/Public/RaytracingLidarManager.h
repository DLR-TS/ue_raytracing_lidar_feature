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
#include "GameFramework/Actor.h"

#include "GEMSTARManager.h"
#include "Utils/GemstarInnerConfig.h"
#include "Interfaces/Weather/WeatherDataUpdate.h"

#include "RaytracingLidarManager.generated.h"

class UGemstarSimulationSubsystem;
class ARaytracingLidarSensor;

UCLASS()
class UE_RAYTRACING_LIDAR_FEATURERUNTIME_API ARaytracingLidarManager
    : public AActor, public IGemstarManager, public IWeatherDataUpdate
{
    GENERATED_BODY()
public:
    ARaytracingLidarManager();
    void RegisterSensor(ARaytracingLidarSensor* Sensor);
    void Configure(TMap<FString, FInnerConfig> Configurations, UGemstarSimulationSubsystem* Root);
    virtual void WeatherDataUpdate_Implementation(FWeatherData& Data, int32 TargetIndex) override;

protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    UPROPERTY(Transient)
    TArray<TObjectPtr<ARaytracingLidarSensor>> Sensors;
    UPROPERTY(Transient)
    TObjectPtr<UGemstarSimulationSubsystem> GemstarRoot;
    bool bHasWeatherData = false;
    FWeatherData LatestWeatherData;
};
