# sky-gfx - Lightweight rendering library

# Features
- Backend API: D3D11, OpenGL 4.4
- GLSL shaders for any backend via SPIRV-Cross
- RAII memory management over objects like Device, Shader, Texture, etc..
- Choosing backend API in runtime, no compilation definitions

# Integration

```cmake
add_subdirectory(sky-gfx)
target_link_libraries(YOUR_TARGET sky-gfx)
```

# Building Examples

## Visual Studio
1. Open the `examples` folder
2. Execute `build.cmd`
3. The `build` folder will be created in a short time and the `.sln` file will be inside

# Examples

## [Triangle](examples/triangle)

![](https://user-images.githubusercontent.com/3295141/173175376-c33d287d-4cc5-4070-9f08-d1379b6b4374.png)

## [Uniform Buffer](examples/uniform)
![](https://user-images.githubusercontent.com/3295141/185045316-e3aff95c-f5e8-44a0-ae11-d435676b88e4.gif)

## [Texture](examples/texture)

![](https://user-images.githubusercontent.com/3295141/173175982-79d1f92f-76bf-4dea-adf2-973f66db4b02.png)

## [Texture Mipmap](examples/texture_mipmap)

![](https://user-images.githubusercontent.com/3295141/173176075-7fdb9759-e3ca-4447-b439-2acd27f7ced9.gif)

## [Cube](examples/cube)

![](https://user-images.githubusercontent.com/3295141/173178283-083e54c7-488d-457f-91f1-e4685ecc3538.gif)

## [Textured Cube](examples/textured_cube)

![](https://user-images.githubusercontent.com/3295141/173226641-41363763-272a-46c4-9da5-beae22fff94c.gif)

## [Light](examples/light)

![](https://user-images.githubusercontent.com/3295141/174522886-2c72e7f0-18b1-405d-9c7b-a40eb65b5544.gif)

## [Render Target](examples/render_target)

![](https://user-images.githubusercontent.com/3295141/174523347-3e8f54bb-db2f-48e1-ab59-ef39c274915d.gif)

## [Sponza](https://github.com/okhmanyuk-ev/sky-gfx-sponza-demo)

![](https://github.com/okhmanyuk-ev/sky-gfx-sponza-demo/blob/master/assets/screenshot.png)
