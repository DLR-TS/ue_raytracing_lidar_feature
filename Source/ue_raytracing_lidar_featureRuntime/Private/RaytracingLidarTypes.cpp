/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarTypes.h"

bool FRaytracingLidarConfig::Validate(FString& Error) const
{
    auto Fail = [&](const TCHAR* Text)
    {
        Error = Text;
        return false;
    };
    for (float V : {UpdateRateHz, ScanTimeOrigin, MinRangeMeters, MaxRangeMeters,
             HorizontalFovDegrees, VerticalFovDegrees, RotatingStartAzimuthDegrees, RangeNoiseStdDevMeters, LissajousFrequencyX,
             LissajousFrequencyY, LissajousPhaseX, LissajousPhaseY})
        if (!FMath::IsFinite(V))
            return Fail(TEXT("all numeric values must be finite"));
    if (UpdateRateHz <= 0 || GetScanDurationSeconds() > 60)
        return Fail(TEXT("UpdateRateHz must be positive; derived scan duration must be <= 60 s"));
    if (ScanTimeOrigin < 0 || ScanTimeOrigin > 60)
        return Fail(TEXT("ScanTimeOrigin must be 0..60 seconds (OSI unsigned timing)"));
    if (MinRangeMeters < 0 || MaxRangeMeters <= MinRangeMeters || MaxRangeMeters > 100000 || RangeNoiseStdDevMeters < 0)
        return Fail(TEXT("invalid range/noise"));
    if (HorizontalFovDegrees <= 0 || HorizontalFovDegrees > 360 || VerticalFovDegrees <= 0 || VerticalFovDegrees >= 180)
        return Fail(TEXT("FOV must be horizontal (0,360], vertical (0,180)"));
    if (Rectilinear && (HorizontalFovDegrees >= 180 || VerticalFovDegrees >= 180))
        return Fail(TEXT("rectilinear FOV must be <180"));
    if (HorizontalResolution <= 0 || VerticalResolution <= 0 || RotatingChannels <= 0 || RotatingChannels > 65536 || LissajousRayCount <= 0)
        return Fail(TEXT("resolutions/channels/ray count must be positive"));
    // Custom supplies explicit rays; Lissajous uses LissajousRayCount; grid patterns use width * height.
    // Count in 64 bits so oversized dimensions are rejected before allocating GPU buffers.
    int64 Count =
        Pattern == ERaytracingLidarPattern::Custom ? Custom.Num()
        : Pattern == ERaytracingLidarPattern::Lissajous
            ? LissajousRayCount
            : int64(HorizontalResolution) * (Pattern == ERaytracingLidarPattern::Rotating ? RotatingChannels : VerticalResolution);
    if (Count < 1 || Count > 1048576)
        return Fail(TEXT("scan must contain 1..1048576 rays"));
    if ((!RotatingVerticalAngles.IsEmpty() && RotatingVerticalAngles.Num() != RotatingChannels) ||
        (!RotatingAzimuthCorrections.IsEmpty() && RotatingAzimuthCorrections.Num() != RotatingChannels))
        return Fail(TEXT("channel angles/corrections must have RotatingChannels entries"));
    for (float V : RotatingVerticalAngles)
        if (!FMath::IsFinite(V) || FMath::Abs(V) > 90)
            return Fail(TEXT("invalid vertical angle"));
    for (float V : RotatingAzimuthCorrections)
        if (!FMath::IsFinite(V) || FMath::Abs(V) > 360)
            return Fail(TEXT("invalid azimuth correction"));
    if (LissajousFrequencyX <= 0 || LissajousFrequencyY <= 0)
        return Fail(TEXT("Lissajous frequencies must be positive"));
    if (Pattern == ERaytracingLidarPattern::Custom)
    {
        // OutputIndex must be a permutation of [0, Count): every GPU result slot is written once.
        // Custom directions need not be unit length, but must be finite and nonzero.
        TSet<uint32> Seen;
        for (const auto& Ray : Custom)
        {
            if (Ray.LocalDirection.ContainsNaN() || Ray.LocalDirection.IsNearlyZero() ||
                !FMath::IsFinite(Ray.LocalDirection.SizeSquared()) || !FMath::IsFinite(Ray.NormalizedTime) ||
                Ray.NormalizedTime < 0 || Ray.NormalizedTime > 1 || Ray.OutputIndex >= uint32(Count) ||
                Seen.Contains(Ray.OutputIndex))
                return Fail(TEXT("invalid custom direction/time or non-permutation OutputIndex"));
            Seen.Add(Ray.OutputIndex);
        }
    }
    Error.Reset();
    return true;
}

