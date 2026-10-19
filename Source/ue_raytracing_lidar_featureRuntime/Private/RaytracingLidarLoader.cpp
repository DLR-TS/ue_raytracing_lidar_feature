
/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarLoader.h"
#include "RaytracingLidarManager.h"
#include "RaytracingLidarSensor.h"
#include "Subsystems/GemstarSimulationSubsystem.h"
#include "Engine/World.h"

URaytracingLidarLoader::URaytracingLidarLoader()
{
    PrimaryComponentTick.bCanEverTick = false;
}


void URaytracingLidarLoader::BeginPlay()
{
    Super::BeginPlay();
    UWorld* World = GetWorld();
    UGemstarSimulationSubsystem* Root = UGemstarSimulationSubsystem::Get(World);
    if (!World || !Root)
    {
        UE_LOG(LogRaytracingLidar, Error, TEXT("ue_raytracing_lidar_feature: simulation subsystem unavailable"));
        return;
    }
    FActorSpawnParameters Params;
    Params.Owner = GetOwner();
    Manager = World->SpawnActor<ARaytracingLidarManager>(ARaytracingLidarManager::StaticClass(), FTransform::Identity, Params);
    if (!Manager)
    {
        UE_LOG(LogRaytracingLidar, Error, TEXT("ue_raytracing_lidar_feature: could not spawn ARaytracingLidarManager"));
        return;
    }
    Manager->Configure(Root->RegisterManagerAndGetConfiguration(Manager, TEXT("ue_raytracing_lidar_feature")), Root);
    UE_LOG(LogRaytracingLidar, Log, TEXT("Registered and configured Raytracing LiDAR"));
}

void URaytracingLidarLoader::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(Manager))
        Manager->Destroy();
    Manager = nullptr;
    Super::EndPlay(Reason);
}
