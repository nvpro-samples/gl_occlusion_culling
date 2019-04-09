# gl occlusion culling

![sample](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/sample.PNG)

This sample implements a batched occlusion culling system, which is not based on individual occlusion queries anymore, but uses shaders to cull many boxes at once. The principle algorithms are also illustrated towards the end of the presentation slides of [GTC 2014](http://on-demand.gputechconf.com/gtc/2014/presentations/S4379-opengl-44-scene-rendering-techniques.pdf) and [SIGGRAPH 2014](http://on-demand.gputechconf.com/siggraph/2014/presentation/SG4117-OpenGL-Scene-Rendering-Techniques.pdf)

It leverages the **ARB_multi_draw_indirect** (MDI) extension to implement latency-free occlusion culling. The MDI technique works well with a simplified scene setup where all geometry is stored in one big VBO/IBO pairing and no shader changes are done in between.

The slides mention that this approach could be extended to use NV_bindless_multi_draw_indirect to render drawcalls using different VBO/IBOs in one go. With the **NV_command_list** however an even better approach is possible, which is also implemented in the sample and allows more flexible state changes. Please refer to [gl commandlist basic](https://github.com/nvpro-samples/gl_commandlist_basic) for an introduction on NV_command_list.

> **Note:** For simplicity the sample uses one draw shader only, in a real-world use case one would have to organize multiple draw indirect culling lists per shader, or multiple NV_command_list token sequences with stateobjects. The latter is shown in [gl cadscene rendertechniques](https://github.com/nvpro-samples/gl_cadscene_rendertechniques)

### Algorithms
All culling algorithms have in common that they test all the scene's bounding boxes at once. One buffer encodes all matrices of the scene's objects (*buffers.scene_matrices*), and another the geometries bounding boxes (*buffers.scene_bboxes*). A third buffer (*buffers.scene_matrixindices*) links the two together. 

The culling itself is implemented in the *cullingsystem.cpp/hpp* files. It uses SSBO to store the results. The results are always packed into bit-arrays to minimize traffic in the read-back case and maximize cache hits.

- **Frustum:**
 Just a simple frustum culling approach, this could probably done efficiently on the CPU using SIMD as well.

- **HiZ (occlusion):**
 This technique generates a mip-map chain of the depth buffer, and then checks the bounding box against the proper LOD. The LOD is chosen based on the area of the bounding box in screenspace. The core pinciple of the technique is also described [here](http://rastergrid.com/blog/2010/10/hierarchical-z-map-based-occlusion-culling/)

- **Raster (occlusion):**
 As illustrated on [slide 51](http://on-demand.gputechconf.com/siggraph/2014/presentation/SG4117-OpenGL-Scene-Rendering-Techniques.pdf) this algorithm works by rasterizing the bounding box "invisibly". A geometry shader is used to generate bounding boxes. While color buffer writes are disabled our boxes are still rasterized and tested against the current depth-buffer. Those fragments which pass the depth-test, indicate that our bounding box is visible, and therefore flag the object in a visibility buffer: `visible[objectid] = 1`. Prior the operation that buffer is cleared to zero. This method typically yields better results than *HiZ* as the bounding boxes are tested more accurately as their orientation and dimension is better represented.

![raster](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/raster.png)

### Result Processing

- **Current Frame:**
 Here we accurately test using the latest information for the frame. That means the occlusion techniques also have to do a depth-pass (for which we use frustum check before).

- **Last Frame:**
 To avoid synchronization in a frame, we use the last frames results. At low frame-rates or high motion this can result in visible artifacts with objects "popping" up. 

![latency](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/latencyissue.jpg)

- **Temporal Current Frame:**
 This technique uses a temporal coherence to reduce the impact of depth-pass for the occlusion techniques. As described in [slide 52](http://on-demand.gputechconf.com/siggraph/2014/presentation/SG4117-OpenGL-Scene-Rendering-Techniques.pdf) we use the last frames result to limit the number of times an object is drawn to exactly 1 (classic depth-pass would be 2).
 - We start out by drawing the last frame's visible objects, this primes both our depth-buffer and shading
 - Next we test the visibility of objects against this depth-buffer
 - We draw those objects which are visible, but weren't drawn already
 - Finally we could test against depth-buffer again to improve our visibility for the next frame, but if there is not too much big motion, the over-estimation of visibility should not be too bad and correct itself over the next frames quickly.

![temporal](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/temporal.png)

 Our first frame may end up quite heavy, here you could use the regular "Current Frame" approach, to avoid drawing all objects without any depth-pass.

### Drawing Modes
How the results are processed is also influenced by how we draw the scene. To allow drawing the entire scene with little state changes we leverage a trick to pass a unique vertex attribute per-drawcall using the *BaseInstance*. This attribute encodes our matrix index for the GL_TEXTURE_BUFFER, which sores all matrices. To make use of it, the vertex divisor for this attribute is set to a non zero value, since we don't really use instancing we sort of hijack the value. This technique is also described [here on slide 27](http://on-demand.gputechconf.com/gtc/2013/presentations/S3032-Advanced-Scenegraph-Rendering-Pipeline.pdf).

- **Standard CPU:**
We read the results back to the host memory, which stalls the pipeline. The impact of this can be reduced a bit by using *Last Frame* results.

Especially on OpenGL 4.x you want to use "persistent mapped" buffers in the *Last Frame* scenario. The GPU always operates on a dedicated buffer to compute the results, then copies to
a persistent mapped dedicated readback buffer (server-side copy). You can either use different buffers or just shift the offsets within the readback buffer every other frame. At the end of the buffer to buffer copy
retrieve a fence sync and then in the next frame use a client wait before actually accessing the host mapped pointer.
The occlusion culling results should be copied after doing the occlusion-tests and ideally before doing any post-processing, this way we can reduce the wait time on the client. 

- **MultiDrawIndirect GPU:**
This technique leverages the **ARB_multi_draw_indirect** and is free of synchronization. Instead of reading back the results, we manipulate the **GL_DRAW_INDIRECT_BUFFER**. The indirect buffer is cleared to 0, which means it would not render anything if executed. Then we use an **GL_ATOMIC_COUNTER_BUFFER** to append all the visible DrawIndirect structures into this buffer.
> **Note**: Despite having the final drawindirect count available on the GPU through the atomic counter, the sample does not make use GL_ARB_indirect_parameters. Its usage comes with a certain overhead that may make things worse if the drawcalls have only low complexity. 
> 
> Usage of GL_ATOMIC_COUNTER_BUFFER to append the final buffer, means we lose the ordering of the original scene.

- **NVCmdList GPU:**

![cmdlist1](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/cmdlist1.png)

![cmdlist](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/cmdlist.png)


(Slides taken from [GTC 2015 presentation](http://on-demand.gputechconf.com/gtc/2015/presentation/S5135-Christoph-Kubisch-Pierre-Boudier.pdf))

This new extension allows a faster and more flexible implementation of the culling. The regular Indirect method may suffer from running over lots of empty drawindirects and as mentioned aboive GL_ARB_indirect_parameters may also have its issues. Here we can use **GL_TERMINATE_SEQUENCE_COMMAND_NV** to much more quickly opt out of the indirect sequence. We also have greater flexibility when it comes to drawing the scene, as we could store objects in different buffers, can use UBO toggles for each object and so on.
The technique works similar to the indirect method, however because commands have variable size, generating out command buffer is a bit harder, the positive side effect is that we preserve the original ordering.
 - Where indirect could straight record the drawindirect commands depending on their visibility, we first create an output buffer that stores all the sizes of visible commands. We output the original size of a command if it was visible, or zero if not.
 - A scan operation on these output sizes, creates our output offsets using a prefix sum approach. There is plenty literature on the web for parallel friendly scans, our implementation is derived from [CUB](http://nvlabs.github.io/cub/).
 - Using the compact offsets, we can now run over the commands and store them (or not) using the computed offsets. The very first thread may also add the GL_TERMINATE_SEQUENCE_COMMAND_NV token at the end of our token sequence (if there is room).

 In this sample we only have one sequence, because we have one shader state. The code however is already prepared to cull multiple sequences, which is used in [gl cadscene rendertechniques](https://github.com/nvpro-samples/gl_cadscene_rendertechniques). The difference is that for multiple sequences the original start offset of a sequence must be preserved. This requires a bit of extra work as our scan operation for sake of maximizing parallelism, scans across all sequences and therefore does loose the information about sequence starts.

- **NVCmdList emulation:**
Emulates the above, by using a read-back of the GPU-generated token-buffer and interpreting the tokens using standard api calls.

> The occlusion system handling for the commandlist is not done inside the occlusionsystem.cpp/hpp but instead the derived class *CullJobToken* is defined in the main sample file (occlusion-culling.cpp).

### Performance
The scene is made of 26^3 (17 576) objects that use some procedural noise in the fragment shader to add a little more fragment load. 

All timings are preliminary resuls in microseonds taken on Quadro K5000 i7-860 Win7-64 system. The "Current Frame" method does not give performance benefits in this test. In that scenario we suffer too much from the additional depth-pass more than we benefit.

**Standard CPU:**

```
no culling
 Timer Frame;    GL  26116; CPU   2420;
 
HiZ: last frame    (34 % visible)
 Timer Frame;    GL  11051; CPU  10921;
Raster: last frame (12 % visible)
 Timer Frame;    GL   4919; CPU   4820;
 
Raster: Temporal Current Frame
 Timer Frame;    GL   5395; CPU   5286;
```

One can see that using last frame's result dramatically improves performance for the CPU based technique, however it can give visual artifacts in motion as illustrated below.

As expected the HiZ's conservative approach yields higher percentage of pontentially visible objects, and therefore less performance (however the difference may not always be as drastic as in the test frame). By exploiting temporal coherency we can benefit from good performance with current data and avoid artifacts as above. 

We can also avoid any stalls when using the GPU variants (MultiDrawIndirect or NVCmdList).

**Indirect GPU:** 

```
Raster: Temporal Current Frame
 Timer Frame;    GL   4936; CPU    494;
  Timer CullF;   GL     31; CPU     14;
  Timer Last;    GL   4089; CPU     10;
  Timer CullR;   GL    266; CPU    133;
  Timer New;     GL    294; CPU      8;
  Timer TwDraw;  GL    161; CPU    231;
```

**NVCmdList GPU:**

```
Raster: Temporal Current Frame
 Timer Frame;    GL   4500; CPU    520;
  Timer CullF;   GL     51; CPU     25;
  Timer Last;    GL   3896; CPU      8;
  Timer CullR;   GL    286; CPU    161;
  Timer New;     GL     12; CPU      7;
  Timer TwDraw;  GL    161; CPU    225;
```

> **Your Milage May Vary:** In this sample the NV_command_list can beat out the classic MultiDrawIndirect, despite the simple scene, because we have many objects. If we had less objects than MDI could be winning as it is is "simpler" (it also records things "unordered" so creating the indirect buffer is quicker). However in more typical scenarios, where we may not have all data in single buffers, or we might want to change UBO binds/ranges between objects, MDI would suffer more again as we may end up
 with more smaller MultiDrawIndirects between binding/state changes. The NV_command_list will provide greater flexibility here. The slide below contains results from a more complex setup.

![cadscene results](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/cadsceneresults.png)

> Be aware that for MDI the use of ARB_indirect_parameters to pass the number of active draw commands is not advised, either don't or prefer the use of NV_command_list with GL_TERMINATE_SEQUENCE_COMMAND_NV. It is however still beneficial for performance to create a compact stream of draw indirect commands. 

### Sample Highlights

The principle setup is stored in *occlusion-culling.cpp*. Next to the *occlusionsystem.cpp/hpp* files the following functions should be interesting for this sample:

- Sample::initScene
- Sample::drawCullingRegular
- Sample::drawCullingRegularLastFrame
- Sample::drawCullingTemporal
- Sample::drawScene
- Sample::CullJobToken::resultFromBits

Inside the tweak ui you can "freeze" a frame's culling results, and rotate camera to see what is actually drawn in the end.

![frozen culling](https://github.com/nvpro-samples/gl_occlusion_culling/blob/master/doc/frozenculling.jpg)

#### Building
Ideally clone this and other interesting [nvpro-samples](https://github.com/nvpro-samples) repositories into a common subdirectory. You will always need [shared_sources](https://github.com/nvpro-samples/shared_sources) and on Windows [shared_external](https://github.com/nvpro-samples/shared_external). The shared directories are searched either as subdirectory of the sample or one directory up.

If you are interested in multiple samples, you can use [build_all](https://github.com/nvpro-samples/build_all) CMAKE as entry point, it will also give you options to enable/disable individual samples when creating the solutions.

### Related Samples
[gl commandlist basic](https://github.com/nvpro-samples/gl_commandlist_basic) illustrates the core principle of the NV_command_list extension.

[gl cadscene rendertechniques](https://github.com/nvpro-samples/gl_cadscene_rendertechniques) also uses the occlusion system of this sample, however in a more complex scenario, with multiple stateobjects and scene objects having their own VBO/IBO/UBO assignments. 
