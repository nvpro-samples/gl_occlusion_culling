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
 
