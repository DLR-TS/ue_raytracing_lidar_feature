/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarConfig.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

bool ParseRaytracingLidarConfig(const FInnerConfig& Input, FRaytracingLidarConfig& C, FString& Error)
{
    TMap<FString, FString> Values;
    for (const auto& Pair : Input.Entries)
        Values.Add(Pair.Key.ToLower(), Pair.Value);

    // Missing keys preserve defaults; malformed values fail validation below.
    auto Float = [&](const TCHAR* Key, float& V)
    {
        const auto* S = Values.Find(FString(Key).ToLower());
        return !S || FDefaultValueHelper::ParseFloat(*S, V);
    };
    auto Int = [&](const TCHAR* Key, int32& V)
    {
        const auto* S = Values.Find(FString(Key).ToLower());
        return !S || FDefaultValueHelper::ParseInt(*S, V);
    };
    auto Bool = [&](const TCHAR* Key, bool& V)
    {
        const auto* S = Values.Find(FString(Key).ToLower());
        return !S || FDefaultValueHelper::ParseBool(*S, V);
    };
    bool Ok = true;
#define RAYTRACING_LIDAR_FLOAT(Name) Ok &= Float(TEXT(#Name), C.Name)
#define RAYTRACING_LIDAR_INT(Name) Ok &= Int(TEXT(#Name), C.Name)
#define RAYTRACING_LIDAR_BOOL(Name) Ok &= Bool(TEXT(#Name), C.Name)
    RAYTRACING_LIDAR_FLOAT(UpdateRateHz);
    RAYTRACING_LIDAR_FLOAT(ScanTimeOrigin);
    RAYTRACING_LIDAR_FLOAT(MinRangeMeters);
    RAYTRACING_LIDAR_FLOAT(MaxRangeMeters);
    RAYTRACING_LIDAR_FLOAT(HorizontalFovDegrees);
    RAYTRACING_LIDAR_FLOAT(VerticalFovDegrees);
    RAYTRACING_LIDAR_FLOAT(RotatingStartAzimuthDegrees);
    RAYTRACING_LIDAR_FLOAT(RangeNoiseStdDevMeters);
    RAYTRACING_LIDAR_FLOAT(LissajousFrequencyX);
    RAYTRACING_LIDAR_FLOAT(LissajousFrequencyY);
    RAYTRACING_LIDAR_FLOAT(LissajousPhaseX);
    RAYTRACING_LIDAR_FLOAT(LissajousPhaseY);

    RAYTRACING_LIDAR_INT(HorizontalResolution);
    RAYTRACING_LIDAR_INT(VerticalResolution);
    RAYTRACING_LIDAR_INT(RotatingChannels);
    RAYTRACING_LIDAR_INT(LissajousRayCount);

    RAYTRACING_LIDAR_BOOL(Enabled);
    RAYTRACING_LIDAR_BOOL(MotionDistortionEnabled);
    RAYTRACING_LIDAR_BOOL(MaterialResponseEnabled);
    RAYTRACING_LIDAR_BOOL(Reverse);
    RAYTRACING_LIDAR_BOOL(Serpentine);
    RAYTRACING_LIDAR_BOOL(ColumnMajor);
#undef RAYTRACING_LIDAR_FLOAT
#undef RAYTRACING_LIDAR_INT
#undef RAYTRACING_LIDAR_BOOL
    int32 Seed = 1;
    Ok &= Int(TEXT("Seed"), Seed);
    Ok &= Seed >= 0;
    C.Seed = Seed;
    if (const auto* S = Values.Find(TEXT("scanpattern")))
    {
        const TCHAR* Names[] = {TEXT("Rotating"), TEXT("Raster"), TEXT("Flash"), TEXT("Lissajous"), TEXT("Custom")};
        bool Found = false;
        for (int32 I = 0; I < 5; ++I)
            if (S->Equals(Names[I], ESearchCase::IgnoreCase))
            {
                C.Pattern = ERaytracingLidarPattern(I);
                Found = true;
            }
        Ok &= Found;
    }
    if (const auto* S = Values.Find(TEXT("motionmodel")))
        Ok &= S->Equals(TEXT("SensorPoseExtrapolation"), ESearchCase::IgnoreCase);
    if (const auto* S = Values.Find(TEXT("projection")))
    {
        C.Rectilinear = S->Equals(TEXT("Rectilinear"), ESearchCase::IgnoreCase);
        Ok &= C.Rectilinear || S->Equals(TEXT("Angular"), ESearchCase::IgnoreCase);
    }
    for (const TCHAR* Key : {TEXT("rotatingverticalangles"), TEXT("rotatingazimuthcorrections")})
        if (const auto* S = Values.Find(Key))
        {
            TArray<FString> Parts;
            S->ParseIntoArray(Parts, TEXT(","), false);
            auto& List = FString(Key) == TEXT("rotatingverticalangles") ? C.RotatingVerticalAngles : C.RotatingAzimuthCorrections;
            for (const auto& Part : Parts)
            {
                float V = 0;
                Ok &= FDefaultValueHelper::ParseFloat(Part, V);
                List.Add(V);
            }
        }
    // Existing INI strings or standard CSV: x,y,z,normalizedTime[,channel[,outputIndex]].
    FString CustomText;
    if (const auto* S = Values.Find(TEXT("customrays")))
        CustomText = *S;
    if (const auto* S = Values.Find(TEXT("custompatternfile")))
    {
        const FString Path = FPaths::IsRelative(*S) ? FPaths::Combine(FPaths::ProjectDir(), *S) : *S;
        Ok &= FFileHelper::LoadFileToString(CustomText, *Path);
        CustomText.ReplaceInline(TEXT("\r"), TEXT(""));
        CustomText.ReplaceInline(TEXT("\n"), TEXT(";"));
    }
    if (!CustomText.IsEmpty())
    {
        TArray<FString> Rows;
        CustomText.ParseIntoArray(Rows, TEXT(";"), true);
        for (const auto& Row : Rows)
        {
            TArray<FString> Cells;
            Row.ParseIntoArray(Cells, TEXT(","), false);
            if (Cells.Num() < 4 || Cells.Num() > 6)
            {
                Ok = false;
                break;
            }
            FRaytracingLidarRay Ray;
            Ray.OutputIndex = C.Custom.Num();
            Ok &= FDefaultValueHelper::ParseFloat(Cells[0], Ray.LocalDirection.X);
            Ok &= FDefaultValueHelper::ParseFloat(Cells[1], Ray.LocalDirection.Y);
            Ok &= FDefaultValueHelper::ParseFloat(Cells[2], Ray.LocalDirection.Z);
            Ok &= FDefaultValueHelper::ParseFloat(Cells[3], Ray.NormalizedTime);
            int32 Channel = 0, Index = Ray.OutputIndex;
            if (Cells.Num() > 4)
                Ok &= FDefaultValueHelper::ParseInt(Cells[4], Channel);
            if (Cells.Num() > 5)
                Ok &= FDefaultValueHelper::ParseInt(Cells[5], Index);
            Ok &= Channel >= 0 && Index >= 0;
            Ray.Channel = Channel;
            Ray.OutputIndex = Index;
            C.Custom.Add(Ray);
        }
    }
    if (!Ok)
    {
        Error = TEXT("invalid number, boolean, pattern, projection, motion model or custom CSV");
        return false;
    }
    return C.Validate(Error);
}
