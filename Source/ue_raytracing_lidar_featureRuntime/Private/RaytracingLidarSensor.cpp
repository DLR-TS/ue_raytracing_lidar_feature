/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarSensor.h"
#include "Effects/RaytracingLidarWeatherEffectModel.h"
#include "RaytracingLidarRenderer.h"
#include "RaytracingLidarMetadata.h"
#include "Components/SceneComponent.h"
#include "Misc/Crc.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Utils/ToOSI.h"
#include "osi_sensorview.pb.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_EXTERN(RaytracingLidar);

DEFINE_LOG_CATEGORY(LogRaytracingLidar);

ARaytracingLidarSensor::ARaytracingLidarSensor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SensorPose"));
}

void ARaytracingLidarSensor::Configure(const FRaytracingLidarConfig& C, const FSensorConfigData& S)
{
    if (HasActorBegunPlay())
    {
        UE_LOG(LogRaytracingLidar, Error, TEXT("Recreate %s to change its configuration"), *GetName());
        return;
    }
    Config = C;
    SensorConfiguration = S;
}

bool ARaytracingLidarSensor::MatchesProducer(const FString& Producer) const
{
    const FString Id = SensorConfiguration.ID.ToString();
    return Producer.Equals(Id, ESearchCase::IgnoreCase) ||
           Producer.Equals(TEXT("ue_raytracing_lidar_feature:") + Id, ESearchCase::IgnoreCase);
}

void ARaytracingLidarSensor::UpdateTransformAfterAttachment_Implementation()
{
    SetActorRelativeTransform(SensorConfiguration.Offset);
    PreviousPose = GetActorTransform();
}

void ARaytracingLidarSensor::BeginPlay()
{
    Super::BeginPlay();
    PreviousPose = GetActorTransform();
    if (!TickActive)
    {
        Disabled = true;
        return;
    }
    RayTracingAcquired = AcquireRaytracingLidarRayTracing();
    if (!RayTracingAcquired)
    {
        Disabled = true;
        UE_LOG(LogRaytracingLidar, Warning,
            TEXT("%s disabled: requires SM6, enabled hardware RT and inline RT (D3D12/Vulkan)."), *GetName());
        return;
    }
    State = MakeShared<FRaytracingLidarRenderState, ESPMode::ThreadSafe>();
    State->Config = Config;
    State->Config.Seed ^= FCrc::StrCrc32(*SensorConfiguration.ID.ToString());
    FString Error;
    if (!BuildRaytracingLidarPattern(Config, State->Rays, Error))
    {
        Disabled = true;
        UE_LOG(LogRaytracingLidar, Error, TEXT("%s: %s"), *GetName(), *Error);
        return;
    }
    Extension = FSceneViewExtensions::NewExtension<FRaytracingLidarViewExtension>(GetWorld(), State);
}

