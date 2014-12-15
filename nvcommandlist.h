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

#ifndef NV_COMMANDLIST_H__
#define NV_COMMANDLIST_H__

#include <GL/glew.h>


#  if defined(__MINGW32__) || defined(__CYGWIN__)
#    define GLEXT_APIENTRY __stdcall
#  elif (_MSC_VER >= 800) || defined(_STDCALL_SUPPORTED) || defined(__BORLANDC__)
#    define GLEXT_APIENTRY __stdcall
#  else
#    define GLEXT_APIENTRY
#  endif

// bindless UBO

#ifndef GL_UNIFORM_BUFFER_UNIFIED_NV
#define GL_UNIFORM_BUFFER_UNIFIED_NV                        0x936E
#endif
#ifndef GL_UNIFORM_BUFFER_ADDRESS_NV
#define GL_UNIFORM_BUFFER_ADDRESS_NV                        0x936F
#endif
#ifndef GL_UNIFORM_BUFFER_LENGTH_NV
#define GL_UNIFORM_BUFFER_LENGTH_NV                         0x9370
#endif

#define GL_TERMINATE_SEQUENCE_COMMAND_NV                    0x0000
#define GL_NOP_COMMAND_NV                                   0x0001
#define GL_DRAW_ELEMENTS_COMMAND_NV                         0x0002
#define GL_DRAW_ARRAYS_COMMAND_NV                           0x0003
#define GL_DRAW_ELEMENTS_STRIP_COMMAND_NV                   0x0004
#define GL_DRAW_ARRAYS_STRIP_COMMAND_NV                     0x0005
#define GL_DRAW_ELEMENTS_INSTANCED_COMMAND_NV               0x0006
#define GL_DRAW_ARRAYS_INSTANCED_COMMAND_NV                 0x0007
#define GL_ELEMENT_ADDRESS_COMMAND_NV                       0x0008
#define GL_ATTRIBUTE_ADDRESS_COMMAND_NV                     0x0009
#define GL_UNIFORM_ADDRESS_COMMAND_NV                       0x000a
#define GL_BLEND_COLOR_COMMAND_NV                           0x000b
#define GL_STENCIL_REF_COMMAND_NV                           0x000c
#define GL_LINE_WIDTH_COMMAND_NV                            0x000d
#define GL_POLYGON_OFFSET_COMMAND_NV                        0x000e
#define GL_ALPHA_REF_COMMAND_NV                             0x000f
#define GL_VIEWPORT_COMMAND_NV                              0x0010
#define GL_SCISSOR_COMMAND_NV                               0x0011
#define GL_FRONTFACE_COMMAND_NV                             0x0012

#pragma pack(push,1)

  typedef struct {
    GLuint      encoded;
  } CommandHeaderNV;

  typedef  struct {
    GLuint  count;
    GLuint  firstIndex;
    GLuint  baseVertex;
  } DrawElementsCommandNV;

  typedef  struct {
    GLuint  count;
    GLuint  first;
  } DrawArraysCommandNV;

  typedef  struct {
    GLenum  mode;
    GLuint  count;
    GLuint  instanceCount;
    GLuint  firstIndex;
    GLuint  baseVertex;
    GLuint  baseInstance;
  } DrawElementsInstancedCommandNV;

  typedef  struct {
    GLenum  mode;
    GLuint  count;
    GLuint  instanceCount;
    GLuint  first;
    GLuint  baseInstance;
  } DrawArraysInstancedCommandNV;

  typedef struct {
    GLuint64 address;
    GLuint   typeSizeInByte;
  } ElementAddressCommandNV;

  typedef struct {
    GLuint   index;
    GLuint64 address;
  } AttributeAddressCommandNV;

  typedef struct {
    GLushort index;
    GLushort stage;
    GLuint64 address;
  } UniformAddressCommandNV;

  typedef struct {
    float red;
    float green;
    float blue;
    float alpha;
  } BlendColorCommandNV;

  typedef struct {
    GLuint frontStencilRef;
    GLuint backStencilRef;
  } StencilRefCommandNV;

  typedef struct {
    float lineWidth;
  } LineWidthCommandNV;

  typedef struct {
    float scale;
    float bias;
  } PolygonOffsetCommandNV;

  typedef struct {
    float alphaRef;
  } AlphaRefCommandNV;

  typedef struct {
    GLuint x;
    GLuint y;
    GLuint width;
    GLuint height;
  } ViewportCommandNV;

  typedef struct {
    GLuint x;
    GLuint y;
    GLuint width;
    GLuint height;
  } ScissorCommandNV;

  typedef struct {
    GLuint frontFace; // 0 for CW, 1 for CCW
  } FrontFaceCommandNV;


