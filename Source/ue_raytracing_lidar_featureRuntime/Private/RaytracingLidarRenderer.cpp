/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarRenderer.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "FXRenderingUtils.h"
#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneInterface.h"
#include "SceneUniformBuffer.h"
#include "ShaderParameterStruct.h"
#include "ShaderCompilerCore.h"
#include "RayTracingDefinitions.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Misc/EngineVersionComparison.h"
#if RHI_RAYTRACING && UE_VERSION_OLDER_THAN(5, 8, 0)
#include "ScenePrivate.h"
#endif

CSV_DEFINE_CATEGORY(RaytracingLidar, true);
DECLARE_GPU_STAT_NAMED(RaytracingLidarTrace, TEXT("GEMSTAR Raytracing LiDAR"));

#if RHI_RAYTRACING && !UE_VERSION_OLDER_THAN(5, 8, 0) && UE_VERSION_OLDER_THAN(5, 9, 0)
namespace
{
    struct FSceneRequestKey
    {
        const FRenderTarget* Target;
        uint32 Frame, View;
        int32 Eye;
        bool operator==(const FSceneRequestKey& Other) const
        {
            return Target == Other.Target && Frame == Other.Frame && View == Other.View && Eye == Other.Eye;
        }
        friend uint32 GetTypeHash(const FSceneRequestKey& Key)
        {
            return HashCombine(
                HashCombine(PointerHash(Key.Target), Key.Frame), HashCombine(Key.View, GetTypeHash(Key.Eye)));
        }
    };
    struct FSceneRequestSettings
    {
        int32 AO;
        float Intensity;
    };
    FCriticalSection SceneRequestMutex;
    // Shared between sensor extensions: save/restore each view exactly once.
    TMap<FSceneRequestKey, FSceneRequestSettings> SceneRequests;
    FSceneRequestKey SceneRequestKey(const FSceneView& View)
    {
        return {View.Family->RenderTarget, View.Family->FrameNumber, View.GetViewKey(), View.StereoViewIndex};
    }
}
#endif

bool RaytracingLidarSupported()
{
#if RHI_RAYTRACING
    return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6 && GRHISupportsRayTracing && GRHISupportsInlineRayTracing &&
           IsRayTracingEnabled() && RHISupportsInlineRayTracing(GMaxRHIShaderPlatform);
#else
    return false;
#endif
}

#if RHI_RAYTRACING
class FRaytracingLidarCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FRaytracingLidarCS);
    SHADER_USE_PARAMETER_STRUCT(FRaytracingLidarCS, FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(RaytracingAccelerationStructure, TLAS)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FRayTracingSceneMetadataRecord>, RayTracingSceneMetadata)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<RayDefinition>, Rays)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<ObjectMetadata>, Metadata)
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<MaterialProfile>, Profiles)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<HitResult>, Results)
    SHADER_PARAMETER(FVector3f, Origin)
    SHADER_PARAMETER(FVector4f, Rotation)
    SHADER_PARAMETER(FVector3f, LinearVelocity)
    SHADER_PARAMETER(FVector3f, AngularVelocity)
    SHADER_PARAMETER(float, MinRangeCm)
    SHADER_PARAMETER(float, MaxRangeCm)
    SHADER_PARAMETER(float, Duration)
    SHADER_PARAMETER(float, TimeOrigin)
    SHADER_PARAMETER(float, NoiseStdDev)
    SHADER_PARAMETER(uint32, MotionEnabled)
    SHADER_PARAMETER(uint32, MaterialEnabled)
    SHADER_PARAMETER(uint32, RayCount)
    SHADER_PARAMETER(uint32, MetadataCount)
    SHADER_PARAMETER(uint32, RandomSeed)
    SHADER_PARAMETER(uint32, PatternKind)
    SHADER_PARAMETER(FVector2f, PatternFrequency)
    SHADER_PARAMETER(FVector2f, PatternPhase)
    SHADER_PARAMETER(FVector2f, PatternFov)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    {
        return IsFeatureLevelSupported(P.Platform, ERHIFeatureLevel::SM6) &&
               IsRayTracingEnabledForProject(P.Platform) && RHISupportsRayTracing(P.Platform) &&
               RHISupportsInlineRayTracing(P.Platform);
    }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters&, FShaderCompilerEnvironment& E)
    {
        E.CompilerFlags.Add(CFLAG_InlineRayTracing);
        E.SetDefine(TEXT("VF_SUPPORTS_PRIMITIVE_SCENE_DATA"), 1);
    }
};
IMPLEMENT_GLOBAL_SHADER(FRaytracingLidarCS, "/Plugin/GemstarRaytracingLidar/Private/RaytracingLidar.usf", "MainCS", SF_Compute);
#endif

