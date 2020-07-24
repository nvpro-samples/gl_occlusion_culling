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

#include <nvgl/extensions_gl.hpp>

#include <imgui/imgui_helper.h>
#include <imgui/imgui_impl_gl.h>

#include <nvmath/nvmath_glsltypes.h>

#include <nvh/geometry.hpp>
#include <nvh/misc.hpp>
#include <nvh/cameracontrol.hpp>

#include <nvgl/appwindowprofiler_gl.hpp>
#include <nvgl/error_gl.hpp>
#include <nvgl/programmanager_gl.hpp>
#include <nvgl/base_gl.hpp>

#include <vector>

#include "cullingsystem.hpp"

#define NVTOKEN_NO_STATESYSTEM
#include "nvtoken.hpp"
using namespace nvtoken;

#include "scansystem.hpp"

#include "common.h"

namespace ocull
{
  int const SAMPLE_SIZE_WIDTH(800);
  int const SAMPLE_SIZE_HEIGHT(600);
  int const SAMPLE_MAJOR_VERSION(4);
  int const SAMPLE_MINOR_VERSION(5);

  static const GLenum     fboFormat = GL_RGBA16F;
  static const int        grid = 26;
  static const float      globalscale = 8;

  static ScanSystem       s_scanSys;

  class Sample : public nvgl::AppWindowProfilerGL
  {
  public:
    static int const CYCLIC_FRAMES = 2;

    enum GuiEnums {
      GUI_ALGORITHM,
      GUI_RESULT,
      GUI_DRAW,
    };

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
      nvgl::ProgramID
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
      GLuint scene = 0;
    } fbos;

    struct {
      GLuint scene_ubo = 0;
      GLuint scene_vbo = 0;
      GLuint scene_ibo = 0;
      GLuint scene_matrices = 0;
      GLuint scene_bboxes = 0;
      GLuint scene_matrixindices = 0;
      GLuint scene_indirect = 0;
      
      GLuint scene_token = 0;
      GLuint scene_tokenSizes = 0;
      GLuint scene_tokenOffsets = 0;
      GLuint scene_tokenObjects = 0;
      
      GLuint cull_output = 0;
      GLuint cull_bits = 0;
      GLuint cull_bitsLast = 0;
      GLuint cull_bitsReadback[CYCLIC_FRAMES] = {0};
      GLuint cull_indirect = 0;
      GLuint cull_counter = 0;
      
      GLuint cull_token = 0;
      GLuint cull_tokenEmulation = 0;
      GLuint cull_tokenSizes = 0;
      GLuint cull_tokenScan = 0;
      GLuint cull_tokenScanOffsets = 0;
    } buffers;

    struct {
      GLuint64    
        scene_ubo,
        scene_ibo,
        scene_vbo,
        scene_matrixindices;
    } addresses;

    struct {
      GLuint  scene_color = 0;
      GLuint  scene_depthstencil = 0;
      GLuint  scene_matrices = 0;
    } textures;

    struct DrawCmd {
      GLuint count;
      GLuint instanceCount;
      GLuint firstIndex;
      GLint  baseVertex;
      GLuint baseInstance;
    };

    struct CullBbox {
      nvmath::vec4  min;
      nvmath::vec4  max;
    };

    struct Geometry {
      GLuint firstIndex;
      GLuint count;
    };

    struct Vertex {

      Vertex(const nvh::geometry::Vertex& vertex){
        position  = vertex.position;
        normal    = vertex.normal;
        color     = nvmath::vec4(1.0f);
      }

      nvmath::vec4   position;
      nvmath::vec4   normal;
      nvmath::vec4   color;
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
      CullingSystem::MethodType method        = CullingSystem::METHOD_RASTER;
      ResultType                result        = RESULT_REGULAR_CURRENT;
      DrawModes                 drawmode      = DRAW_STANDARD;
      bool                      culling       = false;
      bool                      freeze        = false;
      float                     minPixelSize  = 0.0f;
      float                     animate       = 0;
      float                     animateOffset = 0;
      bool                      noui          = false;
    };

    nvgl::ProgramManager  m_progManager;
    nvh::CameraControl    m_control;

    Tweak           m_tweak;
    Tweak           m_tweakLast;

    ImGuiH::Registry            m_ui;
    double                      m_uiTime;

    SceneData                   m_sceneUbo;

    std::vector<uint32_t>       m_sceneVisBits;
    std::vector<DrawCmd>        m_sceneCmds;
    std::vector<nvmath::mat4f> m_sceneMatrices;
    std::vector<nvmath::mat4f> m_sceneMatricesAnimated;

    GLuint                      m_numTokens;
    std::string                 m_tokenStream;
    std::string                 m_tokenStreamCulled;

    
    double      m_statsTime;
    bool        m_statsPrint;
    bool        m_cmdlistNative;
    bool        m_bindlessVboUbo;
    uint32_t                              m_cullFrameCycle;

    CullingSystem                         m_cullSys;
    CullingSystem::JobReadbackPersistent  m_cullJobReadback;
    CullingSystem::JobIndirectUnordered   m_cullJobIndirect;
    CullJobToken                          m_cullJobToken;
    CullingSystem::Buffer                 m_cullReadbackBuffers[CYCLIC_FRAMES];
    void*                                 m_cullReadbackMappings[CYCLIC_FRAMES];

    bool begin();
    void processUI(double time);
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


    void end() {
      ImGui::ShutdownGL();
    }
    // return true to prevent m_windowState updates
    bool mouse_pos(int x, int y) {
      return ImGuiH::mouse_pos(x, y);
    }
    bool mouse_button(int button, int action) {
      return ImGuiH::mouse_button(button, action);
    }
    bool mouse_wheel(int wheel) {
      return ImGuiH::mouse_wheel(wheel);
    }
    bool key_char(int button) {
      return ImGuiH::key_char(button);
    }
    bool key_button(int button, int action, int mods) {
      return ImGuiH::key_button(button, action, mods);
    }

