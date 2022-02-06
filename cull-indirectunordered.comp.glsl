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


#version 440
#extension GL_ARB_shading_language_include : enable

#pragma optionNV(unroll all)

#include "cull-common.h"

layout(local_size_x=CULLSYS_COMPUTE_THREADS) in;

layout(location=0) uniform uint numObjects;

layout(std430,binding=CULLSYS_JOBIND_SSBO_COUNT) coherent buffer cullCounterBuffer {
  uint cullCounter;
};
layout(std430,binding=CULLSYS_JOBIND_SSBO_OUT)  writeonly buffer outputBuffer {
  int outcmds[];
};
layout(std430,binding=CULLSYS_JOBIND_SSBO_IN)  readonly buffer inputBuffer {
  int incmds[];
};
layout(std430,binding=CULLSYS_JOBIND_SSBO_VIS)  readonly buffer visibleBuffer {
  int visibles[];
};

// default struct size for DrawElementsIndirect
#ifndef COMMANDSIZE
#define COMMANDSIZE 5
#endif

#ifndef COMMANDSTRIDE
#define COMMANDSTRIDE COMMANDSIZE
#endif

void main ()
{
  uint globalThreadID = gl_GlobalInvocationID.x;  
  uint streamMax   = (numObjects - 1 + 31) / 32;
  
#if CULLSYS_JOBIND_BATCH == 1
  uint visibleBits = visibles[min(globalThreadID/32, streamMax)];
  uint visibleMask = (1<<(globalThreadID % 32));
  uint localBits   = visibleBits & visibleMask;
  
  if (globalThreadID >= numObjects) return;
  
  if (localBits != 0) 
  {
    uint slot = atomicAdd(cullCounter, 1);
    
    for (uint i = 0; i < COMMANDSIZE; i++){
      outcmds[slot * COMMANDSTRIDE + i] = incmds[ globalThreadID * COMMANDSTRIDE + i ];
    }
  }
#else
  uint visibleBits = visibles[min(globalThreadID, streamMax)];
  uint visibleMask = ~0u;
  uint localBits   = visibleBits & visibleMask;
  
  if (globalThreadID > streamMax) return;
  
  uint localCount  = bitCount(localBits);
  if (localCount > 0) {
    uint slot = atomicAdd(cullCounter, localCount);
    
    for (uint b = 0; b < CULLSYS_JOBIND_BATCH; b++) {
      if ((localBits & (uint(1) << b)) != 0)
      {
        for (uint i = 0; i < COMMANDSIZE; i++){
          outcmds[slot * COMMANDSTRIDE + i] = incmds[ (globalThreadID * CULLSYS_JOBIND_BATCH + b) * COMMANDSTRIDE + i ];
        }
        slot++;
      }
    }
  }
#endif
}
