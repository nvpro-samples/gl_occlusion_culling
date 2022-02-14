/*
 * Copyright (c) 2014-2022, NVIDIA CORPORATION.  All rights reserved.
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

inline unsigned int minDivide(unsigned int val, unsigned int alignment)
{
  return (val + alignment - 1) / alignment;
}

void CullingSystem::init(const Programs& programs, bool useDualIndex, RasterType rasterType, bool hasRepresentativeTest)
{
  update(programs, useDualIndex, rasterType, hasRepresentativeTest);
  glGenFramebuffers(1, &m_fbo);
  glGenBuffers(1, &m_ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(View), nullptr, GL_DYNAMIC_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);

  // The instanced renderer uses pre-computed uint16_t index buffer for bboxes
  // uint16_t indices provide extra performance on current NVIDIA hardware.
  // We use one canonical index buffer for the local view direction vector (+1,+1,+1).
  // The vertex-shader has code to change the appropriate local coordinates depending
  // on the actual view direction.

  size_t    numIndices = CULLSYS_INSTANCED_BBOXES * CULLSYS_INSTANCED_INDICES;
  uint16_t* indices    = (uint16_t*)malloc(sizeof(uint16_t) * numIndices);

  /* 
    4_____5
   /.    /|
  6_____7 |
  | 0...|.1
  |.    |/
  2_____3
  
  */
  uint32_t refIndices[] = {2, 3, 7, 7, 3, 1, 1, 5, 7, 7, 5, 4, 4, 6, 7, 7, 6, 2};

  for(uint32_t b = 0; b < CULLSYS_INSTANCED_BBOXES; b++)
  {
    for(uint32_t i = 0; i < CULLSYS_INSTANCED_INDICES; i++)
    {
      indices[b * CULLSYS_INSTANCED_INDICES + i] = (uint16_t)(refIndices[i] + b * CULLSYS_INSTANCED_VERTICES);
    }
  }

  glGenBuffers(1, &m_iboInstanced);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboInstanced);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * numIndices, indices, GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  free(indices);
}

void CullingSystem::update(const Programs& programs, bool useDualIndex, RasterType rasterType, bool hasRepresentativeTest)
{
  m_programs             = programs;
  m_useDualIndex         = useDualIndex;
  m_useRepesentativeTest = hasRepresentativeTest;
  m_rasterType           = rasterType;
}

void CullingSystem::deinit()
{
  glDeleteFramebuffers(1, &m_fbo);
}

void CullingSystem::buildDepthMipmaps(GLuint textureDepth, int width, int height)
{
  // this is a simple implementation
  // today's hardware should use compute and shuffle instructions to create
  // multiple mipmaps at a time.

  int level   = 0;
  int dim     = width > height ? width : height;
  int twidth  = width;
  int theight = height;
  int wasEven = 0;

  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
  glDepthFunc(GL_ALWAYS);
  glUseProgram(m_programs.depth_mips);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, textureDepth);


  while(dim)
  {
    if(level)
    {
      twidth  = twidth < 1 ? 1 : twidth;
      theight = theight < 1 ? 1 : theight;
      glViewport(0, 0, twidth, theight);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, textureDepth, level);
      glUniform1i(0, level - 1);
      glUniform1i(1, wasEven);

      glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    wasEven = (twidth % 2 == 0) && (theight % 2 == 0);

    dim /= 2;
    twidth /= 2;
    theight /= 2;
    level++;
  }

  glUseProgram(0);
  glViewport(0, 0, width, height);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDepthFunc(GL_LEQUAL);
  glViewport(0, 0, width, height);
}


