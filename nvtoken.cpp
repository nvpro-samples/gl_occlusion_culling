/*-----------------------------------------------------------------------
  Copyright (c) 2014, NVIDIA. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse 
     or promote products derived from this software without specific
     prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/
/* Contact ckubisch@nvidia.com (Christoph Kubisch) for feedback */

#include "nvtoken.hpp"

namespace nvtoken
{

  //////////////////////////////////////////////////////////////////////////
  // generic

  GLuint   s_nvcmdlist_header[NVTOKEN_TYPES] = {0};
  GLuint   s_nvcmdlist_headerSizes[NVTOKEN_TYPES] = {0};
  GLushort s_nvcmdlist_stages[NVTOKEN_STAGES] = {0};
  bool     s_nvcmdlist_bindless  = false;
  
  static inline GLuint nvtokenHeaderSW(GLuint type, GLuint size){
    return type | (size<<16);
  }
  
  static inline GLenum nvtokenHeaderCommandSW(GLuint header)
  {
    return header & 0xFFFF;
  }

  static inline GLuint nvtokenHeaderSizeSW(GLuint header)
  {
    return header>>16;
  }

  static inline GLenum nvtokenHeaderCommand(GLuint header)
  {
    for (int i = 0; i < NVTOKEN_TYPES; i++){
      if (header == s_nvcmdlist_header[i]) return i;
    }

    assert(0 && "can't find header");
    return -1;
  }

  template <class T>
  static void nvtokenRegisterSize()
  {
    s_nvcmdlist_headerSizes[T::ID] = sizeof(T);
  }

  void nvtokenInitInternals( bool hwsupport, bool bindlessSupport)
  {
    assert( !hwsupport || (hwsupport && bindlessSupport) );

    nvtokenRegisterSize<NVTokenTerminate>();
    nvtokenRegisterSize<NVTokenNop>();
    nvtokenRegisterSize<NVTokenDrawElems>();
    nvtokenRegisterSize<NVTokenDrawArrays>();
    nvtokenRegisterSize<NVTokenDrawElemsStrip>();
    nvtokenRegisterSize<NVTokenDrawArraysStrip>();
    nvtokenRegisterSize<NVTokenDrawElemsInstanced>();
    nvtokenRegisterSize<NVTokenDrawArraysInstanced>();
    nvtokenRegisterSize<NVTokenVbo>();
    nvtokenRegisterSize<NVTokenIbo>();
    nvtokenRegisterSize<NVTokenUbo>();
    nvtokenRegisterSize<NVTokenLineWidth>();
    nvtokenRegisterSize<NVTokenPolygonOffset>();
    nvtokenRegisterSize<NVTokenScissor>();
    nvtokenRegisterSize<NVTokenBlendColor>();
    nvtokenRegisterSize<NVTokenViewport>();
    nvtokenRegisterSize<NVTokenAlphaRef>();
    nvtokenRegisterSize<NVTokenStencilRef>();
    nvtokenRegisterSize<NVTokenFrontFace>();
    
    for (int i = 0; i < NVTOKEN_TYPES; i++){
      GLuint sz = s_nvcmdlist_headerSizes[i];
      assert(sz);
    }
    
    s_nvcmdlist_bindless  = bindlessSupport;
    
    if (hwsupport){
      for (int i = 0; i < NVTOKEN_TYPES; i++){
        s_nvcmdlist_header[i] = glGetCommandHeaderNV(i,s_nvcmdlist_headerSizes[i]);
      }
      s_nvcmdlist_stages[NVTOKEN_STAGE_VERTEX] = glGetStageIndexNV(GL_VERTEX_SHADER);
      s_nvcmdlist_stages[NVTOKEN_STAGE_TESS_CONTROL] = glGetStageIndexNV(GL_TESS_CONTROL_SHADER);
      s_nvcmdlist_stages[NVTOKEN_STAGE_TESS_EVALUATION] = glGetStageIndexNV(GL_TESS_EVALUATION_SHADER);
      s_nvcmdlist_stages[NVTOKEN_STAGE_GEOMETRY] = glGetStageIndexNV(GL_GEOMETRY_SHADER);
      s_nvcmdlist_stages[NVTOKEN_STAGE_FRAGMENT] = glGetStageIndexNV(GL_FRAGMENT_SHADER);
    }
    else{
      for (int i = 0; i < NVTOKEN_TYPES; i++){
        s_nvcmdlist_header[i] = nvtokenHeaderSW(i,s_nvcmdlist_headerSizes[i]);
      }
      for (int i = 0; i < NVTOKEN_STAGES; i++){
        s_nvcmdlist_stages[i] = i;
      }
    }
  }

#define TOSTRING(a)  case a: return #a;
  const char* nvtokenCommandToString(GLenum type){
    switch  (type){
      TOSTRING(GL_NOP_COMMAND_NV                   );
      TOSTRING(GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV);
      TOSTRING(GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV  );
      TOSTRING(GL_ELEMENT_ADDRESS_COMMAND_NV       );
      TOSTRING(GL_ATTRIBUTE_ADDRESS_COMMAND_NV     );
      TOSTRING(GL_UNIFORM_ADDRESS_COMMAND_NV       );
      TOSTRING(GL_BLEND_COLOR_COMMAND_NV           );
      TOSTRING(GL_STENCIL_REF_COMMAND_NV           );
      TOSTRING(GL_TERMINATE_SEQUENCE_COMMAND_NV    );
      TOSTRING(GL_LINE_WIDTH_COMMAND_NV            );
      TOSTRING(GL_POLYGON_OFFSET_COMMAND_NV        );
      TOSTRING(GL_ALPHA_REF_COMMAND_NV             );
      TOSTRING(GL_VIEWPORT_COMMAND_NV              );
      TOSTRING(GL_SCISSOR_COMMAND_NV               );
      TOSTRING(GL_DRAW_ELEMENTS_COMMAND_NV         );
      TOSTRING(GL_DRAW_ARRAYS_COMMAND_NV           );
      TOSTRING(GL_DRAW_ELEMENTS_STRIP_COMMAND_NV   );
      TOSTRING(GL_DRAW_ARRAYS_STRIP_COMMAND_NV     );
    }
    return NULL;
  }

