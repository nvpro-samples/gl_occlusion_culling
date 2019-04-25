#version 430
/**/

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

/*-----------------------------------------------------------------------
  Copyright (c) 2014, NVIDIA. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse 
     or promote products derived from this software without specific
     prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/