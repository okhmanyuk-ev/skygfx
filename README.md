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

# Examples

## Triangle

![image](https://user-images.githubusercontent.com/3295141/173175376-c33d287d-4cc5-4070-9f08-d1379b6b4374.png)

## Texture

![image](https://user-images.githubusercontent.com/3295141/173175982-79d1f92f-76bf-4dea-adf2-973f66db4b02.png)

## Uniform Buffer

![a (4)](https://user-images.githubusercontent.com/3295141/173176075-7fdb9759-e3ca-4447-b439-2acd27f7ced9.gif)
