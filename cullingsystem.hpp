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


#ifndef CULLINGSYSTEM_H__
#define CULLINGSYSTEM_H__

#include <nv_helpers_gl/extensions_gl.hpp>

class CullingSystem {
public:
  struct Programs {
    GLuint  object_frustum;
    GLuint  object_hiz;
    GLuint  object_raster;

    GLuint  bit_temporallast;
    GLuint  bit_temporalnew;
    GLuint  bit_regular;
    GLuint  depth_mips;
  };

  enum MethodType {
    METHOD_FRUSTUM,
    METHOD_HIZ,
    METHOD_RASTER,
    NUM_METHODS,
  };

  enum BitType {
    BITS_CURRENT,
    BITS_CURRENT_AND_LAST,
    BITS_CURRENT_AND_NOT_LAST,
    NUM_BITS,
  };

  struct Buffer {
    GLuint      buffer;
    GLsizei     stride;
    GLintptr    offset;
    GLsizeiptr  size;

    Buffer(GLuint buffer)
      : buffer(buffer)
      , offset(0)
      , stride(0)
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
      , stride(0)
      , offset(0)
      , size(0)
    {

    }

    inline void BindBufferRange(GLenum target, GLuint index) const {
      glBindBufferRange(target, index, buffer, offset, size);
    }
    inline void TexBuffer(GLenum target, GLenum internalformat) const {
      glTexBufferRange(target, internalformat, buffer, offset, size);
    }
    inline void ClearBufferSubData(GLenum target,GLenum internalformat,GLenum format,GLenum type,const GLvoid* data) const {
      glClearBufferSubData(target,internalformat,offset,size,format,type,data);
    }

  };
  
  class Job {
  public:
    int     m_numObjects;
      // world-space matrices {mat4 world, mat4 worldInverseTranspose}
    Buffer  m_bufferMatrices;
    Buffer  m_bufferBboxes; // only used in dualindex mode (2 x vec4)
      // 1 32-bit integer per object (index)
    Buffer  m_bufferObjectMatrix;
      // object-space bounding box (2 x vec4)
      // or 1 32-bit integer per object (dualindex mode)
    Buffer  m_bufferObjectBbox;
    
      // 1 32-bit integer per object
    Buffer  m_bufferVisOutput;
    
      // 1 32-bit integer per 32 objects (1 bit per object)
    Buffer  m_bufferVisBitsCurrent;
    Buffer  m_bufferVisBitsLast;
    
      // for HiZ
    GLuint  m_textureDepthWithMipmaps;

    // derive from this class and implement this function how you want to
    // deal with the results that are provided in the buffer
    virtual void resultFromBits( const Buffer& bufferVisBitsCurrent ) = 0;

  };

  class JobReadback : public Job {
  public:
    // 1 32-bit integer per 32 objects (1 bit per object)
    Buffer  m_bufferVisBitsReadback;
    int*    m_hostVisBits;

    void resultFromBits( const Buffer& bufferVisBitsCurrent );
  };

  // multidrawindirect based
  class JobIndirectUnordered : public Job {
  public:
    GLuint  m_program_indirect_compact;
    // 1 indirectSize per object, 
    Buffer  m_bufferObjectIndirects;
    Buffer  m_bufferIndirectResult;
    // 1 integer
    Buffer  m_bufferIndirectCounter;

    void resultFromBits( const Buffer& bufferVisBitsCurrent );
  };
  
  struct View {
    const float*  viewProjMatrix;
    const float*  viewDir;
    const float*  viewPos;
  };
  
  void init( const Programs &programs, bool dualindex );
  void deinit();
  void update( const Programs &programs, bool dualindex );
  
  // helper function for HiZ method, leaves fbo bound to 0
  void buildDepthMipmaps(GLuint textureDepth, int width, int height);
  
  // assumes relevant fbo bound for raster method
  void buildOutput( MethodType  method, Job &job, const View& view );

  void bitsFromOutput ( Job &job, BitType type );
  void resultFromBits ( Job &job );
  // swaps the Current/Last bit array (for temporal coherent techniques)
  void swapBits       ( Job &job );

private:

  struct Uniforms {
    GLint   depth_lod;
    GLint   depth_even;
    GLint   frustum_viewProj;
    GLint   hiz_viewProj;
    GLint   raster_viewProj;
    GLint   raster_viewDir;
    GLint   raster_viewPos;
  };

  void testBboxes( Job &job, bool raster);
  
  Programs  m_programs;
  Uniforms  m_uniforms;
  GLuint    m_fbo;
  GLuint    m_tbo[2];
  bool      m_dualindex;
  bool      m_usessbo;
};

#endif
