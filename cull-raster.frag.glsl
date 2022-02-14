/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2014-2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


#version 450
#extension GL_ARB_shading_language_include : enable
#include "cull-common.h"

layout(early_fragment_tests) in;

layout(std430,binding=CULLSYS_SSBO_OUT_VIS) buffer visibleBuffer {
  int visibles[];
};

#if CULLSYS_DEBUG_VISIBLEBOXES
layout(location=0,index=0) out vec4 out_Color;
#endif

layout(location=0) in Interpolant {
  flat int f_objectID;
} IN;

void main (){
  visibles[IN.f_objectID] = 1;
#if CULLSYS_DEBUG_VISIBLEBOXES
  out_Color = unpackUnorm4x8(uint(IN.f_objectID) ^ uint(IN.f_objectID << 4));
#endif
}
