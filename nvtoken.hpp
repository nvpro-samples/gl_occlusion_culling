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


#include <assert.h>
#include <string>
#include <vector>

#define NVTOKEN_STATESYSTEM 0
#include <include_gl.h>
#include "platform.h"
#if NVTOKEN_STATESYSTEM
// not needed if emulation is not used, or implemented differently
#include "statesystem.hpp"
#else
namespace StateSystem {
  // Minimal emulation layer
  enum Faces {
    FACE_FRONT,
    FACE_BACK,
    MAX_FACES,
  };
  struct State {
    struct {
      struct {
        GLsizei stride;
      }bindings[16];
    }vertexformat;

    struct {
      GLenum mode;
    }alpha;

    struct {
      struct {
        GLenum func;
        GLuint mask;
      }funcs[MAX_FACES];
    }stencil;
  };
}
#endif


namespace nvtoken
{

  //////////////////////////////////////////////////////////////////////////
  // generic

  // not the cleanest way
  #define NVTOKEN_TYPES (GL_FRONT_FACE_COMMAND_NV+1)

  enum NVTokenShaderStage {
    NVTOKEN_STAGE_VERTEX,
    NVTOKEN_STAGE_TESS_CONTROL,
    NVTOKEN_STAGE_TESS_EVALUATION,
    NVTOKEN_STAGE_GEOMETRY,
    NVTOKEN_STAGE_FRAGMENT,
    NVTOKEN_STAGES,
  };

  extern bool     s_nvcmdlist_bindless;
  extern GLuint   s_nvcmdlist_header[NVTOKEN_TYPES];
  extern GLuint   s_nvcmdlist_headerSizes[NVTOKEN_TYPES];
  extern GLushort s_nvcmdlist_stages[NVTOKEN_STAGES];
  
  class NVPointerStream {
  public:
    size_t          m_max;
    unsigned char*  m_begin;
    unsigned char*  m_end;
    unsigned char* NV_RESTRICT m_cur;

    void init(void* data, size_t size)
    {
      m_begin = (unsigned char*)data;
      m_end   = m_begin + size;
      m_cur   = m_begin;
      m_max   = size;
    }

    size_t size() const
    {
      return m_cur - m_begin;
    }

    size_t  capacity() const
    {
      return m_max;
    }
  };

  struct NVTokenSequence {
    std::vector<GLintptr>  offsets;
    std::vector<GLsizei>   sizes;
    std::vector<GLuint>    states;
    std::vector<GLuint>    fbos;
  };

#pragma pack(push,1)

  typedef struct {
    GLuint   header;
    GLuint   buffer;
    GLuint   _pad;
    GLuint   typeSizeInByte;
  } ElementAddressCommandEMU;

  typedef struct {
    GLuint   header;
    GLuint   index;
    GLuint   buffer;
    GLuint   offset;
  } AttributeAddressCommandEMU;

  typedef struct {
    GLuint      header;
    GLushort    index;
    GLushort    stage;
    GLuint      buffer;
    GLushort    offset256;
    GLushort    size4;
  } UniformAddressCommandEMU;


  struct NVTokenNop {
    static const GLenum   ID = GL_NOP_COMMAND_NV;

    NOPCommandNV      cmd;

