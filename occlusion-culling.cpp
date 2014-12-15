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


#define DEBUG_FILTER     1

#include <GL/glew.h>
#include <nv_helpers/anttweakbar.hpp>
#include <nv_helpers_gl/WindowProfiler.hpp>
#include <nv_math/nv_math_glsltypes.h>

#include <nv_helpers_gl/error.hpp>
#include <nv_helpers_gl/programmanager.hpp>
#include <nv_helpers/geometry.hpp>
#include <nv_helpers/misc.hpp>
#include <nv_helpers_gl/glresources.hpp>
#include <nv_helpers/cameracontrol.hpp>


#include <vector>

#include "cullingsystem.hpp"

#define NVTOKEN_NO_STATESYSTEM
#include "nvtoken.hpp"
using namespace nvtoken;

#include "scansystem.hpp"

using namespace nv_helpers;
using namespace nv_helpers_gl;
using namespace nv_math;

#include "common.h"

namespace ocull
{
  int const SAMPLE_SIZE_WIDTH(800);
  int const SAMPLE_SIZE_HEIGHT(600);
  int const SAMPLE_MAJOR_VERSION(4);
  int const SAMPLE_MINOR_VERSION(3);

  static const GLenum    fboFormat = GL_RGBA16F;
  static const int        grid = 26;
  static const float      globalscale = 8;

  static ScanSystem       s_scanSys;

  class Sample : public nv_helpers_gl::WindowProfiler
  {
    ProgramManager progManager;

    enum DrawModes {
      DRAW_STANDARD,
      DRAW_MULTIDRAWINDIRECT,
      DRAW_TOKENBUFFER_EMULATION,
      DRAW_TOKENBUFFER,
    };

    enum ResultType {
      RESULT_REGULAR_CURRENT,
      RESULT_REGULAR_LASTFRAME,
      RESULT_TEMPORAL_CURRENT,
    };

    struct {
      ProgramManager::ProgramID
        draw_scene,

        object_frustum,
        object_hiz,
        object_raster,
        bit_temporallast,
        bit_temporalnew,
        bit_regular,
        indirect_unordered,
        depth_mips,

        token_sizes,
        token_cmds,

        scan_prefixsum,
        scan_offsets,
        scan_combine;
    } programs;

    struct {
      ResourceGLuint  scene;
    } fbos;

    struct {
      ResourceGLuint  
        scene_ubo,
        scene_vbo,
        scene_ibo,
        scene_matrices,
        scene_bboxes,
        scene_matrixindices,
        scene_indirect,

        scene_token,
        scene_tokenSizes,
        scene_tokenOffsets,
        scene_tokenObjects,

        cull_output,
        cull_bits,
        cull_bitsLast,
        cull_bitsReadback,
        cull_indirect,
        cull_counter,

        cull_token,
        cull_tokenEmulation,
        cull_tokenSizes,
        cull_tokenScan,
        cull_tokenScanOffsets;


    } buffers;

    struct {
      GLuint64    
        scene_ubo,
        scene_ibo,
        scene_vbo,
        scene_matrixindices;
    } addresses;

    struct {
      ResourceGLuint
        scene_color,
        scene_depthstencil,
        scene_matrices;
    } textures;

    struct DrawCmd {
      GLuint count;
      GLuint instanceCount;
      GLuint firstIndex;
      GLint  baseVertex;
      GLuint baseInstance;
    };

    struct CullBbox {
      nv_math::vec4  min;
      nv_math::vec4  max;
    };

    struct Geometry {
      GLuint firstIndex;
      GLuint count;
    };

    struct Vertex {

      Vertex(const geometry::Vertex& vertex){
        position  = vertex.position;
        normal    = vertex.normal;
        color     = nv_math::vec4(1.0f);
      }

      nv_math::vec4   position;
      nv_math::vec4   normal;
      nv_math::vec4   color;
    };

    class CullJobToken : public CullingSystem::Job
    {
    public:
      struct Sequence {
        GLuint    offset;
        GLuint    endoffset;
        int       first;
        int       num;
      };

      void resultFromBits( const CullingSystem::Buffer& bufferVisBitsCurrent );

      GLuint      program_sizes;
      GLuint      program_cmds;
      
      GLuint                numTokens;
      std::vector<Sequence> sequences;

      // input buffers
      ScanSystem::Buffer    tokenOrig;
                                          // for each command
                                          // #cmds rounded to multiple of 4
      ScanSystem::Buffer    tokenSizes;   // 
      ScanSystem::Buffer    tokenOffsets; // 
      ScanSystem::Buffer    tokenObjects; // -1 if no drawcall, otherwise object

      // outputs
      ScanSystem::Buffer    tokenOut;
      ScanSystem::Buffer    tokenOutSizes;
      ScanSystem::Buffer    tokenOutScan;
      ScanSystem::Buffer    tokenOutScanOffset;
    };

    struct Tweak {
      Tweak() 
        : culling(false)
        , animate(0)
        , freeze(false)
        , method(CullingSystem::METHOD_RASTER)
        , result(RESULT_REGULAR_CURRENT)
        , drawmode(DRAW_STANDARD)
      {}

      CullingSystem::MethodType method;
      ResultType                result;
      DrawModes                 drawmode;
      bool      culling;
      bool      freeze;
      float     animate;

    };

    Tweak      tweak;
    Tweak      tweakLast;


    std::vector<int>            sceneVisBits;
    std::vector<DrawCmd>        sceneCmds;
    std::vector<nv_math::mat4f> sceneMatrices;
    std::vector<nv_math::mat4f> sceneMatricesAnimated;

    GLuint                      numTokens;
    std::string                 tokenStream;
    std::string                 tokenStreamCulled;

    SceneData   sceneUbo;
    double      statsTime;
    bool        statsPrint;
    bool        cmdlistNative;
    bool        bindlessVboUbo;

    CullingSystem                cullSys;
    CullingSystem::JobReadback   cullJobReadback;
    CullingSystem::JobIndirectUnordered   cullJobIndirect;
    CullJobToken                          cullJobToken;

    bool begin();
    void think(double time);
    void resize(int width, int height);

    void initCullingJob(CullingSystem::Job& cullJob);

    void drawScene(bool depthonly, const char* what);

    void drawCullingRegular(CullingSystem::Job& cullJob);
    void drawCullingRegularLastFrame(CullingSystem::Job& cullJob);
    void drawCullingTemporal(CullingSystem::Job& cullJob);

