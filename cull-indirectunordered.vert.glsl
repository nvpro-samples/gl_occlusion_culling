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
/**/

layout(binding=0,offset=0)uniform atomic_uint   cullCounterBuffer;
layout(std430,binding=0)  writeonly buffer outputBuffer {
  int outcmds[];
};

layout(std430,binding=1)  readonly buffer inputBuffer {
  int incmds[];
};
layout(std430,binding=2)  readonly buffer visibleBuffer {
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
  if ( (visibles[gl_VertexID/32] & (1<<(gl_VertexID%32))) != 0 ){
    uint slot = atomicCounterIncrement(cullCounterBuffer);
    
    for (uint i = 0; i < COMMANDSIZE; i++){
      outcmds[slot * COMMANDSTRIDE + i] = incmds[ gl_VertexID * COMMANDSTRIDE + i ];
    }
  }
}