    Sample() {
      m_parameterList.add("method", (int32_t*)&m_tweak.method);
      m_parameterList.add("drawmode", (int32_t*)&m_tweak.drawmode);
      m_parameterList.add("result", (int32_t*)&m_tweak.result);
      m_parameterList.add("animate", &m_tweak.animate);
      m_parameterList.add("culling", &m_tweak.culling);
      m_parameterList.add("noui", &m_tweak.noui, true);
      m_parameterList.add("minpixelsize", &m_tweak.minPixelSize);
      m_parameterList.add("animateoffset", &m_tweak.animateOffset);
    }
  };

  bool Sample::initProgram()
  {
    bool validated(true);
    m_progManager.m_filetype = nvh::ShaderFileManager::FILETYPE_GLSL;
    m_progManager.addDirectory( std::string("GLSL_" PROJECT_NAME));
    m_progManager.addDirectory( exePath() + std::string(PROJECT_RELDIRECTORY));

    m_progManager.registerInclude("common.h");
    m_progManager.registerInclude("noise.glsl");

    programs.draw_scene = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,   "scene.vert.glsl"),
      nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "scene.frag.glsl"));

    programs.object_raster = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,   "cull-raster.vert.glsl"),
      nvgl::ProgramManager::Definition(GL_GEOMETRY_SHADER, "cull-raster.geo.glsl"),
      nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "cull-raster.frag.glsl"));

    programs.object_frustum = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,  "cull-basic.vert.glsl"));

    programs.object_hiz = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,  "#define OCCLUSION\n", "cull-basic.vert.glsl"));

    programs.bit_regular = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL 0\n", "cull-bitpack.vert.glsl"));
    programs.bit_temporallast = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL TEMPORAL_LAST\n", "cull-bitpack.vert.glsl"));
    programs.bit_temporalnew = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,  "#define TEMPORAL TEMPORAL_NEW\n", "cull-bitpack.vert.glsl"));

    programs.indirect_unordered = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "cull-indirectunordered.vert.glsl"));

    programs.depth_mips = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER,   "cull-downsample.vert.glsl"),
      nvgl::ProgramManager::Definition(GL_FRAGMENT_SHADER, "cull-downsample.frag.glsl"));

    programs.token_sizes = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "cull-tokensizes.vert.glsl"));
    programs.token_cmds = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_VERTEX_SHADER, "cull-tokencmds.vert.glsl"));

    programs.scan_prefixsum = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_SUM\n", "scan.comp.glsl"));
    programs.scan_offsets = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_OFFSETS\n", "scan.comp.glsl"));
    programs.scan_combine = m_progManager.createProgram(
      nvgl::ProgramManager::Definition(GL_COMPUTE_SHADER,  "#define TASK TASK_COMBINE\n", "scan.comp.glsl"));

    validated = m_progManager.areProgramsValid();

    return validated;
  }

  void Sample::getScanPrograms( ScanSystem::Programs &scanprograms )
  {
    scanprograms.prefixsum  = m_progManager.get( programs.scan_prefixsum );
    scanprograms.offsets    = m_progManager.get( programs.scan_offsets );
    scanprograms.combine    = m_progManager.get( programs.scan_combine );
  }

  void Sample::getCullPrograms( CullingSystem::Programs &cullprograms )
  {
    cullprograms.bit_regular      = m_progManager.get( programs.bit_regular );
    cullprograms.bit_temporallast = m_progManager.get( programs.bit_temporallast );
    cullprograms.bit_temporalnew  = m_progManager.get( programs.bit_temporalnew );
    cullprograms.depth_mips       = m_progManager.get( programs.depth_mips );
    cullprograms.object_frustum   = m_progManager.get( programs.object_frustum );
    cullprograms.object_hiz       = m_progManager.get( programs.object_hiz );
    cullprograms.object_raster    = m_progManager.get( programs.object_raster );
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
      nvgl::newBuffer(buffers.scene_ubo);
      glNamedBufferData(buffers.scene_ubo, sizeof(SceneData) + sizeof(GLuint64), NULL, GL_DYNAMIC_DRAW);
    }

    { // Scene Geometry
      nvh::geometry::Mesh<Vertex>    sceneMesh;

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
          nvh::geometry::Sphere<Vertex>::add(sceneMesh,identity,16*resmul,8*resmul);
          break;
        case 1:
          nvh::geometry::Box<Vertex>::add(sceneMesh,identity,8*resmul,8*resmul,8*resmul);
          break;
        }

        vec4 color(nvh::frand(),nvh::frand(),nvh::frand(),1.0f);
        for (uint v = oldverts; v < sceneMesh.getVerticesCount(); v++){
          sceneMesh.m_vertices[v].color = color;
        }

        Geometry geom;
        geom.firstIndex    = oldindices;
        geom.count         = sceneMesh.getTriangleIndicesCount() - oldindices;

        geometries.push_back(geom);
      }

      nvgl::newBuffer(buffers.scene_ibo);
      glNamedBufferData(buffers.scene_ibo, sceneMesh.getTriangleIndicesSize(), sceneMesh.m_indicesTriangles.data(), GL_STATIC_DRAW);

      nvgl::newBuffer(buffers.scene_vbo);
      glNamedBufferData(buffers.scene_vbo, sceneMesh.getVerticesSize(), sceneMesh.m_vertices.data(), GL_STATIC_DRAW);


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
        pos += (vec3(nvh::frand(),nvh::frand(),nvh::frand())*2.0f ) - vec3(1.0f);
        pos /=  float(grid);

        float scale;
        if ( nvmath::length(pos) < 0.52f ){
          scale = globalscale * 0.35f;
          pos *=  globalscale * 0.5f;
        }
        else{
          scale = globalscale;
          pos *=  globalscale;
        }

        mat4 matrix = 
          nvmath::translation_mat4( pos) *
          nvmath::rotation_mat4_y(nvh::frand()*nv_pi) *
          nvmath::scale_mat4( (vec3(scale) * (vec3(0.25f) + vec3(nvh::frand(),nvh::frand(),nvh::frand())*0.5f ))/float(grid) );

        m_sceneMatrices.push_back(matrix);
        m_sceneMatrices.push_back(nvmath::transpose(nvmath::invert(matrix)));
        matrixIndex.push_back(obj);

        // all have same bbox
        bboxes.push_back(bbox);

        DrawCmd cmd;
        cmd.count         = geometries[obj % geometries.size()].count;
        cmd.firstIndex    = geometries[obj % geometries.size()].firstIndex;
        cmd.baseVertex    = 0;
        cmd.baseInstance  = obj;
        cmd.instanceCount = 1;

        m_sceneCmds.push_back(cmd);
        obj++;
      }

      m_sceneMatricesAnimated.resize( m_sceneMatrices.size() );

      m_sceneVisBits.clear();
      m_sceneVisBits.resize( snapdiv(m_sceneCmds.size(),32), 0xFFFFFFFF );

      nvgl::newBuffer(buffers.scene_indirect);
      glNamedBufferData(buffers.scene_indirect,sizeof(DrawCmd) * m_sceneCmds.size(), m_sceneCmds.data(), GL_STATIC_DRAW);

      nvgl::newBuffer(buffers.scene_matrices);
      glNamedBufferData(buffers.scene_matrices, sizeof(mat4) * m_sceneMatrices.size(), m_sceneMatrices.data(), GL_STATIC_DRAW);
      nvgl::newTexture(textures.scene_matrices, GL_TEXTURE_BUFFER);
      glTextureBuffer(textures.scene_matrices, GL_RGBA32F, buffers.scene_matrices);

      if (has_GL_ARB_bindless_texture){
        GLuint64 handle = glGetTextureHandleARB(textures.scene_matrices);
        glMakeTextureHandleResidentARB(handle);
        glNamedBufferSubData(buffers.scene_ubo, sizeof(SceneData), sizeof(GLuint64), &handle);
      }

      nvgl::newBuffer(buffers.scene_bboxes);
      glNamedBufferData(buffers.scene_bboxes, sizeof(CullBbox) * bboxes.size(), bboxes.data(), GL_STATIC_DRAW);
      
      nvgl::newBuffer(buffers.scene_matrixindices);
      glNamedBufferData(buffers.scene_matrixindices, sizeof(int) * matrixIndex.size(), matrixIndex.data(), GL_STATIC_DRAW);

      // for culling
      nvgl::newBuffer(buffers.cull_indirect);
      glNamedBufferData(buffers.cull_indirect, sizeof(DrawCmd) * m_sceneCmds.size(), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_counter);
      glNamedBufferData(buffers.cull_counter, sizeof(int), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_output);
      glNamedBufferData(buffers.cull_output, snapdiv( m_sceneCmds.size(), 32) * 32 * sizeof(uint32_t), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_bits);
      glNamedBufferData(buffers.cull_bits, snapdiv( m_sceneCmds.size(), 32) * sizeof( uint32_t ), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_bitsLast);
      glNamedBufferData(buffers.cull_bitsLast, snapdiv( m_sceneCmds.size(), 32) * sizeof( uint32_t ), NULL, GL_DYNAMIC_COPY);

      for (int i = 0; i < CYCLIC_FRAMES; i++) {
        nvgl::newBuffer( buffers.cull_bitsReadback[i] );
        glNamedBufferStorage( buffers.cull_bitsReadback[i], snapdiv( m_sceneCmds.size(), 32 ) * sizeof( uint32_t ), NULL, GL_MAP_PERSISTENT_BIT | GL_CLIENT_STORAGE_BIT | GL_MAP_READ_BIT | GL_MAP_COHERENT_BIT );
      }

      // for command list

      if (m_bindlessVboUbo)
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

        offset = nvtokenEnqueue(m_tokenStream, ubo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ubo)));
        tokenOffsets. push_back(num32bit(offset));

        ubo.setBinding(UBO_SCENE, NVTOKEN_STAGE_FRAGMENT);

        offset = nvtokenEnqueue(m_tokenStream, ubo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ubo)));
        tokenOffsets. push_back(num32bit(offset));

        NVTokenVbo vbo;
        vbo.setBinding(0);
        vbo.setBuffer(buffers.scene_vbo, addresses.scene_vbo, 0);

        offset = nvtokenEnqueue(m_tokenStream, vbo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(vbo)));
        tokenOffsets. push_back(num32bit(offset));

        vbo.setBinding(1);
        vbo.setBuffer(buffers.scene_matrixindices, addresses.scene_matrixindices, 0);

        offset = nvtokenEnqueue(m_tokenStream, vbo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(vbo)));
        tokenOffsets. push_back(num32bit(offset));

        NVTokenIbo ibo;
        ibo.setBuffer(buffers.scene_ibo, addresses.scene_ibo);
        ibo.setType(GL_UNSIGNED_INT);

        offset = nvtokenEnqueue(m_tokenStream, ibo);
        tokenObjects. push_back(-1);
        tokenSizes.   push_back(num32bit(sizeof(ibo)));
        tokenOffsets. push_back(num32bit(offset));
      }

      for (size_t i = 0; i < m_sceneCmds.size(); i++){
        const DrawCmd& cmd = m_sceneCmds[i];

        // for commandlist token technique
        NVTokenDrawElemsInstanced drawtoken;
        drawtoken.cmd.baseInstance  = cmd.baseInstance;
        drawtoken.cmd.baseVertex    = cmd.baseVertex;
        drawtoken.cmd.firstIndex    = cmd.firstIndex;
        drawtoken.cmd.instanceCount = cmd.instanceCount;
        drawtoken.cmd.count         = cmd.count;
        drawtoken.cmd.mode          = GL_TRIANGLES;
        offset = nvtokenEnqueue(m_tokenStream,drawtoken);

        // In this simple case we have one token per "object",
        // but typically one would have multiple tokens (vbo,ibo...) per object
        // as well, hence the token culling code presented, accounts for the 
        // more generic use-case.
        tokenObjects. push_back(int(i));
        tokenSizes.   push_back(num32bit(sizeof(drawtoken)));
        tokenOffsets. push_back(num32bit(offset));
      }
      m_numTokens = GLuint(tokenSizes.size());
      // pad to multiple of 4
      while(tokenSizes.size() % 4)
      {
        tokenObjects.push_back(-1);
        tokenSizes.push_back(0);
        tokenOffsets.push_back(0);
      }

      m_tokenStreamCulled = m_tokenStream;

      nvgl::newBuffer(buffers.scene_token);
      glNamedBufferData(buffers.scene_token, m_tokenStream.size(), m_tokenStream.data(), GL_STATIC_DRAW);

      // for command list culling

      nvgl::newBuffer(buffers.scene_tokenSizes);
      glNamedBufferData(buffers.scene_tokenSizes, tokenSizes.size() * sizeof(GLuint), tokenSizes.data(), GL_STATIC_DRAW);

      nvgl::newBuffer(buffers.scene_tokenOffsets);
      glNamedBufferData(buffers.scene_tokenOffsets, tokenOffsets.size() * sizeof(GLuint), tokenOffsets.data(), GL_STATIC_DRAW);

      nvgl::newBuffer(buffers.scene_tokenObjects);
      glNamedBufferData(buffers.scene_tokenObjects, tokenObjects.size() * sizeof(GLint), tokenObjects.data(), GL_STATIC_DRAW);

      nvgl::newBuffer(buffers.cull_token);
      glNamedBufferData(buffers.cull_token, m_tokenStream.size(), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_tokenEmulation); // only for emulation
      glNamedBufferData(buffers.cull_tokenEmulation, m_tokenStream.size(), NULL, GL_DYNAMIC_READ);

      nvgl::newBuffer(buffers.cull_tokenSizes);
      glNamedBufferData(buffers.cull_tokenSizes, tokenSizes.size() * sizeof(GLuint), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_tokenScan);
      glNamedBufferData(buffers.cull_tokenScan, tokenSizes.size() * sizeof(GLuint), NULL, GL_DYNAMIC_COPY);

      nvgl::newBuffer(buffers.cull_tokenScanOffsets);
      glNamedBufferData(buffers.cull_tokenScanOffsets, ScanSystem::getOffsetSize(GLuint(tokenSizes.size())), NULL, GL_DYNAMIC_COPY);
    }

    return true;
  }


  bool Sample::initFramebuffers(int width, int height)
  {

    nvgl::newTexture(textures.scene_color, GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, textures.scene_color);
    glTexStorage2D(GL_TEXTURE_2D, 1, fboFormat, width, height);

    int dim = width > height ? width : height;
    int levels = 0;
    while (dim){
      levels++;
      dim/=2;
    }

    nvgl::newTexture(textures.scene_depthstencil, GL_TEXTURE_2D);
    glBindTexture (GL_TEXTURE_2D, textures.scene_depthstencil);
    glTexStorage2D(GL_TEXTURE_2D, levels, GL_DEPTH24_STENCIL8, width, height);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);

    nvgl::newFramebuffer(fbos.scene);
    glBindFramebuffer(GL_FRAMEBUFFER,     fbos.scene);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,        GL_TEXTURE_2D, textures.scene_color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, textures.scene_depthstencil, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return true;
  }

  void Sample::initCullingJob(CullingSystem::Job& cullJob)
  {
    cullJob.m_numObjects = (int)m_sceneCmds.size();

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
    m_statsPrint = false;
    
    ImGuiH::Init(m_windowState.m_winSize[0], m_windowState.m_winSize[1], this);
    ImGui::InitGL();

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    m_cmdlistNative   = has_GL_NV_command_list != 0;
    m_bindlessVboUbo  = has_GL_NV_vertex_buffer_unified_memory && has_GL_NV_uniform_buffer_unified_memory;
    nvtokenInitInternals(m_cmdlistNative, m_bindlessVboUbo);

    bool validated(true);

    GLuint defaultVAO;
    glGenVertexArrays(1, &defaultVAO);
    glBindVertexArray(defaultVAO);

    validated = validated && initProgram();
    validated = validated && initScene();
    validated = validated && initFramebuffers(m_windowState.m_winSize[0],m_windowState.m_winSize[1]);

    if (!validated) return false;

    {
      ScanSystem::Programs scanprograms;
      getScanPrograms(scanprograms);
      s_scanSys.init(scanprograms);
    }

    {
      CullingSystem::Programs cullprograms;
      getCullPrograms(cullprograms);
      m_cullSys.init( cullprograms, false );

      m_cullFrameCycle = 0;

      for (int i = 0; i < CYCLIC_FRAMES; i++) {
        m_cullReadbackBuffers[i] = CullingSystem::Buffer( buffers.cull_bitsReadback[i] );
        m_cullReadbackMappings[i] = glMapNamedBufferRange( buffers.cull_bitsReadback[i], 0, m_cullReadbackBuffers[i].size, GL_MAP_PERSISTENT_BIT | GL_MAP_READ_BIT | GL_MAP_COHERENT_BIT );
      }

      initCullingJob( m_cullJobReadback );
      m_cullJobReadback.m_bufferVisBitsReadback = m_cullReadbackBuffers[0];
      m_cullJobReadback.m_bufferVisBitsMapping = m_cullReadbackMappings[0];
      m_cullJobReadback.m_fence = NULL;
      

      initCullingJob(m_cullJobIndirect);
      m_cullJobIndirect.m_program_indirect_compact = m_progManager.get( programs.indirect_unordered );
      m_cullJobIndirect.m_bufferObjectIndirects = CullingSystem::Buffer(buffers.scene_indirect);
      m_cullJobIndirect.m_bufferIndirectCounter = CullingSystem::Buffer(buffers.cull_counter);
      m_cullJobIndirect.m_bufferIndirectResult  = CullingSystem::Buffer(buffers.cull_indirect);

      initCullingJob(m_cullJobToken);
      m_cullJobToken.program_cmds   = m_progManager.get( programs.token_cmds );
      m_cullJobToken.program_sizes  = m_progManager.get( programs.token_sizes );
      m_cullJobToken.numTokens      = m_numTokens;

      // if we had multiple stateobjects, we would be using multiple sequences
      // where each sequence covers the token range per stateobject
      CullJobToken::Sequence sequence;
      sequence.first = 0;
      sequence.num   = m_numTokens;
      sequence.offset = 0;
      sequence.endoffset = GLuint(m_tokenStream.size()/sizeof(GLuint));
      m_cullJobToken.sequences.push_back(sequence);


      m_cullJobToken.tokenOrig    = ScanSystem::Buffer(buffers.scene_token);
      m_cullJobToken.tokenObjects = ScanSystem::Buffer(buffers.scene_tokenObjects);
      m_cullJobToken.tokenOffsets = ScanSystem::Buffer(buffers.scene_tokenOffsets);
      m_cullJobToken.tokenSizes   = ScanSystem::Buffer(buffers.scene_tokenSizes);

      m_cullJobToken.tokenOut           = ScanSystem::Buffer(buffers.cull_token);
      m_cullJobToken.tokenOutSizes      = ScanSystem::Buffer(buffers.cull_tokenSizes);
      m_cullJobToken.tokenOutScan       = ScanSystem::Buffer(buffers.cull_tokenScan);
      m_cullJobToken.tokenOutScanOffset = ScanSystem::Buffer(buffers.cull_tokenScanOffsets);
    }

    {
      m_ui.enumAdd(GUI_ALGORITHM, CullingSystem::METHOD_FRUSTUM, "frustum");
      m_ui.enumAdd(GUI_ALGORITHM, CullingSystem::METHOD_HIZ, "hiz");
      m_ui.enumAdd(GUI_ALGORITHM, CullingSystem::METHOD_RASTER, "raster");

      m_ui.enumAdd(GUI_RESULT, RESULT_REGULAR_CURRENT, "regular current frame");
      m_ui.enumAdd(GUI_RESULT, RESULT_REGULAR_LASTFRAME, "regular last frame");
      m_ui.enumAdd(GUI_RESULT, RESULT_TEMPORAL_CURRENT, "temporal current frame");

      m_ui.enumAdd(GUI_DRAW, DRAW_STANDARD, "standard CPU");
      m_ui.enumAdd(GUI_DRAW, DRAW_MULTIDRAWINDIRECT, "MultiDrawIndirect GPU");
      m_ui.enumAdd(GUI_DRAW, DRAW_TOKENBUFFER_EMULATION, "nvcmdlist emulation");
      if (m_cmdlistNative) {
        m_ui.enumAdd(GUI_DRAW, DRAW_TOKENBUFFER, "nvcmdlist GPU");
      }
    }

    m_control.m_sceneOrbit = vec3(0.0f);
    m_control.m_sceneDimension = float(globalscale) * 2.0f;
    float dist = m_control.m_sceneDimension * 0.75f;
    m_control.m_viewMatrix = nvmath::look_at(m_control.m_sceneOrbit - normalize(vec3(1,0,-1))*dist, m_control.m_sceneOrbit, vec3(0,1,0));

    m_statsTime = NVPSystem::getTime();

    return validated;
  }

  void Sample::processUI(double time)
  {
    int width = m_windowState.m_winSize[0];
    int height = m_windowState.m_winSize[1];

    // Update imgui configuration
    auto &imgui_io = ImGui::GetIO();
    imgui_io.DeltaTime = static_cast<float>(time - m_uiTime);
    imgui_io.DisplaySize = ImVec2(width, height);

    m_uiTime = time;

    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("NVIDIA " PROJECT_NAME, nullptr)) {
      ImGui::Checkbox("culling", &m_tweak.culling);
      ImGui::Checkbox("freeze result", &m_tweak.freeze);
      ImGui::SliderFloat("min.pixelsize", &m_tweak.minPixelSize, 0.0f, 16.0f);
      m_ui.enumCombobox(GUI_ALGORITHM, "algorithm", &m_tweak.method);
      m_ui.enumCombobox(GUI_RESULT, "result", &m_tweak.result);
      m_ui.enumCombobox(GUI_DRAW, "drawmode", &m_tweak.drawmode);
      ImGui::SliderFloat("animate", &m_tweak.animate, 0.0f, 32.0f);
    }
    ImGui::End();
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
    memset(m_sceneVisBits.data(),0xFFFFFFFF,sizeof(uint32_t) * m_sceneVisBits.size() );
    // rest token buffer
    glCopyNamedBufferSubData(buffers.scene_token, buffers.cull_token, 0, 0, m_tokenStream.size());
    glCopyNamedBufferSubData(buffers.scene_token, buffers.cull_tokenEmulation, 0, 0, m_tokenStream.size());
    // reset indirect buffer
    glCopyNamedBufferSubData(buffers.scene_indirect, buffers.cull_indirect, 0, 0, m_sceneCmds.size() * sizeof(DrawCmd));
  }

  void Sample::drawScene(bool depthonly, const char* what)
  {
    NV_PROFILE_GL_SECTION(what);

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

    glUseProgram(m_progManager.get(programs.draw_scene));

    // these bindings are replicated in the tokenbuffer as well
    glBindBufferBase(GL_UNIFORM_BUFFER, UBO_SCENE, buffers.scene_ubo);
    glBindVertexBuffer(0,buffers.scene_vbo,0,sizeof(Vertex));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers.scene_ibo);
    glBindVertexBuffer(1,buffers.scene_matrixindices,0,sizeof(GLint));


    if (!has_GL_ARB_bindless_texture){
      glActiveTexture(GL_TEXTURE0 + TEX_MATRICES);
      glBindTexture(GL_TEXTURE_BUFFER,textures.scene_matrices);
    }

    if (m_tweak.drawmode == DRAW_MULTIDRAWINDIRECT){
      glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_tweak.culling ? buffers.cull_indirect : buffers.scene_indirect );
      if (m_tweak.culling){
        glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
      }
      glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, NULL, (GLsizei)m_sceneCmds.size(), 0);
      glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    }
    else if (m_tweak.drawmode == DRAW_TOKENBUFFER || m_tweak.drawmode == DRAW_TOKENBUFFER_EMULATION)
    {
      if (m_bindlessVboUbo){
        glEnableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
        glEnableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
        glEnableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
      }
      if (m_tweak.culling){
        if (m_tweak.drawmode == DRAW_TOKENBUFFER_EMULATION){
          NV_PROFILE_GL_SECTION("Read");
          m_cullJobToken.tokenOut.GetNamedBufferSubData(&m_tokenStreamCulled[0]);
        }
        else{
          glMemoryBarrier(GL_COMMAND_BARRIER_BIT);
        }

        NV_PROFILE_GL_SPLIT();
      }

      GLintptr offset = 0;
      GLsizei  size   = GLsizei(m_tokenStream.size());
      if (m_tweak.drawmode == DRAW_TOKENBUFFER_EMULATION){
        StateSystem::State state;
        state.vertexformat.bindings[0].stride = sizeof(Vertex);
        state.vertexformat.bindings[1].stride = sizeof(GLint);

        const std::string& stream = m_tweak.culling ? m_tokenStreamCulled : m_tokenStream;

        nvtokenDrawCommandsSW(GL_TRIANGLES, stream.data(), stream.size(), &offset, &size, 1, state);
      }
      else{
        glDrawCommandsNV(GL_TRIANGLES, m_tweak.culling ? buffers.cull_token : buffers.scene_token, &offset, &size, 1);
      }
      
      if (m_bindlessVboUbo){
        glDisableClientState(GL_UNIFORM_BUFFER_UNIFIED_NV);
        glDisableClientState(GL_VERTEX_ATTRIB_ARRAY_UNIFIED_NV);
        glDisableClientState(GL_ELEMENT_ARRAY_UNIFIED_NV);
      }

    }
    else{
      int visible = 0;
      for (size_t i = 0; i < m_sceneCmds.size(); i++)
      {
        if (m_sceneVisBits[i / 32] & (1<< (i%32)) ){
          glDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT, &m_sceneCmds[i] );
          visible++;
        }
      }
      if (m_statsPrint){
        LOGI("%s visible: %d pct\n", what, (visible * 100) / (int)m_sceneCmds.size() );
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
    if (!has_GL_ARB_bindless_texture){
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
    view.viewWidth         = float(m_windowState.m_winSize[0]);
    view.viewHeight        = float(m_windowState.m_winSize[1]);
    view.viewCullThreshold = m_tweak.minPixelSize;
    memcpy(view.viewPos, m_sceneUbo.viewPos.get_value(), sizeof(view.viewPos));
    memcpy(view.viewDir, m_sceneUbo.viewDir.get_value(), sizeof(view.viewDir));
    memcpy(view.viewProjMatrix, m_sceneUbo.viewProjMatrix.get_value(), sizeof(view.viewProjMatrix));

    switch(m_tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
    {
      // kinda pointless to use temporal ;)
      {
        NV_PROFILE_GL_SECTION("CullF");
        m_cullSys.buildOutput( m_tweak.method, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
        m_cullSys.resultFromBits( cullJob );
        m_cullSys.resultClient( cullJob );
      }

      drawScene(false,"Scene");
    }
    break;
    case CullingSystem::METHOD_HIZ:
    {
      {
        NV_PROFILE_GL_SECTION("CullF");
#if !CULL_TEMPORAL_NOFRUSTUM
        m_cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_LAST );
        m_cullSys.resultFromBits( cullJob );
#endif
        m_cullSys.resultClient( cullJob );
      }

      drawScene(false,"Last");

      // changes FBO binding
      m_cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_windowState.m_winSize[0], m_windowState.m_winSize[1]);

      {
        NV_PROFILE_GL_SECTION("CullH");
        m_cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );

        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_NOT_LAST );
        m_cullSys.resultFromBits( cullJob );
        m_cullSys.resultClient( cullJob );

        // for next frame
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
#if CULL_TEMPORAL_NOFRUSTUM
        m_cullSys.resultFromBits( cullJob );
#endif
        m_cullSys.swapBits( cullJob );  // last/output

      }

      glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene );
      drawScene(false,"New");
    }
    break;
    case CullingSystem::METHOD_RASTER:
    {
      {
        NV_PROFILE_GL_SECTION("CullF");
#if !CULL_TEMPORAL_NOFRUSTUM
        m_cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_LAST );
        m_cullSys.resultFromBits( cullJob );
#endif
        m_cullSys.resultClient( cullJob );
      }

      drawScene(false,"Last");

      {
        NV_PROFILE_GL_SECTION("CullR");
        m_cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT_AND_NOT_LAST );
        m_cullSys.resultFromBits( cullJob );
        m_cullSys.resultClient( cullJob );

        // for next frame
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
#if CULL_TEMPORAL_NOFRUSTUM
        m_cullSys.resultFromBits( cullJob );
