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

uint getOffset( int id, uint scan, uint size, bool exclusive)
{
  int scanBatch = id / SCAN_BATCHSIZE;
  uint  scanOffset  = scan;
        scanOffset += scanBatch > 0 ? cullScanOffsets[ scanBatch-1] : 0;
  
  if (exclusive){
    scanOffset -= size;
  }
  return scanOffset;
}

uint getOffset( int id, bool exclusive)
{
  return getOffset(id, cullScan[id], cullSizes[id], exclusive);
}

uint rebaseOffset(uint cullOffset)
{
  // where the current sequence starts
  uint startCullOffset = getOffset(startID, true);

  // rebase from where it should start
  uint outOffset    = startOffset + (cullOffset - startCullOffset);
  
  return outOffset;
}

#define DEBUG 0

void main ()
{
  if (cmdCullSize > 0)
  {
    // cullOffset goes across "stateobject" sequences
    uint cullOffset = getOffset(gl_VertexID,cmdCullScan,cmdCullSize,true);
  
    uint outOffset  = rebaseOffset(cullOffset);
    
  #if DEBUG
    outcmds[(gl_VertexID)*2+0] = outOffset;
    outcmds[(gl_VertexID)*2+1] = cmdOffset;
  #else
    for (uint i = 0; i < cmdCullSize; i++){
      outcmds[outOffset+i] = incmds[cmdOffset+i];
    }
  #endif
  }
#if DEBUG
  else {
    outcmds[(gl_VertexID)*2+0] = ~0;
    outcmds[(gl_VertexID)*2+1] = cmdOffset;
  }
#endif

  if (gl_VertexID == startID)
  {
    // add terminator if sequence not original
    uint lastOffset = rebaseOffset( getOffset(endID, false) );
    if (lastOffset != endOffset) {
#if !DEBUG
      outcmds[lastOffset] = terminateCmd;
#endif
    }
    
#if DEBUG && 0
    outcmds[(startID)*2+0] = lastOffset;
    outcmds[(startID)*2+1] = endOffset;
#endif
  }
}