void ARaytracingLidarSensor::Tick(float Dt)
{
    Super::Tick(Dt);
    if (!TickActive) return;
    if (Disabled || !State)
        return;
    const auto StateLifetime = State; // Subscribers may destroy the sensor during OnScanCompleted.
    for (const auto& Slot : State->Slots)
        if (Slot.State.load() == ERaytracingLidarSlotState::Queued && FPlatformTime::Seconds() - Slot.QueuedWallTime > 5)
        {
            Disabled = true;
            ReleaseRaytracingLidar(State);
            UE_LOG(LogRaytracingLidar, Error,
                TEXT(
                    "%s disabled: no usable PostTLAS scene for 5s. UE 5.8.1 needs the documented inline scene-request correction when no visual RT effect is active."),
                *GetName());
            return;
        }
    if (State->Failed || !RaytracingLidarSupported())
    {
        Disabled = true;
        ReleaseRaytracingLidar(State);
        UE_LOG(LogRaytracingLidar, Error, TEXT("%s disabled after rendering/readback capability failure"), *GetName());
        return;
    }
    PollRaytracingLidar(State);

    // Deliver ready scans in acquisition order, even if GPU completion order differs.
    TArray<FRaytracingLidarSlot*> Completed;
    for (auto& Slot : State->Slots)
        if (Slot.State.load() == ERaytracingLidarSlotState::Complete)
            Completed.Add(&Slot);
    Completed.Sort(
        [](const FRaytracingLidarSlot& A, const FRaytracingLidarSlot& B)
        {
            return A.ScanId < B.ScanId;
        });
    for (auto* Slot : Completed)
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(RaytracingLidar_OSI);
        const double ConversionStart = FPlatformTime::Seconds();
        if (Slot->ScanId <= LastCompletedScan)
        {
            Slot->State.store(ERaytracingLidarSlotState::Free);
            continue;
        }
        osi3::SensorView View;
        View.mutable_version()->set_version_major(3);
        View.mutable_version()->set_version_minor(6);
        View.mutable_version()->set_version_patch(0);
        const int64 Ns = FMath::RoundToInt64(Slot->Timestamp * 1e9);
        View.mutable_timestamp()->set_seconds(Ns / 1000000000);
        View.mutable_timestamp()->set_nanos(Ns % 1000000000);
        const uint32 SensorId = FCrc::StrCrc32(*SensorConfiguration.ID.ToString());
        View.mutable_sensor_id()->set_value(SensorId);
        View.set_allocated_mounting_position(ToOSI::toMountingPosition(SensorConfiguration));
        auto* Lidar = View.add_lidar_sensor_view();
        auto* VC = Lidar->mutable_view_configuration();
        VC->mutable_sensor_id()->set_value(SensorId);
        VC->mutable_mounting_position()->CopyFrom(View.mounting_position());
        VC->set_field_of_view_horizontal(FMath::DegreesToRadians(Config.HorizontalFovDegrees));
        VC->set_field_of_view_vertical(FMath::DegreesToRadians(Config.VerticalFovDegrees));
        VC->set_number_of_rays_horizontal(
            Config.Pattern == ERaytracingLidarPattern::Custom || Config.Pattern == ERaytracingLidarPattern::Lissajous
                ? State->Rays.Num()
                : Config.HorizontalResolution);
        VC->set_number_of_rays_vertical(
            Config.Pattern == ERaytracingLidarPattern::Rotating ? Config.RotatingChannels
            : Config.Pattern == ERaytracingLidarPattern::Custom || Config.Pattern == ERaytracingLidarPattern::Lissajous
                ? 1
                : Config.VerticalResolution);
        VC->set_max_number_of_interactions(1);
        VC->set_emitter_frequency(299792458.0 / 905e-9);
        VC->set_num_of_pixels(State->Rays.Num());
        // Custom patterns may acquire rays in a different order than their output indices.
        TArray<const FRaytracingLidarRay*> Ordered;
        Ordered.SetNumUninitialized(State->Rays.Num());
        for (const auto& Ray : State->Rays)
            Ordered[Ray.OutputIndex] = &Ray;
        int32 Hits = 0;
        for (int32 I = 0; I < Ordered.Num(); ++I)
        {
            const auto& Ray = *Ordered[I];
            const auto& Hit = Slot->Hits[I];
            auto* Direction = VC->add_directions();
            Direction->set_x(Hit.LocalDirection.X);
            Direction->set_y(-Hit.LocalDirection.Y);
            Direction->set_z(Hit.LocalDirection.Z);
            VC->add_timings(
                FMath::RoundToInt64((Config.ScanTimeOrigin + Ray.NormalizedTime * Config.GetScanDurationSeconds()) * 1e6));
            auto* Reflection = Lidar->add_reflection();
            Reflection->mutable_object_id()->set_value(MAX_uint64);
            if (!Hit.Valid)
                continue;
            ++Hits;
            Reflection->set_time_of_flight(2.0 * Hit.RangeMeters / 299792458.0);
            Reflection->set_signal_strength(10.0 * FMath::LogX(10.0, FMath::Max(double(Hit.Intensity), 1e-12)));
            Reflection->mutable_object_id()->set_value(uint64(Hit.ObjectLow) | (uint64(Hit.ObjectHigh) << 32));
        }
        auto* Parent = GetAttachParentActor();
        if (Parent != nullptr) {
            auto* moving_object = View.mutable_global_ground_truth()->add_moving_object();
            uint32_t uniqueID = Parent->GetUniqueID();
            moving_object->mutable_id()->set_value(uniqueID);
            moving_object->mutable_base()->set_allocated_position(ToOSI::Position(Parent->GetActorLocation()));
            moving_object->mutable_base()->set_allocated_orientation(ToOSI::Orientation3d(Parent->GetActorQuat()));
            View.mutable_global_ground_truth()->mutable_host_vehicle_id()->set_value(uniqueID);
        }
        // Apply weather degradation before delivering the SensorView to interface consumers.
        if (HasWeatherData)
            FRaytracingLidarWeatherEffectModel().ApplyLidarWeatherEffect(View, CurrentWeatherData);

        LastCompletedScan = Slot->ScanId;
        if (PublishingActive)
            SendData(View);
        UE_LOG(LogRaytracingLidar, Verbose, TEXT("%s scan=%llu rays=%d hits=%d drops=%llu"), *GetName(), Slot->ScanId,
            Ordered.Num(), Hits, Drops);
        CSV_CUSTOM_STAT(
            RaytracingLidar, ConversionMs, (FPlatformTime::Seconds() - ConversionStart) * 1000, ECsvCustomStatOp::Accumulate);
        CSV_CUSTOM_STAT(RaytracingLidar, Hits, Hits, ECsvCustomStatOp::Accumulate);
        OnScanCompleted.Broadcast(Slot->ScanId, Slot->Timestamp, Slot->Pose, Slot->Hits);
        if (Disabled || !State)
            return;
        Slot->State.store(ERaytracingLidarSlotState::Free);
    }
    const FTransform Pose = GetActorTransform();
    FVector Linear = FVector::ZeroVector, Angular = FVector::ZeroVector;
    if (Dt > SMALL_NUMBER)
    {
        Linear = (Pose.GetLocation() - PreviousPose.GetLocation()) / Dt;
        FQuat Delta = (Pose.GetRotation() * PreviousPose.GetRotation().Inverse()).GetNormalized();
        if (Delta.W < 0)
            Delta = Delta * -1;
        // ToAxisAndAngle substitutes an axis near identity, losing small per-frame rotations.
        const FVector Imaginary(Delta.X, Delta.Y, Delta.Z);
        const double SinHalf = Imaginary.Length();
        Angular = Imaginary * (SinHalf > 1e-12 ? 2 * FMath::Atan2(SinHalf, Delta.W) / (SinHalf * Dt) : 2.0 / Dt);
    }
    PreviousPose = Pose;
    int32 Occupied = 0;
    for (const auto& Slot : State->Slots)
        Occupied += Slot.State.load() != ERaytracingLidarSlotState::Free;
    CSV_CUSTOM_STAT(RaytracingLidar, SlotsUsed, Occupied, ECsvCustomStatOp::Accumulate);
    Accumulator += Dt;
    if (Accumulator < 1.0 / Config.UpdateRateHz)
        return;
    // At most one scan per game frame, discard overdue periods (no unbounded catch-up).
    Accumulator = FMath::Fmod(Accumulator, 1.0 / Config.UpdateRateHz);
    FRaytracingLidarSlot* Free = State->TryBeginScan();
    if (!Free)
    {
        ++Drops;
        CSV_CUSTOM_STAT(RaytracingLidar, Drops, 1, ECsvCustomStatOp::Accumulate);
        return;
    }
    Free->ScanId = NextScan++;
    Free->Timestamp = GetWorld()->GetTimeSeconds();
    Free->Pose = Pose;
    Free->QueuedWallTime = FPlatformTime::Seconds();
    Free->LinearCmS = Linear;
    Free->AngularRadS = Angular;
    GatherRaytracingLidarMetadata(GetWorld(), Free->Metadata, Free->Profiles);

    // Queue the slot only after all scan inputs are ready for the render thread.
    Free->State.store(ERaytracingLidarSlotState::Queued);
}

void ARaytracingLidarSensor::EndPlay(const EEndPlayReason::Type Reason)
{
    Disabled = true;
    ReleaseRaytracingLidar(State);
    Extension.Reset();
    State.Reset();
    if (RayTracingAcquired)
    {
        ReleaseRaytracingLidarRayTracing();
        RayTracingAcquired = false;
    }
    Super::EndPlay(Reason);
}





