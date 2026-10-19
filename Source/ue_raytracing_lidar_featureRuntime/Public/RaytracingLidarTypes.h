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

enum class ERaytracingLidarPattern : uint8
{
    Rotating,
    Raster,
    Flash,
    Lissajous,
    Custom
};

// GPU ABI: tightly packed structured buffer, not a constant buffer.
struct FRaytracingLidarRay
{
    FVector3f LocalDirection = FVector3f::ForwardVector;
    float NormalizedTime = 0;
    uint32 Channel = 0;
    uint32 OutputIndex = 0;
};
static_assert(sizeof(FRaytracingLidarRay) == 24);

struct FRaytracingLidarConfig
{
    bool Enabled = true, MotionDistortionEnabled = false, MaterialResponseEnabled = true;
    bool Reverse = false, Serpentine = false, ColumnMajor = false, Rectilinear = false;
    float UpdateRateHz = 10, ScanTimeOrigin = 0;
    float MinRangeMeters = 0.1f, MaxRangeMeters = 200, HorizontalFovDegrees = 360, VerticalFovDegrees = 30;
    float RotatingStartAzimuthDegrees = 0, RangeNoiseStdDevMeters = 0;
    float LissajousFrequencyX = 17, LissajousFrequencyY = 19, LissajousPhaseX = 0, LissajousPhaseY = 1.5707963f;
    int32 HorizontalResolution = 1024, VerticalResolution = 32, RotatingChannels = 32, LissajousRayCount = 32768;
    uint32 Seed = 1;
    ERaytracingLidarPattern Pattern = ERaytracingLidarPattern::Rotating;
    TArray<float> RotatingVerticalAngles, RotatingAzimuthCorrections;
    TArray<FRaytracingLidarRay> Custom;
    float GetScanDurationSeconds() const { return 1.f / UpdateRateHz; }
    bool Validate(FString& Error) const;
};

UE_RAYTRACING_LIDAR_FEATURERUNTIME_API bool BuildRaytracingLidarPattern(
    const FRaytracingLidarConfig& Config, TArray<FRaytracingLidarRay>& Rays, FString& Error);
UE_RAYTRACING_LIDAR_FEATURERUNTIME_API FTransform ExtrapolateRaytracingLidarPose(
    const FTransform& Pose, const FVector& LinearCmS, const FVector& AngularRadS, double Time);

struct FRaytracingLidarHit
{
    FVector3f PositionCm = FVector3f::ZeroVector;
    float RangeMeters = 0;
    FVector3f Normal = FVector3f::ZeroVector;
    float Intensity = 0;
    uint32 ObjectLow = MAX_uint32, ObjectHigh = MAX_uint32, ProfileId = 0, Channel = 0;
    uint32 OutputIndex = 0;
    float TimeOffset = 0;
    uint32 Valid = 0, SemanticClass = 0;
    FVector3f LocalDirection = FVector3f::ZeroVector;
    float Padding = 0;
};
static_assert(sizeof(FRaytracingLidarHit) == 80);

struct FRaytracingLidarMetadata
{
    uint32 ComponentId = 0, Segment = 0, ObjectLow = MAX_uint32, ObjectHigh = MAX_uint32;
    uint32 ProfileIndex = 0, SemanticClass = 0, Padding0 = 0, Padding1 = 0;
};

struct FRaytracingLidarProfileGpu
{
    uint32 ProfileId = 0;
    float Reflectivity = 0.5f, RetroReflectivity = 0, Roughness = 0.5f;
    float SpecularResponse = 0, Transmission = 0, Absorption = 0, WavelengthResponse = 1;
};
static_assert(sizeof(FRaytracingLidarMetadata) == 32 && sizeof(FRaytracingLidarProfileGpu) == 32);
