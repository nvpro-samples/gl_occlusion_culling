/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


#version 450
#extension GL_ARB_shading_language_include : enable
#extension GL_NV_mesh_shader : require
#extension GL_NV_gpu_shader5 : require

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_vote : require

#include "cull-common.h"

#pragma optionNV(unroll all)

//////////////////////////////////////////////

layout(local_size_x=CULLSYS_TASK_BATCH) in;

//////////////////////////////////////////////

layout(location=0) uniform uint objectMax;

layout(binding=CULLSYS_UBO_VIEW, std140) uniform viewBuffer {
  ViewData view;
};

layout(binding=CULLSYS_SSBO_MATRICES, std430) readonly buffer matricesBuffer {
  MatrixData matrices[];
};

#ifdef DUALINDEX
layout(binding=CULLSYS_SSBO_BBOXES, std430) readonly buffer bboxBuffer {
  BboxData bboxes[];
};
layout(binding=CULLSYS_SSBO_INPUT_BBOX, std430) readonly buffer bboxIndexBuffer {
  int bboxIndices[];
};
#else
layout(binding=CULLSYS_SSBO_INPUT_BBOX, std430) readonly buffer bboxBuffer {
  BboxData bboxes[];
};
#endif

layout(binding=CULLSYS_SSBO_INPUT_MATRIX, std430) readonly buffer matrixIndexBuffer {
  int matrixIndices[];
};

layout(std430,binding=CULLSYS_SSBO_OUT_VIS) writeonly buffer visibleBuffer {
  int visibles[];
};

//////////////////////////////////////////////

taskNV out Task {
  uint      baseID;
  uint      validCount;
  uint8_t   direction_subID[CULLSYS_TASK_BATCH]; // 3 + 5 bits
} OUT;

//////////////////////////////////////////////

void main()
{
  uint laneID      = gl_LocalInvocationID.x;
  uint objectID    = gl_GlobalInvocationID.x;
  uint objectRead  = min(objectID,objectMax);
  
  bool isValid     = objectID <= objectMax;
  uint direction   = 0;

  int  matrixIndex = matrixIndices[objectRead];
#ifdef DUALINDEX
  int  bboxIndex   = bboxIndices[objectRead];
#else
  int  bboxIndex   = int(objectRead);
#endif

  vec4 bboxMin     = bboxes[bboxIndex].bboxMin;
  vec4 bboxMax     = bboxes[bboxIndex].bboxMax;

  vec3 ctr =((bboxMin + bboxMax)*0.5).xyz;
  vec3 dim =((bboxMax - bboxMin)*0.5).xyz;
  
  mat4 worldInvTransTM = matrices[matrixIndex].worldInvTransTM;
    
  vec3 localViewPos = (vec4(view.viewPos,1) * worldInvTransTM).xyz;
  localViewPos -= ctr;

  if (isValid)
  {
    if (all(lessThan(abs(localViewPos),dim))){
      // inside bbox
      visibles[objectID] = 1;
      isValid = false;
    }
    else {
    #if 1
      // avoid loading data again (for precision you might prefer below)
      mat4 worldTM = inverse(transpose(worldInvTransTM));
    #else
      mat4 worldTM = matrices[matrixIndex].worldTM;
    #endif
      mat4 worldViewProjTM = view.viewProjTM * worldTM;
    
    // this could be disabled if you don't need it
    #if 1
      // frustum and pixel cull
      vec4 hPos0    = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, 0);
      vec3 clipmin  = projected(hPos0);
      vec3 clipmax  = clipmin;
      uint clipbits = getCullBits(hPos0);

      for (int n = 1; n < 8; n++){
        vec4 hPos   = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, n);
        vec3 ab     = projected(hPos);
        clipmin = min(clipmin,ab);
        clipmax = max(clipmax,ab);
        clipbits &= getCullBits(hPos);
      }    
      
      if (clipbits != 0 || pixelCull(view.viewSize, view.viewCullThreshold, clipmin, clipmax))
      {
        // invisible
        // skip rasterization of this box
        isValid = false;
      }
      else
    #endif
      {
        // localViewPos is local to the bbox, and allows us to compute the relative direction
        // vector of the camera to the center of the bbox.
        // This way we can figure out which of the 3 sides of the bbox are visible
      
        direction |= localViewPos.x > 0 ? 1 : 0;
        direction |= localViewPos.y > 0 ? 2 : 0;
        direction |= localViewPos.z > 0 ? 4 : 0;
      }
    }
  }
  uvec4 validVote   = subgroupBallot(isValid);
  uint  validCount  = subgroupBallotBitCount(validVote);
  uint  validPrefix = subgroupBallotExclusiveBitCount(validVote);
  
  if (laneID == 0) {
    OUT.baseID     = gl_WorkGroupID.x * CULLSYS_TASK_BATCH;
    OUT.validCount = validCount;
    gl_TaskCountNV = (validCount + CULLSYS_MESH_BATCH - 1) / CULLSYS_MESH_BATCH;
  }
  
  if (isValid) {
    OUT.direction_subID[validPrefix] = uint8_t( (laneID << 3) | direction);
  }
}
