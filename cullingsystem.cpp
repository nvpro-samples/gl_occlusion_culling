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

#include "cullingsystem.hpp"
#include <assert.h>
#include <string.h>

#include <nvmath/nvmath_glsltypes.h>
#include "cull-common.h"

static_assert(sizeof(CullingSystem::View) == sizeof(cullsys_glsl::ViewData), "ViewData glsl/c mismatch");

#define DEBUG_VISIBLEBOXES  0

inline unsigned int minDivide(unsigned int val, unsigned int alignment)
{
  return (val+alignment-1)/alignment;
}

void CullingSystem::init( const Programs &programs, bool dualindex )
{
  update(programs,dualindex);
  glGenFramebuffers(1,&m_fbo);
  glGenBuffers(1, &m_ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(View), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void CullingSystem::update( const Programs &programs, bool dualindex )
{
  m_programs = programs;
  m_dualindex = dualindex;
  m_useRepesentativeTest = !!has_GL_NV_representative_fragment_test;
}

void CullingSystem::deinit()
{
  glDeleteFramebuffers(1,&m_fbo);
}

void CullingSystem::buildDepthMipmaps( GLuint textureDepth, int width, int height )
{
  int level = 0;
  int dim = width > height ? width : height;
  int twidth  = width;
  int theight = height;
  int wasEven = 0;

  glBindFramebuffer(GL_FRAMEBUFFER,m_fbo);
  glDepthFunc(GL_ALWAYS);
  glUseProgram(m_programs.depth_mips);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureDepth);


  while (dim){
    if (level){
      twidth  = twidth < 1 ? 1 : twidth;
      theight = theight < 1 ? 1 : theight;
      glViewport(0,0,twidth,theight);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D, textureDepth, level);
      glUniform1i(0, level-1);
      glUniform1i(1, wasEven);

      glDrawArrays(GL_TRIANGLES,0,3);
    }

    wasEven = (twidth % 2 == 0) && (theight % 2 == 0);
    
    dim       /=  2;
    twidth    /=  2;
    theight   /=  2;
    level++;
  }

  glUseProgram(0);
  glViewport(0,0,width,height);
  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDepthFunc(GL_LEQUAL);
  glViewport(0,0,width,height);
}



void CullingSystem::testBboxes( Job &job, bool raster )
{
  // send the scene's bboxes as points stream

  glBindBuffer(GL_ARRAY_BUFFER, job.m_bufferObjectBbox.buffer);
  if (m_dualindex){
    glVertexAttribIPointer(0, 1, GL_INT, job.m_bufferObjectBbox.stride, (const void*) job.m_bufferObjectBbox.offset);
    glVertexAttribDivisor(0, 0);
    glEnableVertexAttribArray(0);
  }
  else{
    GLsizei stride = job.m_bufferObjectBbox.stride ? job.m_bufferObjectBbox.stride : sizeof(float)*4*2;
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void*)job.m_bufferObjectBbox.offset);
    glVertexAttribDivisor(0, 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (const void*)(sizeof(float)*4 + job.m_bufferObjectBbox.offset));
    glVertexAttribDivisor(1, 0);
    glEnableVertexAttribArray(1);
  }
  
  glBindBuffer(GL_ARRAY_BUFFER, job.m_bufferObjectMatrix.buffer);
  glVertexAttribIPointer(2, 1, GL_INT, job.m_bufferObjectMatrix.stride, (const void*) job.m_bufferObjectMatrix.offset);
  glVertexAttribDivisor(2, 0);
  glEnableVertexAttribArray(2);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS);

  job.m_bufferMatrices.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_MATRICES);
  if (m_dualindex){
    job.m_bufferBboxes.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_BBOXES);
  }

  if (raster){
    if (m_useRepesentativeTest) {
      glEnable( GL_REPRESENTATIVE_FRAGMENT_TEST_NV );
    }
#if !DEBUG_VISIBLEBOXES
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
#endif
  }
  else {
    glEnable(GL_RASTERIZER_DISCARD);
  }

  glDrawArrays(GL_POINTS,0,job.m_numObjects);

  if (raster){
    if (m_useRepesentativeTest) {
      glDisable( GL_REPRESENTATIVE_FRAGMENT_TEST_NV );
    }
#if !DEBUG_VISIBLEBOXES
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
#endif
  }
  else {
    glDisable(GL_RASTERIZER_DISCARD);
  }

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_MATRICES, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_BBOXES, 0);
}

void CullingSystem::bitsFromOutput( Job &job, BitType type)
{
  // for GL 3.3 compatibility we use xfb
  // in GL 4.3 SSBO is used
  // 
  // using compute instead of "invisible" point drawing
  // would be better if we had really huge thread counts

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  glEnable(GL_RASTERIZER_DISCARD);

  job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_IN);
  
  if (type == BITS_CURRENT){
    glUseProgram(m_programs.bit_regular);
  }
  else{
    glUseProgram(type == BITS_CURRENT_AND_LAST ? m_programs.bit_temporallast : m_programs.bit_temporalnew);

    job.m_bufferVisBitsLast.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_LAST);
  }

  job.m_bufferVisBitsCurrent.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_OUT);
  glMemoryBarrier(GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

  glDrawArrays(GL_POINTS,0, minDivide(job.m_numObjects,32));

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_IN, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_LAST, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_OUT, 0);

  glDisable(GL_RASTERIZER_DISCARD);
}