    bool initProgram();
    bool initFramebuffers(int width, int height);
    bool initScene();
    void getCullPrograms( CullingSystem::Programs &cullprograms );
    void getScanPrograms( ScanSystem::Programs &scanprograms );
    void systemChange();


    CameraControl m_control;

    void end() {
      TwTerminate();
    }
    // return true to prevent m_window updates
    bool mouse_pos    (int x, int y) {
      return !!TwEventMousePosGLFW(x,y); 
    }
    bool mouse_button (int button, int action) {
      return !!TwEventMouseButtonGLFW(button, action);
    }
    bool mouse_wheel  (int wheel) {
      return !!TwEventMouseWheelGLFW(wheel); 
    }
    bool key_button   (int button, int action, int mods) {
      return handleTwKeyPressed(button,action,mods);
    }

  };

  bool Sample::initProgram()
  {
    bool validated(true);
    progManager.addDirectory( std::string(PROJECT_NAME));
    progManager.addDirectory( sysExePath() + std::string(PROJECT_RELDIRECTORY));
    progManager.addDirectory( std::string(PROJECT_ABSDIRECTORY));

    progManager.registerInclude("common.h", "common.h");
    progManager.registerInclude("noise.glsl", "noise.glsl");

    programs.draw_scene = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,   "scene.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER, "scene.frag.glsl"));

    programs.object_raster = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,   "cull-raster.vert.glsl"),
      ProgramManager::Definition(GL_GEOMETRY_SHADER, "cull-raster.geo.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER, "cull-raster.frag.glsl"));

    programs.object_frustum = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,  "cull-xfb.vert.glsl"));

    programs.object_hiz = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,  "#define OCCLUSION\n", "cull-xfb.vert.glsl"));

    programs.bit_regular = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL 0\n", "cull-bitpack.vert.glsl"));
    programs.bit_temporallast = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL TEMPORAL_LAST\n", "cull-bitpack.vert.glsl"));
    programs.bit_temporalnew = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL TEMPORAL_NEW\n", "cull-bitpack.vert.glsl"));

    programs.indirect_unordered = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER, "cull-indirectunordered.vert.glsl"));

    programs.depth_mips = progManager.createProgram(
      ProgramManager::Definition(GL_VERTEX_SHADER,   "cull-downsample.vert.glsl"),
      ProgramManager::Definition(GL_FRAGMENT_SHADER, "cull-downsample.frag.glsl"));

    programs.token_sizes = progManager.createProgram(
      nv_helpers_gl::ProgramManager::Definition(GL_VERTEX_SHADER, "cull-tokensizes.vert.glsl"));
    programs.token_cmds = progManager.createProgram(
      nv_helpers_gl::ProgramManager::Definition(GL_VERTEX_SHADER, "cull-tokencmds.vert.glsl"));

    programs.scan_prefixsum = progManager.createProgram(
      nv_helpers_gl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_SUM\n", "scan.comp.glsl"));
    programs.scan_offsets = progManager.createProgram(
      nv_helpers_gl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_OFFSETS\n", "scan.comp.glsl"));
    programs.scan_combine = progManager.createProgram(
      nv_helpers_gl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_COMBINE\n", "scan.comp.glsl"));

    validated = progManager.areProgramsValid();

    return validated;
  }

  void Sample::getScanPrograms( ScanSystem::Programs &scanprograms )
  {
    scanprograms.prefixsum  = progManager.get( programs.scan_prefixsum );
    scanprograms.offsets    = progManager.get( programs.scan_offsets );
    scanprograms.combine    = progManager.get( programs.scan_combine );
  }

  void Sample::getCullPrograms( CullingSystem::Programs &cullprograms )
  {
    cullprograms.bit_regular      = progManager.get( programs.bit_regular );
    cullprograms.bit_temporallast = progManager.get( programs.bit_temporallast );
    cullprograms.bit_temporalnew  = progManager.get( programs.bit_temporalnew );
    cullprograms.depth_mips       = progManager.get( programs.depth_mips );
    cullprograms.object_frustum   = progManager.get( programs.object_frustum );
    cullprograms.object_hiz       = progManager.get( programs.object_hiz );
    cullprograms.object_raster    = progManager.get( programs.object_raster );
  }

  static inline size_t snapdiv(size_t input, size_t align)
  {
    return (input + align - 1) / align;
  }

  static inline GLuint num32bit(size_t input)
  {
    return GLuint(input/sizeof(GLuint));
  }

  bool Sample::initScene()
  {
    { // Scene UBO
      newBuffer(buffers.scene_ubo);
      glNamedBufferDataEXT(buffers.scene_ubo, sizeof(SceneData) + sizeof(GLuint64), NULL, GL_DYNAMIC_DRAW);
    }

    { // Scene Geometry
      geometry::Mesh<Vertex>    sceneMesh;

      // we store all geometries in one big mesh, for sake of simplicity
      // and to allow standard MultiDrawIndirect to be efficient

      std::vector<Geometry>     geometries;
      for(int i = 0; i < 37; i++) {
        const int resmul = 2;
        mat4 identity;
        identity.identity();

        uint oldverts   = sceneMesh.getVerticesCount();
        uint oldindices = sceneMesh.getTriangleIndicesCount();

        switch(i % 2){
        case 0:
          geometry::Sphere<Vertex>::add(sceneMesh,identity,16*resmul,8*resmul);
          break;
        case 1:
          geometry::Box<Vertex>::add(sceneMesh,identity,8*resmul,8*resmul,8*resmul);
          break;
        }

        vec4 color(frand(),frand(),frand(),1.0f);
        for (uint v = oldverts; v < sceneMesh.getVerticesCount(); v++){
          sceneMesh.m_vertices[v].color = color;
        }

        Geometry geom;
        geom.firstIndex    = oldindices;
        geom.count         = sceneMesh.getTriangleIndicesCount() - oldindices;

        geometries.push_back(geom);
      }

      newBuffer(buffers.scene_ibo);
      glNamedBufferDataEXT(buffers.scene_ibo, sceneMesh.getTriangleIndicesSize(), &sceneMesh.m_indicesTriangles[0], GL_STATIC_DRAW);

      newBuffer(buffers.scene_vbo);
      glNamedBufferDataEXT(buffers.scene_vbo, sceneMesh.getVerticesSize(), &sceneMesh.m_vertices[0], GL_STATIC_DRAW);


      // Scene Objects
      std::vector<CullBbox>     bboxes;
      std::vector<int>          matrixIndex;

      CullBbox  bbox;
      bbox.min = vec4(-1,-1,-1,1);
      bbox.max = vec4(1,1,1,1);

      int obj = 0;
      for (int i = 0; i < grid * grid * grid; i++){

        vec3  pos(i % grid, (i / grid) % grid, i / (grid * grid));

        pos -=  vec3( grid/2, grid/2, grid/2);
        pos += (vec3(frand(),frand(),frand())*2.0f ) - vec3(1.0f);
        pos /=  float(grid);

        float scale;
        if ( nv_math::length(pos) < 0.52f ){
          scale = globalscale * 0.35f;
          pos *=  globalscale * 0.5f;
        }
        else{
          scale = globalscale;
          pos *=  globalscale;
        }

        mat4 matrix = 
          nv_math::translation_mat4( pos) *
          nv_math::rotation_mat4_y(frand()*nv_pi) *
          nv_math::scale_mat4( (vec3(scale) * (vec3(0.25f) + vec3(frand(),frand(),frand())*0.5f ))/float(grid) );

        sceneMatrices.push_back(matrix);
        sceneMatrices.push_back(nv_math::transpose(nv_math::invert(matrix)));
        matrixIndex.push_back(obj);

        // all have same bbox
        bboxes.push_back(bbox);

        DrawCmd cmd;
        cmd.count         = geometries[obj % geometries.size()].count;
        cmd.firstIndex    = geometries[obj % geometries.size()].firstIndex;
        cmd.baseVertex    = 0;
        cmd.baseInstance  = obj;
        cmd.instanceCount = 1;

        sceneCmds.push_back(cmd);
        obj++;
      }

      sceneMatricesAnimated.resize( sceneMatrices.size() );

      sceneVisBits.clear();
      sceneVisBits.resize( snapdiv(sceneCmds.size(),32), 0xFFFFFFFF );

      newBuffer(buffers.scene_indirect);
      glNamedBufferDataEXT(buffers.scene_indirect,sizeof(DrawCmd) * sceneCmds.size(), &sceneCmds[0], GL_STATIC_DRAW);

      newBuffer(buffers.scene_matrices);
      glNamedBufferDataEXT(buffers.scene_matrices, sizeof(mat4) * sceneMatrices.size(), &sceneMatrices[0], GL_STATIC_DRAW);
      newTexture(textures.scene_matrices);
      glTextureBufferEXT(textures.scene_matrices, GL_TEXTURE_BUFFER, GL_RGBA32F, buffers.scene_matrices);

      if (GLEW_ARB_bindless_texture){
        GLuint64 handle = glGetTextureHandleARB(textures.scene_matrices);
        glMakeTextureHandleResidentARB(handle);
        glNamedBufferSubDataEXT(buffers.scene_ubo, sizeof(SceneData), sizeof(GLuint64), &handle);
      }

      newBuffer(buffers.scene_bboxes);
      glNamedBufferDataEXT(buffers.scene_bboxes, sizeof(CullBbox) * bboxes.size(), &bboxes[0], GL_STATIC_DRAW);
      
      newBuffer(buffers.scene_matrixindices);
      glNamedBufferDataEXT(buffers.scene_matrixindices, sizeof(int) * matrixIndex.size(), &matrixIndex[0], GL_STATIC_DRAW);

      // for culling
      newBuffer(buffers.cull_indirect);
      glNamedBufferDataEXT(buffers.cull_indirect, sizeof(DrawCmd) * sceneCmds.size(), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_counter);
      glNamedBufferDataEXT(buffers.cull_counter, sizeof(int), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_output);
      glNamedBufferDataEXT(buffers.cull_output, snapdiv( sceneCmds.size(), 32) * 32 * sizeof(int), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_bits);
      glNamedBufferDataEXT(buffers.cull_bits, snapdiv( sceneCmds.size(), 32) * sizeof(int), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_bitsLast);
      glNamedBufferDataEXT(buffers.cull_bitsLast, snapdiv( sceneCmds.size(), 32) * sizeof(int), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_bitsReadback);
      glNamedBufferDataEXT(buffers.cull_bitsReadback, snapdiv( sceneCmds.size(), 32) * sizeof(int), NULL, GL_DYNAMIC_READ);

      // for command list

      if (bindlessVboUbo)
      {
        glGetNamedBufferParameterui64vNV(buffers.scene_ubo, GL_BUFFER_GPU_ADDRESS_NV, &addresses.scene_ubo);
        glMakeNamedBufferResidentNV(buffers.scene_ubo, GL_READ_ONLY);

        glGetNamedBufferParameterui64vNV(buffers.scene_vbo, GL_BUFFER_GPU_ADDRESS_NV, &addresses.scene_vbo);
        glMakeNamedBufferResidentNV(buffers.scene_vbo, GL_READ_ONLY);

        glGetNamedBufferParameterui64vNV(buffers.scene_ibo, GL_BUFFER_GPU_ADDRESS_NV, &addresses.scene_ibo);
        glMakeNamedBufferResidentNV(buffers.scene_ibo, GL_READ_ONLY);

        glGetNamedBufferParameterui64vNV(buffers.scene_matrixindices, GL_BUFFER_GPU_ADDRESS_NV, &addresses.scene_matrixindices);
        glMakeNamedBufferResidentNV(buffers.scene_matrixindices, GL_READ_ONLY);
      }

      std::vector<int>          tokenObjects;
      std::vector<GLuint>       tokenSizes;
      std::vector<GLuint>       tokenOffsets;
      size_t offset;
      {
        // default setup for the scene
        NVTokenUbo  ubo;
        ubo.setBuffer(buffers.scene_ubo, addresses.scene_ubo, 0, sizeof(SceneData)+sizeof(GLuint64));
        ubo.setBinding(UBO_SCENE, NVTOKEN_STAGE_VERTEX);

        offset = nvtokenEnqueue(tokenStream, ubo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ubo)));
        tokenOffsets. push_back(num32bit(offset));

        ubo.setBinding(UBO_SCENE, NVTOKEN_STAGE_FRAGMENT);

        offset = nvtokenEnqueue(tokenStream, ubo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ubo)));
        tokenOffsets. push_back(num32bit(offset));

        NVTokenVbo vbo;
        vbo.setBinding(0);
        vbo.setBuffer(buffers.scene_vbo, addresses.scene_vbo, 0);

        offset = nvtokenEnqueue(tokenStream, vbo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(vbo)));
        tokenOffsets. push_back(num32bit(offset));

        vbo.setBinding(1);
        vbo.setBuffer(buffers.scene_matrixindices, addresses.scene_matrixindices, 0);

        offset = nvtokenEnqueue(tokenStream, vbo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(vbo)));
        tokenOffsets. push_back(num32bit(offset));

        NVTokenIbo ibo;
        ibo.setBuffer(buffers.scene_ibo, addresses.scene_ibo);
        ibo.setType(GL_UNSIGNED_INT);

        offset = nvtokenEnqueue(tokenStream, ibo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ibo)));
        tokenOffsets. push_back(num32bit(offset));
      }

      for (size_t i = 0; i < sceneCmds.size(); i++){
        const DrawCmd& cmd = sceneCmds[i];

        // for commandlist token technique
        NVTokenDrawElemsInstanced drawtoken;
        drawtoken.cmd.baseInstance  = cmd.baseInstance;
        drawtoken.cmd.baseVertex    = cmd.baseVertex;
        drawtoken.cmd.firstIndex    = cmd.firstIndex;
        drawtoken.cmd.instanceCount = cmd.instanceCount;
        drawtoken.cmd.count         = cmd.count;
        drawtoken.cmd.mode          = GL_TRIANGLES;
        offset = nvtokenEnqueue(tokenStream,drawtoken);

        // In this simple case we have one token per "object",
        // but typically one would have multiple tokens (vbo,ibo...) per object
        // as well, hence the token culling code presented, accounts for the 
        // more generic use-case.
        tokenObjects. push_back(int(i));
        tokenSizes.   push_back(num32bit(sizeof(drawtoken)));
        tokenOffsets. push_back(num32bit(offset));
      }
      numTokens = GLuint(tokenSizes.size());
      // pad to multiple of 4
      while(tokenSizes.size() % 4)
      {
        tokenObjects.push_back(-1);
        tokenSizes.push_back(0);
        tokenOffsets.push_back(0);
      }

      tokenStreamCulled = tokenStream;

      newBuffer(buffers.scene_token);
      glNamedBufferDataEXT(buffers.scene_token, tokenStream.size(), &tokenStream[0], GL_STATIC_DRAW);

      // for command list culling

      newBuffer(buffers.scene_tokenSizes);
      glNamedBufferDataEXT(buffers.scene_tokenSizes, tokenSizes.size() * sizeof(GLuint), &tokenSizes[0], GL_STATIC_DRAW);

      newBuffer(buffers.scene_tokenOffsets);
      glNamedBufferDataEXT(buffers.scene_tokenOffsets, tokenOffsets.size() * sizeof(GLuint), &tokenOffsets[0], GL_STATIC_DRAW);

      newBuffer(buffers.scene_tokenObjects);
      glNamedBufferDataEXT(buffers.scene_tokenObjects, tokenObjects.size() * sizeof(GLint), &tokenObjects[0], GL_STATIC_DRAW);

      newBuffer(buffers.cull_token);
      glNamedBufferDataEXT(buffers.cull_token, tokenStream.size(), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_tokenEmulation); // only for emulation
      glNamedBufferDataEXT(buffers.cull_tokenEmulation, tokenStream.size(), NULL, GL_DYNAMIC_READ);

      newBuffer(buffers.cull_tokenSizes);
      glNamedBufferDataEXT(buffers.cull_tokenSizes, tokenSizes.size() * sizeof(GLuint), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_tokenScan);
      glNamedBufferDataEXT(buffers.cull_tokenScan, tokenSizes.size() * sizeof(GLuint), NULL, GL_DYNAMIC_COPY);

      newBuffer(buffers.cull_tokenScanOffsets);
      glNamedBufferDataEXT(buffers.cull_tokenScanOffsets, ScanSystem::getOffsetSize(GLuint(tokenSizes.size())), NULL, GL_DYNAMIC_COPY);
    }

    return true;
  }


  bool Sample::initFramebuffers(int width, int height)
  {

    newTexture(textures.scene_color);
    glBindTexture (GL_TEXTURE_2D, textures.scene_color);
    glTexStorage2D(GL_TEXTURE_2D, 1, fboFormat, width, height);

    int dim = width > height ? width : height;
    int levels = 0;
    while (dim){
      levels++;
      dim/=2;
    }

    newTexture(textures.scene_depthstencil);
    glBindTexture (GL_TEXTURE_2D, textures.scene_depthstencil);
    glTexStorage2D(GL_TEXTURE_2D, levels, GL_DEPTH24_STENCIL8, width, height);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    newFramebuffer(fbos.scene);
    glBindFramebuffer(GL_FRAMEBUFFER,     fbos.scene);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D, textures.scene_color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, textures.scene_depthstencil, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
  }

  void Sample::initCullingJob(CullingSystem::Job& cullJob)
  {
    cullJob.m_numObjects = (int)sceneCmds.size();

    cullJob.m_bufferMatrices        = CullingSystem::Buffer(buffers.scene_matrices);
    cullJob.m_bufferObjectMatrix    = CullingSystem::Buffer(buffers.scene_matrixindices);
    cullJob.m_bufferObjectBbox      = CullingSystem::Buffer(buffers.scene_bboxes);
    
    cullJob.m_textureDepthWithMipmaps = textures.scene_depthstencil;

    cullJob.m_bufferVisOutput       = CullingSystem::Buffer(buffers.cull_output);

    cullJob.m_bufferVisBitsCurrent  = CullingSystem::Buffer(buffers.cull_bits);
    cullJob.m_bufferVisBitsLast     = CullingSystem::Buffer(buffers.cull_bitsLast);
  }

  bool Sample::begin()
  {
    statsPrint = false;

    TwInit(TW_OPENGL_CORE,NULL);
    TwWindowSize(m_window.m_viewsize[0],m_window.m_viewsize[1]);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    cmdlistNative   = !!init_NV_command_list(NVPWindow::sysGetProcAddress);
    bindlessVboUbo  = GLEW_NV_vertex_buffer_unified_memory && NVPWindow::sysExtensionSupported("GL_NV_uniform_buffer_unified_memory");
    nvtokenInitInternals(cmdlistNative, bindlessVboUbo);

    bool validated(true);

    GLuint defaultVAO;
    glGenVertexArrays(1, &defaultVAO);
    glBindVertexArray(defaultVAO);

    validated = validated && initProgram();
    validated = validated && initScene();
    validated = validated && initFramebuffers(m_window.m_viewsize[0],m_window.m_viewsize[1]);

    if (!validated) return false;

    {
      ScanSystem::Programs scanprograms;
      getScanPrograms(scanprograms);
      s_scanSys.init(scanprograms);
    }

    {
      CullingSystem::Programs cullprograms;
      getCullPrograms(cullprograms);
      cullSys.init( cullprograms, false );

      initCullingJob(cullJobReadback);
      cullJobReadback.m_bufferVisBitsReadback = CullingSystem::Buffer(buffers.cull_bitsReadback);

      initCullingJob(cullJobIndirect);
      cullJobIndirect.m_program_indirect_compact = progManager.get( programs.indirect_unordered );
      cullJobIndirect.m_bufferObjectIndirects = CullingSystem::Buffer(buffers.scene_indirect);
      cullJobIndirect.m_bufferIndirectCounter = CullingSystem::Buffer(buffers.cull_counter);
      cullJobIndirect.m_bufferIndirectResult  = CullingSystem::Buffer(buffers.cull_indirect);

      initCullingJob(cullJobToken);
      cullJobToken.program_cmds   = progManager.get( programs.token_cmds );
      cullJobToken.program_sizes  = progManager.get( programs.token_sizes );
      cullJobToken.numTokens      = numTokens;

      // if we had multiple stateobjects, we would be using multiple sequences
      // where each sequence covers the token range per stateobject
      CullJobToken::Sequence sequence;
      sequence.first = 0;
      sequence.num   = numTokens;
      sequence.offset = 0;
      sequence.endoffset = GLuint(tokenStream.size()/sizeof(GLuint));
      cullJobToken.sequences.push_back(sequence);


      cullJobToken.tokenOrig    = ScanSystem::Buffer(buffers.scene_token);
      cullJobToken.tokenObjects = ScanSystem::Buffer(buffers.scene_tokenObjects);
      cullJobToken.tokenOffsets = ScanSystem::Buffer(buffers.scene_tokenOffsets);
      cullJobToken.tokenSizes   = ScanSystem::Buffer(buffers.scene_tokenSizes);

      cullJobToken.tokenOut           = ScanSystem::Buffer(buffers.cull_token);
      cullJobToken.tokenOutSizes      = ScanSystem::Buffer(buffers.cull_tokenSizes);
      cullJobToken.tokenOutScan       = ScanSystem::Buffer(buffers.cull_tokenScan);
      cullJobToken.tokenOutScanOffset = ScanSystem::Buffer(buffers.cull_tokenScanOffsets);
    }


    TwBar *bar = TwNewBar("mainbar");
    TwDefine(" GLOBAL contained=true help='OpenGL samples.\nCopyright NVIDIA Corporation 2013-2014' ");
    TwDefine(" mainbar position='0 0' size='250 140' color='0 0 0' alpha=128 valueswidth=fit ");
    TwDefine((std::string(" mainbar label='") + PROJECT_NAME + "'").c_str());

    TwEnumVal enumVals[] = {
      {CullingSystem::METHOD_FRUSTUM,"frustum"},
      {CullingSystem::METHOD_HIZ,"hiz"},
      {CullingSystem::METHOD_RASTER,"raster"},
    };
    TwType algorithmType = TwDefineEnum("algorithm", enumVals, sizeof(enumVals)/sizeof(enumVals[0]));

    TwEnumVal enumVals2[] = {
      {RESULT_REGULAR_CURRENT,"regular current frame"},
      {RESULT_REGULAR_LASTFRAME,"regular last frame"},
      {RESULT_TEMPORAL_CURRENT,"temporal current frame"},
    };
    TwType resultType = TwDefineEnum("result", enumVals2, sizeof(enumVals2)/sizeof(enumVals2[0]));

    TwEnumVal enumVals3[] = {
      {DRAW_STANDARD,"standard CPU"},
      {DRAW_MULTIDRAWINDIRECT,"MultiDrawIndirect GPU"},
      {DRAW_TOKENBUFFER_EMULATION,"nvcmdlist emulation"},
      {DRAW_TOKENBUFFER,"nvcmdlist GPU"},
    };
    TwType drawType = TwDefineEnum("draw", enumVals3, cmdlistNative ? 4 : 3);

    TwAddVarRW(bar, "animate", TW_TYPE_FLOAT,  &tweak.animate, " label='turn speed' ");
    TwAddVarRW(bar, "culling", TW_TYPE_BOOLCPP,  &tweak.culling, " label='culling' ");
    TwAddVarRW(bar, "freeze", TW_TYPE_BOOLCPP,  &tweak.freeze, " label='freeze result' ");
    TwAddVarRW(bar, "algorithm",  algorithmType, &tweak.method, " label='algorithm' ");
    TwAddVarRW(bar, "result",  resultType, &tweak.result, " label='result' ");
    TwAddVarRW(bar, "drawmode", drawType, &tweak.drawmode, " label='drawmode' ");

    m_control.m_sceneOrbit = vec3(0.0f);
    m_control.m_sceneDimension = float(globalscale) * 2.0f;
    float dist = m_control.m_sceneDimension * 0.75f;
    m_control.m_viewMatrix = nv_math::look_at(m_control.m_sceneOrbit - normalize(vec3(1,0,-1))*dist, m_control.m_sceneOrbit, vec3(0,1,0));

    statsTime = NVPWindow::sysGetTime();

    return validated;
  }
  
  void Sample::CullJobToken::resultFromBits( const CullingSystem::Buffer& bufferVisBitsCurrent )
  {
    // First we compute sizes based on culling result
    // it generates an output stream where size[token] is either 0 or original size
    // depending on which object the token belonged to.
    glUseProgram(program_sizes);

    glBindBuffer(GL_ARRAY_BUFFER, tokenSizes.buffer);
    glVertexAttribIPointer(0,1,GL_UNSIGNED_INT,0,(const void*)tokenSizes.offset);
    glBindBuffer(GL_ARRAY_BUFFER, tokenObjects.buffer);
    glVertexAttribIPointer(1,1,GL_INT,0,(const void*)tokenObjects.offset);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);

    tokenOutSizes.BindBufferRange(GL_SHADER_STORAGE_BUFFER,0);
    bufferVisBitsCurrent.BindBufferRange(GL_SHADER_STORAGE_BUFFER,1);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    glEnable(GL_RASTERIZER_DISCARD);
    glDrawArrays(GL_POINTS,0,numTokens);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    // now let the scan system compute the running offsets for the visible tokens
    // that way we get a compact token stream with the original ordering back

    s_scanSys.scanData(((numTokens+3)/4)*4,tokenOutSizes,tokenOutScan,tokenOutScanOffset);

    // finally we build the actual culled tokenbuffer, using those offsets

    glUseProgram(program_cmds);

    glBindBuffer(GL_ARRAY_BUFFER, tokenOffsets.buffer);
    glVertexAttribIPointer(0,1,GL_UNSIGNED_INT,0,(const void*)tokenOffsets.offset);
    glBindBuffer(GL_ARRAY_BUFFER, tokenOutSizes.buffer);
    glVertexAttribIPointer(1,1,GL_UNSIGNED_INT,0,(const void*)tokenOutSizes.offset);
    glBindBuffer(GL_ARRAY_BUFFER, tokenOutScan.buffer);
    glVertexAttribIPointer(2,1,GL_UNSIGNED_INT,0,(const void*)tokenOutScan.offset);

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);

    tokenOut.BindBufferRange(GL_SHADER_STORAGE_BUFFER,0);
    tokenOrig.BindBufferRange(GL_SHADER_STORAGE_BUFFER,1);
    tokenOutSizes.BindBufferRange(GL_SHADER_STORAGE_BUFFER,2);
    tokenOutScan.BindBufferRange(GL_SHADER_STORAGE_BUFFER,3);
    tokenOutScanOffset.BindBufferRange(GL_SHADER_STORAGE_BUFFER,4);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);

    for (size_t i = 0; i < sequences.size(); i++){
      const Sequence& sequence = sequences[i];

      glUniform1ui(glGetUniformLocation(program_cmds,"startOffset"),sequence.offset);
      glUniform1i (glGetUniformLocation(program_cmds,"startID"),    sequence.first);
      glUniform1ui(glGetUniformLocation(program_cmds,"endOffset"),  sequence.endoffset);
      glUniform1i (glGetUniformLocation(program_cmds,"endID"),      sequence.first + sequence.num - 1);
      glUniform1ui(glGetUniformLocation(program_cmds,"terminateCmd"),nvtoken::s_nvcmdlist_header[GL_TERMINATE_SEQUENCE_COMMAND_NV]);
      glDrawArrays(GL_POINTS,sequence.first,sequence.num);
    }
    

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);

    glBindBuffer(GL_ARRAY_BUFFER,0);

    for (GLuint i = 0; i < 5; i++){
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,0);
    }

    glDisable(GL_RASTERIZER_DISCARD);
  }
  
  void Sample::systemChange()
  {
    // clear last visibles to 0
    glBindBuffer(GL_COPY_WRITE_BUFFER, buffers.cull_bitsLast);
    glClearBufferData(GL_COPY_WRITE_BUFFER, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    // current are all visible
    memset(&sceneVisBits[0],0xFFFFFFFF,sizeof(int) * sceneVisBits.size() );
  }

  void Sample::drawScene(bool depthonly, const char* what)
  {
    NV_PROFILE_SECTION(what);

    if (depthonly){
      glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    }

    // need to set here, as culling also modifies vertex format state
    glVertexAttribFormat(VERTEX_POS,    3, GL_FLOAT, GL_FALSE,  offsetof(Vertex,position));
    glVertexAttribFormat(VERTEX_NORMAL, 3, GL_FLOAT, GL_FALSE,  offsetof(Vertex,normal));
    glVertexAttribFormat(VERTEX_COLOR,  4, GL_FLOAT, GL_FALSE,  offsetof(Vertex,color));
    glVertexAttribBinding(VERTEX_POS,   0);
    glVertexAttribBinding(VERTEX_NORMAL,0);
    glVertexAttribBinding(VERTEX_COLOR, 0);

    glVertexAttribIFormat(VERTEX_MATRIXINDEX, 1, GL_INT, 0);
    glVertexAttribBinding(VERTEX_MATRIXINDEX, 1);
    glVertexBindingDivisor(1, 1);

    glEnableVertexAttribArray(VERTEX_POS);
    glEnableVertexAttribArray(VERTEX_NORMAL);
    glEnableVertexAttribArray(VERTEX_COLOR);

    glEnableVertexAttribArray(VERTEX_MATRIXINDEX);

    glUseProgram(progManager.get(programs.draw_scene));

    // these bindings are replicated in the tokenbuffer as well
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SCENE, buffers.scene_ubo);
    glBindVertexBuffer(0,buffers.scene_vbo,0,sizeof(Vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.scene_ibo);
    glBindVertexBuffer(1,buffers.scene_matrixindices,0,sizeof(GLint));


    if (!GLEW_ARB_bindless_texture){
      glActiveTexture(GL_TEXTURE0 + TEX_MATRICES);
      glBindTexture(GL_TEXTURE_BUFFER,textures.scene_matrices);
    }

    if (tweak.drawmode == DRAW_MULTIDRAWINDIRECT){
      glBindBuffer(GL_DRAW_INDIRECT_BUFFER, tweak.culling ? buffers.cull_indirect : buffers.scene_indirect );
      if (tweak.culling){
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
      }
      glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, NULL, (GLsizei)sceneCmds.size(), 0);
      glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    }
    else if (tweak.drawmode == DRAW_TOKENBUFFER || tweak.drawmode == DRAW_TOKENBUFFER_EMULATION)
    {
      if (bindlessVboUbo){
        glEnableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
        glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
        glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
      }
      if (tweak.culling){
        if (tweak.drawmode == DRAW_TOKENBUFFER_EMULATION){
          NV_PROFILE_SECTION("Read");
          cullJobToken.tokenOut.GetNamedBufferSubData(&tokenStreamCulled[0]);
        }
        else{
          glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
        }

        NV_PROFILE_SPLIT();
      }

      GLintptr offset = 0;
      GLsizei  size   = GLsizei(tokenStream.size());
      if (tweak.drawmode == DRAW_TOKENBUFFER_EMULATION){
        StateSystem::State state;
        state.vertexformat.bindings[0].stride = sizeof(Vertex);
        state.vertexformat.bindings[1].stride = sizeof(GLint);

        const std::string& stream = tweak.culling ? tokenStreamCulled : tokenStream;

        nvtokenDrawCommandsSW(GL_TRIANGLES, &stream[0], stream.size(), &offset, &size, 1, state);
      }
      else{
        glDrawCommandsNV(GL_TRIANGLES, tweak.culling ? buffers.cull_token : buffers.scene_token, &offset, &size, 1);
      }
      
      if (bindlessVboUbo){
        glDisableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
        glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
        glDisableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
      }

    }
    else{
      int visible = 0;
      for (size_t i = 0; i < sceneCmds.size(); i++)
      {
        if (sceneVisBits[i / 32] & (1<< (i%32)) ){
          glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, &sceneCmds[i] );
          visible++;
        }
      }
      if (statsPrint){
        printf("%s visible: %d pct\n", what, (visible * 100) / sceneCmds.size() );
      }
    }

    glDisableVertexAttribArray(VERTEX_POS);
    glDisableVertexAttribArray(VERTEX_NORMAL);
    glDisableVertexAttribArray(VERTEX_COLOR);
    glDisableVertexAttribArray(VERTEX_MATRIXINDEX);
    glVertexBindingDivisor(1, 0);

    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SCENE, 0);
    glBindVertexBuffer(0,0,0,0);
    glBindVertexBuffer(1,0,0,0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    if (!GLEW_ARB_bindless_texture){
      glActiveTexture(GL_TEXTURE0 + TEX_MATRICES);
      glBindTexture(GL_TEXTURE_BUFFER,0);
    }

    if (depthonly){
      glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }
  }

