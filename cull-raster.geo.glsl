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

#ifndef FLIPWIND
#define FLIPWIND        1
#endif

#ifndef PERSPECTIVE
#define PERSPECTIVE     1
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

/////////////////////////////////////////////

#if PERSPECTIVE
  // not so trivial to find the 3 visible sides, let hw do the culling
  layout(points,invocations=6) in;  
#else
  // render the 3 visible sides based on view direction and box normal
  layout(points,invocations=3) in;  
#endif
// one side each invocation
layout(triangle_strip,max_vertices=4) out;

in VertexOut{
  vec3 bboxCtr;
  vec3 bboxDim;
  flat int matrixIndex;
  flat int objid;
} IN[1];

flat out int objid;

////////////////////////////////////////////

void main()
{
  if (IN[0].objid == CULL_SKIP_ID) return;

  int  matindex = (IN[0].matrixIndex*MATRICES + MATRIX_WORLD)*4;
  mat4 worldTM = mat4(
    texelFetch(matricesTex,matindex + 0),
    texelFetch(matricesTex,matindex + 1),
    texelFetch(matricesTex,matindex + 2),
    texelFetch(matricesTex,matindex + 3));

  vec3 faceNormal = vec3(0);
  vec3 edgeBasis0 = vec3(0);
  vec3 edgeBasis1 = vec3(0);
  
#if PERSPECTIVE
  int id = gl_InvocationID % 3;
#else
  int id = gl_InvocationID;
#endif

  if (id == 0)
  {
      faceNormal.x = IN[0].bboxDim.x;
      edgeBasis0.y = IN[0].bboxDim.y;
      edgeBasis1.z = IN[0].bboxDim.z;
  }
  else if(id == 1)
  {
      faceNormal.y = IN[0].bboxDim.y;
      edgeBasis1.x = IN[0].bboxDim.x;
      edgeBasis0.z = IN[0].bboxDim.z;
  }
  else if(id == 2)
  {
      faceNormal.z = IN[0].bboxDim.z;
      edgeBasis0.x = IN[0].bboxDim.x;
      edgeBasis1.y = IN[0].bboxDim.y;
  }
  
#if PERSPECTIVE
  float proj = gl_InvocationID < 3 ? 1 : -1;
#else
  vec3 worldNormal = mat3(worldTM) * faceNormal;
  float proj = sign(dot(viewDir,worldNormal));
#endif
  
#if FLIPWIND
  proj *= -1;
#endif
  
  
  faceNormal = mat3(worldTM) * (faceNormal) * proj;
  edgeBasis0 = mat3(worldTM) * (edgeBasis0);
  edgeBasis1 = mat3(worldTM) * (edgeBasis1) * proj;
  
  vec3 worldCtr = (worldTM * vec4(IN[0].bboxCtr,1)).xyz;
  
#if FLIPWIND
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
#else
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  objid = IN[0].objid;
  gl_Position = viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 + edgeBasis1),1);
  EmitVertex();
#endif
  
}