ESceneViewExtensionFlags FRaytracingLidarViewExtension::GetFlags() const
{
    if (!State->Active)
        return ESceneViewExtensionFlags::None;
    auto Flags = ESceneViewExtensionFlags::SubscribesToPostTLASBuild;
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
    Flags |= ESceneViewExtensionFlags::RequiresHardwareInlineRayTracing;
#endif
    return Flags;
}

void FRaytracingLidarViewExtension::BeginRenderViewFamily(FSceneViewFamily& Family)
{
#if RHI_RAYTRACING && !UE_VERSION_OLDER_THAN(5, 8, 0) && UE_VERSION_OLDER_THAN(5, 9, 0)
    if (!State->Active || !IsRayTracingEnabled() || Family.FrameNumber == MAX_uint32)
        return;
    const auto* AO = IConsoleManager::Get().FindConsoleVariable(TEXT("r.RayTracing.AmbientOcclusion"));
    if (!AO || AO->GetInt() != -1)
        return; // Respect explicit global AO overrides.
    FScopeLock Lock(&SceneRequestMutex);
    for (const auto* ConstView : Family.Views)
    {
        if (ConstView->bIsSceneCapture || !ConstView->IsRayTracingAllowedForView())
            continue;
        auto& View = *const_cast<FSceneView*>(ConstView);
        const auto Key = SceneRequestKey(View);
        if (SceneRequests.Contains(Key))
            continue;
        auto& PP = View.FinalPostProcessSettings;
        SceneRequests.Add(Key, {int32(PP.RayTracingAO), PP.RayTracingAOIntensity});
        // UE 5.8 omits inline extensions from AnyRayTracingPassEnabled. Request the
        // scene through a view-local AO flag, restored before any AO rendering.
        PP.RayTracingAO = true;
        PP.RayTracingAOIntensity = FMath::Max(PP.RayTracingAOIntensity, 1.f);
    }
#endif
}

void FRaytracingLidarViewExtension::PreRenderView_RenderThread(FRDGBuilder&, FSceneView& View)
{
#if RHI_RAYTRACING && !UE_VERSION_OLDER_THAN(5, 8, 0) && UE_VERSION_OLDER_THAN(5, 9, 0)
    FScopeLock Lock(&SceneRequestMutex);
    FSceneRequestSettings Previous;
    if (SceneRequests.RemoveAndCopyValue(SceneRequestKey(View), Previous))
    {
        // This public hook follows InitializeRayTracingFlags_RenderThread and
        // precedes CommitFinalPipelineState/RT shader setup. Keep the TLAS request,
        // but preserve the original lighting, including legitimately enabled AO.
        View.FinalPostProcessSettings.RayTracingAO = Previous.AO;
        View.FinalPostProcessSettings.RayTracingAOIntensity = Previous.Intensity;
    }
#endif
}

