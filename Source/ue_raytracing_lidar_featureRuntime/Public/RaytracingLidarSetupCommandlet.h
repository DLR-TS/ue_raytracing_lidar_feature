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
#include "Commandlets/Commandlet.h"
#include "RaytracingLidarSetupCommandlet.generated.h"

// Reproducibly creates the binary GameFeatureData asset using Unreal serialization.
UCLASS()
class URaytracingLidarSetupCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    virtual int32 Main(const FString& Params) override;
};
