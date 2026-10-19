/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "Effects/RaytracingLidarWeatherEffectModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRaytracingLidarWeatherEffectModelTest,
    "GEMSTAR.RaytracingLidar.WeatherMatchesSimpleLidar",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)

bool FRaytracingLidarWeatherEffectModelTest::RunTest(const FString& Parameters)
{
    // Expected dropouts follow SimpleLidar; RT encodes the input strength in dB.
    const struct
    {
        const TCHAR* Name;
        FName WeatherType;
        FName PrecipitationType;
        double MOR;
        double PrecipitationIntensity;
        double LinearStrength;
        double Distance;
        bool Disappears;
        bool Enabled = true;
    } Cases[] = {
        {TEXT("Auto prefers fog over rain"), NAME_None, GemstarPrecipitationType::Rain, 200, 1, 0.05, 40, false},
        {TEXT("Auto prefers fog over snow"), NAME_None, GemstarPrecipitationType::Snow, 200, 10, 0.05, 55, false},
        {TEXT("Auto falls back to rain"), NAME_None, GemstarPrecipitationType::Rain, 0, 10, 0.05, 30, true},
        {TEXT("Auto falls back to snow"), NAME_None, GemstarPrecipitationType::Snow, 0, 10, 0.05, 49.31, true},
        {TEXT("Explicit rain overrides fog"), GemstarWeatherTypes::Rain, GemstarPrecipitationType::Rain, 200, 1, 0.05, 40, true},
        {TEXT("Unavailable explicit rain has no fallback"), GemstarWeatherTypes::Rain, GemstarPrecipitationType::Snow, 100, 10, 0.05, 80, false},
        {TEXT("Fog low reflectivity"), GemstarWeatherTypes::Fog, GemstarPrecipitationType::None, 100, 0, 0.05, 32, true},
        {TEXT("Fog high reflectivity"), GemstarWeatherTypes::Fog, GemstarPrecipitationType::None, 100, 0, 0.2, 32, false},
        {TEXT("Zero dB is a valid return"), GemstarWeatherTypes::Fog, GemstarPrecipitationType::None, 100, 0, 1, 40, true},
        {TEXT("Snow coefficient retains near boundary"), GemstarWeatherTypes::Snow, GemstarPrecipitationType::Snow, 0, 10, 0.05, 49.3, false},
        {TEXT("Zero visual range matches SimpleLidar"), GemstarWeatherTypes::Rain, GemstarPrecipitationType::Rain, 0, 200, 0.05, 40, false},
        {TEXT("Clear disables weather"), GemstarWeatherTypes::Clear, GemstarPrecipitationType::Rain, 100, 10, 0.05, 80, false},
        {TEXT("Disabled weather keeps reflections"), GemstarWeatherTypes::Fog, GemstarPrecipitationType::None, 100, 0, 0.05, 80, false, false},
        {TEXT("Missing weather keeps reflections"), NAME_None, GemstarPrecipitationType::None, 0, 0, 0.05, 80, false},
    };

    for (const auto& Case : Cases)
    {
        FWeatherData Weather;
        Weather.WeatherState.bWeatherEffectEnabled = Case.Enabled;
        Weather.WeatherState.ActiveWeatherType = Case.WeatherType;
        Weather.WeatherState.bHasMOR = Case.MOR > 0;
        Weather.WeatherState.MOR = Case.MOR;
        Weather.WeatherState.bHasPrecipitation = Case.PrecipitationIntensity > 0;
        Weather.WeatherState.PrecipitationType = Case.PrecipitationType;
        Weather.WeatherState.PrecipitationIntensity = Case.PrecipitationIntensity;

        osi3::SensorView View;
        auto* Lidar = View.add_lidar_sensor_view();
        auto* Reflection = Lidar->add_reflection();
        const double TimeOfFlight = 2.0 * Case.Distance / 299792458.0;
        const double StrengthDb = 10.0 * FMath::LogX(10.0, Case.LinearStrength);
        Reflection->set_time_of_flight(TimeOfFlight);
        Reflection->set_signal_strength(StrengthDb);
        Reflection->mutable_object_id()->set_value(42);

        FRaytracingLidarWeatherEffectModel().ApplyLidarWeatherEffect(View, Weather);

        TestEqual(Case.Name, Reflection->object_id().value(),
            Case.Disappears ? TNumericLimits<uint64>::Max() : uint64(42));
        TestEqual(Case.Name, Reflection->signal_strength(), Case.Disappears ? 0.0 : StrengthDb);
        TestEqual(Case.Name, Reflection->time_of_flight(), TimeOfFlight);
        TestEqual(Case.Name, Lidar->reflection_size(), 1);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
