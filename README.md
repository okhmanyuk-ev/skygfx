# sky-gfx - Lightweight rendering library

# Features
- Supported platforms: Windows, Mac, iOS, Emscripten
- Supported graphic backends: OpenGL(ES), D3D11, D3D12, Metal, Vulkan
- GLSL shaders for any backend via SPIRV-Cross
- Choosing backend API in runtime, no compilation definitions

# Integration

```cmake
add_subdirectory(skygfx)
target_link_libraries(YOUR_TARGET skygfx)
```

# Building Examples

## Visual Studio
1. Open the `examples` folder
2. Execute `build.sh`
3. The `build` folder will be created in a short time and the `.sln` file will be inside

## Xcode
1. Open terminal and go to the `examples` folder
2. Run `sh build.sh` command 
3. The `build` folder will be created in a short time and the `.xcodeproj` file will be inside

# Examples

## [Triangle](examples/01_triangle)

![](https://user-images.githubusercontent.com/3295141/173175376-c33d287d-4cc5-4070-9f08-d1379b6b4374.png)

## [Uniform Buffer](examples/02_uniform)

![](https://user-images.githubusercontent.com/3295141/185045316-e3aff95c-f5e8-44a0-ae11-d435676b88e4.gif)

## [Texture](examples/03_texture)

![](https://user-images.githubusercontent.com/3295141/173175982-79d1f92f-76bf-4dea-adf2-973f66db4b02.png)

## [Texture Mipmap](examples/04_texture_mipmap)

![](https://user-images.githubusercontent.com/3295141/173176075-7fdb9759-e3ca-4447-b439-2acd27f7ced9.gif)

## [Cube](examples/05_cube)

![](https://user-images.githubusercontent.com/3295141/173178283-083e54c7-488d-457f-91f1-e4685ecc3538.gif)

## [Textured Cube](examples/06_textured_cube)

![](https://user-images.githubusercontent.com/3295141/173226641-41363763-272a-46c4-9da5-beae22fff94c.gif)

## [Light](examples/07_light)

![](https://user-images.githubusercontent.com/3295141/174522886-2c72e7f0-18b1-405d-9c7b-a40eb65b5544.gif)

## [Render Target](examples/08_render_target)

![](https://user-images.githubusercontent.com/3295141/174523347-3e8f54bb-db2f-48e1-ab59-ef39c274915d.gif)

## [Sponza](https://github.com/okhmanyuk-ev/sky-gfx-sponza-demo)

![](https://github.com/okhmanyuk-ev/sky-gfx-sponza-demo/blob/master/assets/screenshot.png)
