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

layout(location=0) uniform int       depthLod;
layout(location=1) uniform bool      evenLod;
layout(binding=0)  uniform sampler2D depthTex;

in vec2 uv;

void main()
{
  ivec2 lodSize = textureSize(depthTex,depthLod);
  float depth = 0;
  
  if (evenLod){
    ivec2 offsets[] = ivec2[](
      ivec2(0,0),
      ivec2(0,1),
      ivec2(1,1),
      ivec2(1,0)
    );
    ivec2 coord = ivec2(gl_FragCoord.xy);
    coord *= 2;
    
    for (int i = 0; i < 4; i++){
      depth = max(
        depth, 
        texelFetch(depthTex,
          clamp(coord + offsets[i], ivec2(0), lodSize - ivec2(1)),
          depthLod).r );
    }
  }
  else{
    // need this to handle non-power of two
    // very conservative
    
    vec2 offsets[] = vec2[](
      vec2(-1,-1),
      vec2( 0,-1),
      vec2( 1,-1),
      vec2(-1, 0),
      vec2( 0, 0),
      vec2( 1, 0),
      vec2(-1, 1),
      vec2( 0, 1),
      vec2( 1, 1)
    );
    vec2 coord = uv;
    vec2 texel = 1.0/(vec2(lodSize));
    
    for (int i = 0; i < 9; i++){
      vec2 pos = coord + offsets[i] * texel;
      depth = max(
        depth, 
        #if 1
        texelFetch(depthTex,
          clamp(ivec2(pos * lodSize), ivec2(0), lodSize - ivec2(1)),
          depthLod).r 
        #else
        textureLod(depthTex,
          pos,
          depthLod).r 
        #endif
        );
    }
  }

  gl_FragDepth = depth;
}
