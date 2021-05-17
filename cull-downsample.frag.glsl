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
