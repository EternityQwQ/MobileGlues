# MobileGlues 性能优化开发目标（详细版）

## 项目概述
MobileGlues 是一个 OpenGL 到 OpenGL ES 的转换层，用于在移动设备上运行 Minecraft: Java Edition 等桌面 OpenGL 应用。本文档基于深度代码审查（含 60+ 个已识别性能问题），制定详细的、可执行的开发目标。

---

## 优先级定义

| 级别 | 含义 | 判定标准 |
|------|------|----------|
| **CRITICAL** | 第一优先级 | 每帧绘制热路径上、直接影响帧率的瓶颈 |
| **HIGH** | 第二优先级 | 显著影响性能，但不在每帧热路径上；或影响面广的基础设施问题 |
| **MEDIUM** | 第三优先级 | 有明显优化空间，但影响范围有限或仅特定场景触发 |
| **LOW** | 可延后 | 初始化阶段一次性开销、微优化、代码质量改进 |

---

## 第一部分：CRITICAL — 绘制管线热路径优化

---

### 目标 1：引入完整的 GL 状态脏标志追踪系统

**严重程度**：CRITICAL  
**影响面**：整个绘制管线  
**原因**：这是多数绘制热路径性能问题的**根本原因**——代码无法判断状态是否发生变化，被迫在每个 draw call 上重复执行昂贵的查询和设置操作。

#### 1.1 当前状态

