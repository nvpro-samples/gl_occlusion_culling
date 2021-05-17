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

layout(location=0) in uint  cmdSize;
layout(location=1) in int   cmdObject;

layout(std430,binding=0)  writeonly buffer outputBuffer {
  uint outsizes[];
};

layout(std430,binding=1)  readonly buffer visibleBuffer {
  int visibles[];
};

#define DEBUG false

void main ()
{
  if (cmdObject >= 0 && !DEBUG){
    outsizes[gl_VertexID] = (visibles[cmdObject/32] & (1<<(cmdObject%32))) != 0 ? cmdSize : 0;
  }
  else{
    outsizes[gl_VertexID] = cmdSize;
  }
}