  //////////////////////////////////////////////////////////////////////////


  void nvtokenGetStats( const void* NVP_RESTRICT stream, size_t streamSize, int stats[NVTOKEN_TYPES] )
  {
    const GLubyte* NVP_RESTRICT current = (GLubyte*)stream;
    const GLubyte* streamEnd = current + streamSize;

    while (current < streamEnd){
      const GLuint*             header  = (const GLuint*)current;

      GLenum type = nvtokenHeaderCommand(*header);
      stats[type]++;

      current += s_nvcmdlist_headerSizes[type];
    }
  }


  // Emulation related

  static __forceinline GLenum nvtokenDrawCommandSequenceSW( const void* NVP_RESTRICT stream, size_t streamSize, GLenum mode, GLenum type, const StateSystem::State& state ) 
  {
    const GLubyte* NVP_RESTRICT current = (GLubyte*)stream;
    const GLubyte* streamEnd = current + streamSize;

    GLenum modeStrip;
    if      (mode == GL_LINES)                modeStrip = GL_LINE_STRIP;
    else if (mode == GL_TRIANGLES)            modeStrip = GL_TRIANGLE_STRIP;
    else if (mode == GL_QUADS)                modeStrip = GL_QUAD_STRIP;
    else if (mode == GL_LINES_ADJACENCY)      modeStrip = GL_LINE_STRIP_ADJACENCY;
    else if (mode == GL_TRIANGLES_ADJACENCY)  modeStrip = GL_TRIANGLE_STRIP_ADJACENCY;
    else    modeStrip = mode;

    GLenum modeSpecial;
    if      (mode == GL_LINES)      modeSpecial = GL_LINE_LOOP;
    else if (mode == GL_TRIANGLES)  modeSpecial = GL_TRIANGLE_FAN;
    else    modeSpecial = mode;

    while (current < streamEnd){
      const GLuint*             header  = (const GLuint*)current;

      GLenum cmdtype = nvtokenHeaderCommand(*header);
      // if you always use emulation on non-native tokens you can use 
      // cmdtype = nvtokenHeaderCommandSW(header->encoded)
      switch(cmdtype){
      case GL_TERMINATE_SEQUENCE_COMMAND_NV:
        {
          return type;
        }
        break;
      case GL_NOP_COMMAND_NV:
        {
        }
        break;
      case GL_DRAW_ELEMENTS_COMMAND_NV:
        {
          const DrawElementsCommandNV* cmd = (const DrawElementsCommandNV*)current;
          glDrawElementsBaseVertex(mode, cmd->count, type, (const GLvoid*)(cmd->firstIndex * sizeof(GLuint)), cmd->baseVertex);
        }
        break;
      case GL_DRAW_ARRAYS_COMMAND_NV:
        {
          const DrawArraysCommandNV* cmd = (const DrawArraysCommandNV*)current;
          glDrawArrays(mode, cmd->first, cmd->count);
        }
        break;
      case GL_DRAW_ELEMENTS_STRIP_COMMAND_NV:
        {
          const DrawElementsCommandNV* cmd = (const DrawElementsCommandNV*)current;
          glDrawElementsBaseVertex(modeStrip, cmd->count, type, (const GLvoid*)(cmd->firstIndex * sizeof(GLuint)), cmd->baseVertex);
        }
        break;
      case GL_DRAW_ARRAYS_STRIP_COMMAND_NV:
        {
          const DrawArraysCommandNV* cmd = (const DrawArraysCommandNV*)current;
          glDrawArrays(modeStrip, cmd->first, cmd->count);
        }
        break;
      case GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV:
        {
          const DrawElementsInstancedCommandNV* cmd = (const DrawElementsInstancedCommandNV*)current;

          assert (cmd->mode == mode || cmd->mode == modeStrip || cmd->mode == modeSpecial);

          glDrawElementsIndirect(cmd->mode, type, &cmd->count);
        }
        break;
      case GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV:
        {
          const DrawArraysInstancedCommandNV* cmd = (const DrawArraysInstancedCommandNV*)current;

          assert (cmd->mode == mode || cmd->mode == modeStrip || cmd->mode == modeSpecial);

          glDrawArraysIndirect(cmd->mode, &cmd->count);
        }
        break;
      case GL_ELEMENT_ADDRESS_COMMAND_NV:
        {
          const ElementAddressCommandNV* cmd = (const ElementAddressCommandNV*)current;
          type = cmd->typeSizeInByte == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
          if (s_nvcmdlist_bindless){
            glBufferAddressRangeNV(GL_ELEMENT_ARRAY_ADDRESS_NV, 0, GLuint64(cmd->addressLo) | (GLuint64(cmd->addressHi)<<32), 0x7FFFFFFF);
          }
          else{
            const ElementAddressCommandEMU* cmd = (const ElementAddressCommandEMU*)current;
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cmd->buffer);
          }
        }
        break;
      case GL_ATTRIBUTE_ADDRESS_COMMAND_NV:
        {
          if (s_nvcmdlist_bindless){
            const AttributeAddressCommandNV* cmd = (const AttributeAddressCommandNV*)current;
            glBufferAddressRangeNV(GL_VERTEX_ATTRIB_ARRAY_ADDRESS_NV, cmd->index, GLuint64(cmd->addressLo) | (GLuint64(cmd->addressHi)<<32), 0x7FFFFFFF);
          }
          else{
            const AttributeAddressCommandEMU* cmd = (const AttributeAddressCommandEMU*)current;
            glBindVertexBuffer(cmd->index, cmd->buffer, cmd->offset, state.vertexformat.bindings[cmd->index].stride);
          }
        }
        break;
      case GL_UNIFORM_ADDRESS_COMMAND_NV:
        {
           if (s_nvcmdlist_bindless){
            const UniformAddressCommandNV* cmd = (const UniformAddressCommandNV*)current;
            glBufferAddressRangeNV(GL_UNIFORM_BUFFER_ADDRESS_NV, cmd->index, GLuint64(cmd->addressLo) | (GLuint64(cmd->addressHi)<<32), 0x10000);
          }
          else{
            const UniformAddressCommandEMU* cmd = (const UniformAddressCommandEMU*)current;
            glBindBufferRange(GL_UNIFORM_BUFFER,cmd->index, cmd->buffer, cmd->offset256 * 256, cmd->size4*4);
          }
        }
        break;
      case GL_BLEND_COLOR_COMMAND_NV:
        {
          const BlendColorCommandNV* cmd = (const BlendColorCommandNV*)current;
          glBlendColor(cmd->red,cmd->green,cmd->blue,cmd->alpha);
        }
        break;
      case GL_STENCIL_REF_COMMAND_NV:
        {
          const StencilRefCommandNV* cmd = (const StencilRefCommandNV*)current;
          glStencilFuncSeparate(GL_FRONT, state.stencil.funcs[StateSystem::FACE_FRONT].func, cmd->frontStencilRef, state.stencil.funcs[StateSystem::FACE_FRONT].mask);
          glStencilFuncSeparate(GL_BACK,  state.stencil.funcs[StateSystem::FACE_BACK ].func, cmd->backStencilRef,  state.stencil.funcs[StateSystem::FACE_BACK ].mask);
        }
        break;

      case GL_LINE_WIDTH_COMMAND_NV:
        {
          const LineWidthCommandNV* cmd = (const LineWidthCommandNV*)current;
          glLineWidth(cmd->lineWidth);
        }
        break;
      case GL_POLYGON_OFFSET_COMMAND_NV:
        {
          const PolygonOffsetCommandNV* cmd = (const PolygonOffsetCommandNV*)current;
          glPolygonOffset(cmd->scale,cmd->bias);
        }
        break;
      case GL_ALPHA_REF_COMMAND_NV:
        {
          const AlphaRefCommandNV* cmd = (const AlphaRefCommandNV*)current;
          glAlphaFunc(state.alpha.mode, cmd->alphaRef);
        }
        break;
      case GL_VIEWPORT_COMMAND_NV:
        {
          const ViewportCommandNV* cmd = (const ViewportCommandNV*)current;
          glViewport(cmd->x, cmd->y, cmd->width, cmd->height);
        }
        break;
      case GL_SCISSOR_COMMAND_NV:
        {
          const ScissorCommandNV* cmd = (const ScissorCommandNV*)current;
          glScissor(cmd->x,cmd->y,cmd->width,cmd->height);
        }
        break;
      case GL_FRONTFACE_COMMAND_NV:
        {
          FrontFaceCommandNV* cmd = (FrontFaceCommandNV*)current;
          glFrontFace(cmd->frontFace?GL_CW:GL_CCW);
        }
        break;
      }


      GLuint tokenSize = s_nvcmdlist_headerSizes[cmdtype];
      assert(tokenSize);

      current += tokenSize;

    }
    return type;
  }

  void nvtokenDrawCommandsSW(GLenum mode, const void* NVP_RESTRICT stream, size_t streamSize, 
    const GLintptr* NVP_RESTRICT offsets, const GLsizei* NVP_RESTRICT sizes, 
    GLuint count, 
    StateSystem::State &state)
  {
    const char* NVP_RESTRICT tokens = (const char*)stream;
    GLenum type = GL_UNSIGNED_SHORT;
    for (GLuint i = 0; i < count; i++)
    {
      size_t offset = offsets[i];
      size_t size   = sizes[i];

      assert(size + offset <= streamSize);

      type = nvtokenDrawCommandSequenceSW(&tokens[offset], size, mode, type, state);
    }

  }