void CullingSystem::testBboxes(Job& job, bool raster)
{
  // send the scene's bboxes as points stream
  job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS);
  job.m_bufferMatrices.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_MATRICES);
  if(m_useDualIndex)
  {
    job.m_bufferBboxes.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_BBOXES);
  }

  job.m_bufferObjectBbox.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_INPUT_BBOX);
  job.m_bufferObjectMatrix.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_INPUT_MATRIX);

  if(raster && m_rasterType == RASTER_INSTANCED)
  {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboInstanced);
  }

  if(raster)
  {
#if !CULLSYS_DEBUG_VISIBLEBOXES
    if(m_useRepesentativeTest)
    {
      glEnable(GL_REPRESENTATIVE_FRAGMENT_TEST_NV);
    }
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
#endif
    glDisable(GL_CULL_FACE);
  }
  else
  {
    glEnable(GL_RASTERIZER_DISCARD);
  }

  if(raster && m_rasterType == RASTER_INSTANCED)
  {
    int instanceCount = job.m_numObjects / CULLSYS_INSTANCED_BBOXES;
    int tailCount     = job.m_numObjects % CULLSYS_INSTANCED_BBOXES;
    glUniform1i(0, 0);

    // instance many
    glDrawElementsInstanced(GL_TRIANGLES, CULLSYS_INSTANCED_BBOXES * CULLSYS_INSTANCED_INDICES, GL_UNSIGNED_SHORT,
                            nullptr, instanceCount);
    // non-instanced tail
    glUniform1i(0, instanceCount * CULLSYS_INSTANCED_BBOXES);
    glDrawElements(GL_TRIANGLES, tailCount * CULLSYS_INSTANCED_INDICES, GL_UNSIGNED_SHORT, nullptr);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }
  else if (raster && m_rasterType == RASTER_MESH_SHADER)
  {
    glUniform1ui(0, job.m_numObjects - 1);
    glDrawMeshTasksNV(0, (job.m_numObjects + CULLSYS_TASK_BATCH - 1) / CULLSYS_TASK_BATCH);
  }
  else
  {
    glDrawArrays(GL_POINTS, 0, job.m_numObjects);
  }

  if(raster)
  {
    glEnable(GL_CULL_FACE);
#if !CULLSYS_DEBUG_VISIBLEBOXES
    if(m_useRepesentativeTest)
    {
      glDisable(GL_REPRESENTATIVE_FRAGMENT_TEST_NV);
    }
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#endif
  }
  else
  {
    glDisable(GL_RASTERIZER_DISCARD);
  }

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_MATRICES, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_BBOXES, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_INPUT_MATRIX, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_INPUT_BBOX, 0);
}

void CullingSystem::bitsFromOutput(Job& job, BitType type)
{
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_IN);

  if(type == BITS_CURRENT)
  {
    glUseProgram(m_programs.bit_regular);
  }
  else
  {
    glUseProgram(type == BITS_CURRENT_AND_LAST ? m_programs.bit_temporallast : m_programs.bit_temporalnew);

    job.m_bufferVisBitsLast.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_LAST);
  }

  job.m_bufferVisBitsCurrent.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_OUT);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

  glUniform1ui(0, job.m_numObjects);
  glDispatchCompute(minDivide(minDivide(job.m_numObjects, 32), CULLSYS_COMPUTE_THREADS), 1, 1);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_IN, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_LAST, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_BIT_SSBO_OUT, 0);
}

void CullingSystem::resultFromBits(Job& job)
{
  job.resultFromBits(job.m_bufferVisBitsCurrent);
}

void CullingSystem::resultClient(Job& job)
{
  job.resultClient();
}

