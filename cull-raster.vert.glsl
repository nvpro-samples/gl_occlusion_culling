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

layout(std430,binding=0) buffer visibleBuffer {
  int visibles[];
};

uniform samplerBuffer matricesTex;

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

uniform vec3 viewPos;

out VertexOut{
  vec3 bboxCtr;
  vec3 bboxDim;
  flat int matrixIndex;
  flat int objid;
} OUT;

void main()
{
  int objid = gl_VertexID;
  vec3 ctr =((bboxMin + bboxMax)*0.5).xyz;
  vec3 dim =((bboxMax - bboxMin)*0.5).xyz;
  OUT.bboxCtr = ctr;
  OUT.bboxDim = dim;
  OUT.matrixIndex = matrixIndex;
  OUT.objid = objid;
  
  {
    // if camera is inside the bbox then none of our
    // side faces will be visible, must treat object as 
    // visible
    int matindex = (matrixIndex * MATRICES + MATRIX_WORLD_IT)*4;
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
    }
  }
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