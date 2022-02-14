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

#ifndef FLIPWIND
#define FLIPWIND        1
#endif

//////////////////////////////////////////////

layout(binding=CULLSYS_UBO_VIEW, std140) uniform viewBuffer {
  ViewData  view;
};

layout(binding=CULLSYS_SSBO_MATRICES, std430) readonly buffer matricesBuffer {
  MatrixData matrices[];
};

/////////////////////////////////////////////

layout(points,invocations=3) in;

// one side each invocation
layout(triangle_strip,max_vertices=4) out;

layout(location=0) in VertexOut{
  vec3 bboxCtr;
  vec3 bboxDim;
  flat uint direction_matrixIndex;
  flat int  objectID;
} IN[1];

layout(location=0) out Interpolant {
  flat int f_objectID;
} OUT;

////////////////////////////////////////////

void main()
{
  if (IN[0].objectID == CULL_SKIP_ID) return;
  
  uint directionIndex = IN[0].direction_matrixIndex;
  uint matrixIndex    = IN[0].direction_matrixIndex >> 3;

  mat4 worldTM = matrices[matrixIndex].worldTM;

  vec3 faceNormal = vec3(0);
  vec3 edgeBasis0 = vec3(0);
  vec3 edgeBasis1 = vec3(0);
  
  int id = gl_InvocationID;

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
  
  float proj = ((directionIndex & (1<<id)) != 0) ? 1 : -1;  
  
#if !FLIPWIND
  proj *= -1;
#endif
  
  
  faceNormal = mat3(worldTM) * (faceNormal) * proj;
  edgeBasis0 = mat3(worldTM) * (edgeBasis0);
  edgeBasis1 = mat3(worldTM) * (edgeBasis1) * proj;
  
  vec3 worldCtr = (worldTM * vec4(IN[0].bboxCtr,1)).xyz;
  
#if FLIPWIND
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
#else
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal - edgeBasis0 + edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 - edgeBasis1),1);
  EmitVertex();
  
  OUT.f_objectID = IN[0].objectID;
  gl_Position = view.viewProjTM * vec4(worldCtr + (faceNormal + edgeBasis0 + edgeBasis1),1);
  EmitVertex();
#endif
  
}

