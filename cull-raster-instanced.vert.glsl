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
#include "cull-common.h"

#pragma optionNV(unroll all)

//////////////////////////////////////////////

layout(location=0) uniform int objectOffset;

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

layout(location=0) out Interpolant {
  flat int f_objectID;
} OUT;

//////////////////////////////////////////////

void main()
{
  // the index buffer is generated so that vertex indices [0..7] are repeatedly offset by
  // for each box, so we get CULLSYS_INSTANCED_BBOXES many boxes
  // [0..7][8..15][16..23] .... [65528..65535]
  // therefore we can reconstruct which vertex [0..7] and object we are
  // 
  // We draw all bboxes through 2 drawcalls:
  // The first uses instancing and per instance does CULLSYS_INSTANCED_BBOXES
  // many boxes. The second one renders the tail/remaining number of of objects without hw-instancing.
  // We provide the number of bboxes from the first hw-instanced pass as uniform.
  
  int boxVertexID = gl_VertexID % (CULLSYS_INSTANCED_VERTICES);
  int objectID    = (gl_VertexID / CULLSYS_INSTANCED_VERTICES) + (gl_InstanceID * CULLSYS_INSTANCED_BBOXES) + objectOffset;

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
  
  mat4 worldInvTransTM = matrices[matrixIndex].worldInvTransTM;
    
  vec3 localViewPos = (vec4(view.viewPos,1) * worldInvTransTM).xyz;
  localViewPos -= ctr;
  if (all(lessThan(abs(localViewPos),dim))){
    // inside bbox
    visibles[objectID] = 1;
    // skip rasterization of this box
    gl_Position = vec4(-2,-2,-2,1);
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
      gl_Position = vec4(-2,-2,-2,1);
    }
    else
  #endif
    {
      // localViewPos is local to the bbox, and allows us to compute the relative direction
      // vector of the camera to the center of the bbox.
      // This way we can figure out which of the 3 sides of the bbox are visible
    
      uint directionIndex = 0;
      directionIndex |= localViewPos.x > 0 ? 1 : 0;
      directionIndex |= localViewPos.y > 0 ? 2 : 0;
      
      // We use one canonical index buffer for the local view direction vector (+1,+1,+1).
      // This code remaps the vertex [0..7] to a different vertex of the box, according to
      // on the actual view direction.
      // See `cull-raster-instanced.lua` for the generation of these magic values.
      
      uvec4 vertexMapLower  = uvec4(0x2134657u, 0x10325476u, 0x23016745u, 0x31207564u);
      uvec4 vertexMapUppper = uvec4(0x45670123u, 0x57461302u, 0x64752031u, 0x76543210u);
      
      uvec4 vertexMap = localViewPos.z < 0 ? vertexMapLower : vertexMapUppper;
      
      uint localMap = vertexMap [directionIndex];
      uint localIdx = (localMap >> (boxVertexID * 4)) & 7;
      
      OUT.f_objectID  = objectID;
      gl_Position     = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, int(localIdx));
    }
  }
}