void FRaytracingLidarViewExtension::PostTLASBuild_RenderThread(FRDGBuilder& Graph, FSceneView& View)
{
#if RHI_RAYTRACING
    if (!State->Active || !View.Family || !View.Family->Scene || View.bIsSceneCapture || !View.bIsViewInfo ||
        View.Family->FrameNumber == MAX_uint32)
        return;
    if (!RaytracingLidarSupported())
    {
        State->Failed = true;
        return;
    }
    auto& Scene = *View.Family->Scene;
    if (!UE::FXRenderingUtils::RayTracing::HasRayTracingScene(Scene))
        return;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
    const auto& ViewInfo = static_cast<const FViewInfo&>(View);
    auto* RenderScene = Scene.GetRenderScene();
    if (!RenderScene)
        return;
    auto* TLAS =
        RenderScene->RayTracingScene.GetLayerView(ERayTracingSceneLayer::Base, ViewInfo.GetRayTracingSceneViewHandle());
    auto* Bindings = ViewInfo.InlineRayTracingBindingDataBuffer;
#else
    auto* Bindings = View.GetInlineRayTracingBindingDataBuffer();
    // A created scene can be stale for a view that did not request RT this frame.
    // Check required inline bindings before asking for its layer SRV. Vulkan
    // position fetch needs no inline SBT, so a null binding buffer is valid there.
    if (!Bindings && GRHIGlobals.RayTracing.RequiresInlineRayTracingSBT)
        return;
    auto* TLAS = UE::FXRenderingUtils::RayTracing::GetRayTracingSceneViewRDG(Scene, View);
#endif
    if (!TLAS || (!Bindings && GRHIGlobals.RayTracing.RequiresInlineRayTracingSBT))
        return;
    FRDGBufferRef Pattern = nullptr;
    for (auto& Slot : State->Slots)
    {
        if (Slot.State.load() != ERaytracingLidarSlotState::Queued)
            continue;
        RDG_EVENT_SCOPE_STAT(Graph, RaytracingLidarTrace, "GEMSTAR Raytracing LiDAR %u rays", State->Rays.Num());
        if (!Pattern && State->PatternBuffer)
            Pattern = Graph.RegisterExternalBuffer(State->PatternBuffer);
        if (!Pattern)
        {
            Pattern = CreateStructuredBuffer(Graph, TEXT("RaytracingLidar.Pattern"), State->Rays);
            Graph.QueueBufferExtraction(Pattern, &State->PatternBuffer);
        }
        const uint32 MetadataCount = Slot.Metadata.Num();
        if (Slot.Metadata.IsEmpty())
            Slot.Metadata.Add(FRaytracingLidarMetadata());
        auto* Objects = CreateStructuredBuffer(Graph, TEXT("RaytracingLidar.Metadata"), Slot.Metadata);
        auto* Materials = CreateStructuredBuffer(Graph, TEXT("RaytracingLidar.Profiles"), Slot.Profiles);
        auto* Output = Graph.CreateBuffer(
            FRDGBufferDesc::CreateStructuredDesc(sizeof(FRaytracingLidarHit), State->Rays.Num()), TEXT("RaytracingLidar.Results"));
        auto* P = Graph.AllocParameters<FRaytracingLidarCS::FParameters>();
        P->TLAS = TLAS;
        // Some RHIs fetch triangle positions directly and need no metadata binding.
        P->RayTracingSceneMetadata = Bindings ? Graph.CreateSRV(Bindings) : nullptr;
        auto& Uniforms = UE::FXRenderingUtils::CreateSceneUniformBuffer(Graph, &Scene);
        P->Scene = UE::FXRenderingUtils::GetSceneUniformBuffer(Graph, Uniforms);
        P->View = View.ViewUniformBuffer;
        P->Rays = Graph.CreateSRV(Pattern);
        P->Metadata = Graph.CreateSRV(Objects);
        P->Profiles = Graph.CreateSRV(Materials);
        P->Results = Graph.CreateUAV(Output);
        P->Origin = FVector3f(Slot.Pose.GetLocation() + View.ViewMatrices.GetPreViewTranslation());
        const auto Q = Slot.Pose.GetRotation();
        P->Rotation = FVector4f(Q.X, Q.Y, Q.Z, Q.W);
        P->LinearVelocity = FVector3f(Slot.LinearCmS);
        P->AngularVelocity = FVector3f(Slot.AngularRadS);
        const auto& C = State->Config;
        P->MinRangeCm = C.MinRangeMeters * 100;
        P->MaxRangeCm = C.MaxRangeMeters * 100;
        P->Duration = C.GetScanDurationSeconds();
        P->TimeOrigin = C.ScanTimeOrigin;
        P->NoiseStdDev = C.RangeNoiseStdDevMeters;
        P->MotionEnabled = C.MotionDistortionEnabled;
        P->MaterialEnabled = C.MaterialResponseEnabled;
        P->RayCount = State->Rays.Num();
        P->MetadataCount = MetadataCount;
        P->RandomSeed = C.Seed ^ uint32(Slot.ScanId);
        P->PatternKind = uint32(C.Pattern);
        P->PatternFrequency = FVector2f(C.LissajousFrequencyX, C.LissajousFrequencyY);
        P->PatternPhase = FVector2f(FMath::Fmod(Slot.Timestamp * C.LissajousFrequencyX * 2 * PI + C.LissajousPhaseX, 2 * PI),
            FMath::Fmod(Slot.Timestamp * C.LissajousFrequencyY * 2 * PI + C.LissajousPhaseY, 2 * PI));
        P->PatternFov =
            FVector2f(FMath::DegreesToRadians(C.HorizontalFovDegrees), FMath::DegreesToRadians(C.VerticalFovDegrees));
        TShaderMapRef<FRaytracingLidarCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
        FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("RaytracingLidar.TraceWholeScan"), Shader, P,
            FComputeShaderUtils::GetGroupCount(State->Rays.Num(), 64));
        if (!Slot.Readback)
            Slot.Readback = MakeUnique<FRHIGPUBufferReadback>(TEXT("RaytracingLidar.Readback"));
        AddEnqueueCopyPass(Graph, Slot.Readback.Get(), Output, State->Rays.Num() * sizeof(FRaytracingLidarHit));
        Slot.Frame = GFrameNumberRenderThread;
        Slot.State.store(ERaytracingLidarSlotState::Pending);
        CSV_CUSTOM_STAT(RaytracingLidar, Rays, State->Rays.Num(), ECsvCustomStatOp::Accumulate);
        // A queued scan is consumed once; secondary/stereo views cannot dispatch it again.
    }