void CullingSystem::buildOutput(MethodType method, Job& job, const View& view)
{
  glBindBufferBase(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, m_ubo);
  glBufferSubData(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, sizeof(View), &view);

  switch(method)
  {
    case METHOD_FRUSTUM: {
      glUseProgram(m_programs.object_frustum);

      testBboxes(job, false);
    }
    break;
    case METHOD_HIZ: {
      glUseProgram(m_programs.object_hiz);
      glActiveTexture(GL_TEXTURE0 + CULLSYS_TEX_DEPTH);
      glBindTexture(GL_TEXTURE_2D, job.m_textureDepthWithMipmaps);

      testBboxes(job, false);

      glActiveTexture(GL_TEXTURE0 + CULLSYS_TEX_DEPTH);
      glBindTexture(GL_TEXTURE_2D, 0);
      glActiveTexture(GL_TEXTURE0);
    }
    break;
    case METHOD_RASTER: {
      // clear visibles
      job.m_bufferVisOutput.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_SSBO_OUT_VIS);
      glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);


      switch(m_rasterType){
      case RASTER_INSTANCED:
        glUseProgram(m_programs.object_raster_instanced);
        break;
      case RASTER_GEOMETRY_SHADER:
        glUseProgram(m_programs.object_raster_geo);
        break;
      case RASTER_MESH_SHADER:
        glUseProgram(m_programs.object_raster_mesh);
        break;
      }

      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(-1, -1);
      testBboxes(job, true);
      glPolygonOffset(0, 0);
      glDisable(GL_POLYGON_OFFSET_FILL);

      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }
    break;
  }

  glBindBufferBase(GL_UNIFORM_BUFFER, CULLSYS_UBO_VIEW, 0);
}


void CullingSystem::swapBits(Job& job)
{
  Buffer temp                = job.m_bufferVisBitsCurrent;
  job.m_bufferVisBitsCurrent = job.m_bufferVisBitsLast;
  job.m_bufferVisBitsLast    = temp;
}


void CullingSystem::setRasterType(RasterType rasterType)
{
  m_rasterType = rasterType;
}

void CullingSystem::JobIndirectUnordered::resultFromBits(const Buffer& bufferVisBitsCurrent)
{
  glUseProgram(m_program_indirect_compact);

  m_bufferIndirectCounter.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_COUNT);
  m_bufferIndirectCounter.ClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);

  bufferVisBitsCurrent.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_VIS);
  m_bufferObjectIndirects.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_IN);
  m_bufferIndirectResult.BindBufferRange(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_OUT);
  if(m_clearResults)
  {
    m_bufferIndirectResult.ClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 0);
  }

  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
  glUniform1ui(0, m_numObjects);
  glDispatchCompute(minDivide(minDivide(m_numObjects, CULLSYS_JOBIND_BATCH), CULLSYS_COMPUTE_THREADS), 1, 1);

  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_COUNT, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_OUT, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_IN, 0);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, CULLSYS_JOBIND_SSBO_VIS, 0);
}

void CullingSystem::JobReadback::resultFromBits(const Buffer& bufferVisBitsCurrent)
{
  GLsizeiptr size = sizeof(int) * minDivide(m_numObjects, 32);
  glBindBuffer(GL_COPY_READ_BUFFER, bufferVisBitsCurrent.buffer);
  glBindBuffer(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.buffer);
  glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, bufferVisBitsCurrent.offset, m_bufferVisBitsReadback.offset, size);
  glBindBuffer(GL_COPY_READ_BUFFER, 0);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void CullingSystem::JobReadback::resultClient()
{
  glBindBuffer(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.buffer);
  glGetBufferSubData(GL_COPY_WRITE_BUFFER, m_bufferVisBitsReadback.offset, m_bufferVisBitsReadback.size, m_hostVisBits);
  glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

void CullingSystem::JobReadbackPersistent::resultFromBits(const Buffer& bufferVisBitsCurrent)
{
  GLsizeiptr size = sizeof(int) * minDivide(m_numObjects, 32);
  glCopyNamedBufferSubData(bufferVisBitsCurrent.buffer, m_bufferVisBitsReadback.buffer, bufferVisBitsCurrent.offset,
                           m_bufferVisBitsReadback.offset, size);
  if(m_fence)
  {
    glDeleteSync(m_fence);
  }
  m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

void CullingSystem::JobReadbackPersistent::resultClient()
{
  if(m_fence)
  {
    GLsizeiptr size = sizeof(int) * minDivide(m_numObjects, 32);
    // as some samples read-back within same frame (not recommended) we use the flush here, normally one wouldn’t use it
    glClientWaitSync(m_fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
    glDeleteSync(m_fence);
    m_fence = NULL;
    memcpy(m_hostVisBits, ((uint8_t*)m_bufferVisBitsMapping) + m_bufferVisBitsReadback.offset, size);
  }
}