#if NVTOKEN_STATESYSTEM
  void nvtokenDrawCommandsStatesSW(const void* NVP_RESTRICT stream, size_t streamSize, 
    const GLintptr* NVP_RESTRICT offsets, const GLsizei* NVP_RESTRICT sizes, 
    const GLuint* NVP_RESTRICT states, const GLuint* NVP_RESTRICT fbos, GLuint count, 
    StateSystem &stateSystem)
  {
    int lastFbo = ~0;
    const char* NVP_RESTRICT tokens = (const char*)stream;

    StateSystem::StateID lastID;

    GLenum type = GL_UNSIGNED_SHORT;
    for (GLuint i = 0; i < count; i++)
    {
      GLuint fbo;

      StateSystem::StateID curID = states[i];
      const StateSystem::State&  state = stateSystem.get(curID);

      if (fbos[i]){
        fbo = fbos[i];
      }
      else{
        fbo = state.fbo.fboDraw;
      }

      if (fbo != lastFbo){
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        lastFbo = fbo;
      }

      if (i == 0){
        stateSystem.applyGL( curID, true ); // quite costly
      }
      else {
        stateSystem.applyGL( curID, lastID, true );
      }
      lastID = curID;

      size_t offset = offsets[i];
      size_t size   = sizes[i];

      GLenum mode = state.basePrimitiveMode;

      assert(size + offset <= streamSize);

      type = nvtokenDrawCommandSequenceSW(&tokens[offset], size, mode, type, state);
    }
  }
#endif
}