    NVTokenNop() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenTerminate {
    static const GLenum   ID = GL_TERMINATE_SEQUENCE_COMMAND_NV;

    TerminateSequenceCommandNV      cmd;

    NVTokenTerminate() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenDrawElemsInstanced {
    static const GLenum   ID = GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV;

    DrawElementsInstancedCommandNV   cmd;

    NVTokenDrawElemsInstanced() {
      cmd.mode = GL_TRIANGLES;
      cmd.baseInstance = 0;
      cmd.baseVertex = 0;
      cmd.firstIndex = 0;
      cmd.count = 0;
      cmd.instanceCount = 1;

      cmd.header  = s_nvcmdlist_header[ID];
    }
    
    void setMode(GLenum primmode) {
      cmd.mode = primmode;
    }

    void setParams(GLuint count, GLuint firstIndex=0, GLuint baseVertex=0)
    {
      cmd.count = count;
      cmd.firstIndex = firstIndex;
      cmd.baseVertex = baseVertex;
    }

    void setInstances(GLuint count, GLuint baseInstance=0){
      cmd.baseInstance  = baseInstance;
      cmd.instanceCount = count;
    }
  };

  struct NVTokenDrawArraysInstanced {
    static const GLenum   ID = GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV;

    DrawArraysInstancedCommandNV          cmd;

    NVTokenDrawArraysInstanced() {
      cmd.mode = GL_TRIANGLES;
      cmd.baseInstance = 0;
      cmd.first = 0;
      cmd.count = 0;
      cmd.instanceCount = 1;

      cmd.header  = s_nvcmdlist_header[ID];
    }
    
    void setMode(GLenum primmode) {
      cmd.mode = primmode;
    }

    void setParams(GLuint count, GLuint first=0)
    {
      cmd.count = count;
      cmd.first = first;
    }

    void setInstances(GLuint count, GLuint baseInstance=0){
      cmd.baseInstance  = baseInstance;
      cmd.instanceCount = count;
    }
  };

  struct NVTokenDrawElems {
    static const GLenum   ID = GL_DRAW_ELEMENTS_COMMAND_NV;

    DrawElementsCommandNV   cmd;

    NVTokenDrawElems() {
      cmd.baseVertex = 0;
      cmd.firstIndex = 0;
      cmd.count = 0;

      cmd.header  = s_nvcmdlist_header[ID];
    }

    void setParams(GLuint count, GLuint firstIndex=0, GLuint baseVertex=0)
    {
      cmd.count = count;
      cmd.firstIndex = firstIndex;
      cmd.baseVertex = baseVertex;
    }
    
    void setMode(GLenum primmode) {
      assert(primmode != GL_TRIANGLE_FAN && /* primmode != GL_POLYGON  && */ primmode != GL_LINE_LOOP);
      
      if (primmode == GL_LINE_STRIP || primmode == GL_TRIANGLE_STRIP || /* primmode == GL_QUAD_STRIP ||*/
          primmode == GL_LINE_STRIP_ADJACENCY || primmode == GL_TRIANGLE_STRIP_ADJACENCY)
      {
        cmd.header = s_nvcmdlist_header[GL_DRAW_ELEMENTS_STRIP_COMMAND_NV];
      }
      else
      {
        cmd.header = s_nvcmdlist_header[GL_DRAW_ELEMENTS_COMMAND_NV];
      }
    }
  };

  struct NVTokenDrawArrays {
    static const GLenum   ID = GL_DRAW_ARRAYS_COMMAND_NV;

    DrawArraysCommandNV   cmd;

    NVTokenDrawArrays() {
      cmd.first = 0;
      cmd.count = 0;

      cmd.header  = s_nvcmdlist_header[ID];
    }

    void setParams(GLuint count, GLuint first=0)
    {
      cmd.count = count;
      cmd.first = first;
    }
    
    void setMode(GLenum primmode) {
      assert(primmode != GL_TRIANGLE_FAN && /* primmode != GL_POLYGON  && */ primmode != GL_LINE_LOOP);
      
      if (primmode == GL_LINE_STRIP || primmode == GL_TRIANGLE_STRIP || /* primmode == GL_QUAD_STRIP || */
          primmode == GL_LINE_STRIP_ADJACENCY || primmode == GL_TRIANGLE_STRIP_ADJACENCY)
      {
        cmd.header = s_nvcmdlist_header[GL_DRAW_ARRAYS_STRIP_COMMAND_NV];
      }
      else
      {
        cmd.header = s_nvcmdlist_header[GL_DRAW_ARRAYS_COMMAND_NV];
      }
    }
  };

  struct NVTokenDrawElemsStrip {
    static const GLenum   ID = GL_DRAW_ELEMENTS_STRIP_COMMAND_NV;

    DrawElementsCommandNV   cmd;

    NVTokenDrawElemsStrip() {
      cmd.baseVertex = 0;
      cmd.firstIndex = 0;
      cmd.count = 0;

      cmd.header  = s_nvcmdlist_header[ID];
    }

    void setParams(GLuint count, GLuint firstIndex=0, GLuint baseVertex=0)
    {
      cmd.count = count;
      cmd.firstIndex = firstIndex;
      cmd.baseVertex = baseVertex;
    }
  };

  struct NVTokenDrawArraysStrip {
    static const GLenum   ID = GL_DRAW_ARRAYS_STRIP_COMMAND_NV;

    DrawArraysCommandNV   cmd;

    NVTokenDrawArraysStrip() {
      cmd.first = 0;
      cmd.count = 0;

      cmd.header  = s_nvcmdlist_header[ID];
    }

    void setParams(GLuint count, GLuint first=0)
    {
      cmd.count = count;
      cmd.first = first;
    }
  };

  struct NVTokenVbo {
    static const GLenum   ID = GL_ATTRIBUTE_ADDRESS_COMMAND_NV;

    union {
      AttributeAddressCommandNV   cmd;
      AttributeAddressCommandEMU  cmdEMU;
    };

    void setBinding(GLuint idx){
      cmd.index = idx;
    }

    void setBuffer(GLuint buffer, GLuint64 address, GLuint offset)
    {
      if (s_nvcmdlist_bindless){
        address += offset;
        cmd.addressLo = GLuint(address & 0xFFFFFFFF);
        cmd.addressHi = GLuint(address >> 32);
      }
      else{
        cmdEMU.buffer = buffer;
        cmdEMU.offset = offset;
      }
    }

    NVTokenVbo() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenIbo {
    static const GLenum   ID = GL_ELEMENT_ADDRESS_COMMAND_NV;

    union{
      ElementAddressCommandNV     cmd;
      ElementAddressCommandEMU    cmdEMU;
    };

    void setType(GLenum type){
      if (type == GL_UNSIGNED_BYTE){
        cmd.typeSizeInByte = 1;
      }
      else if (type == GL_UNSIGNED_SHORT){
        cmd.typeSizeInByte = 2;
      }
      else if (type == GL_UNSIGNED_INT){
        cmd.typeSizeInByte = 4;
      }
      else{
        assert(0 && "illegal type");
      }
    }

    void setBuffer(GLuint buffer, GLuint64 address)
    {
      if (s_nvcmdlist_bindless){
        cmd.addressLo = GLuint(address & 0xFFFFFFFF);
        cmd.addressHi = GLuint(address >> 32);
      }
      else{
        cmdEMU.buffer = buffer;
        cmdEMU._pad   = 0;
      }
    }
    
    NVTokenIbo() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenUbo {
    static const GLenum   ID = GL_UNIFORM_ADDRESS_COMMAND_NV;

    union{
      UniformAddressCommandNV   cmd;
      UniformAddressCommandEMU  cmdEMU;
    };

    void setBuffer(GLuint buffer, GLuint64 address, GLuint offset, GLuint size)
    {
      assert(size % 4 == 0 && offset % 256 == 0);
      if (s_nvcmdlist_bindless){
        address += offset;
        cmd.addressLo = GLuint(address & 0xFFFFFFFF);
        cmd.addressHi = GLuint(address >> 32);
      }
      else{
        cmdEMU.buffer = buffer;
        cmdEMU.offset256 = offset / 256;
        cmdEMU.size4     = size / 4;
      }
    }

    void setBinding(GLuint idx, NVTokenShaderStage stage){
      cmd.index = idx;
      cmd.stage = s_nvcmdlist_stages[stage];
    }
    
    NVTokenUbo() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenBlendColor{
    static const GLenum   ID = GL_BLEND_COLOR_COMMAND_NV;

    BlendColorCommandNV     cmd;

    NVTokenBlendColor() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenStencilRef{
    static const GLenum   ID = GL_STENCIL_REF_COMMAND_NV;

    StencilRefCommandNV cmd;

    NVTokenStencilRef() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  } ;

  struct NVTokenLineWidth{
    static const GLenum   ID = GL_LINE_WIDTH_COMMAND_NV;

    LineWidthCommandNV  cmd;

    NVTokenLineWidth() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenPolygonOffset{
    static const GLenum   ID = GL_POLYGON_OFFSET_COMMAND_NV;

    PolygonOffsetCommandNV  cmd;

    NVTokenPolygonOffset() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenAlphaRef{
    static const GLenum   ID = GL_ALPHA_REF_COMMAND_NV;

    AlphaRefCommandNV cmd;

    NVTokenAlphaRef() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenViewport{
    static const GLenum   ID = GL_VIEWPORT_COMMAND_NV;

    ViewportCommandNV cmd;

    NVTokenViewport() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenScissor {
    static const GLenum   ID = GL_SCISSOR_COMMAND_NV;

    ScissorCommandNV  cmd;

    NVTokenScissor() {
      cmd.header  = s_nvcmdlist_header[ID];
    }
  };

  struct NVTokenFrontFace {
    static const GLenum   ID = GL_FRONT_FACE_COMMAND_NV;

    FrontFaceCommandNV  cmd;

    NVTokenFrontFace() {
      cmd.header  = s_nvcmdlist_header[ID];
    }

    void setFrontFace(GLenum winding){
      cmd.frontFace = winding == GL_CCW;
    }
  };

#pragma pack(pop)

  template <class T>
  void nvtokenMakeNop(T & token){
    NVTokenNop *nop = (NVTokenNop*)&token;
    for (size_t i = 0; i < (sizeof(T))/4; i++){
      nop[i] = NVTokenNop();
    }
  }

  template <class T>
  size_t nvtokenEnqueue(std::string& queue, T& data)
  {
    size_t offset = queue.size();
    std::string cmd = std::string((const char*)&data,sizeof(T));

    queue += cmd;

    return offset;
  }

  template <class T>
  size_t nvtokenEnqueue(NVPointerStream& queue, T& data)
  {
    assert(queue.m_cur + sizeof(T) <= queue.m_end);
    size_t offset = queue.m_cur - queue.m_begin;

    memcpy(queue.m_cur,&data,sizeof(T));
    queue.m_cur += sizeof(T);

    return offset;
  }
  
  //////////////////////////////////////////////////////////
  
  void        nvtokenInitInternals( bool hwsupport, bool bindlessSupport);
  const char* nvtokenCommandToString( GLenum type );
  void        nvtokenGetStats( const void* NV_RESTRICT stream, size_t streamSize, int stats[NVTOKEN_TYPES]);

  void nvtokenDrawCommandsSW(GLenum mode, const void* NV_RESTRICT stream, size_t streamSize, 
    const GLintptr* NV_RESTRICT offsets, const GLsizei* NV_RESTRICT sizes, 
    GLuint count, 
    StateSystem::State &state);

#if NVTOKEN_STATESYSTEM
  void nvtokenDrawCommandsStatesSW(const void* NV_RESTRICT stream, size_t streamSize, 
    const GLintptr* NV_RESTRICT offsets, const GLsizei* NV_RESTRICT sizes, 
    const GLuint* NV_RESTRICT states, const GLuint* NV_RESTRICT fbos, GLuint count, 
    StateSystem &stateSystem);
#endif
}
