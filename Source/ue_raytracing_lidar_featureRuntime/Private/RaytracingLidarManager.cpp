/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarManager.h"
#include "Subsystems/GemstarSimulationSubsystem.h"
#include "RaytracingLidarConfig.h"
#include "RaytracingLidarSensor.h"
#include "Engine/World.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/EngineVersionComparison.h"


void ARaytracingLidarManager::RegisterSensor(ARaytracingLidarSensor* Sensor)
{
	Sensors.Add(Sensor);
	if (bHasWeatherData && IsValid(Sensor))
		Sensor->SetWeatherData(LatestWeatherData);
}


ARaytracingLidarManager::ARaytracingLidarManager()
{
    PrimaryActorTick.bCanEverTick = false;
    AcceptChannel(
        GemstarInterfaceType::SENSORVIEW,
        [this](FDataTarget& Target, const FString& Producer)
        {
            if (!IsValid(Target.Actor))
                return false;
            for (ARaytracingLidarSensor* Sensor : Sensors)
                if (IsValid(Sensor) && Sensor->MatchesProducer(Producer))
                {
                    Sensor->AddDataTarget(Target);
                    Target.Actor->AddTickPrerequisiteActor(Sensor);
                    return true;
                }
            return false;
        },
        [this](FDataTarget& Target, const FString&)
        {
            bool Removed = false;
            for (ARaytracingLidarSensor* Sensor : Sensors)
                if (IsValid(Sensor))
                {
                    Sensor->RemoveDataTarget(Target);
                    Target.Actor->RemoveTickPrerequisiteActor(Sensor);
                    Removed = true;
                }
            return Removed;
        });
}


void ARaytracingLidarManager::Configure(TMap<FString, FInnerConfig> Sections, UGemstarSimulationSubsystem* Root)
{
    if (!Root)
        return;
    GemstarRoot = Root;
    for (const auto& Section : Sections)
    {
        if (!Section.Key.StartsWith(TEXT("ue_raytracing_lidar_feature:"), ESearchCase::IgnoreCase))
            continue;
        FRaytracingLidarConfig C;
        FString Error;
        if (!ParseRaytracingLidarConfig(Section.Value, C, Error))
        {
            UE_LOG(LogRaytracingLidar, Error, TEXT("[%s]: %s"), *Section.Key, *Error);
            continue;
        }
        if (!C.Enabled)
            continue;
        FSensorConfigData S;
        S.ID = FName(*Section.Key);
        S.Type = SensorType::LIDAR;
        if (S.ID.IsNone())
        {
            UE_LOG(LogRaytracingLidar, Error, TEXT("empty raytracing_lidar sensor name"));
            continue;
        }
        FVector Position(0);
        FRotator Rotation(0);
        bool ValidPose = true;
        for (const auto& E : Section.Value.Entries)
        {
            if (E.Key.Equals(TEXT("Mount"), ESearchCase::IgnoreCase))
                S.Mount = E.Value;
            const TCHAR* Keys[] = { TEXT("MountingPositionX"), TEXT("MountingPositionY"), TEXT("MountingPositionZ"),
                TEXT("MountingOrientationPitch"), TEXT("MountingOrientationYaw"), TEXT("MountingOrientationRoll") };
            double* Values[] = { &Position.X, &Position.Y, &Position.Z, &Rotation.Pitch, &Rotation.Yaw, &Rotation.Roll };
            for (int32 I = 0; I < 6; ++I)
                if (E.Key.Equals(Keys[I], ESearchCase::IgnoreCase))
                {
                    float V = 0;
                    ValidPose &= FDefaultValueHelper::ParseFloat(E.Value, V) && FMath::IsFinite(V);
                    *Values[I] = V;
                }
        }
        if (!ValidPose)
        {
            UE_LOG(LogRaytracingLidar, Error, TEXT("[%s]: invalid mounting pose"), *Section.Key);
            continue;
        }
        S.Offset = FTransform(Rotation, Position);
        FActorSpawnParameters Params;
        Params.Name = S.ID;
        Params.Owner = this;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
        Params.CustomPreSpawnInitalization =
#else
        Params.CustomPreSpawnInitialization =
#endif
            [C, S](AActor* A)
            {
                CastChecked<ARaytracingLidarSensor>(A)->Configure(C, S);
            };
        auto* A = GetWorld()->SpawnActor<ARaytracingLidarSensor>(ARaytracingLidarSensor::StaticClass(), FTransform::Identity, Params);
        if (!A)
            continue;
        RegisterSensor(A);
        if (const FString* WeatherInput = Section.Value.Entries.Find(TEXT("WeatherInput")))
        {
            FDataTarget Target;
            Target.Actor = this;
            ConnectInputs(WeatherInput, GemstarRoot, Target, GemstarInterfaceType::WEATHERDATA);
        }
        if (!S.Mount.IsEmpty())
            IGemstarManager::Execute_MountOn(GemstarRoot, S.Mount, A);
        ILocalTransformUpdater::Execute_UpdateTransformAfterAttachment(A);
    }
}

void ARaytracingLidarManager::WeatherDataUpdate_Implementation(FWeatherData& Data, int32 TargetIndex)
{
    // Match the regular Lidar plugin: one weather state is shared by this manager's sensors.
    LatestWeatherData = Data;
    bHasWeatherData = true;
    for (ARaytracingLidarSensor* Sensor : Sensors)
        if (IsValid(Sensor))
            Sensor->SetWeatherData(LatestWeatherData);
}

void ARaytracingLidarManager::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(GemstarRoot))
        GemstarRoot->UnregisterManager(this);
    GemstarRoot = nullptr;
    for (ARaytracingLidarSensor* Sensor : Sensors)
        if (IsValid(Sensor))
            Sensor->Destroy();
    Sensors.Reset();
    Super::EndPlay(Reason);
}
