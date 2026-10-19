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
#include "RaytracingLidarTypes.h"
#include "SceneViewExtension.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"
#include <atomic>

// Game thread: Free -> Writing -> Queued. Render thread: Queued -> Pending -> Complete.
// The game thread delivers completed results and returns the slot to Free.
enum class ERaytracingLidarSlotState : uint8
{
    Free,
    Writing,
    Queued,
    Pending,
    Complete
};
struct FRaytracingLidarSlot
{
    std::atomic<ERaytracingLidarSlotState> State{ERaytracingLidarSlotState::Free};
    uint64 ScanId = 0, Frame = 0;
    double Timestamp = 0;
    double QueuedWallTime = 0;
    FTransform Pose;
    FVector LinearCmS = FVector::ZeroVector, AngularRadS = FVector::ZeroVector;
    TArray<FRaytracingLidarMetadata> Metadata;
    TArray<FRaytracingLidarProfileGpu> Profiles;
    TArray<FRaytracingLidarHit> Hits;
    TUniquePtr<FRHIGPUBufferReadback> Readback; // render thread only
};

struct FRaytracingLidarRenderState
{
    FRaytracingLidarConfig Config;
    TArray<FRaytracingLidarRay> Rays;
    FRaytracingLidarSlot Slots[3];
    std::atomic<bool> Active{true};
    std::atomic<bool> Failed{false};
    TRefCountPtr<FRDGPooledBuffer> PatternBuffer; // render thread only
    FRaytracingLidarSlot* TryBeginScan()
    {
        if (!Active)
            return nullptr;
        for (auto& Slot : Slots)
        {
            ERaytracingLidarSlotState Expected = ERaytracingLidarSlotState::Free;
            if (Slot.State.compare_exchange_strong(Expected, ERaytracingLidarSlotState::Writing))
                return &Slot;
        }
        return nullptr;
    }
};

bool RaytracingLidarSupported();
bool AcquireRaytracingLidarRayTracing();
void ReleaseRaytracingLidarRayTracing();
void PollRaytracingLidar(const TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe>& State);
void ReleaseRaytracingLidar(TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe> State);

class FRaytracingLidarViewExtension final : public FWorldSceneViewExtension
{
public:
    FRaytracingLidarViewExtension(
        const FAutoRegister& AutoRegister, UWorld* World, TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe> InState)
        : FWorldSceneViewExtension(AutoRegister, World), State(MoveTemp(InState))
    {
    }
    virtual ESceneViewExtensionFlags GetFlags() const override;
    virtual void BeginRenderViewFamily(FSceneViewFamily& ViewFamily) override;
    virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View) override;
    virtual void PostTLASBuild_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& View) override;

private:
    TSharedPtr<FRaytracingLidarRenderState, ESPMode::ThreadSafe> State;
};
