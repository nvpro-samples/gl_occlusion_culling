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

#define TASK_SUM      0
#define TASK_OFFSETS  1
#define TASK_COMBINE  2

#ifndef TASK
#define TASK TASK_SUM
#endif

#define THREADBLOCK_SIZE  512
#define BATCH_SIZE        (THREADBLOCK_SIZE*4)


uniform uint numElements;

///////////////////////////////////////////////////////
// based on CUDA Sample "scan.cu" 

layout (local_size_x = THREADBLOCK_SIZE) in;

#if TASK != TASK_COMBINE

uint threadIdx = gl_LocalInvocationID.x;

#extension GL_NV_shader_thread_group : enable
#extension GL_NV_shader_thread_shuffle : enable

#if GL_NV_shader_thread_group

#define USESHUFFLE
#define LOG2_WARP_SIZE 5U
#define      WARP_SIZE (1U << LOG2_WARP_SIZE)

// Almost the same as naive scan1Inclusive but doesn't need barriers
// nor shared memory
// and works only for size <= WARP_SIZE

#if GL_NV_shader_thread_shuffle

shared uint s_Data[(THREADBLOCK_SIZE / WARP_SIZE)];

uint warpScanInclusive(uint idata, uint size){
  uint sum = idata;
  
  for (int STEP = 0; STEP < 5 && (1<<(STEP+1)) <= size; STEP++){
    bool valid = false;
    uint temp = shuffleUpNV(sum, 1 << STEP, 32, valid);
    if (valid) {
      sum += temp;
    }
  }

  return sum;
}

#else

shared uint s_Data[THREADBLOCK_SIZE * 2];

// Almost the same as naive scan1Inclusive but doesn't need barriers
// and works only for size <= WARP_SIZE

uint warpScanInclusive(uint idata, uint size){
  uint pos = 2 * threadIdx.x - (threadIdx.x & (size - 1));
  s_Data[pos] = 0;
  pos += size;
  s_Data[pos] = idata;

  if(size >=  2) s_Data[pos] += s_Data[pos -  1];
  if(size >=  4) s_Data[pos] += s_Data[pos -  2];
  if(size >=  8) s_Data[pos] += s_Data[pos -  4];
  if(size >= 16) s_Data[pos] += s_Data[pos -  8];
  if(size >= 32) s_Data[pos] += s_Data[pos - 16];

  return s_Data[pos];
}

#endif

uint warpScanExclusive(uint idata, uint size){
    return warpScanInclusive(idata, size) - idata;
}

uint scan1Inclusive(uint idata, uint size){
  if(size > WARP_SIZE){
    //Bottom-level inclusive warp scan
    uint warpResult = warpScanInclusive(idata, WARP_SIZE);

    //Save top elements of each warp for exclusive warp scan
  #if !GL_NV_shader_thread_shuffle
    //sync to wait for warp scans to complete (because l_Data is being overwritten)
    memoryBarrierShared();
    barrier();
  #endif
    if( (threadIdx & (WARP_SIZE - 1)) == (WARP_SIZE - 1) )
        s_Data[threadIdx >> LOG2_WARP_SIZE] = warpResult;

    //wait for warp scans to complete
    memoryBarrierShared();
    barrier();
    if( threadIdx < (THREADBLOCK_SIZE / WARP_SIZE) ){
        //grab top warp elements
        uint val = s_Data[threadIdx];
        //calculate exclsive scan and write back to shared memory
        s_Data[threadIdx] = warpScanExclusive(val, size >> LOG2_WARP_SIZE);
    }

    //return updated warp scans with exclusive scan results
    memoryBarrierShared();
    barrier();
    return warpResult + s_Data[threadIdx >> LOG2_WARP_SIZE];
  }else{
    return warpScanInclusive(idata, size);
  }
}

#else

shared uint s_Data[THREADBLOCK_SIZE * 2];

uint scan1Inclusive(uint idata, uint size)
{
    uint pos = 2 * threadIdx.x - (threadIdx.x & (size - 1));
    s_Data[pos] = 0;
    pos += size;
    s_Data[pos] = idata;

    for (uint offset = 1; offset < size; offset <<= 1)
    {
        memoryBarrierShared();
        barrier();
        uint t = s_Data[pos] + s_Data[pos - offset];
        memoryBarrierShared();
        barrier();
        s_Data[pos] = t;
    }

    return s_Data[pos];
}

#endif

uint scan1Exclusive(uint idata, uint size)
{
    return scan1Inclusive(idata, size) - idata;
}

uvec4 scan4Inclusive(uvec4 idata4, uint size)
{
    //Level-0 inclusive scan
    idata4.y += idata4.x;
    idata4.z += idata4.y;
    idata4.w += idata4.z;

    //Level-1 exclusive scan
    uint oval = scan1Exclusive(idata4.w, size / 4);

    idata4.x += oval;
    idata4.y += oval;
    idata4.z += oval;
    idata4.w += oval;

    return idata4;
}

//Exclusive vector scan: the array to be scanned is stored
//in local thread memory scope as uint4
uvec4 scan4Exclusive(uvec4 idata4, uint size)
{
    uvec4 odata4 = scan4Inclusive(idata4, size);
    odata4.x -= idata4.x;
    odata4.y -= idata4.y;
    odata4.z -= idata4.z;
    odata4.w -= idata4.w;
    return odata4;
}

#endif


#if TASK == TASK_SUM

layout (std430, binding=1) buffer inputBuffer {
  uvec4 indata[];
};

layout (std430, binding=0) buffer outputBuffer {
  uvec4 outdata[];
};

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  uint maxidx = ((numElements + 3) / 4);
  
  bool valid = idx < maxidx;

  //Load data
  uvec4 idata4 = valid ? indata[idx] : uvec4(0);

  // Calculate scan
  //uvec4 odata4 = scan4Inclusive(idata4, min(BATCH_SIZE,  (maxidx-idx)*4));
  uvec4 odata4 = scan4Inclusive(idata4, BATCH_SIZE);

  //Write back
  if (valid) outdata[idx] = odata4;
}
#endif

#if TASK == TASK_OFFSETS

layout (std430, binding=1) buffer inputBuffer {
  uint indata[];
};

layout (std430, binding=0) buffer outputBuffer {
  uvec4 outdata[];
};

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  uint startIdx = (idx * BATCH_SIZE * 4);
  
  bool valid = false;
  
  //Load data
  uvec4 idata4 = uvec4(0);
  for (uint i = 0; i < 4; i++){
    uint readIdx = startIdx + (i+1)*BATCH_SIZE - 1u;
    if ( readIdx < numElements ){
      idata4[i] = indata[readIdx];
      valid = true;
    }
  }

  //Calculate scan
  uvec4 odata4 = scan4Inclusive(idata4, BATCH_SIZE);

  //Write back
  if (valid) outdata[idx] = odata4;
}
#endif

#if TASK == TASK_COMBINE

layout (std430, binding=1) buffer inputBuffer {
  uint indata[];
};

layout (std430, binding=0) buffer outputBuffer {
  uint outdata[];
};

void main()
{
  uint idx = gl_GlobalInvocationID.x;
  
  bool valid = idx < numElements;
  uint batch = idx / BATCH_SIZE;
  
  if (valid && batch > 0) {
    outdata[idx] += indata[batch-1];
  }
}
#endif
