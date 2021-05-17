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

#ifndef SCANSYSTEM_H__
#define SCANSYSTEM_H__

#include <nvgl/extensions_gl.hpp>
#include <cstddef>

class ScanSystem {
public:
  const static size_t GROUPSIZE = 512;
  const static size_t BATCH_ELEMENTS = GROUPSIZE*4;

  struct Programs {
    GLuint prefixsum;
    GLuint offsets;
    GLuint combine;
  };

  struct Buffer {
    GLuint      buffer;
    GLintptr    offset;
    GLsizeiptr  size;

    void create(size_t sizei, const void* data, GLbitfield flags)
    {
      size = sizei;
      offset = 0;
      glCreateBuffers(1,&buffer);
      glNamedBufferStorage(buffer, size, data, flags);
    }

    Buffer(GLuint buffer)
      : buffer(buffer)
      , offset(0)
    {
      glBindBuffer(GL_COPY_READ_BUFFER, buffer);
      if (sizeof(GLsizeiptr) > 4)
        glGetBufferParameteri64v(GL_COPY_READ_BUFFER,GL_BUFFER_SIZE, (GLint64*)&size);
      else
        glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, (GLint*)&size);
      glBindBuffer(GL_COPY_READ_BUFFER, 0);
    }

    Buffer()
      : buffer(0)
      , offset(0)
      , size(0)
    {

    }

    inline void BindBufferRange(GLenum target, GLuint index) const {
      glBindBufferRange(target, index, buffer, offset, size);
    }
    inline void BindBufferRange(GLenum target, GLuint index, GLintptr offseta, GLsizeiptr sizea) const {
      glBindBufferRange(target, index, buffer, offset+offseta, size+sizea);
    }

    inline void GetNamedBufferSubData(void* data){
      glGetNamedBufferSubData(buffer,offset,size,data);
    }

  };

  void init(const Programs& progs);
  void update(const Programs& progs);

  void test();

  // returns true if offsets are needed
  // the offset value needs to be added using the BATCH_ELEMENTS
  bool scanData( GLuint elements, const Buffer& input, const Buffer& output, const Buffer& offsets);
  void combineWithOffsets(GLuint elements, const Buffer& output, const Buffer& offsets);

  static size_t getOffsetSize(GLuint elements);

public:
  Programs    programs;

  GLuint      maxGrpsPrefix;
  GLuint      maxGrpsOffsets;
  GLuint      maxGrpsCombine;
 };

#endif
 
