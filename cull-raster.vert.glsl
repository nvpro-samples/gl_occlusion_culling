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