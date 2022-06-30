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

layout(local_size_x=32) in;
layout(triangles,max_vertices= CULLSYS_MESH_BATCH * CULLSYS_INSTANCED_VERTICES, max_primitives= CULLSYS_MESH_BATCH * CULLSYS_INSTANCED_TRIANGLES) out;

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

taskNV in Task {
  uint      baseID;
  uint      validCount;
  uint8_t   direction_subID[CULLSYS_TASK_BATCH]; // 3 + 5 bits
} IN;

layout(location=0) out Interpolant {
  flat int f_objectID;
} OUT[];

///////////////////////////////////////////////

void main()
{
  const uint lanesPerBox = 32 / CULLSYS_MESH_BATCH;

  uint laneID    = gl_LocalInvocationID.x;
  uint subLaneID = gl_LocalInvocationID.x & (lanesPerBox-1);
  uint boxID     = laneID / lanesPerBox;
  uint boxTaskID = boxID + gl_WorkGroupID.x * CULLSYS_MESH_BATCH;
  
  uint directionIndex = uint(IN.direction_subID[boxTaskID]);
  uint subID          = uint(IN.direction_subID[boxTaskID]) >> 3;
  uint objectID       = IN.baseID + subID;
  
  // we process CULLSYS_MESH_BATCH boxes in this warp,
  // however the parent task shader might not have that many valid
  // outputs. So disable those threads that overshot.
  bool isValid          = boxTaskID < IN.validCount;
  // only test first subLaneID of each box, so that subgroupBallotBitCount
  // returns the actual number of boxes
  uvec4 validVote       = subgroupBallot(subLaneID == 0 && isValid);
  if (laneID == 0)
  {
    gl_PrimitiveCountNV = subgroupBallotBitCount(validVote) * CULLSYS_INSTANCED_TRIANGLES;
  }
  
  uint objectRead      = isValid ? objectID : IN.baseID;
  int  matrixIndex     = matrixIndices[objectRead];
  
  mat4 worldTM         = matrices[matrixIndex].worldTM;
  mat4 worldViewProjTM = view.viewProjTM * worldTM;
  
#ifdef DUALINDEX
  int  bboxIndex   = bboxIndices[objectRead];
#else
  int  bboxIndex   = int(objectRead);
#endif

  vec4 bboxMin     = bboxes[bboxIndex].bboxMin;
  vec4 bboxMax     = bboxes[bboxIndex].bboxMax;
  
  // 4 threads per box (32/8)
  // needs two iterations for all 8 vertices
  for (uint i = 0; i < 2; i++)
  {
    uint vert    = subLaneID + i * lanesPerBox;
    uint vertOut = vert + boxID * CULLSYS_INSTANCED_VERTICES;
    
    uvec4 vertexMapLower  = uvec4(0x2134657u, 0x10325476u, 0x23016745u, 0x31207564u);
    uvec4 vertexMapUppper = uvec4(0x45670123u, 0x57461302u, 0x64752031u, 0x76543210u);
    
    uvec4 vertexMap = ((directionIndex & 4) == 0) ? vertexMapLower : vertexMapUppper;
    
    uint localMap = vertexMap [directionIndex & 3];
    uint localIdx = (localMap >> (vert * 4)) & 7;
  
    gl_MeshVerticesNV[vertOut].gl_Position = worldViewProjTM * getBoxCorner(bboxMin, bboxMax, int(localIdx));
    OUT[vertOut].f_objectID = int(objectID);
  }
  
  // 6 triangles per box
  for (uint i = 0; i < 2; i++)
  {
    uint tri    = subLaneID + i * lanesPerBox;
    uint triOut = tri + boxID * CULLSYS_INSTANCED_TRIANGLES;
    
    if (isValid && tri < CULLSYS_INSTANCED_TRIANGLES) {
      // encoded indexbuffer (see `cull-raster-instanced.lua`)
      uvec3 indexBits = uvec3(0x1370732u, 0x4570751u, 0x2670764u);
      
      uint triBits     = indexBits[tri / 2] >> ((tri & 1) * 16);      
      uvec3 triIndices = uvec3((triBits >> 0) & 7,
                               (triBits >> 4) & 7,
                               (triBits >> 8) & 7);
      
      triIndices += boxID * CULLSYS_INSTANCED_VERTICES;
      
      gl_PrimitiveIndicesNV[triOut * 3 + 0] = triIndices.x;
      gl_PrimitiveIndicesNV[triOut * 3 + 1] = triIndices.y;
      gl_PrimitiveIndicesNV[triOut * 3 + 2] = triIndices.z;
    }
  }
}