// Build sensor-local directions and logical acquisition times, not world-space hit points.
// The renderer uploads this definition once and reuses it for subsequent scans.
// Ray time in seconds is ScanTimeOrigin + NormalizedTime / UpdateRateHz;
// all rays still query one frozen scene in a single GPU dispatch.
bool BuildRaytracingLidarPattern(const FRaytracingLidarConfig& C, TArray<FRaytracingLidarRay>& Rays, FString& Error)
{
    Rays.Reset();
    if (!C.Validate(Error))
        return false;
    if (C.Pattern == ERaytracingLidarPattern::Custom)
    {
        // Preserve authored order, channel IDs, timestamps and output indices.
        // Normalize only directions: their magnitude must not change the measured range.
        Rays = C.Custom;
        for (auto& R : Rays)
            R.LocalDirection.Normalize();
        return true;
    }
    // Rotating uses physical rings as height; Raster/Flash use vertical grid samples.
    // Lissajous has no grid and ignores W/H when choosing its number of samples.
    const int32 W = C.HorizontalResolution,
                H = C.Pattern == ERaytracingLidarPattern::Rotating ? C.RotatingChannels : C.VerticalResolution;
    const int32 Count = C.Pattern == ERaytracingLidarPattern::Lissajous ? C.LissajousRayCount : W * H;
    Rays.Reserve(Count);
    // Map a grid coordinate to degrees in [-Fov/2, +Fov/2], including both edges.
    // A single sample looks along the center instead of dividing by N - 1 == 0.
    auto Angle = [](int32 I, int32 N, float Fov)
    {
        return N == 1 ? 0.f : (float(I) / (N - 1) - 0.5f) * Fov;
    };
    for (int32 I = 0; I < Count; ++I)
    {
        FRaytracingLidarRay R;
        // Generated rays use acquisition order as output order. I / Count lies in [0, 1),
        // so the last sample does not repeat the start of the next scan.
        // Rotating and Flash override this default time below.
        R.OutputIndex = I;
        R.NormalizedTime = float(I) / Count;
        float Az = 0, El = 0;
        if (C.Pattern == ERaytracingLidarPattern::Lissajous)
        {
            // Two sinusoidal angular motions form the Lissajous path: frequencies in Hz,
            // phases in radians, and angular amplitudes equal to half the respective FOV.
            // Uniform time steps do not imply uniform angular spacing or full FOV coverage.
            // These are initial directions; the shader recomputes them with the scan's
            // simulation timestamp added to the phase, keeping motion continuous across scans.
            Az = C.HorizontalFovDegrees * 0.5f *
                 FMath::Sin(2 * PI * C.LissajousFrequencyX * R.NormalizedTime * C.GetScanDurationSeconds() + C.LissajousPhaseX);
            El = C.VerticalFovDegrees * 0.5f *
                 FMath::Sin(2 * PI * C.LissajousFrequencyY * R.NormalizedTime * C.GetScanDurationSeconds() + C.LissajousPhaseY);
        }
        else if (C.Pattern == ERaytracingLidarPattern::Rotating)
        {
            // Flatten as [step 0: all rings, step 1: all rings, ...].
            // All rings at one azimuth share the same logical firing time.
            R.Channel = I % H;
            const int32 Step = I / H;

            // Divide by W, not W - 1: a full 360-degree sweep must not repeat its first ray.
            // Reverse changes the azimuth sweep direction while time still advances forward.
            R.NormalizedTime = float(Step) / W;
            Az = C.RotatingStartAzimuthDegrees + (C.Reverse ? -1 : 1) * C.HorizontalFovDegrees * R.NormalizedTime;

            // Per-ring calibration shifts directions without changing firing times.
            // Explicit vertical angles override the evenly spaced vertical FOV distribution.
            if (!C.RotatingAzimuthCorrections.IsEmpty())
                Az += C.RotatingAzimuthCorrections[R.Channel];
            El = C.RotatingVerticalAngles.IsEmpty() ? Angle(R.Channel, H, C.VerticalFovDegrees) : C.RotatingVerticalAngles[R.Channel];
        }
        else
        {
            // Raster and Flash share the same spatial grid. Reverse mirrors traversal;
            // it does not reverse timestamps or the sequential OutputIndex values.
            const int32 J = C.Reverse ? Count - 1 - I : I;

            // Row-major: X advances fastest. Column-major: Y advances fastest.
            // Serpentine reverses every other row/column to form a back-and-forth sweep.
            const int32 Major = J / (C.ColumnMajor ? H : W), Minor = J % (C.ColumnMajor ? H : W);
            const int32 M = C.Serpentine && (Major & 1) ? (C.ColumnMajor ? H : W) - 1 - Minor : Minor;
            const int32 X = C.ColumnMajor ? Major : M, Y = C.ColumnMajor ? M : Major;

            // Channel identifies the spatial row even when column-major traversal is used.
            R.Channel = Y;
            Az = Angle(X, W, C.HorizontalFovDegrees);
            El = Angle(Y, H, C.VerticalFovDegrees);
            // Raster retains sequential acquisition times; Flash samples the whole grid
            // at one logical time. ScanTimeOrigin can still offset that shared time.
            if (C.Pattern == ERaytracingLidarPattern::Flash)
                R.NormalizedTime = 0;
            if (C.Rectilinear)
            {
                // Sample a pinhole image plane at X = 1 with uniformly spaced Y/Z positions.
                // tan(FOV/2) sets its half-extents; normalization produces unit directions.
                // Unlike Angular, this gives uniform image-plane spacing rather than angles.
                R.LocalDirection = FVector3f(1,
                    W == 1 ? 0
                           : (2.f * X / (W - 1) - 1) * FMath::Tan(FMath::DegreesToRadians(C.HorizontalFovDegrees / 2)),
                    H == 1 ? 0
                           : (2.f * Y / (H - 1) - 1) * FMath::Tan(FMath::DegreesToRadians(C.VerticalFovDegrees / 2)))
                                       .GetSafeNormal();
                Rays.Add(R);
                continue;
            }
        }
        // Angular projection: convert azimuth/elevation to a unit vector in Unreal's
        // sensor frame (+X forward, +Y right, +Z up). This also handles Rotating/Lissajous.
        Az = FMath::DegreesToRadians(Az);
        El = FMath::DegreesToRadians(El);
        R.LocalDirection = FVector3f(FMath::Cos(El) * FMath::Cos(Az), FMath::Cos(El) * FMath::Sin(Az), FMath::Sin(El));
        Rays.Add(R);
    }
    return true;
}

FTransform ExtrapolateRaytracingLidarPose(const FTransform& Pose, const FVector& Linear, const FVector& Angular, double Time)
{
    // Linear velocity is in cm/s, angular velocity in rad/s, and Time in seconds.
    // Both velocities use world axes, so the incremental rotation precedes the original pose.
    const double Speed = Angular.Length();
    const FQuat Delta = Speed > UE_SMALL_NUMBER ? FQuat(Angular / Speed, Speed * Time) : FQuat::Identity;
    return FTransform((Delta * Pose.GetRotation()).GetNormalized(), Pose.GetLocation() + Linear * Time);
}
