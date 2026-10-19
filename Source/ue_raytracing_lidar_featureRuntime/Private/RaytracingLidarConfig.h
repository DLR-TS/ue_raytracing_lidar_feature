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
#include "Utils/GemstarInnerConfig.h"

bool ParseRaytracingLidarConfig(const FInnerConfig& Input, FRaytracingLidarConfig& Out, FString& Error);
