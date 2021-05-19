#version 430
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

#ifndef MATRIX_WORLD
#define MATRIX_WORLD    0
#endif

#ifndef MATRIX_WORLD_IT
#define MATRIX_WORLD_IT 1
#endif

#ifndef MATRICES
#define MATRICES        2
#endif

const int CULL_SKIP_ID = ~0;

//////////////////////////////////////////////

layout(binding=0, std140) uniform viewBuffer {
  mat4    viewProjTM;
  vec3    viewDir;
  vec3    viewPos;
  vec2    viewSize;
  float   viewCullThreshold;
};

layout(binding=0) uniform samplerBuffer matricesTex;
#ifdef DUALINDEX
layout(binding=1) uniform samplerBuffer bboxesTex;
#endif

layout(std430,binding=0) buffer visibleBuffer {
  int visibles[];
};

//////////////////////////////////////////////

#ifdef DUALINDEX
layout(location=0) in int  bboxIndex;
layout(location=2) in int  matrixIndex;

vec4 bboxMin = texelFetch(bboxesTex, bboxIndex*2+0);
vec4 bboxMax = texelFetch(bboxesTex, bboxIndex*2+1);
#else
layout(location=0) in vec4 bboxMin;
layout(location=1) in vec4 bboxMax;
layout(location=2) in int  matrixIndex;
#endif

out VertexOut{
  vec3 bboxCtr;
  vec3 bboxDim;
  flat int matrixIndex;
  flat int objid;
} OUT;

//////////////////////////////////////////////

vec4 getBoxCorner(int n)
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

bool pixelCull(vec3 clipmin, vec3 clipmax)
{
  vec2 dim = (clipmax.xy - clipmin.xy) * 0.5 * viewSize;
  return max(dim.x, dim.y) < viewCullThreshold;
}

//////////////////////////////////////////////

void main()
{
  int objid = gl_VertexID;
  vec3 ctr =((bboxMin + bboxMax)*0.5).xyz;
  vec3 dim =((bboxMax - bboxMin)*0.5).xyz;
  OUT.bboxCtr = ctr;
  OUT.bboxDim = dim;
  OUT.matrixIndex = matrixIndex;
  OUT.objid = objid;
  
  
  // if camera is inside the bbox then none of our
  // side faces will be visible, must treat object as 
  // visible
  
  int matindex = (matrixIndex * MATRICES + MATRIX_WORLD_IT) * 4;
  mat4 worldInvTransTM = mat4(
    texelFetch(matricesTex,matindex + 0),
    texelFetch(matricesTex,matindex + 1),
    texelFetch(matricesTex,matindex + 2),
    texelFetch(matricesTex,matindex + 3));
    
  vec3 objPos = (vec4(viewPos,1) * worldInvTransTM).xyz;
  objPos -= ctr;
  if (all(lessThan(abs(objPos),dim))){
    // inside bbox
    visibles[objid] = 1;
    // skip rasterization of this box
    OUT.objid = CULL_SKIP_ID;
  }
  else {
  #if 1
    // avoid loading data
    mat4 worldTM = inverse(transpose(worldInvTransTM));
  #else
    int matindex2 = (matrixIndex * MATRICES + MATRIX_WORLD) * 4;
    mat4 worldTM = mat4(
      texelFetch(matricesTex,matindex + 0),
      texelFetch(matricesTex,matindex + 1),
      texelFetch(matricesTex,matindex + 2),
      texelFetch(matricesTex,matindex + 3));
  #endif
    mat4 worldViewProjTM = viewProjTM * worldTM;
  
    // frustum and pixel cull
    vec4 hPos0    = worldViewProjTM * getBoxCorner(0);
    vec3 clipmin  = projected(hPos0);
    vec3 clipmax  = clipmin;
    uint clipbits = getCullBits(hPos0);

    for (int n = 1; n < 8; n++){
      vec4 hPos   = worldViewProjTM * getBoxCorner(n);
      vec3 ab     = projected(hPos);
      clipmin = min(clipmin,ab);
      clipmax = max(clipmax,ab);
      clipbits &= getCullBits(hPos);
    }    
    
    if (clipbits != 0 || pixelCull(clipmin, clipmax))
    {
      // invisible
      // skip rasterization of this box
      OUT.objid = CULL_SKIP_ID;
    }
  }
}