#pragma pack(pop)

typedef void (GLEXT_APIENTRY *PFNGLCREATESTATESNVPROC)(GLsizei n, GLuint *states);
typedef void (GLEXT_APIENTRY *PFNGLDELETESTATESNVPROC)(GLsizei n, const GLuint *states);
typedef GLboolean (*PFNGLISSTATENVPROC)(GLuint state);
typedef void (GLEXT_APIENTRY *PFNGLSTATECAPTURENVPROC)(GLuint state, GLenum mode);
typedef void (GLEXT_APIENTRY *PFNGLDRAWCOMMANDSNVPROC)(GLenum mode, GLuint buffer, const GLintptr* indirects, const GLsizei* sizes, GLuint count);
typedef void (GLEXT_APIENTRY *PFNGLDRAWCOMMANDSADDRESSNVPROC)(GLenum mode, const GLuint64* indirects, const GLsizei* sizes, GLuint count);
typedef void (GLEXT_APIENTRY *PFNGLDRAWCOMMANDSSTATESNVPROC)(GLuint buffer, const GLintptr* indirects, const GLsizei* sizes, 
  const GLuint* states, const GLuint* fbos, GLuint count);
typedef void (GLEXT_APIENTRY *PFNGLDRAWCOMMANDSSTATESADDRESSNVPROC)(const GLuint64* indirects, const GLsizei* sizes, 
  const GLuint* states, const GLuint* fbos, GLuint count);
typedef void (GLEXT_APIENTRY *PFNGLCREATECOMMANDLISTSNVPROC)(GLsizei n, GLuint *lists);
typedef void (GLEXT_APIENTRY *PFNGLDELETECOMMANDLISTSNVPROC)(GLsizei n, const GLuint *lists);
typedef GLboolean (GLEXT_APIENTRY *PFNGLISCOMMANDLISTNVPROC)(GLuint list);
typedef void (GLEXT_APIENTRY *PFNGLLISTDRAWCOMMANDSSTATESCLIENTNVPROC)(GLuint list, GLuint segment, const void** indirects, 
  const GLsizei* sizes, const GLuint* states, const GLuint* fbos, GLuint count);
typedef void (GLEXT_APIENTRY *PFNGLCOMMANDLISTSEGMENTSNVPROC)(GLuint list, GLuint segments);
typedef void (GLEXT_APIENTRY *PFNGLCOMPILECOMMANDLISTNVPROC)(GLuint list);
typedef void (GLEXT_APIENTRY *PFNGLCALLCOMMANDLISTNVPROC)(GLuint list);
typedef GLuint (GLEXT_APIENTRY *PFNGLGETCOMMANDHEADERNVPROC)(GLenum id, GLuint tokenSize);
typedef GLushort(GLEXT_APIENTRY* PFNGLGETSTAGEINDEXNVPROC)(GLenum shadertype);