[gl_state_s](file:///workspace/MobileGlues-cpp/gl/mg.h#L70-L78) 仅追踪 6 个值：

```cpp
struct gl_state_s {
    int proxy_width, proxy_height;
    GLenum proxy_intformat;
    GLuint current_program;
    GLuint current_tex_unit;
    GLuint current_draw_fbo;
};
```

**完全缺失**：
- 任何脏标志（dirty flag）或 generation counter
- 每纹理单元的当前绑定纹理 ID
- 当前绑定 VAO / 元素数组缓冲
- Blend / depth / stencil 状态
- `glUseProgram` 变化的通知机制
- 绑定的 SSBO 状态
- Uniform 值缓存

#### 1.2 具体问题清单

| 问题位置 | 描述 |
|----------|------|
| [drawing.cpp:L69-L100](file:///workspace/MobileGlues-cpp/gl/drawing.cpp#L69-L100) | `prepareForDraw()` 无条件调用 `setupBufferTextureUniforms`，即使 program 和纹理均未变化 |
| [drawing.cpp:L69-L92](file:///workspace/MobileGlues-cpp/gl/drawing.cpp#L69-L92) | `setupBufferTextureUniforms` 每次遍历所有 sampler，对每个执行 `glActiveTexture` + `glGetIntegerv` + 3 次 `glUniform` |
| [multidraw.cpp:L88](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L88) | 绕过自身 `g_bound_buffers_arr`，直接用 `glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING)` 查询 |
| [multidraw.cpp:L691-L713](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L691-L713) | Compute draw 路径用 5 次 `glGetIntegeri_v` 保存 SSBO 绑定 + `glGetIntegerv` 查询 program 和 array buffer |
| [getter.cpp:L58-L64](file:///workspace/MobileGlues-cpp/gl/getter.cpp#L58-L64) | `GL_MAX_TEXTURE_IMAGE_UNITS` 每次都查询驱动，从未缓存 |
| [gl.cpp:L88-L111](file:///workspace/MobileGlues-cpp/gl/gl.cpp#L88-L111) | `DrawDepthClearTri` 每次用 `glGetBooleanv`/`glGetIntegerv` 保存恢复 depth/color mask |

#### 1.3 实施方案

**步骤 1：扩展 `gl_state_s` 结构体**

在 [mg.h](file:///workspace/MobileGlues-cpp/gl/mg.h) 中扩展状态结构：

```cpp
struct gl_state_s {
    // 现有字段（保留）
    int proxy_width, proxy_height;
    GLenum proxy_intformat;
    GLuint current_program;
    GLuint current_tex_unit;
    GLuint current_draw_fbo;

    // 新增：Generation counter
    uint32_t state_generation;          // 任意状态变更时自增
    uint32_t program_generation;        // glUseProgram 时自增
    uint32_t texture_generation;        // 纹理绑定变更时自增

    // 新增：纹理单元绑定状态（支持多纹理单元）
    GLuint bound_textures_2d[MAX_TEXTURE_UNITS];      // 每单元当前绑定的 2D 纹理
    GLuint bound_textures_buffer[MAX_TEXTURE_UNITS];  // 每单元当前绑定的 buffer 纹理

    // 新增：缓冲区绑定状态
    GLuint bound_array_buffer;
    GLuint bound_element_array_buffer;
    GLuint bound_draw_indirect_buffer;
    GLuint bound_ssbo[MAX_SSBO_BINDINGS];

    // 新增：当前 VAO 和程序
    GLuint current_vao;
    uint32_t program_sampler_hash;     // 程序 sampler 布局的哈希（用于快速变化检测）
};
```

**步骤 2：添加 generation counter 更新宏**

```cpp
#define BUMP_STATE()    do { gl_state->state_generation++; } while(0)
#define BUMP_PROGRAM()  do { gl_state->program_generation++; BUMP_STATE(); } while(0)
#define BUMP_TEXTURE()  do { gl_state->texture_generation++; BUMP_STATE(); } while(0)
```

**步骤 3：在各 GL 函数包装中更新状态**

在以下函数中添加状态更新：
- `glUseProgram` → `program_generation++`，设置 `current_program`
- `glBindTexture` → 更新对应单元的 `bound_textures_*`，`texture_generation++`
- `glActiveTexture` → 设置 `current_tex_unit`
- `glBindBuffer` → 更新 `bound_*_buffer`，根据 target
- `glBindVertexArray` → 设置 `current_vao`
- `glBindBufferBase` / `glBindBufferRange` → 更新 `bound_ssbo[]`

**步骤 4：在 `prepareForDraw` 中使用 generation counter**

在 [drawing.cpp](file:///workspace/MobileGlues-cpp/gl/drawing.cpp) 中修改：

```cpp
// 添加 per-draw 状态缓存
static uint32_t last_draw_program_gen = 0;
static uint32_t last_draw_texture_gen = 0;
static GLuint last_draw_program = 0;

void prepareForDraw() {
    if (hardware->emulate_texture_buffer) {
        // 仅在 program 或纹理绑定发生变化时才重新设置
        if (last_draw_program != gl_state->current_program ||
            last_draw_program_gen != gl_state->program_generation ||
            last_draw_texture_gen != gl_state->texture_generation) {
            
            setupBufferTextureUniforms(gl_state->current_program);
            
            last_draw_program = gl_state->current_program;
            last_draw_program_gen = gl_state->program_generation;
            last_draw_texture_gen = gl_state->texture_generation;
        }
    }
}
```

**步骤 5：替换冗余的 `glGet*` 调用**

| 位置 | 当前做法 | 改为 |
|------|----------|------|
| [multidraw.cpp:L88](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L88) | `glGetIntegerv(GL_DRAW_INDIRECT_BUFFER_BINDING)` | `gl_state->bound_draw_indirect_buffer` |
| [multidraw.cpp:L691-L695](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L691-L695) | 5 次 `glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING)` | `gl_state->bound_ssbo[i]` |
| [multidraw.cpp:L709-L713](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L709-L713) | `glGetIntegerv(GL_CURRENT_PROGRAM)` + `glGetIntegerv(GL_ARRAY_BUFFER_BINDING)` | `gl_state->current_program` + `gl_state->bound_array_buffer` |
| [getter.cpp:L58-L64](file:///workspace/MobileGlues-cpp/gl/getter.cpp#L58-L64) | 每次 `glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS)` 查询驱动 | 初始化时查询一次，缓存到 `gl_state` |
| [gl.cpp:L88-L111](file:///workspace/MobileGlues-cpp/gl/gl.cpp#L88-L111) | `DrawDepthClearTri` 中 `glGetBooleanv` 保存掩码 | 从 `gl_state` 读取缓存的 depth/color mask |

#### 1.4 修改文件清单

- `gl/mg.h` — 扩展 `gl_state_s`，添加 generation counter
- `gl/mg.cpp` — 初始化新状态字段
- `gl/drawing.cpp` — 在 `prepareForDraw` 中使用 generation 检测
- `gl/gl.cpp` — `DrawDepthClearTri` 使用缓存状态
- `gl/multidraw.cpp` — 用缓存状态替代 `glGet*` 查询
- `gl/getter.cpp` — 缓存 `GL_MAX_TEXTURE_IMAGE_UNITS`
- `gl/program.cpp` — `glUseProgram` 更新 generation
- `gl/texture.cpp` — 纹理绑定更新 generation
- `gl/buffer.cpp` — 缓冲区绑定更新 generation

#### 1.5 预期收益

- 减少 70-90% 的 draw call 热路径上的 GLES 查询调用
- 消除绝大部分 "即使状态未变也重复设置" 的开销
- 为后续所有优化提供基础设施

---

### 目标 2：消除 DrawCall 热路径中的 malloc/free

**严重程度**：CRITICAL  
**影响面**：所有 `glDrawElementsBaseVertex` 调用及 MultiDraw 回退路径

#### 2.1 问题清单

| 位置 | 问题描述 |
|------|----------|
| [drawing.cpp:L192](file:///workspace/MobileGlues-cpp/gl/drawing.cpp#L192) | `glDrawElementsBaseVertex` 模拟路径每次 draw call 执行 `malloc(count * indexSize)` + `free` |
| [multidraw.cpp:L147-L238](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L147-L238) | MultiDraw 回退在 `primcount` 循环内每迭代一次做一次 `malloc` + `BufferPool_Acquire` + `BufferPool_Release` + `free` |
| [multidraw.cpp:L181](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L181) | 循环内 `malloc(bufferSize)`，循环内 `free` |

对于每帧 1000+ draw call 的应用，这意味着每帧 1000+ 次 malloc/free 配对。

#### 2.2 实施方案

**方案 A：线程局部预分配缓冲区（推荐用于小/中等大小的 draw call）**

```cpp
// 在 drawing.cpp 顶部
static thread_local std::vector<uint8_t> tls_index_buffer;
static thread_local size_t tls_index_buffer_capacity = 0;

// 在 glDrawElementsBaseVertex 中替换 malloc/free：
size_t required_size = count * indexSize;
if (tls_index_buffer_capacity < required_size) {
    tls_index_buffer.resize(required_size * 2);  // 预留增长空间
    tls_index_buffer_capacity = required_size * 2;
}
void* tempIndices = tls_index_buffer.data();
// ... 使用 tempIndices ...
// 不需要 free！线程本地缓冲在下次 draw call 时复用
```

**方案 B：对于 MultiDraw 回退，跨迭代复用缓冲区**

```cpp
// 在 mg_glMultiDrawElementsBaseVertex_drawelements 中：
// 将 malloc/BufferPool_Acquire 移出循环
size_t max_buffer_size = 0;
for (GLsizei i = 0; i < primcount; ++i) {
    max_buffer_size = std::max(max_buffer_size, counts[i] * indexSize);
}
void* tempIndices = malloc(max_buffer_size);
GLuint tempBuffer = BufferPool_Acquire(GL_ELEMENT_ARRAY_BUFFER, max_buffer_size, GL_DYNAMIC_DRAW);

for (GLsizei i = 0; i < primcount; ++i) {
    // 复用 tempIndices 和 tempBuffer，只拷贝不同的 count 数据
    // ...
}
BufferPool_Release(tempBuffer, max_buffer_size);
free(tempIndices);
```

**方案 C：使用 `alloca` 处理小尺寸 draw call**

对于 `count <= 256` 的 draw call，使用栈分配：

```cpp
if (allocSize <= STACK_ALLOC_THRESHOLD) {
    tempIndices = alloca(allocSize);
} else {
    // 使用方案 A 或 B
}
```

#### 2.3 修改文件清单

- `gl/drawing.cpp` — `glDrawElementsBaseVertex` / `glDrawElementsBaseVertexInstanced`
- `gl/multidraw.cpp` — `mg_glMultiDrawElementsBaseVertex_drawelements` / 其他回退变体

#### 2.4 预期收益

- 消除每帧数千次 malloc/free 调用
- CPU 分配器压力降低 90%+
- 多绘制场景下 CPU 帧时间减少 15-25%

---

### 目标 3：消除 DrawCall 热路径中的 O(N) 标量索引偏移循环

**严重程度**：CRITICAL  
**位置**：[drawing.cpp:L213-L229](file:///workspace/MobileGlues-cpp/gl/drawing.cpp#L213-L229)  
**同样位置**：[multidraw.cpp:L201-L217](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L201-L217)

#### 3.1 问题描述

每个 basevertex draw call 都对 N 个索引元素逐一做标量 `+= basevertex`：

```cpp
for (int j = 0; j < count; ++j) {
    ((GLuint*)tempIndices)[j] += basevertex;
}
```

这是纯标量代码，编译器很难自动向量化（因为 `basevertex` 是运行时变量）。

#### 3.2 实施方案

**方案 A：使用 `std::transform` + 编译器向量化提示**

```cpp
#include <algorithm>
std::transform((GLuint*)tempIndices, (GLuint*)tempIndices + count,
               (GLuint*)tempIndices,
               [bv = (GLuint)basevertex](GLuint idx) { return idx + bv; });
```

**方案 B：使用 NEON SIMD 内在函数（ARM/Android）**

```cpp
#ifdef __ARM_NEON
#include <arm_neon.h>
// 每次处理 4 个 uint32
uint32x4_t bv_vec = vdupq_n_u32((uint32_t)basevertex);
for (int j = 0; j + 3 < count; j += 4) {
    uint32x4_t idx = vld1q_u32(&((GLuint*)tempIndices)[j]);
    idx = vaddq_u32(idx, bv_vec);
    vst1q_u32(&((GLuint*)tempIndices)[j], idx);
}
// 处理尾部
for (int j = (count / 4) * 4; j < count; ++j) {
    ((GLuint*)tempIndices)[j] += basevertex;
}
#endif
```

**方案 C：在 Compute Shader MultiDraw 路径中利用 GPU 并行**

Compute shader 变体（已存在但仅用于 baseVertex 场景）可以扩展以在 GPU 上并行处理索引偏移。

#### 3.3 预期收益

- 索引偏移循环速度提升 2-4x（NEON）或 1.5-2x（编译器自动向量化）
- 对大量索引的 draw call 效果尤其明显

---

### 目标 4：MultiDraw 回退路径真正的批处理合并

**严重程度**：CRITICAL  
**位置**：[multidraw.cpp:L147-L238](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L147-L238)

#### 4.1 问题描述

`mg_glMultiDrawElementsBaseVertex_drawelements` 名为"批处理"，实际在 `primcount` 循环内逐个调用 `glDrawElements`，每个迭代完成一次完整的 acquire-draw-release 周期。对于 `primcount=100`，这是 100 次独立 draw。

#### 4.2 实施方案

**方案 A：合并索引到一次 glDrawElements**

当所有子 draw 使用相同的 index type 且 basevertex 相同时，将所有索引合并到一个连续缓冲区，然后调用一次 `glDrawElements`（使用 primitive restart 分隔）。

**方案 B：使用 glMultiDrawElementsIndirectEXT 替代循环**

```cpp
// 构建 DrawElementsIndirectCommand 数组
std::vector<DrawElementsIndirectCommand> commands(primcount);
for (GLsizei i = 0; i < primcount; ++i) {
    commands[i] = { counts[i], 1, first_indices[i], base_vertices[i] };
}
// 一次调用
GLES.glMultiDrawElementsIndirectEXT(mode, type, commands.data(), primcount, 0);
```

**方案 C：改进 Compute Shader 变体以覆盖非 baseVertex 场景**

当前 [multidraw.cpp:L328-L342](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L328-L342) 的 compute 变体只是一个普通的 draw 循环——完全没有使用 compute shader。应在非 baseVertex 场景也实现 compute shader 合并。

#### 4.3 预期收益

- MultiDraw 场景 GPU draw call 数量从 N 降为 1
- CPU 端驱动开销大幅减少

---

## 第二部分：HIGH — GLSL 着色器处理管线优化

---

### 目标 5：消除着色器处理管线中的 `std::regex` 滥用

**严重程度**：HIGH  
**影响面**：每次着色器编译/链接  
**根因**：Android NDK 的 libstdc++ 中 `std::regex` 实现性能极差

#### 5.1 问题清单（按影响严重程度排序）

**5.1.A 循环内构造正则对象** — [glsl_for_es.cpp:L511-L521](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L511-L521)

```cpp
for (auto& var : atomic_vars) {
    source = std::regex_replace(source, std::regex(R"(\batomicCounterIncrement\s*...)" + var + ...));
    source = std::regex_replace(source, std::regex(R"(\batomicCounterDecrement\s*...)" + var + ...));
    source = std::regex_replace(source, std::regex(R"(\batomicCounterAdd\s*...)" + var + ...));
    source = std::regex_replace(source, std::regex(R"(\batomicCounter\s*...)" + var + ...));
}
```

每次迭代构造 4 个 `std::regex`，每次 `regex_replace` 返回完整字符串拷贝。

**实施方案**：改为 `find()` + `replace()` 纯字符串操作：

```cpp
for (auto& var : atomic_vars) {
    // atomicCounterIncrement(var) -> atomicAdd(var, 1u)
    std::string pattern = "atomicCounterIncrement(" + var + ")";
    size_t pos = 0;
    while ((pos = source.find(pattern, pos)) != std::string::npos) {
        source.replace(pos, pattern.length(), "atomicAdd(" + var + ", 1u)");
    }
    // ... 对其他模式做同样处理 ...
}
```

**5.1.B `removeLayoutBinding` 每次两次 regex_replace** — [glsl_for_es.cpp:L207-L211](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L207-L211)

**实施方案**：`layout(binding = N)` 是简单固定模式，用 `find("layout")` + 手动扫描括号匹配即可。

**5.1.C `updateLayoutLocation` 每次构造新正则 + 三次拷贝** — [program.cpp:L37-L51](file:///workspace/MobileGlues-cpp/gl/program.cpp#L37-L51)

**实施方案**：查找 `out ... name` 位置，就地插入 `layout(location=N)` 前缀。

**5.1.D `inject_temporal_filter` 用 `sregex_iterator` 扫描全量** — [glsl_for_es.cpp:L648-L656](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L648-L656)

**实施方案**：从字符串尾部用 `rfind("uniform ")` 向前搜索，找到最后一个 uniform 声明位置。

**5.1.E `getGLSLVersion` 用正则匹配固定格式** — [glsl_for_es.cpp:L138-L147](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L138-L147)

**实施方案**：`#version` 总是在前几个字符，用 `strncmp(glsl_code, "#version ", 9)` + `atoi(glsl_code + 9)`。

**5.1.F `processOutColorLocations` 用正则替换固定模式** — [glsl_for_es.cpp:L321-L325](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L321-L325)

**实施方案**：`find("outColor")` + `insert` 添加 `layout(location=N)`。

#### 5.2 修改文件清单

- `gl/glsl/glsl_for_es.cpp` — 主要修改文件（约 8 处正则替换）
- `gl/program.cpp` — `updateLayoutLocation`

#### 5.3 预期收益

- 着色器预处理速度提升 5-20x（取决于正则替代率）
- 消除 Android NDK `std::regex` 的最大性能瓶颈

---

### 目标 6：大幅减少着色器处理管线中的完整字符串拷贝

**严重程度**：HIGH  
**影响面**：每次着色器编译

#### 6.1 问题清单

| 位置 | 问题 | 拷贝次数 |
|------|------|----------|
| [glsl_for_es.cpp:L333-L336](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L333-L336) | 构造缓存 key 时全量拷贝 shader 源码 | 1 |
| [glsl_for_es.cpp:L706](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L706) | `preprocess_glsl` 按值传递初始拷贝 | 1 |
| [glsl_for_es.cpp:L709](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L709) | `replace_line_starting_with` 返回新 string | 1 |
| [glsl_for_es.cpp:L149-L203](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L149-L203) | `forceSupporterOutput` 逐行拆分重建 | 2-3 |
| [glsl_for_es.cpp:L882-L887](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L882-L887) | `GLSLtoGLSLES_2` 中多个处理步骤 | 每次 1 |
| [cache.cpp:L53-L61](file:///workspace/MobileGlues-cpp/gl/glsl/cache.cpp#L53-L61) | `computeSHA256` 缓存查找仍需拷贝 | 1 |
| [glsl_for_es.cpp:L339](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L339) | 为一次 `find()` 构造临时 `std::string` | 1 |

**合计**：一个 50KB shader 约产生 500KB-1MB 临时内存分配。

#### 6.2 实施方案

**步骤 1：合并 `preprocess_glsl` 的多趟扫描为一遍**

将 [glsl_for_es.cpp:L706-L736](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L706-L736) 中 7-8 次独立的全量扫描合并为单趟状态机扫描：

```cpp
std::string preprocess_glsl(const std::string& glsl, ...) {
    std::string result;
    result.reserve(glsl.size() * 1.1);  // 预分配，避免多次扩容
    // 单趟扫描：同时处理 replace_all、inject、process_* 的逻辑
    // ... 
    return result;
}
```

**步骤 2：`forceSupporterOutput` 就地修改替代拆分重建**

不使用 `istringstream` 逐行拆分，改为就地：
- 找到 precision 行位置
- 用双指针技术删除行：读指针扫描、写指针跟踪输出位置

**步骤 3：缓存 key 用 `std::string_view` + 版本参数**

修改 `Cache::get()` 接口，接受 `const char* source` + 版本号参数，避免构造 key 时的全量拷贝。

**步骤 4：修复 `checkIfAtomicCounterBufferEmulated` 的临时拷贝**

[glsl_for_es.cpp:L339](file:///workspace/MobileGlues-cpp/gl/glsl/glsl_for_es.cpp#L339)：
```cpp
// 当前：
bool atomicCounterEmulated = checkIfAtomicCounterBufferEmulated(std::string(cachedESSL));
// 改为：
bool atomicCounterEmulated = strstr(cachedESSL, atomicCounterEmulatedWatermark) != nullptr;
```

**步骤 5：修复 `glBindFragDataLocation` 的多重拷贝**

[program.cpp:L77-L100](file:///workspace/MobileGlues-cpp/gl/program.cpp#L77-L100) 当前执行 `malloc` + `strcpy` → `std::string` 构造 → `regex_replace` → `new char[]` + `strcpy`，共 4 次全量拷贝。

改为就地修改 `shaderInfo.converted`（配合 `std::string` 的 `find` + `insert`），零额外分配。

#### 6.3 修改文件清单

- `gl/glsl/glsl_for_es.cpp` — 主要修改
- `gl/glsl/cache.cpp` + `cache.h` — key 构造优化
- `gl/program.cpp` — `glBindFragDataLocation` 优化

#### 6.4 预期收益

- 着色器编译路径内存分配减少 60-80%
- 对大 shader（100KB+）提速尤其明显

---

### 目标 7：用 xxHash 替代 SHA-256 作为着色器缓存 key

**严重程度**：MEDIUM-HIGH  
**位置**：[cache.cpp:L53-L61](file:///workspace/MobileGlues-cpp/gl/glsl/cache.cpp#L53-L61)

#### 7.1 问题描述

1. SHA-256 加密哈希对缓存 key 来说过强，计算开销大
2. 每次 `computeSHA256` 命中哈希缓存（`g_hashCache`）时，仍先做 `std::string key(data)` 全量拷贝
3. `SHA256Hash::operator()`（[cache.cpp:L121-L130](file:///workspace/MobileGlues-cpp/gl/glsl/cache.cpp#L121-L130)）只用 8 字节构造 `size_t`

#### 7.2 实施方案

1. **替换哈希算法**：使用项目已集成的 xxHash（`xxh3` 或 `xxh64`），速度比 SHA-256 快 20-100x
2. **改进缓存查找**：使用 `std::string_view` 做异质查找，避免构造临时 `std::string`：
   ```cpp
   // 使用 unordered_map with transparent hash
   struct StringViewHash {
       using is_transparent = void;
       size_t operator()(std::string_view sv) const { return XXH64(sv.data(), sv.size(), 0); }
   };
   // 直接用 string_view 查找，零分配
   auto hit = g_hashCache.find(std::string_view(data));
   ```
3. **简化哈希函数**：用 `XXH64` 产生 64 位哈希，直接作为 map key

---

### 目标 8：为 300+ GL 包装函数添加 always_inline

**严重程度**：HIGH  
**位置**：[gles/loader.h:L103-L117](file:///workspace/MobileGlues-cpp/gles/loader.h#L103-L117)  
**影响面**：所有 natvie GL 函数调用（`gl_native.cpp` 中 300+ 函数）

#### 8.1 问题描述

`NATIVE_FUNCTION_END` 宏生成的函数（如 `glUniform4f`、`glDrawArrays`、`glBindTexture` 等高频 GL 调用）都是普通外部链接函数，体积极小（一次 LOG + 一次函数指针调用），但编译器不一定内联它们。

#### 8.2 实施方案

在 `NATIVE_FUNCTION_HEAD` 宏中添加 `inline` 和 `__attribute__((always_inline))`：

```cpp
#define NATIVE_FUNCTION_HEAD(type, name, ...) \
    __attribute__((always_inline)) static inline type name(__VA_ARGS__) {
```

> **注意**：`gl_native.cpp` 中的函数需要被外部调用，不能简单改为 `static inline`。需要将函数定义移到头文件中，或依赖 LTO 内联。

更安全的方案（保持 ABI 兼容）：

```cpp
// 在 gles/loader.h 中
#define NATIVE_FUNCTION(...) \
    extern "C" __attribute__((flatten, hot)) NATIVE_FUNCTION_IMPL(...)
```

`__attribute__((flatten))` 强制内联函数体内所有调用；`hot` 提示编译器此函数是热路径。

#### 8.3 预期收益

- 高频 GL 调用消除函数调用的栈帧开销
- 配合 LTO，编译器有更多优化机会

---

## 第三部分：MEDIUM — 基础设施与特定场景优化

---

### 目标 9：修复 DSA 包装器的 Save/Query/Restore 反模式

**严重程度**：MEDIUM  
**位置**：[DSAWrapper.cpp](file:///workspace/MobileGlues-cpp/gl/ExtWrappers/DSAWrapper.cpp)

#### 9.1 问题描述

5 个 `temporarilyBind*` 函数（buffer/framebuffer/renderbuffer/texture/vertexarray，分别在 L114、L430、L759、L878、L1237）都执行相同的模式：`glGetIntegerv`(查询绑定) → `glBind*`(绑定目标) → 操作 → `glBind*`(恢复)。

每次 DSA 操作产生 2-3 次额外 GL 调用，其中 `glGetIntegerv` 最为昂贵。

#### 9.2 实施方案

利用目标 1 中建立的 `gl_state` 追踪系统，将 `glGetIntegerv(bindingQuery, &prev)` 替换为从 `gl_state` 读取缓存值：

```cpp
void temporarilyBindBuffer(GLuint bufferID, GLenum target = GL_ARRAY_BUFFER) {
    // 从本地缓存获取当前绑定，无需查询驱动
    GLuint prev = gl_state_get_bound_buffer(target);
    if (prev == bufferID) return;  // 已经绑定，无需操作
    GLES.glBindBuffer(target, bufferID);
    // 将恢复操作推入栈...
}
```

#### 9.3 预期收益

- DSA 操作路径消除 2 次 `glGetIntegerv` 驱动查询
- 消除 5 个 `thread_local map` 的运行时 overhead（改用固定大小数组）

---

### 目标 10：修复 FSR1 每帧冗余 `glGetUniformLocation`

**严重程度**：MEDIUM-HIGH  
**位置**：[FSR1.cpp:L326-L332](file:///workspace/MobileGlues-cpp/gl/FSR1/FSR1.cpp#L326-L332)

#### 10.1 问题描述

`ApplyFSR()` 每帧调用 3 次 `glGetUniformLocation("uInputTex")` / `("uConst0")` / `("uViewportSize")` 进行字符串哈希查找。但 [InitFSRResources](file:///workspace/MobileGlues-cpp/gl/FSR1/FSR1.cpp#L210-L212) 中已获取了 `const0Loc` 和 `viewportSizeLoc`，却完全未被使用。

#### 10.2 实施方案

```cpp
// InitFSRResources() 中已获取：
GLint inputTexLoc  = glGetUniformLocation(program, "uInputTex");
GLint const0Loc    = glGetUniformLocation(program, "uConst0");
GLint viewportLoc  = glGetUniformLocation(program, "uViewportSize");

// 存储为全局/静态变量，AppyFSR 中直接使用
void ApplyFSR() {
    // 不要调用 glGetUniformLocation
    GLES.glUniform1i(inputTexLoc, 0);
    GLES.glUniform4fv(const0Loc, 1, ...);
    GLES.glUniform2fv(viewportLoc, 1, ...);
}
```

在 [FSR1.h](file:///workspace/MobileGlues-cpp/gl/FSR1/FSR1.h) 的 `FSR1_Context` 中增加 3 个 `GLint` 字段存储这些 location。

---

### 目标 11：日志系统惰性求值与去 fflush

**严重程度**：MEDIUM  
**位置**：[mg.cpp:L43](file:///workspace/MobileGlues-cpp/gl/mg.cpp#L43)、[log.h](file:///workspace/MobileGlues-cpp/gl/log.h)

#### 11.1 问题清单

| 位置 | 问题 |
|------|------|
| [mg.cpp:L43](file:///workspace/MobileGlues-cpp/gl/mg.cpp#L43) | 每条日志都 `fflush(file)`，强制内核态同步 |
| [log.h:L77](file:///workspace/MobileGlues-cpp/gl/log.h#L77) | Android 上同时使用 `__android_log_print` 和 `printf`（双重输出） |
| [log.h:L85-L86](file:///workspace/MobileGlues-cpp/gl/log.h#L85-L86) | `printf(__VA_ARGS__)` + `printf("\n")` 各一次调用 |

#### 11.2 实施方案

1. **去 fflush**：将 `fflush` 移至受 `FORCE_SYNC_WITH_LOG_FILE` 控制的 `fsync` 之后，或使用行缓冲模式 `setlinebuf(file)`
2. **合并 printf 调用**：`printf(__VA_ARGS__ "\n")` 替代两次调用
3. **Android 上去 printf**：Android 上 `printf` 输出最终也进 logcat，属于双重输出。在 Android 构建中仅保留 `__android_log_print`

---

### 目标 12：修复内存泄漏

**严重程度**：MEDIUM  
**影响**：长期运行稳定性

#### 12.1 泄漏清单

| 位置 | 泄漏内容 | 触发条件 |
|------|----------|----------|
| [config.cpp:L46-L48](file:///workspace/MobileGlues-cpp/config/config.cpp#L46-L48) | `concatenate()` 返回的 `new char[]` 作为全局指针，永不释放 | 初始化 |
| [log.cpp:L1121](file:///workspace/MobileGlues-cpp/gl/log.cpp#L1121) | `concatenate(mg_directory_path, "/glcalls.txt")` 临时结果直接泄漏 | 每次 `start_log()` |
| [texture.cpp:L1163](file:///workspace/MobileGlues-cpp/gl/texture.cpp#L1163) | `concatenate(mg_directory_path, "/readpixels/")` 临时结果泄漏 | 每次截帧 |
| [getter.cpp:L395-L397](file:///workspace/MobileGlues-cpp/gl/getter.cpp#L395-L397) | `glGetString(GL_SETTINGS_MG)` 中 `strdup` 覆盖旧值，旧值泄漏 | 每次调用 |
| [getter.cpp:L464-L471](file:///workspace/MobileGlues-cpp/gl/getter.cpp#L464-L471) | `glGetStringi` 中 `strdup` 缓存永不释放 | 初始化 |
| [gpu_utils.cpp:L236-L248](file:///workspace/MobileGlues-cpp/config/gpu_utils.cpp#L236-L248) | `hasVulkan12()` 找到目标时提前返回泄漏 `physicalDevices` + `vulkan_lib` | 支持 Vulkan 1.2 的设备 |
| [gles/loader.cpp:L96-L105](file:///workspace/MobileGlues-cpp/gles/loader.cpp#L96-L105) | `hardware` + `gl_state` 的 `new` 分配永不 `delete` | 初始化 |

#### 12.2 实施方案

1. **`concatenate()`**：改为返回 `std::string`，或调用方使用 `std::string` 拼接后 `.c_str()` 传入 `fopen`
2. **`glGetString` strdup**：`free` 旧值后再 `strdup` 新值
3. **`glGetStringi`**：在 `gl_state` 中添加清理标志，或在恰当位置（如 context 销毁时）释放
4. **`hasVulkan12()`**：将 `free(physicalDevices)` 和 `dlclose(vulkan_lib)` 移到提前返回路径之前，或使用 RAII 包装
5. **`hardware`/`gl_state`**：添加 `delete` 调用（如在 `__attribute__((destructor))` 函数中）

---

### 目标 13：Compute MultiDraw 路径 SSBO 复用

**严重程度**：MEDIUM  
**位置**：[multidraw.cpp:L668-L684](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp#L668-L684)

#### 13.1 问题描述

每次 Compute MultiDraw 调用都用 `glBufferData(GL_DYNAMIC_DRAW)` 分配 4 个新 SSBO。首次分配后，后续调用应使用 `glBufferSubData` 上传数据，避免将 buffer 变成孤儿。

#### 13.2 实施方案

```cpp
// 首次分配或扩容
if (first_use || need_resize) {
    GLES.glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_DYNAMIC_DRAW);
    first_use = false;
} else {
    GLES.glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, size, data);
}
```

使用静态/全局变量跟踪当前 SSBO 分配大小。

---

### 目标 14：`hasVulkan12()` 启动时创建 Vulkan 实例开销优化

**严重程度**：LOW-MEDIUM  
**位置**：[gpu_utils.cpp:L167-L258](file:///workspace/MobileGlues-cpp/config/gpu_utils.cpp#L167-L258)

#### 14.1 问题描述

`init_settings()` 无条件调用 `hasVulkan12()`，加载 `libvulkan`、创建 Vulkan 实例并枚举物理设备，仅为了检查 API 版本。即使应用不使用 Vulkan 也会触发。

#### 14.2 实施方案

1. 用 `dlopen` + `dlsym` 直接获取 `vkEnumerateInstanceVersion`（Vulkan 1.1+），无需创建实例
2. 或将检查延迟到首次需要时（懒加载）

---

## 第四部分：汇总 — 开发实施顺序

### Phase 1：基础状态追踪（预计收益 20-30%）

| 序号 | 目标 | 修改文件 | 依赖 |
|------|------|----------|------|
| 1-1 | 目标 1 — 扩展 `gl_state_s` + generation counter | `mg.h`, `mg.cpp` | 无 |
| 1-2 | 目标 1 — 在各 GL 包装中更新状态 | `program.cpp`, `texture.cpp`, `buffer.cpp` | 1-1 |
| 1-3 | 目标 1 — `prepareForDraw` 使用 generation 检测 | `drawing.cpp` | 1-1, 1-2 |
| 1-4 | 目标 1 — 替换冗余 `glGet*` 调用 | `multidraw.cpp`, `getter.cpp`, `gl.cpp` | 1-1, 1-2 |

### Phase 2：热路径消除分配（预计收益 10-15%）

| 序号 | 目标 | 修改文件 | 依赖 |
|------|------|----------|------|
| 2-1 | 目标 2 — DrawCall 热路径去 malloc/free | `drawing.cpp` | Phase 1 |
| 2-2 | 目标 3 — 索引偏移 SIMD 向量化 | `drawing.cpp`, `multidraw.cpp` | Phase 1 |
| 2-3 | 目标 4 — MultiDraw 批处理合并 | `multidraw.cpp` | Phase 1 |

### Phase 3：着色器管线优化（预计收益 5-10%）

| 序号 | 目标 | 修改文件 | 依赖 |
|------|------|----------|------|
| 3-1 | 目标 5 — 消除正则替换（最多改动） | `glsl_for_es.cpp`, `program.cpp` | 无 |
| 3-2 | 目标 6 — 减少字符串拷贝 | `glsl_for_es.cpp`, `cache.cpp` | 3-1 |
| 3-3 | 目标 7 — xxHash 替代 SHA-256 | `cache.cpp`, `cache.h` | 无 |

### Phase 4：基础设施与泄漏修复

| 序号 | 目标 | 修改文件 | 依赖 |
|------|------|----------|------|
| 4-1 | 目标 8 — GL 包装函数 inline | `gles/loader.h`, `gl_native.cpp` | 无 |
| 4-2 | 目标 9 — DSA 包装器状态追踪 | `DSAWrapper.cpp` | Phase 1 |
| 4-3 | 目标 10 — FSR1 uniform location 缓存 | `FSR1.cpp`, `FSR1.h` | 无 |
| 4-4 | 目标 11 — 日志去 fflush | `mg.cpp`, `log.h` | 无 |
| 4-5 | 目标 12 — 修复内存泄漏 | `config.cpp`, `getter.cpp`, `gpu_utils.cpp`, `log.cpp`, `texture.cpp`, `gles/loader.cpp` | 无 |
| 4-6 | 目标 13 — SSBO 复用 | `multidraw.cpp` | 无 |
| 4-7 | 目标 14 — hasVulkan12 懒加载 | `gpu_utils.cpp` | 无 |

---

## 验证标准

每个目标实施后：

1. **编译验证**：确保 Android NDK + CMake 编译通过
2. **功能测试**：在目标设备上运行 Minecraft JE，确认无渲染异常
3. **性能对比**：Perfetto trace 对比优化前后的 GPU/CPU 时间
4. **回归测试**：确保非目标场景（不同 multidraw_mode、FSR 设置等）无退化
5. **性能基准**：
   - FPS 提升 ≥ 预期值
   - glGet* 调用次数显著减少（目标 1）
   - malloc 次数显著减少（目标 2）
   - 着色器编译时间缩短（目标 5-7）

---

## 风险评估

| 风险 | 影响等级 | 缓解措施 |
|------|----------|----------|
| 状态追踪与实际 GL 状态不一致 | 高 | 添加 `force_sync()` 接口；在关键点验证；保守策略（怀疑不一致时查询驱动） |
| NEON SIMD 兼容性问题（目标 3） | 中 | 提供标量回退路径；编译期检测 NEON 可用性 |
| 字符串处理改动引入 GLSL 转换 bug | 中 | 为目标 5-6 编写 GLSL 转换单元测试；对比优化前后输出 |
| inline 改动影响 ABI | 低 | 使用 `flatten` 而非修改函数签名；保持外部链接 |

---

## 总结

本计划涵盖 **14 个具体开发目标**，分 4 个阶段实施，覆盖从绘制热路径到着色器编译管线的全链路优化：

- **Phase 1**：建立状态追踪基础设施 — 多项优化的**先决条件**
- **Phase 2**：消除绘制热路径的分配和循环瓶颈
- **Phase 3**：重构着色器处理管线中最昂贵的路径
- **Phase 4**：修补基础设施问题（泄漏、inline、日志等）

推荐严格按 Phase 顺序推进，因为后续 Phase 依赖 Phase 1 的状态追踪基础设施。