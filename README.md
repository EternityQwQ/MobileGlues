> [!WARNING]
> **此项目完全由ai驱动开发**
# MobileGLESWrapper

> **注意：这是非官方版本（Unofficial Fork）**
>
> 本项目基于 [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) 修改而来，并非官方发布版本。
> 官方项目请访问：https://github.com/MobileGL-Dev/MobileGlues
>
> 本版本将项目名称及相关标识从 `MobileGlues` 修改为 `MobileGLESWrapper`，其余内容可能与官方版本存在差异。请勿向官方项目提交与本版本相关的 Issue 或 PR。

---

**MobileGLESWrapper**（原 MobileGlues，意为 "(on) Mobile, GL uses ES"）是一个运行在 OpenGL ES 3.2 之上的 GL 实现，主要面向 Minecraft: Java Edition 的运行场景。

> **注意：本项目已放弃对 OpenGL ES 3.1 及更低版本的支持，仅支持 OpenGL ES 3.2。**

> [!IMPORTANT]
> **关于 ANGLE 支持**
>
> 本 Fork 版本**不支持启用 ANGLE**。`enableANGLE` 配置项已被移除，ANGLE 后端始终处于关闭状态。如需 ANGLE 支持，请使用 [官方版本](https://github.com/MobileGL-Dev/MobileGlues)。

# 面向着色器开发者

1. MobileGLESWrapper 会自动：
   - 将桌面版 GLSL 转换为 GLSL ES
   - 移除 `layout(binding)` 语法
   - 处理版本指令
   - 始终显式声明精度：
     ```glsl
     precision highp float;
     precision highp int;
     ```

2. MobileGLESWrapper（自 V1.2.6 起）会在着色器中注入以下宏：
   ```glsl
   #define MG_MOBILEGLUES                   // 标识 MobileGlues 环境
   #define MG_MOBILEGLUES_VERSION 1260      // 版本号（如 1260 = V1.2.6）
   ```

   使用这些宏进行平台特定逻辑判断：
   ```glsl
   #ifdef MG_MOBILEGLUES
       #if MG_MOBILEGLUES_VERSION >= 1270
           // MobileGlues 版本 >= V1.2.7 的逻辑
       #else
           // MobileGlues 版本 < V1.2.7 的逻辑
       #endif
   #else
       // ...
   #endif
   ```

3. 如遇到问题：
   - 启用 `Ignore shader/program error`，并检查日志（位于 `/sdcard/MG/latest.log`）。

# 许可证

本项目基于 **GNU LGPL-2.1 License** 许可。

请参阅原始项目 [LICENSE](https://github.com/MobileGL-Dev/MobileGlues/blob/main/LICENSE)。

# 第三方组件

**SPIRV-Cross** by **KhronosGroup** - [Apache License 2.0](https://github.com/KhronosGroup/SPIRV-Cross/blob/master/LICENSE): [github](https://github.com/KhronosGroup/SPIRV-Cross)

**glslang** by **KhronosGroup** - [Various Licenses](https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt): [github](https://github.com/KhronosGroup/glslang)

**cJSON** by **DaveGamble** - [MIT License](https://github.com/DaveGamble/cJSON/blob/master/LICENSE): [github](https://github.com/DaveGamble/cJSON)

**OpenGL Mathematics (*GLM*)** by **G-Truc Creation** - [The Happy Bunny License](https://github.com/g-truc/glm/blob/master/copying.txt): [github](https://github.com/g-truc/glm)

**FidelityFX-FSR** by **AMD** - [MIT License](https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/license.txt): [github](https://github.com/GPUOpen-Effects/FidelityFX-FSR)

**Perfetto** by **Google** - [Apache License 2.0](https://github.com/google/perfetto/blob/main/LICENSE): [github](https://github.com/google/perfetto)

**xxHash** by **Yann Collet** - [BSD 2-Clause License](https://github.com/Cyan4973/xxHash/blob/dev/LICENSE): [github](https://github.com/Cyan4973/xxHash)
