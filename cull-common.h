/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


#ifdef __cplusplus
namespace cullsys_glsl
{
  using namespace nvmath;
#endif

struct MatrixData {
  mat4    worldTM;
  mat4    worldInvTransTM;
};

struct BboxData {
  vec4    bboxMin;
  vec4    bboxMax;
};

struct ViewData {
  mat4    viewProjTM;
  vec3    viewDir;
  vec3    viewPos;
  vec2    viewSize;
  float   viewCullThreshold;
};

#ifdef __cplusplus
}
#else
  const int CULL_SKIP_ID = ~0;
  
  vec4 getBoxCorner(vec4 bboxMin, vec4 bboxMax, int n)
  {
  #if 1
    bvec3 useMax = bvec3((n & 1) != 0, (n & 2) != 0, (n & 4) != 0);
    return vec4(mix(bboxMin.xyz, bboxMax.xyz, useMax),1);
  #else
    switch(n){
    case 0:
      return vec4(bboxMin.x,bboxMin.y,bboxMin.z,1);
    case 1:
      return vec4(bboxMax.x,bboxMin.y,bboxMin.z,1);
    case 2:
      return vec4(bboxMin.x,bboxMax.y,bboxMin.z,1);
    case 3:
      return vec4(bboxMax.x,bboxMax.y,bboxMin.z,1);
    case 4:
      return vec4(bboxMin.x,bboxMin.y,bboxMax.z,1);
    case 5:
      return vec4(bboxMax.x,bboxMin.y,bboxMax.z,1);
    case 6:
      return vec4(bboxMin.x,bboxMax.y,bboxMax.z,1);
    case 7:
      return vec4(bboxMax.x,bboxMax.y,bboxMax.z,1);
    }
  #endif
  }

  uint getCullBits(vec4 hPos)
  {
    uint cullBits = 0;
    cullBits |= hPos.x < -hPos.w ?  1 : 0;
    cullBits |= hPos.x >  hPos.w ?  2 : 0;
    cullBits |= hPos.y < -hPos.w ?  4 : 0;
    cullBits |= hPos.y >  hPos.w ?  8 : 0;
    cullBits |= hPos.z < -hPos.w ? 16 : 0;
    cullBits |= hPos.z >  hPos.w ? 32 : 0;
    cullBits |= hPos.w <= 0      ? 64 : 0; 
    return cullBits;
  }

  vec3 projected(vec4 pos) {
    return pos.xyz/pos.w;
  }

  bool pixelCull(vec2 viewSize, float viewCullThreshold, vec3 clipmin, vec3 clipmax)
  {
    vec2 dim = (clipmax.xy - clipmin.xy) * 0.5 * viewSize;;
    return  max(dim.x, dim.y) < viewCullThreshold;
  }
#endif

#define CULLSYS_UBO_VIEW            0
#define CULLSYS_SSBO_OUT_VIS        0
#define CULLSYS_SSBO_MATRICES       1
#define CULLSYS_SSBO_BBOXES         2
#define CULLSYS_SSBO_INPUT_BBOX     3
#define CULLSYS_SSBO_INPUT_MATRIX   4
#define CULLSYS_TEX_DEPTH           0

#define CULLSYS_BIT_SSBO_OUT     0
#define CULLSYS_BIT_SSBO_IN      1
#define CULLSYS_BIT_SSBO_LAST    2

#define CULLSYS_JOBIND_SSBO_COUNT 0
#define CULLSYS_JOBIND_SSBO_OUT   1
#define CULLSYS_JOBIND_SSBO_IN    2
#define CULLSYS_JOBIND_SSBO_VIS   3

// how many cmds per thread are processed
// at high rejection rates 32 is faster
// 1 or 32
#define CULLSYS_JOBIND_BATCH          1

#define CULLSYS_COMPUTE_THREADS       64

#define CULLSYS_TASK_BATCH            32
#define CULLSYS_MESH_BATCH            8

// the instanced renderer uses pre-computed uint16_t index buffer for bboxes
// uint16_t indices provide extra performance on current NVIDIA hardware
#define CULLSYS_INSTANCED_VERTICES    8
#define CULLSYS_INSTANCED_TRIANGLES   6
#define CULLSYS_INSTANCED_INDICES     (CULLSYS_INSTANCED_TRIANGLES * 3)
#define CULLSYS_INSTANCED_BBOXES      (0x10000 / CULLSYS_INSTANCED_VERTICES)

// for debugging
#define CULLSYS_DEBUG_VISIBLEBOXES    0
