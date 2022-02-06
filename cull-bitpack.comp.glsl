/*
 * Copyright (c) 2014-2021, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2014-2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */



#version 430
#extension GL_ARB_shading_language_include : enable
#include "cull-common.h"

layout(local_size_x=CULLSYS_COMPUTE_THREADS) in;

#define TEMPORAL_LAST 1
#define TEMPORAL_NEW  2

#ifndef TEMPORAL
#define TEMPORAL 0
#endif


layout(location=0) uniform uint numObjects;

layout(std430,binding=CULLSYS_BIT_SSBO_IN)  readonly buffer inputBuffer {
  uvec4 instream[];
};

#if TEMPORAL
layout(std430,binding=CULLSYS_BIT_SSBO_LAST)  readonly buffer lastBuffer {
  uint lasts[];
};
#endif

layout(std430,binding=CULLSYS_BIT_SSBO_OUT)  writeonly buffer outputBuffer {
  uint outstream[];
};


void main ()
{
  uint streamMax    = (numObjects - 1 + 31) / 32;
  uint globalThreadID = gl_GlobalInvocationID.x;

  uint bits = 0u;
  int outbit = 0;
  for (int i = 0; i < 8; i++){
    uvec4 inbytes4 = instream[min(globalThreadID,streamMax) * 8 + i];
    for (int n = 0; n < 4; n++, outbit++){
      uint checkbytes = inbytes4[n];
      bits |= (checkbytes & 1u) << outbit;
    }
  }
  
  if (globalThreadID > streamMax) return;
  
#if TEMPORAL
  uint last = lasts[globalThreadID];
#endif
  
#if TEMPORAL == TEMPORAL_LAST
  // render what was visible in last frame and passes current test
  bits &= last;
#elif TEMPORAL == TEMPORAL_NEW
  // render what was not visible in last frame (already rendered), but is now visible
  bits &= (~last);
#endif

  outstream[globalThreadID] = bits;
}
