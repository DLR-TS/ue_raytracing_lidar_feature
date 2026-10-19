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

#include "Interfaces/Weather/WeatherDataUpdate.h"

#include "CoreMinimal.h"

#include "osi_sensorview.pb.h"

class FRaytracingLidarWeatherEffectModel
{
public:
    void ApplyLidarWeatherEffect(osi3::SensorView& SensorView, const FWeatherData& WeatherData) const;

private:
    void ApplyVanishingDistanceDegradation(osi3::SensorView& SensorView, const FWeatherData& WeatherData) const;
    double GetVanishingSlope(const FName& WeatherType, double Reflectivity, double ReflectivityThreshold) const;
    double CalculateDistanceFromTimeOfFlight(double TimeOfFlight) const;
    double CalculateVanishingDistance(const FName& WeatherType, double Reflectivity, double ReflectivityThreshold,
                                      const FWeatherData& WeatherData) const;

    bool ShouldReflectionDisappear(double Distance, double VanishingDistance) const;
    bool IsWeatherTypeAvailable(const FName& WeatherType, const FWeatherData& WeatherData) const;
    bool DetermineActiveWeatherType(const FWeatherData& WeatherData, FName& OutActiveWeatherType) const;
    double GetSensorWeatherParameterAsDouble(const FWeatherData& WeatherData, const FName& Parameterkey,
                                             double DefaultValue) const;
    double GetVisualRange(const FName& WeatherType, const FWeatherData& WeatherData) const;
    FString BuildLidarWeatherParameterkey(const FName& Parameterkey) const;
};
