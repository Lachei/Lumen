# Creating your own Integrator

This raytracing framework is written in such a way that you can easily add your own integrator or expand existing ones.

In the following the general structure of the files as well as the parts where you should insert your own code/modify existing code will be pointed out.

## Project structure
The source code of the project can be found in the `src` folder. Next to the `main.cpp` which contains the main function of the example application, the `CMakeLists` file used for cmake integration and the general `LumenPCH.h` the folder also contains 3 other folders:

- **Framework:** Here all basic abstractions used to make the use of the VulkanAPI easier are located, as well as functionality to do scene loading and texture processing.
- **RayTracer:** Contains all files related to the preimplemented integrators. Further it also contains the `LumenScene` files which handle the scene loading. If special requirements for loading are required simply adjust/add things in this file. The integrator itself then holds a reference to the loaded `LumenScene` and additional information can be accessed via this reference. A good starting point here is to look at the standard `Path` pathtracer, which implements a standard unidirectional pathtracer with multiple importance sampling and next event estimation.
- **shaders:** Here all shaders are stored. The shaders are loaded and runtime compiled automatically when they are needed. For the example application the shaders are loaded directly from this folder and can be reloaded via a simple ui button. If there should be a runtime error, always check the terminal output if something in the shader compilation went wrong (Currently it might happen that the code halts at Pipeline layout creation which might lead to wrong conclusions where the error might have occurred).

## Building and running the program

The program is setup to be compiled via cmake. To build the program simply follow the standard cmake procedure in the Lumen folder:
```
mkdir build
cd build
cmake ..
make -j8
```

When you run the so created application not that it has to be started in the top level of the `Lumen` folder. If not the shaders and resource are not found. The example application itself expects a scene to be given in the command prompt:
```
Lumen path/to/scnees/scene.json
```

If you are using vs code you can use the example configuration shown in the `examples_config` folder. The README there also contains more details on how to use it exactly.

## The general struture of the pipelines

Here we will describe the general structure of the example integrators to enable new users to quickly be able to setup their own integrators.

The base for all integrators is the `Ìntegrator` class which holds most of the needed basis data such as the resulting 2D Image (which is then automatically shown in the window) as well as data fields for the scene data such as geometry information and light information. Here also the bottom/top level acceleration structure (BLAS/TLAS) is implemented and can be adopted if different geometry types have to be supported.

All other integrators included in the project simply derive from this class and add additional resources needed for their specific rendering task. To create your own new Integrator it is advised to simply copy the Integrator closest to your needs (For beginners the `Path` integrator is advised to give an easier first overview) and simply adjust the code inside.

Each integrator consists essentially of 2 Parts:

- **Initialization** (`init()`) takes care of the initialization which includes resource creation and initialization as well as creating the ray tracing acceleration structures by calling `Ìntegrator::init()`. Note that no shaders are compiled here, nor any pipelines. These are handled automatically when they are added to the scene graph, which is described in [a later section](#scene-graph-usage).
- **Rendering** (`render()`) takes care of adding the required pipelines to the scene graph to render the final image and submitting the pipeline for execution. Here, only the shader path has to be given as well as the parameters to setup teh rendering command, without having to explicitly create hte pipeline or define the descriptor set layout. Instead the pipeline is created on demand and the resources are bound automatically by their ordering. [More infos in this section](#scene-graph-usage).


## Scene graph usage

In order to avoid boilerplate code for creating the pipelines and managing the input handling for the resources (eg creating descriptor set layouts and descriptor sets), the framework allows an automatic on demand compilation of used shaders with caching to reuse already created pipelines.

To add a ray tracing pass (in VulkanAPI this corresponds to a `vkCmdTraceRays` command) call the `add_rt(...)` function on the `instance->vkb.rg` object of the instance. Here you can specify the shaders that should be used, the image dimensions as well as the acceleration  structure. Internally it is checked if the pipeline was already created in the past and will use the cached pipeline if available or else loads the shaders, compiles them, does inspection on them to determine the descriptor set layouts needed.

The `add_rt(...)` function then returns a `RenderPass` object on which then all needed resources can be bound. `push_constants(...)` is a function which corresponds to the `vkCmdPushConstants(...)` function and will be called **before** the `vkCmdTraceRays(...)` is issued. Successive calls of `bind(...)` enables the user to bind one resource after the other, meaning that the first call will put the resource into binding slot 0, and each call binds to the next slot. Resources are then available with `layout(binding = 0) ...` in your shader code (see `commons.glsl` to see hwo the default setup looks). One can also insert multiple resources at once to the `bind(...)` function to bind multiple resources at once.

There do exist `àdd_gfx(...)` and `àdd_compute(...)` calls which enable the user to create a standard graphics and compute pipeline as well.

To finally run all prerecorded pipelines simply call `instance->vkb.rg->run_and_submit(cmd)` on the instance.

## Using the new integrator