#endif
        m_cullSys.swapBits( cullJob );  // last/output
      }

      drawScene(false,"New");
    }
    break;
    }
  }

  void Sample::drawCullingRegular(CullingSystem::Job& cullJob)
  {
    CullingSystem::View view;
    view.viewWidth         = float(m_windowState.m_winSize[0]);
    view.viewHeight        = float(m_windowState.m_winSize[1]);
    view.viewCullThreshold = m_tweak.minPixelSize;
    memcpy(view.viewPos, m_sceneUbo.viewPos.get_value(), sizeof(view.viewPos));
    memcpy(view.viewDir, m_sceneUbo.viewDir.get_value(), sizeof(view.viewDir));
    memcpy(view.viewProjMatrix, m_sceneUbo.viewProjMatrix.get_value(), sizeof(view.viewProjMatrix));

    switch(m_tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
      {
        {
          NV_PROFILE_GL_SECTION("CullF");
          m_cullSys.buildOutput( m_tweak.method, cullJob, view );
          m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          m_cullSys.resultFromBits( cullJob );
          m_cullSys.resultClient( cullJob );
        }

        drawScene(false,"Scene");
      }
      break;
    case CullingSystem::METHOD_HIZ:
      {
        {
          NV_PROFILE_GL_SECTION("CullF");
          m_cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
          m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          m_cullSys.resultFromBits( cullJob );
          m_cullSys.resultClient( cullJob );
        }

        drawScene(true,"Depth");

        {
          NV_PROFILE_GL_SECTION("Mip");
          // changes FBO binding
          m_cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_windowState.m_winSize[0], m_windowState.m_winSize[1]);
        }


        {
          NV_PROFILE_GL_SECTION("CullH");
          m_cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );
          m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          m_cullSys.resultFromBits( cullJob );
          m_cullSys.resultClient( cullJob );
        }

        glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene );
        drawScene(false,"Scene");
      }
      break;
    case CullingSystem::METHOD_RASTER:
      {
        {
          NV_PROFILE_GL_SECTION("CullF");
          m_cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
          m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          m_cullSys.resultFromBits( cullJob );
          m_cullSys.resultClient( cullJob );
        }

        drawScene(true,"Depth");
        

        {
          NV_PROFILE_GL_SECTION("CullR");
          m_cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
          m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
          m_cullSys.resultFromBits( cullJob );
          m_cullSys.resultClient( cullJob );
        }

        drawScene(false,"Scene");
      }
      break;
    }
  }

  void Sample::drawCullingRegularLastFrame(CullingSystem::Job& cullJob)
  {
    CullingSystem::View view;
    view.viewWidth         = float(m_windowState.m_winSize[0]);
    view.viewHeight        = float(m_windowState.m_winSize[1]);
    view.viewCullThreshold = m_tweak.minPixelSize;
    memcpy(view.viewPos, m_sceneUbo.viewPos.get_value(), sizeof(view.viewPos));
    memcpy(view.viewDir, m_sceneUbo.viewDir.get_value(), sizeof(view.viewDir));
    memcpy(view.viewProjMatrix, m_sceneUbo.viewProjMatrix.get_value(), sizeof(view.viewProjMatrix));

    switch(m_tweak.method){
    case CullingSystem::METHOD_FRUSTUM:
    {
      {
        NV_PROFILE_GL_SECTION("Wait");
        m_cullSys.resultClient(cullJob);
      }

      drawScene(false,"Scene");

      {
        NV_PROFILE_GL_SECTION("CullF");
        m_cullSys.buildOutput( CullingSystem::METHOD_FRUSTUM, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
        m_cullSys.resultFromBits( cullJob );
      }
    }
    break;
    case CullingSystem::METHOD_HIZ:
    {

      {
        NV_PROFILE_GL_SECTION("Wait");
        m_cullSys.resultClient(cullJob);
      }

      drawScene(false,"Scene");

      {
        NV_PROFILE_GL_SECTION("Mip");
        // changes FBO binding
        m_cullSys.buildDepthMipmaps( textures.scene_depthstencil, m_windowState.m_winSize[0], m_windowState.m_winSize[1]);
      }

      {
        NV_PROFILE_GL_SECTION("Cull");
        m_cullSys.buildOutput( CullingSystem::METHOD_HIZ, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
        m_cullSys.resultFromBits( cullJob );
      }
    }
    break;
    case CullingSystem::METHOD_RASTER:
    {
      {
        NV_PROFILE_GL_SECTION("Wait");
        m_cullSys.resultClient( cullJob );
      }

      drawScene(false,"Scene");
      
      {
        NV_PROFILE_GL_SECTION("Cull");
        m_cullSys.buildOutput( CullingSystem::METHOD_RASTER, cullJob, view );
        m_cullSys.bitsFromOutput( cullJob, CullingSystem::BITS_CURRENT );
        m_cullSys.resultFromBits( cullJob );
      }
    }
    break;
    }
  }

  void Sample::think(double time)
  {
    NV_PROFILE_GL_SECTION("Frame");

    processUI(time);

    m_control.processActions(m_windowState.m_winSize,
      nvmath::vec2f(m_windowState.m_mouseCurrent[0],m_windowState.m_mouseCurrent[1]),
      m_windowState.m_mouseButtonFlags, m_windowState.m_mouseWheel);

    if (m_windowState.onPress(KEY_R)){
      m_progManager.reloadPrograms();

      ScanSystem::Programs scanprograms;
      getScanPrograms(scanprograms);
      s_scanSys.update(scanprograms);

      CullingSystem::Programs cullprograms;
      getCullPrograms(cullprograms);
      m_cullSys.update( cullprograms, false );
      m_cullJobIndirect.m_program_indirect_compact = m_progManager.get( programs.indirect_unordered );
      m_cullJobToken.program_cmds  = m_progManager.get( programs.token_cmds );
      m_cullJobToken.program_sizes = m_progManager.get( programs.token_sizes );
    }

    if (!m_progManager.areProgramsValid()){
      waitEvents();
      return;
    }

    if ( memcmp(&m_tweak,&m_tweakLast,sizeof(Tweak)) != 0 && m_tweak.freeze == m_tweakLast.freeze) {
      systemChange();
      m_tweak.freeze = false;
    }
    if (!m_tweak.culling || m_tweak.result == RESULT_TEMPORAL_CURRENT){
      m_tweak.freeze = false;
    }

#if _DEBUG
    if (m_tweak.drawmode != m_tweakLast.drawmode && has_GL_NV_shader_buffer_load){
      // disable a few expected but annoying debug messages for NVIDIA
      if (m_tweakLast.drawmode == DRAW_TOKENBUFFER_EMULATION || m_tweakLast.drawmode == DRAW_TOKENBUFFER){
        glDebugMessageControlARB(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
      }
      if (m_tweak.drawmode == DRAW_TOKENBUFFER_EMULATION || m_tweak.drawmode == DRAW_TOKENBUFFER){
        GLuint msgid = 65537; // residency warning
        glDebugMessageControlARB(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_OTHER, GL_DONT_CARE, 1, &msgid, GL_FALSE);
        msgid = 131186;       // emulation does GetData on buffer
        glDebugMessageControlARB(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_PERFORMANCE_ARB, GL_DONT_CARE, 1, &msgid, GL_FALSE);
      }
    }
#endif

    if ( m_tweak.drawmode == DRAW_STANDARD && (NVPSystem::getTime() - m_statsTime) > 2.0){
      m_statsTime = NVPSystem::getTime();
      m_statsPrint = true;
    }
    else{
      m_statsPrint = false;
    }

    int width   = m_windowState.m_winSize[0];
    int height  = m_windowState.m_winSize[1];

    {
      glBindFramebuffer(GL_FRAMEBUFFER, fbos.scene);
      glViewport(0, 0, width, height);
      glClearColor(1.0,1.0,1.0,1.0);
      glClearDepth(1.0);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);


      { // Update UBO
        nvmath::mat4 projection = nvmath::perspective((45.f), float(width)/float(height), 0.1f, 100.0f);
        nvmath::mat4 view = m_control.m_viewMatrix;

        m_sceneUbo.viewProjMatrix = projection * view;
        m_sceneUbo.viewMatrix = view;
        m_sceneUbo.viewMatrixIT = nvmath::transpose(nvmath::invert(view));

        m_sceneUbo.viewPos = m_sceneUbo.viewMatrixIT.row(3);
        m_sceneUbo.viewDir = -view.row(2);

        glBindBuffer(GL_UNIFORM_BUFFER, buffers.scene_ubo);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(SceneData), &m_sceneUbo);
      }

    }

    if (m_tweak.animate || m_tweak.animateOffset != m_tweakLast.animateOffset)
    {
      mat4 rotator = nvmath::rotation_mat4_y( float(time)*0.1f * m_tweak.animate + m_tweak.animateOffset);

      for (size_t i = 0; i < m_sceneMatrices.size()/2; i++){
        mat4 changed = rotator * m_sceneMatrices[i*2 + 0];
        m_sceneMatricesAnimated[i*2 + 0] = changed;
        m_sceneMatricesAnimated[i*2 + 1] = nvmath::transpose(nvmath::invert(changed));
      }

      glNamedBufferSubData(buffers.scene_matrices,0,sizeof(mat4)*m_sceneMatricesAnimated.size(), m_sceneMatricesAnimated.data() );
    }


    if (m_tweak.culling && !m_tweak.freeze) {
      m_cullJobReadback.m_hostVisBits = m_sceneVisBits.data();

      // We change the output buffer for token emulation, as once the driver sees frequent readbacks on buffers
      // it moves the allocation to read-friendly memory. This would be bad for the native tokenbuffer.
      m_cullJobToken.tokenOut.buffer = (m_tweak.drawmode == DRAW_TOKENBUFFER_EMULATION ? buffers.cull_tokenEmulation : buffers.cull_token);

      CullingSystem::Job&  cullJob = 
        (m_tweak.drawmode == DRAW_STANDARD) ? (CullingSystem::Job&)m_cullJobReadback : 
        (m_tweak.drawmode == DRAW_MULTIDRAWINDIRECT ? (CullingSystem::Job&)m_cullJobIndirect : 
        (CullingSystem::Job&)m_cullJobToken);
      
      if (m_tweak.drawmode == DRAW_STANDARD) {
        if (m_tweak.result == RESULT_REGULAR_LASTFRAME) {
          // When using persistent mapped bindings, we optimize our readback behavior.
          // We perform the "server-side" result copy for the current frame,
          // but read the client-side mapped results from the previous frame.
          m_cullJobReadback.m_bufferVisBitsReadback = m_cullReadbackBuffers[m_cullFrameCycle];
          m_cullJobReadback.m_bufferVisBitsMapping = m_cullReadbackMappings[m_cullFrameCycle ^ 1];
        }
        else {
          m_cullJobReadback.m_bufferVisBitsReadback = m_cullReadbackBuffers[0];
          m_cullJobReadback.m_bufferVisBitsMapping = m_cullReadbackMappings[0];
        }
      }

      switch(m_tweak.result)
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

      m_cullFrameCycle = m_cullFrameCycle ^ 1;
    }
    else{
      drawScene(false,"Draw");
    }

    if (m_statsPrint){
      LOGI("\n");
    }


    // blit to background
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos.scene);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0,0,width,height,
      0,0,width,height,GL_COLOR_BUFFER_BIT, GL_NEAREST);

    m_tweakLast = m_tweak;

    if (!m_tweak.noui)
    {
      NV_PROFILE_GL_SECTION("GUI");
      ImGui::Render();
      ImGui::RenderDrawDataGL(ImGui::GetDrawData());
    }

    ImGui::EndFrame();
  }

  void Sample::resize(int width, int height)
  {
    initFramebuffers(width,height);
  }
  
}

using namespace ocull;

int main(int argc, const char** argv)
{
  NVPSystem system(argv[0], PROJECT_NAME);

  Sample sample;
  return sample.run(
    PROJECT_NAME,
    argc, argv,
    SAMPLE_SIZE_WIDTH, SAMPLE_SIZE_HEIGHT);
}

