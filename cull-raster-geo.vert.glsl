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

//////////////////////////////////////////////

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

layout(location=0) out VertexOut{
  vec3 bboxCtr;
  vec3 bboxDim;
  flat uint direction_matrixIndex;
  flat int  objectID;
} OUT;

//////////////////////////////////////////////

void main()
{
  int objectID = gl_VertexID;
  
  int  matrixIndex = matrixIndices[objectID];
#ifdef DUALINDEX
  int  bboxIndex   = bboxIndices[objectID];
#else
  int  bboxIndex   = objectID;
#endif

  vec4 bboxMin     = bboxes[bboxIndex].bboxMin;
  vec4 bboxMax     = bboxes[bboxIndex].bboxMax;
  
  vec3 ctr =((bboxMin + bboxMax)*0.5).xyz;
  vec3 dim =((bboxMax - bboxMin)*0.5).xyz;
  OUT.bboxCtr               = ctr;
  OUT.bboxDim               = dim;
  OUT.direction_matrixIndex = uint(matrixIndex) << 3;
  OUT.objectID              = objectID;
  
  
  // if camera is inside the bbox then none of our
  // side faces will be visible, must treat object as 
  // visible
  
  mat4 worldInvTransTM = matrices[matrixIndex].worldInvTransTM;
    
  vec3 localViewPos = (vec4(view.viewPos,1) * worldInvTransTM).xyz;
  localViewPos -= ctr;
  if (all(lessThan(abs(localViewPos),dim))){
    // inside bbox
    visibles[objectID] = 1;
    // skip rasterization of this box
    OUT.objectID = CULL_SKIP_ID;
  }
  else {
  // this could be disabled if you don't need it
  #if 1
    #if 1
      // avoid loading data again (for precision you might prefer below)
      mat4 worldTM = inverse(transpose(worldInvTransTM));
    #else
      mat4 worldTM = matrices[matrixIndex].worldTM;
    #endif
      mat4 worldViewProjTM = view.viewProjTM * worldTM;
  
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
      OUT.objectID = CULL_SKIP_ID;
    }
    else 
  #endif
    {
      // localViewPos is local to the bbox, and allows us to compute the relative direction
      // vector of the camera to the center of the bbox.
      // This way we can figure out which of the 3 sides of the bbox are visible
      
      OUT.direction_matrixIndex  |= localViewPos.x > 0 ? 1 : 0;
      OUT.direction_matrixIndex  |= localViewPos.y > 0 ? 2 : 0;
      OUT.direction_matrixIndex  |= localViewPos.z > 0 ? 4 : 0;
    }
  }
}
