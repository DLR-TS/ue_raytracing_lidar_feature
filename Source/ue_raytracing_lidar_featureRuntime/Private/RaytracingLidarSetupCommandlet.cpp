/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarSetupCommandlet.h"
#include "RaytracingLidarLoader.h"
#include "Subsystems/GemstarGameState.h"
#include "GameFeatureData.h"
#include "GameFeatureAction_AddComponents.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

int32 URaytracingLidarSetupCommandlet::Main(const FString&)
{
#if WITH_EDITOR
    const FString Name = TEXT("/ue_raytracing_lidar_feature/ue_raytracing_lidar_feature");
    auto* Data = LoadObject<UGameFeatureData>(nullptr, *(Name + TEXT(".ue_raytracing_lidar_feature")));
    UPackage* Package = Data ? Data->GetOutermost() : CreatePackage(*Name);
    if (!Data)
        Data = NewObject<UGameFeatureData>(Package, TEXT("ue_raytracing_lidar_feature"), RF_Public | RF_Standalone);
    auto* Action = NewObject<UGameFeatureAction_AddComponents>(Data);
    FGameFeatureComponentEntry Entry;
    Entry.ActorClass = AGemstarGameState::StaticClass();
    Entry.ComponentClass = URaytracingLidarLoader::StaticClass();
    Entry.bClientComponent = true;
    Entry.bServerComponent = true;
    Action->ComponentList.Add(Entry);
    Data->GetMutableActionsInEditor().Reset();
    Data->GetMutableActionsInEditor().Add(Action);
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public | RF_Standalone;
    return UPackage::SavePackage(Package, Data,
               *FPackageName::LongPackageNameToFilename(Name, FPackageName::GetAssetPackageExtension()), Args)
               ? 0
               : 1;
#else
    return 1;
#endif
}