void CullingSystem::resultFromBits( Job &job )
{
  job.resultFromBits(job.m_bufferVisBitsCurrent);
}

void CullingSystem::resultClient(Job &job)
{
  job.resultClient();
}

void CullingSystem::buildOutput( MethodType method, Job &job, const View& view )
{
  glBindBufferBase(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, m_ubo);
  glBufferSubData(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, sizeof(View), &view);

  switch(method){
  case METHOD_FRUSTUM:
    {
      glUseProgram(m_programs.object_frustum);
      
      testBboxes(job,false);
    }
    break;
  case METHOD_HIZ:
    {
      glUseProgram(m_programs.object_hiz);
      glActiveTexture(GL_TEXTURE0 + CULLSYS_TEX_DEPTH);
      glBindTexture(GL_TEXTURE_2D,job.m_textureDepthWithMipmaps);
      
      testBboxes(job,false);
      
      glActiveTexture(GL_TEXTURE0 + CULLSYS_TEX_DEPTH);
      glBindTexture(GL_TEXTURE_2D,0);
      glActiveTexture(GL_TEXTURE0);
    }
    break;
  case METHOD_RASTER:
    {
      // clear visibles
      job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS);
      glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI,GL_RED_INTEGER,GL_UNSIGNED_INT,0);

      glUseProgram(m_programs.object_raster);
      
      glEnable( GL_POLYGON_OFFSET_FILL );
      glPolygonOffset(-1,-1);
      testBboxes(job,true);
      glPolygonOffset(0,0);
      glDisable( GL_POLYGON_OFFSET_FILL );

      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    break;
  }

  glBindBufferBase(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, 0);
}


void CullingSystem::swapBits( Job &job )
{
  Buffer temp = job.m_bufferVisBitsCurrent;
  job.m_bufferVisBitsCurrent = job.m_bufferVisBitsLast;
  job.m_bufferVisBitsLast = temp;
}


void CullingSystem::JobIndirectUnordered::resultFromBits( const Buffer& bufferVisBitsCurrent )
{
  glEnable(GL_RASTERIZER_DISCARD);

  glUseProgram(m_program_indirect_compact);

  m_bufferIndirectCounter.BindBufferRange(GL_ATOMIC_COUNTER_BUFFER, CULLSYS_JOBIND_ATOM_COUNT);
  m_bufferIndirectCounter.ClearBufferSubData (GL_ATOMIC_COUNTER_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);

  bufferVisBitsCurrent.   BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_VIS);
  m_bufferObjectIndirects.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_IN);
  m_bufferIndirectResult. BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_OUT);
  if (m_clearResults)
  {
    m_bufferIndirectResult. ClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);
  }

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glDrawArrays(GL_POINTS,0,m_numObjects);

  glDisable(GL_RASTERIZER_DISCARD);
  glBindBufferBase  (GL_ATOMIC_COUNTER_BUFFER, CULLSYS_JOBIND_ATOM_COUNT, 0);
  glBindBufferBase  (GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_OUT, 0);
  glBindBufferBase  (GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_IN, 0);
  glBindBufferBase  (GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_VIS, 0);
}

void CullingSystem::JobReadback::resultFromBits( const Buffer& bufferVisBitsCurrent )
{
  GLsizeiptr size = sizeof(int) * minDivide(m_numObjects,32);
  glBindBuffer(GL_COPY_READ_BUFFER, bufferVisBitsCurrent.buffer );
  glBindBuffer(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.buffer );
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, bufferVisBitsCurrent.offset, m_bufferVisBitsReadback.offset, size);
  glBindBuffer( GL_COPY_READ_BUFFER, 0 );
  glBindBuffer( GL_COPY_WRITE_BUFFER, 0 );
}

void CullingSystem::JobReadback::resultClient()
{
  glBindBuffer(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.buffer);
  glGetBufferSubData(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.offset, m_bufferVisBitsReadback.size, m_hostVisBits);
  glBindBuffer( GL_COPY_WRITE_BUFFER, 0);
}

void CullingSystem::JobReadbackPersistent::resultFromBits(const Buffer& bufferVisBitsCurrent)
{
  GLsizeiptr size = sizeof( int ) * minDivide( m_numObjects, 32 );
  glCopyNamedBufferSubData( bufferVisBitsCurrent.buffer, m_bufferVisBitsReadback.buffer, bufferVisBitsCurrent.offset, m_bufferVisBitsReadback.offset, size);
  if (m_fence) {
    glDeleteSync( m_fence );
  }
  m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void CullingSystem::JobReadbackPersistent::resultClient()
{
  if (m_fence) {
    GLsizeiptr size = sizeof( int ) * minDivide( m_numObjects, 32 );
    // as some samples read-back within same frame (not recommended) we use the flush here, normally one wouldn’t use it
    glClientWaitSync(m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    glDeleteSync(m_fence);
    m_fence = NULL;
    memcpy( m_hostVisBits, ((uint8_t*)m_bufferVisBitsMapping) + m_bufferVisBitsReadback.offset, size );
  }
}
