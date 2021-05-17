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
/**/

#extension GL_ARB_shading_language_include : enable
#include "common.h"
#include "noise.glsl"

in layout(location=VERTEX_POS)    vec3 pos;
in layout(location=VERTEX_NORMAL) vec3 normal;
in layout(location=VERTEX_COLOR)  vec4 color;

in layout(location=VERTEX_MATRIXINDEX) int matrixIndex;

#if !GL_ARB_bindless_texture
layout(binding=TEX_MATRICES) uniform samplerBuffer texMatrices;
#endif

out Interpolants {
  vec3 oPos;
  vec3 wNormal;
  flat vec4 color;
} OUT;

mat4 getMatrix(samplerBuffer tex, int idx)
{
  return mat4(texelFetch(tex,idx*4 + 0),
              texelFetch(tex,idx*4 + 1),
              texelFetch(tex,idx*4 + 2),
              texelFetch(tex,idx*4 + 3));
}

void main()
{
  vec3 oPos = pos;
  vec3 oNormal = normal;
  
  vec4 wPos = getMatrix(texMatrices, matrixIndex*2+0) * vec4(oPos,1);
  gl_Position = scene.viewProjMatrix * wPos;
  
  OUT.oPos = pos;
  OUT.wNormal = mat3(getMatrix(texMatrices, matrixIndex*2+1)) * oNormal;
  OUT.color = color;
}
