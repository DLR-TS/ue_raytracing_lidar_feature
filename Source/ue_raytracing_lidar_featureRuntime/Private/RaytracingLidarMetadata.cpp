/*
 * Copyright (c) 2026 DLR e.V.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 */

#include "RaytracingLidarMetadata.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "StaticMeshResources.h"

bool FRaytracingLidarMaterialProfile::ToGpu(FRaytracingLidarProfileGpu& Out) const
{
    if (ProfileId < 0)
        return false;
    for (float V :
        {Reflectivity, RetroReflectivity, Roughness, SpecularResponse, Transmission, Absorption, WavelengthResponse})
        if (!FMath::IsFinite(V) || V < 0)
            return false;
    if (Reflectivity > 1 || Roughness > 1 || Transmission > 1 || Absorption > 1)
        return false;
    Out = {uint32(ProfileId), Reflectivity, RetroReflectivity, Roughness, SpecularResponse, Transmission, Absorption,
        WavelengthResponse};
    return true;
}

void GatherRaytracingLidarMetadata(UWorld* World, TArray<FRaytracingLidarMetadata>& Metadata, TArray<FRaytracingLidarProfileGpu>& Profiles)
{
    check(IsInGameThread());
    Metadata.Reset();
    Profiles.Reset();
    Profiles.Add(FRaytracingLidarProfileGpu());
    TMap<uint32, uint32> ProfileIndices;
    ProfileIndices.Add(0, 0);
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        const auto* Tag = It->FindComponentByClass<URaytracingLidarMetadataComponent>();
        if (!Tag)
            continue;
        TInlineComponentArray<UStaticMeshComponent*> Components(*It);
        for (const auto* C : Components)
        {
            if (!C->IsRegistered() || !C->GetPrimitiveSceneId().IsValid())
                continue;
            if (!Tag->PrimitiveComponentName.IsNone() && C->GetFName() != Tag->PrimitiveComponentName)
                continue;
            const UStaticMesh* Mesh = C->GetStaticMesh();
            const auto* Data = Mesh ? Mesh->GetRenderData() : nullptr;
            if (!Data || Data->LODResources.IsEmpty())
                continue;
            const auto& Sections = Data->LODResources[0].Sections;
            // Segment-to-slot mapping is only safe when all RT-selectable LODs agree.
            bool Compatible = true;
            for (const auto& LOD : Data->LODResources)
            {
                if (LOD.Sections.Num() != Sections.Num())
                {
                    Compatible = false;
                    break;
                }
                for (int32 S = 0; S < Sections.Num(); ++S)
                    Compatible &= LOD.Sections[S].MaterialIndex == Sections[S].MaterialIndex;
            }
            if (!Compatible)
                continue;
            for (int32 S = 0; S < Sections.Num(); ++S)
            {
                const int32 Slot = Sections[S].MaterialIndex;
                FRaytracingLidarProfileGpu Profile;
                if (Tag->MaterialSlots.IsValidIndex(Slot) && !Tag->MaterialSlots[Slot].ToGpu(Profile))
                    continue;
                uint32 ProfileIndex = 0;
                if (const uint32* Existing = ProfileIndices.Find(Profile.ProfileId))
                {
                    // Reject conflicting definitions for a stable profile ID.
                    if (FMemory::Memcmp(&Profiles[*Existing], &Profile, sizeof(Profile)) != 0)
                        continue;
                    ProfileIndex = *Existing;
                }
                else
                {
                    ProfileIndex = Profiles.Add(Profile);
                    ProfileIndices.Add(Profile.ProfileId, ProfileIndex);
                }
                const uint64 Id = Tag->OsiObjectId < 0 ? MAX_uint64 : uint64(Tag->OsiObjectId);
                Metadata.Add({C->GetPrimitiveSceneId().PrimIDValue, uint32(S), uint32(Id), uint32(Id >> 32),
                    ProfileIndex, uint32(FMath::Max(0, Tag->SemanticClass)), 0, 0});
            }
        }
    }
    // The shader performs a binary search by component ID, then geometry segment.
    Metadata.Sort(
        [](const auto& A, const auto& B)
        {
            return A.ComponentId != B.ComponentId ? A.ComponentId < B.ComponentId : A.Segment < B.Segment;
        });
}
