/* Copyright (c) 2014-2019, NVIDIA CORPORATION. All rights reserved.
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

#include <stdint.h>
#include <nvgl/extensions_gl.hpp>


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

    Buffer(GLuint buffer, size_t sizei = 0)
      : buffer(buffer)
      , offset(0)
      , stride(0)
    {
      if (!sizei) {
        glBindBuffer( GL_COPY_READ_BUFFER, buffer );
        if (sizeof( GLsizeiptr ) > 4)
          glGetBufferParameteri64v( GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, (GLint64*)&size );
        else
          glGetBufferParameteriv( GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, (GLint*)&size );
        glBindBuffer( GL_COPY_READ_BUFFER, 0 );
      }
      else {
        size = sizei;
      }
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
    // for readback methods we need to wait for a result
    virtual void resultClient() {};

  };

  class JobReadback : public Job {
  public:
    // 1 32-bit integer per 32 objects (1 bit per object)
    Buffer      m_bufferVisBitsReadback;
    uint32_t*   m_hostVisBits;

    // Do not use this Job class unless you have to. Persistent 
    // mapped buffers are preferred.

    // Copies result into readback buffer
    void resultFromBits( const Buffer& bufferVisBitsCurrent );

    // getBufferData into hostVisBits (blocking!)
    void resultClient();
  };

  class JobReadbackPersistent : public Job {
  public:
    // 1 32-bit integer per 32 objects (1 bit per object)
    Buffer      m_bufferVisBitsReadback;
    void*       m_bufferVisBitsMapping;
    uint32_t*   m_hostVisBits;
    GLsync      m_fence;

    // Copies result into readback buffer and records
    // a fence.
    void resultFromBits(const Buffer& bufferVisBitsCurrent);

    // waits on fence and copies mapping into hostVisBits
    void resultClient();
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
    // std140 padding
    float  viewProjMatrix[16];

    float  viewDir[3];
    float  _pad0;

    float  viewPos[3];
    float  _pad1;

    float  viewWidth;
    float  viewHeight;
    float  viewCullThreshold;
    float  _pad2;
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
  void resultClient   ( Job &job );

  // swaps the Current/Last bit array (for temporal coherent techniques)
  void swapBits       ( Job &job );

private:

  void testBboxes( Job &job, bool raster);
  
  Programs  m_programs;

  GLuint    m_ubo;
  GLuint    m_fbo;
  GLuint    m_tbo[2];
  bool      m_dualindex;
  bool      m_useRepesentativeTest;
};

#endif
