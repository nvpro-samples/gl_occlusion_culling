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

#define TEMPORAL_LAST 1
#define TEMPORAL_NEW  2

#ifndef TEMPORAL
#define TEMPORAL 0
#endif

layout(location=0) in uvec4 instream[8];

#if TEMPORAL
layout(location=9) in uint last;
#endif

layout(std430,binding=0)  writeonly buffer outputBuffer {
  uint outstream[];
};

void main ()
{
  uint bits = 0u;
  int outbit = 0;
  for (int i = 0; i < 8; i++){
    for (int n = 0; n < 4; n++, outbit++){
      uint checkbytes = instream[i][n];
      bits |= (checkbytes & 1u) << outbit;
    }
  }
  
#if TEMPORAL == TEMPORAL_LAST
  // render what was visible in last frame and passes current test
  bits &= last;
#elif TEMPORAL == TEMPORAL_NEW
  // render what was not visible in last frame (already rendered), but is now visible
  bits &= (~last);
#endif

  outstream[gl_VertexID] = bits;
}