#define CULL_TEMPORAL_NOFRUSTUM 1

  void Sample::drawCullingTemporal(CullingSystem::Job& cullJob)
  {
    CullingSystem::View view;
    view.viewPos = sceneUbo.viewPos.get_value();
    view.viewDir = sceneUbo.viewDir.get_value();
    view.viewProjMatrix = sceneUbo.viewProjMatrix.get_value();

    switch(tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
    {
      // kinda pointless to use temporal ;)
      {
        NV_PROFILE_SECTION("CullF");
        cullSys.buildOutput( tweak.method, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
        cullSys.resultFromBits( cullJob );
      }

      drawScene(false,"Scene");
    }
    break;
    case CullingSystem::METHOD_HIZ:
    {
      {
        NV_PROFILE_SECTION("CullF");
#if CULL_TEMPORAL_NOFRUSTUM
        cullSys.resultFromBits( cullJob );
        cullSys.swapBits( cullJob );  // last/output
#else
        cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_LAST );
        cullSys.resultFromBits( cullJob );
#endif
      }

      drawScene(false,"Last");

      // changes FBO binding
      cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_window.m_viewsize[0], m_window.m_viewsize[1]);

      {
        NV_PROFILE_SECTION("CullH");
        cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );

        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_NOT_LAST );
        cullSys.resultFromBits( cullJob );

        // for next frame
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
#if !CULL_TEMPORAL_NOFRUSTUM
        cullSys.swapBits( cullJob );  // last/output
#endif
      }

      glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene );
      drawScene(false,"New");
    }
    break;
    case CullingSystem::METHOD_RASTER:
    {
      {
        NV_PROFILE_SECTION("CullF");
#if CULL_TEMPORAL_NOFRUSTUM
        cullSys.resultFromBits( cullJob );
        cullSys.swapBits( cullJob );  // last/output
#else
        cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_LAST );
        cullSys.resultFromBits( cullJob );
#endif
      }

      drawScene(false,"Last");

      {
        NV_PROFILE_SECTION("CullR");
        cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_NOT_LAST );
        cullSys.resultFromBits( cullJob );

        // for next frame
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
#if !CULL_TEMPORAL_NOFRUSTUM
        cullSys.swapBits( cullJob );  // last/output
