/* Copyright (c) 2014-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
