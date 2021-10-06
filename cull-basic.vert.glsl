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
#endif

layout(std430,binding=CULLSYS_SSBO_OUT_VIS) writeonly buffer visibleBuffer {
  int visibles[];
};

#ifdef OCCLUSION
layout(binding=CULLSYS_TEX_DEPTH) uniform sampler2D depthTex;
#endif

//////////////////////////////////////////////

#ifdef DUALINDEX
layout(location=0) in int  bboxIndex;
layout(location=2) in int  matrixIndex;

uniform samplerBuffer     bboxesTex;
vec4 bboxMin = texelFetch(bboxesTex, bboxIndex*2+0);
vec4 bboxMax = texelFetch(bboxesTex, bboxIndex*2+1);
#else
layout(location=0) in vec4 bboxMin;
layout(location=1) in vec4 bboxMax;
layout(location=2) in int  matrixIndex;
#endif

//////////////////////////////////////////////

void main (){
  bool isVisible = false;
  mat4 worldTM = matrices[matrixIndex].worldTM;
    
  mat4 worldViewProjTM = (view.viewProjTM * worldTM);
  
  // clipspace bbox
  vec4 hPos0    = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, 0);
  vec3 clipmin  = projected(hPos0);
  vec3 clipmax  = clipmin;
  uint clipbits = getCullBits(hPos0);

  for (int n = 1; n < 8; n++){
    vec4 hPos   = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, n);
    vec3 ab     = projected(hPos);
    clipmin     = min(clipmin,ab);
    clipmax     = max(clipmax,ab);
    clipbits    &= getCullBits(hPos);
  }

  isVisible = (clipbits == 0 && !pixelCull(view.viewSize, view.viewCullThreshold, clipmin, clipmax));

#ifdef OCCLUSION
  if (isVisible){
    clipmin = clipmin * 0.5 + 0.5;
    clipmax = clipmax * 0.5 + 0.5;
    vec2 size = (clipmax.xy - clipmin.xy);
    ivec2 texsize = textureSize(depthTex,0);
    float maxsize = max(size.x, size.y) * float(max(texsize.x,texsize.y));
    float miplevel = ceil(log2(maxsize));
    
    float depth = 0;
    float a = textureLod(depthTex,clipmin.xy,miplevel).r;
    float b = textureLod(depthTex,vec2(clipmax.x,clipmin.y),miplevel).r;
    float c = textureLod(depthTex,clipmax.xy,miplevel).r;
    float d = textureLod(depthTex,vec2(clipmin.x,clipmax.y),miplevel).r;
    depth = max(depth,max(max(max(a,b),c),d));

    isVisible =  clipmin.z <= depth;
  }
#endif
  
  visibles[gl_VertexID] = isVisible ? 1 : 0;
}
