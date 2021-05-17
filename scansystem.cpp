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


/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#include "scansystem.hpp"
#include <assert.h>

inline static GLuint snapdiv(GLuint input, GLuint align)
{
  return (input + align - 1) / align;
}

size_t ScanSystem::getOffsetSize(GLuint elements)
{
  GLuint groups = snapdiv(elements,BATCH_ELEMENTS);

  if (groups == 1) return 0;

  GLuint groupcombines = snapdiv(groups,BATCH_ELEMENTS);
  size_t size = groupcombines*BATCH_ELEMENTS*sizeof(GLuint);
  
  if (groupcombines > 1){
    // add another layer
    GLuint combines = snapdiv(groupcombines,BATCH_ELEMENTS);
    size += combines*BATCH_ELEMENTS*sizeof(GLuint);
  }

  return GLsizei(size);
}

bool ScanSystem::scanData( GLuint elements, const Buffer& input, const Buffer& output, const Buffer& offsets )
{
  assert( (elements % 4) == 0 );
  assert( size_t(elements) < (GLuint64)BATCH_ELEMENTS*BATCH_ELEMENTS*BATCH_ELEMENTS);
  assert( size_t(elements) * sizeof(GLuint) <= size_t(input.size) );
  assert( input.size <= output.size );

  glUseProgram(programs.prefixsum);
  glUniform1ui(0,elements);

  input.BindBufferRange(GL_SHADER_STORAGE_BUFFER,1);
  output.BindBufferRange(GL_SHADER_STORAGE_BUFFER,0);

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  GLuint groups = snapdiv(elements,BATCH_ELEMENTS);

  assert(groups <= maxGrpsPrefix);
  glDispatchCompute(groups,1,1);

  if (groups > 1){

    GLuint groupcombines = snapdiv(groups,BATCH_ELEMENTS);

    assert( groupcombines <= BATCH_ELEMENTS );
    assert( getOffsetSize(elements) <= size_t(offsets.size));
        
    glUseProgram(programs.offsets);
    glUniform1ui(0,elements);

    output.BindBufferRange(GL_SHADER_STORAGE_BUFFER,  1);
    offsets.BindBufferRange(GL_SHADER_STORAGE_BUFFER, 0);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    assert(groupcombines <= maxGrpsOffsets);
    glDispatchCompute(groupcombines,1,1);

    if (groupcombines > 1){
      glUniform1ui(0,groupcombines*BATCH_ELEMENTS);

      Buffer additionaloffsets = offsets; // derive from offsets
      GLintptr required = groupcombines*BATCH_ELEMENTS*sizeof(GLuint);;

      additionaloffsets.offset += required;
      additionaloffsets.size = offsets.size - required;

      offsets.BindBufferRange(GL_SHADER_STORAGE_BUFFER,1);
      additionaloffsets.BindBufferRange(GL_SHADER_STORAGE_BUFFER,0);

      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

      glDispatchCompute(1,1,1);

      combineWithOffsets(groupcombines*BATCH_ELEMENTS, offsets, additionaloffsets);
    }
  }

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,1);
  
  return groups > 1;
}

void ScanSystem::combineWithOffsets(GLuint elements, const Buffer& output, const Buffer& offsets )
{
  //assert((elements % 4) == 0);
  assert(size_t(elements) * sizeof(GLuint) <= size_t(output.size));

  glUseProgram(programs.combine);
  glUniform1ui(0,elements);

  offsets.BindBufferRange(GL_SHADER_STORAGE_BUFFER, 1);
  output.BindBufferRange(GL_SHADER_STORAGE_BUFFER, 0);

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  GLuint groups = snapdiv(elements,GROUPSIZE);
  assert(groups < maxGrpsCombine);
  glDispatchCompute(groups,1,1);
}

void ScanSystem::init( const Programs& progs )
{
  update(progs);
}

void ScanSystem::update( const Programs& progs )
{
  GLuint    maxGroups[3];
  glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0,(GLint*)&maxGroups[0]);

  //GLuint    groupSize[3];
  //glGetProgramiv(progs.combine,    GL_COMPUTE_WORK_GROUP_SIZE, (GLint*)groupSize);
  maxGrpsCombine = maxGroups[0];
  //glGetProgramiv(progs.offsets,    GL_COMPUTE_WORK_GROUP_SIZE, (GLint*)groupSize);
  maxGrpsOffsets = maxGroups[0];
  //glGetProgramiv(progs.prefixsum,    GL_COMPUTE_WORK_GROUP_SIZE, (GLint*)groupSize);
  maxGrpsPrefix = maxGroups[0];

  programs = progs;
}

void ScanSystem::test()
{
  GLuint scanbuffers[3];
  glCreateBuffers(3,scanbuffers);

  GLuint low  = ScanSystem::BATCH_ELEMENTS/2;
  GLuint mid  = ScanSystem::BATCH_ELEMENTS*ScanSystem::BATCH_ELEMENTS;
  GLuint high = ScanSystem::BATCH_ELEMENTS*ScanSystem::BATCH_ELEMENTS*2;
  size_t offsize = ScanSystem::getOffsetSize(high);

  GLuint* data = new GLuint[high];
  for (GLuint i = 0; i < high; i++){
    data[i] = 1;
  }

  glNamedBufferStorage(scanbuffers[0], high * sizeof(GLuint), &data[0], 0 );
  glNamedBufferStorage(scanbuffers[1], high * sizeof(GLuint),0, GL_MAP_READ_BIT );
  glNamedBufferStorage(scanbuffers[2], offsize,0,GL_MAP_READ_BIT);

  delete [] data;

  GLuint result;
  bool needcombine;

  // low
  needcombine = scanData(low, scanbuffers[0], scanbuffers[1], scanbuffers[2]);
  assert(needcombine == false);
  result = 0;
  glGetNamedBufferSubData(scanbuffers[1],sizeof(GLuint) * (low-1), sizeof(GLuint), &result);
  assert(result == low);

  // med
  needcombine = scanData(mid, scanbuffers[0], scanbuffers[1], scanbuffers[2]);
  assert(needcombine == true);
  result = 0;
  glGetNamedBufferSubData(scanbuffers[2],sizeof(GLuint) * (ScanSystem::BATCH_ELEMENTS-1), sizeof(GLuint), &result);
  assert(result == mid);

  combineWithOffsets(mid, scanbuffers[1], scanbuffers[2]);
  result = 0;
  glGetNamedBufferSubData(scanbuffers[1],sizeof(GLuint) * (mid-1), sizeof(GLuint), &result);
  assert(result == mid);

  // high
  needcombine = scanData(high, scanbuffers[0], scanbuffers[1], scanbuffers[2]);
  assert(needcombine == true);
  combineWithOffsets(high, scanbuffers[1], scanbuffers[2]);
  result = 0;
  glGetNamedBufferSubData(scanbuffers[1],sizeof(GLuint) * (high-1), sizeof(GLuint), &result);
  assert(result == high);

  glDeleteBuffers(3,scanbuffers);
}