#endif
}

void PollRaytracingLidar(const TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe>& State)
{
    ENQUEUE_RENDER_COMMAND(RaytracingLidarPoll)(
        [State](FRHICommandListImmediate&)
        {
            for (auto& Slot : State->Slots)
            {
                if (Slot.State.load() != ERaytracingLidarSlotState::Pending || !Slot.Readback || !Slot.Readback->IsReady())
                    continue;
                const uint32 Bytes = State->Rays.Num() * sizeof(FRaytracingLidarHit);
                const void* Data = Slot.Readback->Lock(Bytes);
                if (!Data)
                {
                    State->Failed = true;
                    Slot.State.store(ERaytracingLidarSlotState::Free);
                    continue;
                }
                Slot.Hits.SetNumUninitialized(State->Rays.Num());
                FMemory::Memcpy(Slot.Hits.GetData(), Data, Bytes);
                Slot.Readback->Unlock();
                CSV_CUSTOM_STAT(
                    RaytracingLidar, ReadbackFrames, int32(GFrameNumberRenderThread - Slot.Frame), ECsvCustomStatOp::Set);
                Slot.State.store(ERaytracingLidarSlotState::Complete);
            }
        });
}

void ReleaseRaytracingLidar(TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe> State)
{
    if (!State)
        return;
    State->Active = false;
    ENQUEUE_RENDER_COMMAND(RaytracingLidarRelease)(
        [State](FRHICommandListImmediate&)
        {
            // RHI references use deferred deletion; no game-thread fence/wait is needed.
            for (auto& Slot : State->Slots)
                Slot.Readback.Reset();
            State->PatternBuffer.SafeRelease();
        });
}
