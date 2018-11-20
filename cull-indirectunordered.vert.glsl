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

#version 440
/**/

layout(binding=0,offset=0)uniform atomic_uint   cullCounterBuffer;
layout(std430,binding=0)  writeonly buffer outputBuffer {
  int outcmds[];
};

layout(std430,binding=1)  readonly buffer inputBuffer {
  int incmds[];
};
layout(std430,binding=2)  readonly buffer visibleBuffer {
  int visibles[];
};

// default struct size for DrawElementsIndirect
#ifndef COMMANDSIZE
#define COMMANDSIZE 5
#endif

#ifndef COMMANDSTRIDE
#define COMMANDSTRIDE COMMANDSIZE
#endif

void main ()
{
  if ( (visibles[gl_VertexID/32] & (1<<(gl_VertexID%32))) != 0 ){
    uint slot = atomicCounterIncrement(cullCounterBuffer);
    
    for (uint i = 0; i < COMMANDSIZE; i++){
      outcmds[slot * COMMANDSTRIDE + i] = incmds[ gl_VertexID * COMMANDSTRIDE + i ];
    }
  }
}