#endif
      }

      drawScene(false,"New");
    }
    break;
    }
  }

  void Sample::drawCullingRegular(CullingSystem::Job& cullJob)
  {
    CullingSystem::View view;
    view.viewPos = sceneUbo.viewPos.get_value();
    view.viewDir = sceneUbo.viewDir.get_value();
    view.viewProjMatrix = sceneUbo.viewProjMatrix.get_value();

    switch(tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
      {
        {
          NV_PROFILE_SECTION("CullF");
          cullSys.buildOutput( tweak.method, cullJob, view );
          cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          cullSys.resultFromBits( cullJob );
        }

        drawScene(false,"Scene");
      }
      break;
    case CullingSystem::METHOD_HIZ:
      {
        {
          NV_PROFILE_SECTION("CullF");
          cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
          cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          cullSys.resultFromBits( cullJob );
        }

        drawScene(true,"Depth");

        {
          NV_PROFILE_SECTION("Mip");
          // changes FBO binding
          cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_window.m_viewsize[0], m_window.m_viewsize[1]);
        }


        {
          NV_PROFILE_SECTION("CullH");
          cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );
          cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          cullSys.resultFromBits( cullJob );
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene );
        drawScene(false,"Scene");
      }
      break;
    case CullingSystem::METHOD_RASTER:
      {
        {
          NV_PROFILE_SECTION("CullF");
          cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
          cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          cullSys.resultFromBits( cullJob );
        }

        drawScene(true,"Depth");
        

        {
          NV_PROFILE_SECTION("CullR");
          cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
          cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          cullSys.resultFromBits( cullJob );
        }

        drawScene(false,"Scene");
      }
      break;
    }
  }

  void Sample::drawCullingRegularLastFrame(CullingSystem::Job& cullJob)
  {
    CullingSystem::View view;
    view.viewPos = sceneUbo.viewPos.get_value();
    view.viewDir = sceneUbo.viewDir.get_value();
    view.viewProjMatrix = sceneUbo.viewProjMatrix.get_value();

    switch(tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
    {
      {
        NV_PROFILE_SECTION("Result");
        cullSys.resultFromBits( cullJob );
      }

      drawScene(false,"Scene");

      {
        NV_PROFILE_SECTION("CullF");
        cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
      }
    }
    break;
    case CullingSystem::METHOD_HIZ:
    {

      {
        NV_PROFILE_SECTION("Result");
        cullSys.resultFromBits( cullJob );
      }

      drawScene(false,"Scene");

      {
        NV_PROFILE_SECTION("Mip");
        // changes FBO binding
        cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_window.m_viewsize[0], m_window.m_viewsize[1]);
      }

      {
        NV_PROFILE_SECTION("Cull");
        cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
      }
    }
    break;
    case CullingSystem::METHOD_RASTER:
    {
      {
        NV_PROFILE_SECTION("Result");
        cullSys.resultFromBits( cullJob );
      }

      drawScene(false,"Scene");
      
      {
        NV_PROFILE_SECTION("Cull");
        cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
        cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
      }
    }
    break;
    }
  }

  void Sample::think(double time)
  {
    m_control.processActions(m_window.m_viewsize,
      nv_math::vec2f(m_window.m_mouseCurrent[0],m_window.m_mouseCurrent[1]),
      m_window.m_mouseButtonFlags, m_window.m_wheel);

    if (m_window.onPress(KEY_R)){
      progManager.reloadPrograms();

      ScanSystem::Programs scanprograms;
      getScanPrograms(scanprograms);
      s_scanSys.update(scanprograms);

      CullingSystem::Programs cullprograms;
      getCullPrograms(cullprograms);
      cullSys.update( cullprograms, false );
      cullJobIndirect.m_program_indirect_compact = progManager.get( programs.indirect_unordered );
      cullJobToken.program_cmds  = progManager.get( programs.token_cmds );
      cullJobToken.program_sizes = progManager.get( programs.token_sizes );
    }

    if (!progManager.areProgramsValid()){
      waitEvents();
      return;
    }

    if ( memcmp(&tweak,&tweakLast,sizeof(Tweak)) != 0 && tweak.freeze == tweakLast.freeze) {
      systemChange();
      tweak.freeze = false;
    }
    if (!tweak.culling || tweak.result == RESULT_TEMPORAL_CURRENT){
      tweak.freeze = false;
    }

#if _DEBUG
    if (tweak.drawmode != tweakLast.drawmode && GLEW_NV_shader_buffer_load){
      // disable a few expected but annoying debug messages for NVIDIA
      if (tweakLast.drawmode == DRAW_TOKENBUFFER_EMULATION || tweakLast.drawmode == DRAW_TOKENBUFFER){
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
      }
      if (tweak.drawmode == DRAW_TOKENBUFFER_EMULATION || tweak.drawmode == DRAW_TOKENBUFFER){
        GLuint msgid = 65537; // residency warning
        glDebugMessageControlARB(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, &msgid, GL_FALSE);
        msgid = 131186;       // emulation does GetData on buffer
        glDebugMessageControlARB(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_PERFORMANCE_ARB, GL_DONT_CARE, 1, &msgid, GL_FALSE);
      }
    }
#endif

    if ( tweak.drawmode == DRAW_STANDARD && (NVPWindow::sysGetTime() - statsTime) > 2.0){
      statsTime = NVPWindow::sysGetTime();
      statsPrint = true;
    }
    else{
      statsPrint = false;
    }

    int width   = m_window.m_viewsize[0];
    int height  = m_window.m_viewsize[1];

    {
      glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene);
      glViewport(0, 0, width, height);
      glClearColor(1.0,1.0,1.0,1.0);
      glClearDepth(1.0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);


      { // Update UBO
        nv_math::mat4 projection = nv_math::perspective((45.f), float(width)/float(height), 0.1f, 100.0f);
        nv_math::mat4 view = m_control.m_viewMatrix;

        sceneUbo.viewProjMatrix = projection * view;
        sceneUbo.viewMatrix = view;
        sceneUbo.viewMatrixIT = nv_math::transpose(nv_math::invert(view));

        sceneUbo.viewPos = -view.col(3);
        sceneUbo.viewDir = -view.row(2);

        glBindBuffer(GL_UNIFORM_BUFFER, buffers.scene_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneData), &sceneUbo);
      }

    }

    if (tweak.animate)
    {
      mat4 rotator = nv_math::rotation_mat4_y( float(time)*0.1f * tweak.animate);

      for (size_t i = 0; i < sceneMatrices.size()/2; i++){
        mat4 changed = rotator * sceneMatrices[i*2 + 0];
        sceneMatricesAnimated[i*2 + 0] = changed;
        sceneMatricesAnimated[i*2 + 1] = nv_math::transpose(nv_math::invert(changed));
      }

      glNamedBufferSubDataEXT(buffers.scene_matrices,0,sizeof(mat4)*sceneMatricesAnimated.size(), &sceneMatricesAnimated[0] );
    }


    if (tweak.culling && !tweak.freeze) {

      cullJobReadback.m_hostVisBits = &sceneVisBits[0];

      // We change the output buffer for token emulation, as once the driver sees frequent readbacks on buffers
      // it moves the allocation to read-friendly memory. This would be bad for the native tokenbuffer.
      cullJobToken.tokenOut.buffer = (tweak.drawmode == DRAW_TOKENBUFFER_EMULATION ? buffers.cull_tokenEmulation : buffers.cull_token);

      CullingSystem::Job&  cullJob = tweak.drawmode == DRAW_STANDARD ? (CullingSystem::Job&)cullJobReadback : 
        (tweak.drawmode == DRAW_MULTIDRAWINDIRECT ? (CullingSystem::Job&)cullJobIndirect : (CullingSystem::Job&)cullJobToken);
      
      switch(tweak.result)
      {
      case RESULT_REGULAR_CURRENT:
        drawCullingRegular(cullJob);
        break;
      case RESULT_REGULAR_LASTFRAME:
        drawCullingRegularLastFrame(cullJob);
        break;
      case RESULT_TEMPORAL_CURRENT:
        drawCullingTemporal(cullJob);
        break;
      }
    }
    else{
      drawScene(false,"Draw");
    }

    if (statsPrint){
      printf("\n");
    }


    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,
      0,0,width,height,GL_COLOR_BUFFER_BIT, GL_NEAREST);

    tweakLast = tweak;

    {
      NV_PROFILE_SECTION("TwDraw");
      TwDraw();
    }
  }

  void Sample::resize(int width, int height)
  {
    TwWindowSize(width,height);
    initFramebuffers(width,height);
  }

}

using namespace ocull;

int sample_main(int argc, const char** argv)
{
  Sample sample;
  return sample.run(
    PROJECT_NAME,
    argc, argv,
    SAMPLE_SIZE_WIDTH, SAMPLE_SIZE_HEIGHT,
    SAMPLE_MAJOR_VERSION, SAMPLE_MINOR_VERSION);
}

void sample_print(int level, const char * fmt)
{

}

