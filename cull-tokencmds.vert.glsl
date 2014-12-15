#version 440
/**/

#define SCAN_BATCHSIZE 2048

layout(location=0) in uint  cmdOffset;
layout(location=1) in uint  cmdCullSize;
layout(location=2) in uint  cmdCullScan;

uniform uint startOffset;
uniform int  startID;
uniform uint endOffset;
uniform int  endID;
uniform uint terminateCmd;

layout(std430,binding=0)  writeonly buffer outputBuffer {
  uint outcmds[];
};

layout(std430,binding=1)  readonly buffer commandBuffer {
  uint incmds[];
};

layout(std430,binding=2)  readonly buffer cullSizesBuffer {
  uint cullSizes[];
};

layout(std430,binding=3)  readonly buffer cullScanBuffer {
  uint cullScan[];
};

layout(std430,binding=4)  readonly buffer cullScanOffsetBuffer {
  uint cullScanOffsets[];
};

uint getOffset( int id, uint scan, uint size, bool exclusive=true)
{
  int scanBatch = id / SCAN_BATCHSIZE;
  uint  scanOffset  = scan;
        scanOffset += scanBatch > 0 ? cullScanOffsets[ scanBatch-1] : 0;
  
  if (exclusive){
    scanOffset -= size;
  }
  return scanOffset;
}

uint getOffset( int id, bool exclusive=true)
{
  return getOffset(id, cullScan[id], cullSizes[id], exclusive);
}

#define DEBUG 0

void main ()
{
  if (cmdCullSize > 0)
  {
    // cullOffset goes across "stateobject" sequences
    uint cullOffset = getOffset(gl_VertexID + startID,cmdCullScan,cmdCullSize);
  
    // where the current sequence starts
    uint startCullOffset = getOffset(startID);
  
    // rebase from where it should start
    uint outOffset    = startOffset + (cullOffset - startCullOffset);
    
  #if DEBUG
    outcmds[(gl_VertexID+startID)*2+0] = outOffset;
    outcmds[(gl_VertexID+startID)*2+1] = cmdOffset;
  #else
    for (uint i = 0; i < cmdCullSize; i++){
      outcmds[outOffset+i] = incmds[cmdOffset+i];
    }
  #endif
  }
#if DEBUG
  else {
    outcmds[(gl_VertexID+startID)*2+0] = ~0;
    outcmds[(gl_VertexID+startID)*2+1] = cmdOffset;
  }
#endif

  if (gl_VertexID == 0)
  {
    // add terminator if sequence not original
    uint lastOffset = getOffset(endID, false) + getOffset(startID);
    if (lastOffset != endOffset) {
      outcmds[lastOffset] = terminateCmd;
    }
    
#if DEBUG
    outcmds[(gl_VertexID+startID)*2+0] = lastOffset;
    outcmds[(gl_VertexID+startID)*2+1] = endOffset;
#endif
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