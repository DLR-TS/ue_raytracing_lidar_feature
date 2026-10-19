/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"
#include "RaytracingLidarRenderer.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogRaytracingLidarStartup, Log, All);

namespace
{
    int32 RayTracingUsers = 0; // Game thread, shared by sensors across PIE worlds.
    const FName StartupTag(TEXT("RaytracingLidarStartup"));
    const FName ActiveTag(TEXT("RaytracingLidarActive"));
}

bool AcquireRaytracingLidarRayTracing()
{
    check(IsInGameThread());
    auto* Enable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
    if (!Enable || !GIsRHIInitialized)
        return false;
    if (RayTracingUsers++ == 0)
    {
        Enable->Set(1, ECVF_SetByPluginHighPriority, ActiveTag);
        UE_LOG(LogRaytracingLidarStartup, Log, TEXT("Raytracing LiDAR requested runtime ray tracing: r.RayTracing.Enable=%d"),
            Enable->GetInt());
    }
    if (RaytracingLidarSupported())
        return true;
    ReleaseRaytracingLidarRayTracing();
    UE_LOG(LogRaytracingLidarStartup, Error,
        TEXT(
            "Raytracing LiDAR could not enable hardware RT. Requires an editor restart after installing the plugin, SM6 and a supported inline-RT GPU/RHI. Explicit higher-priority RT overrides are respected."));
    return false;
}

void ReleaseRaytracingLidarRayTracing()
{
    check(IsInGameThread());
    check(RayTracingUsers > 0);
    if (--RayTracingUsers == 0)
    {
        auto* Enable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.Enable"));
        Enable->Unset(ECVF_SetByPluginHighPriority, ActiveTag);
        UE_LOG(LogRaytracingLidarStartup, Log, TEXT("Last Raytracing LiDAR released its runtime override: r.RayTracing.Enable=%d"),
            Enable->GetInt());
    }
}

class FRaytracingLidarModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("ue_raytracing_lidar_feature"));
        if (Plugin)
            AddShaderSourceDirectoryMapping(
                TEXT("/Plugin/GemstarRaytracingLidar"), FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
#if !UE_SERVER
        // PostConfigInit runs before renderer settings, RHI and shader-platform masks.
        // CVar overrides are process-local: never modify or save project INI files.
        bool ProjectRayTracing = false;
        GConfig->GetBool(TEXT("/Script/Engine.RendererSettings"), TEXT("r.RayTracing"), ProjectRayTracing, GEngineIni);
        auto& Console = IConsoleManager::Get();
        for (const TCHAR* Name :
            {TEXT("r.RayTracing"), TEXT("r.SkinCache.CompileShaders"), TEXT("r.RayTracing.EnableOnDemand")})
        {
            if (auto* Variable = Console.FindConsoleVariable(Name))
            {
                int32 Configured = Variable->GetInt();
                GConfig->GetInt(TEXT("/Script/Engine.RendererSettings"), Name, Configured, GEngineIni);
                Variable->Set(FMath::Max(1, Configured), ECVF_SetByPluginHighPriority, StartupTag);
#if WITH_EDITOR && ALLOW_OTHER_PLATFORM_CONFIG
                // Cross-platform cooks use platform CVar values, not the host value.
                if (FCString::Strcmp(Name, TEXT("r.SkinCache.CompileShaders")) == 0)
                    for (const FName Platform : {FName(TEXT("Windows")), FName(TEXT("Linux"))})
                        if (auto PlatformVariable = Variable->GetPlatformValueVariable(Platform))
                            PlatformVariable->Set(
                                FMath::Max(1, PlatformVariable->GetInt()), ECVF_SetByPluginHighPriority, StartupTag);
#endif
            }
        }
        // Preserve projects that already use RT. Otherwise keep it dormant until a sensor starts.
        if (!ProjectRayTracing)
            if (auto* Enable = Console.FindConsoleVariable(TEXT("r.RayTracing.Enable")))
                Enable->Set(0, ECVF_SetByPluginHighPriority, StartupTag);
        UE_LOG(LogRaytracingLidarStartup, Log,
            TEXT(
                "Prepared hardware RT and skin-cache shaders for on-demand Raytracing LiDAR (process-local, project INI unchanged)."));
#endif
    }
};
IMPLEMENT_MODULE(FRaytracingLidarModule, ue_raytracing_lidar_featureRuntime)
