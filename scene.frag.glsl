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

in Interpolants {
  vec3 oPos;
  vec3 wNormal;
  flat vec4 color;
} IN;

layout(location=0,index=0) out vec4 out_Color;

void main()
{
  vec3  light = normalize(vec3(-1,2,1));
  vec3  normal = IN.wNormal;
  
  #if 0
  vec3 delta = vec3(0);
  delta.x = SimplexPerlin3D(IN.oPos * 4.0);
  delta.y = -delta.x;
  delta.z = SimplexPerlin3D(IN.oPos * 7.0 + 3.0);
  normal += delta;
  #endif
  
  float intensity = dot(normalize(normal),light) * 0.5 + 0.5;
  vec4  color = IN.color * mix(vec4(0.1,0.1,0.25,0),vec4(1,1,0.8,0),intensity);
  
  color.rgb *= clamp(SimplexPerlin3D(IN.oPos * 4.0)*0.5 + 0.5, 0, 1) * 0.1 + 0.9;
  color.rgb *= clamp(SimplexPerlin3D(IN.oPos * 30.0)*0.5 + 0.5, 0, 1) * 0.3 + 0.7;
  
  out_Color = color;
}