extern PFNGLCREATESTATESNVPROC __nvcCreateStatesNV;
inline void glCreateStatesNV(GLsizei n, GLuint *states)
{
  __nvcCreateStatesNV(n,states);
}
extern PFNGLDELETESTATESNVPROC __nvcDeleteStatesNV;
inline void glDeleteStatesNV(GLsizei n, const GLuint *states)
{
  __nvcDeleteStatesNV(n,states);
}
extern PFNGLISSTATENVPROC __nvcIsStateNV;
inline GLboolean glIsStateNV(GLuint state)
{
  return __nvcIsStateNV(state);
}
extern PFNGLSTATECAPTURENVPROC __nvcStateCaptureNV;
inline void glStateCaptureNV(GLuint state, GLenum mode)
{
  __nvcStateCaptureNV(state,mode);
}
extern PFNGLDRAWCOMMANDSNVPROC __nvcDrawCommandsNV;
inline void glDrawCommandsNV(GLenum mode, GLuint buffer, const GLintptr* indirects, const GLsizei* sizes, GLuint count)
{
  __nvcDrawCommandsNV(mode,buffer,indirects,sizes,count);
}
extern PFNGLDRAWCOMMANDSADDRESSNVPROC __nvcDrawCommandsAddressNV;
inline void glDrawCommandsAddressNV(GLenum mode, const GLuint64* indirects, const GLsizei* sizes, GLuint count)
{
  __nvcDrawCommandsAddressNV(mode,indirects,sizes,count);
}
extern PFNGLDRAWCOMMANDSSTATESNVPROC __nvcDrawCommandsStatesNV;
inline void glDrawCommandsStatesNV(GLuint buffer, const GLintptr* indirects, const GLsizei* sizes, 
  const GLuint* states, const GLuint* fbos, GLuint count)
{
  __nvcDrawCommandsStatesNV(buffer,indirects,sizes,states,fbos,count);
}
extern PFNGLDRAWCOMMANDSSTATESADDRESSNVPROC __nvcDrawCommandsStatesAddressNV;
inline void glDrawCommandsStatesAddressNV(const GLuint64* indirects, const GLsizei* sizes, 
  const GLuint* states, const GLuint* fbos, GLuint count)
{
  __nvcDrawCommandsStatesAddressNV(indirects,sizes,states,fbos,count);
}
extern PFNGLCREATECOMMANDLISTSNVPROC __nvcCreateCommandListsNV;
inline void glCreateCommandListsNV(GLsizei n, GLuint *lists)
{
  __nvcCreateCommandListsNV(n,lists);
}
extern PFNGLDELETECOMMANDLISTSNVPROC __nvcDeleteCommandListsNV;
inline void glDeleteCommandListsNV(GLsizei n, const GLuint *lists)
{
  __nvcDeleteCommandListsNV(n,lists);
}
extern PFNGLISCOMMANDLISTNVPROC __nvcIsCommandListNV;
inline GLboolean glIsCommandListNV(GLuint list)
{
  return __nvcIsCommandListNV(list);
}
extern PFNGLLISTDRAWCOMMANDSSTATESCLIENTNVPROC __nvcListDrawCommandsStatesClientNV;
inline void glListDrawCommandsStatesClientNV(GLuint list, GLuint segment, const void** indirects, 
  const GLsizei* sizes, const GLuint* states, const GLuint* fbos, GLuint count)
{
  __nvcListDrawCommandsStatesClientNV(list,segment,indirects,sizes,states,fbos,count);
}
extern PFNGLCOMMANDLISTSEGMENTSNVPROC __nvcCommandListSegmentsNV;
inline void glCommandListSegmentsNV(GLuint list, GLuint segments)
{
  __nvcCommandListSegmentsNV(list,segments);
}
extern PFNGLCOMPILECOMMANDLISTNVPROC __nvcCompileCommandListNV;
inline void glCompileCommandListNV(GLuint list)
{
  __nvcCompileCommandListNV(list);
}
extern PFNGLCALLCOMMANDLISTNVPROC __nvcCallCommandListNV;
inline void glCallCommandListNV(GLuint list)
{
  __nvcCallCommandListNV(list);
}
extern PFNGLGETCOMMANDHEADERNVPROC __nvcGetCommandHeaderNV;
inline GLuint glGetCommandHeaderNV(GLenum tokenId, GLuint tokenSize)
{
  return __nvcGetCommandHeaderNV(tokenId,tokenSize);
}
extern PFNGLGETSTAGEINDEXNVPROC __nvcGetStageIndexNV;
inline GLushort glGetStageIndexNV(GLenum shadertype)
{
  return __nvcGetStageIndexNV(shadertype);
}

typedef void (*NVCPROC)(void);
int init_NV_command_list(NVCPROC (*fnGetProc)(const char* name));

#endif

