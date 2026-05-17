# GL 状态跟踪系统技术设计文档

## 概述
本文档详细描述了 MobileGlues 项目中本地 GL 状态跟踪系统的设计与实现方案，用于优化频繁的 `glGet*` 调用性能。

---

## 1. 问题分析

### 1.1 当前实现的问题
通过代码审查发现以下高频查询模式：
- `glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, ...)` 在 `gl/multidraw.cpp` 中频繁调用
- `glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, ...)` 在 `gl/texture.cpp` 中多次出现
- `glGetIntegerv(GL_CURRENT_PROGRAM, ...)` 在多处使用
- `glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, ...)` 被频繁调用

### 1.2 性能影响
- 每次 `glGet*` 调用需要用户空间到内核空间切换
- 驱动层开销大
- 在绘制关键路径上造成延迟

---

## 2. 设计目标

### 2.1 核心目标
- 减少 50-70% 的 `glGet*` 调用
- 提供零开销的本地状态查询
- 保持与原生 GL 行为一致
- 线程安全（如需要）

### 2.2 性能指标
- 本地状态查询 < 10ns
- 状态更新开销可忽略
- 内存增加 < 10KB

---

## 3. 架构设计

### 3.1 整体结构

```
GLStateTracker (单例)
├── ProgramState (程序状态)
├── BufferState (缓冲区状态)
│   ├── ElementArrayBuffer
│   ├── ArrayBuffer
│   ├── ShaderStorageBuffer[]
│   └── ...
├── TextureState (纹理状态)
│   ├── ActiveTextureUnit
│   ├── TextureUnits[]
│   └── ...
├── FramebufferState (帧缓冲区状态)
│   ├── DrawFramebuffer
│   ├── ReadFramebuffer
│   └── ...
└── VertexArrayState (顶点数组状态)
```

### 3.2 核心组件

#### 3.2.1 GLStateTracker 类
```cpp
class GLStateTracker {
public:
    static GLStateTracker& getInstance();
    
    // 程序状态
    GLuint getCurrentProgram() const;
    void setCurrentProgram(GLuint program);
    
    // 缓冲区状态
    GLuint getElementArrayBufferBinding() const;
    void setElementArrayBufferBinding(GLuint buffer);
    
    GLuint getArrayBufferBinding() const;
    void setArrayBufferBinding(GLuint buffer);
    
    GLuint getShaderStorageBufferBinding(GLuint index) const;
    void setShaderStorageBufferBinding(GLuint index, GLuint buffer);
    
    // 纹理状态
    GLuint getActiveTextureUnit() const;
    void setActiveTextureUnit(GLuint unit);
    
    GLuint getTextureBinding(GLenum target, GLuint unit) const;
    void setTextureBinding(GLenum target, GLuint unit, GLuint texture);
    
    // 帧缓冲区状态
    GLuint getDrawFramebufferBinding() const;
    void setDrawFramebufferBinding(GLuint buffer);
    
    GLuint getReadFramebufferBinding() const;
    void setReadFramebufferBinding(GLuint buffer);
    
    // 其他状态...
    
    // 同步状态（当需要时调用）
    void syncStateFromGL();
    
private:
    GLStateTracker();
    ~GLStateTracker();
    
    // 程序状态
    GLuint currentProgram = 0;
    bool programDirty = false;
    
    // 缓冲区状态
    struct BufferBindings {
        GLuint elementArrayBuffer = 0;
        GLuint arrayBuffer = 0;
        std::array<GLuint, 16> shaderStorageBuffers = {};
        // 其他缓冲区...
    };
    BufferBindings bufferBindings;
    bool buffersDirty = false;
    
    // 纹理状态
    struct TextureState {
        GLuint activeUnit = 0;
        std::array<std::unordered_map<GLenum, GLuint>, 32> textureUnits;
    };
    TextureState textureState;
    bool texturesDirty = false;
    
    // 帧缓冲区状态
    struct FramebufferState {
        GLuint drawFramebuffer = 0;
        GLuint readFramebuffer = 0;
    };
    FramebufferState framebufferState;
    bool framebuffersDirty = false;
    
    // 标记是否需要初始化
    bool initialized = false;
};
```

---

## 4. 实现方案

### 4.1 核心函数实现

#### 4.1.1 get/setCurrentProgram
```cpp
GLuint GLStateTracker::getCurrentProgram() const {
    if (programDirty) {
        // 需要从 GL 同步
        GLES.glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&const_cast<GLStateTracker*>(this)->currentProgram);
        const_cast<GLStateTracker*>(this)->programDirty = false;
        LOG_D("GLStateTracker: synced currentProgram=%u", currentProgram);
    }
    return currentProgram;
}

void GLStateTracker::setCurrentProgram(GLuint program) {
    if (currentProgram != program) {
        currentProgram = program;
        // 调用实际的 GL 函数
        GLES.glUseProgram(program);
        LOG_D("GLStateTracker: set currentProgram=%u", program);
    }
}
```

#### 4.1.2 get/setElementArrayBufferBinding
```cpp
GLuint GLStateTracker::getElementArrayBufferBinding() const {
    if (buffersDirty) {
        // 同步所有缓冲区状态
        GLES.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, 
                          (GLint*)&const_cast<GLStateTracker*>(this)->bufferBindings.elementArrayBuffer);
        // 同步其他缓冲区...
        const_cast<GLStateTracker*>(this)->buffersDirty = false;
        LOG_D("GLStateTracker: synced buffer bindings");
    }
    return bufferBindings.elementArrayBuffer;
}

void GLStateTracker::setElementArrayBufferBinding(GLuint buffer) {
    if (bufferBindings.elementArrayBuffer != buffer) {
        bufferBindings.elementArrayBuffer = buffer;
        GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
        LOG_D("GLStateTracker: set elementArrayBuffer=%u", buffer);
    }
}
```

### 4.2 拦截 GL 函数

#### 4.2.1 包装 glUseProgram
```cpp
void glUseProgram(GLuint program) {
    LOG()
    LOG_D("glUseProgram(%u)", program)
    GLStateTracker::getInstance().setCurrentProgram(program);
    CHECK_GL_ERROR
}
```

#### 4.2.2 包装 glBindBuffer
```cpp
void glBindBuffer(GLenum target, GLuint buffer) {
    LOG()
    LOG_D("glBindBuffer(%s, %u)", glEnumToString(target), buffer)
    
    switch (target) {
        case GL_ELEMENT_ARRAY_BUFFER:
            GLStateTracker::getInstance().setElementArrayBufferBinding(buffer);
            break;
        case GL_ARRAY_BUFFER:
            GLStateTracker::getInstance().setArrayBufferBinding(buffer);
            break;
        // 其他目标...
        default:
            GLES.glBindBuffer(target, buffer);
            break;
    }
    
    CHECK_GL_ERROR
}
```

### 4.3 优化现有代码

#### 4.3.1 修改 gl/multidraw.cpp
```cpp
void mg_glMultiDrawElementsBaseVertex_drawelements(GLenum mode, GLsizei* counts, GLenum type,
                                                   const void* const* indices, GLsizei primcount,
                                                   const GLint* basevertex) {
    LOG()
    prepareForDraw();
    
    // 使用本地状态跟踪替代 glGetIntegerv
    GLint prevElementBuffer = GLStateTracker::getInstance().getElementArrayBufferBinding();
    
    for (GLsizei i = 0; i < primcount; ++i) {
        if (counts[i] <= 0) continue;
        
        // ... 现有代码 ...
        
        // 使用缓冲区池...
    }
    
    // 恢复状态使用本地跟踪
    GLStateTracker::getInstance().setElementArrayBufferBinding(prevElementBuffer);
    CHECK_GL_ERROR
}
```

---

## 5. 状态同步策略

### 5.1 懒加载策略
- 初始状态标记为 dirty
- 首次查询时从 GL 同步
- 后续查询直接返回本地值

### 5.2 写时更新策略
- 通过拦截函数更新状态
- 减少同步需求
- 保持与 GL 一致

### 5.3 强制同步接口
```cpp
void GLStateTracker::syncStateFromGL() {
    // 强制从 GL 同步所有状态
    GLES.glGetIntegerv(GL_CURRENT_PROGRAM, (GLint*)&currentProgram);
    GLES.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, (GLint*)&bufferBindings.elementArrayBuffer);
    // ... 其他状态 ...
    
    programDirty = false;
    buffersDirty = false;
    texturesDirty = false;
    framebuffersDirty = false;
    
    LOG_D("GLStateTracker: fully synced from GL");
}
```

---

## 6. 配置与调试

### 6.1 编译时配置
- `GL_STATE_TRACKER_ENABLED`：启用/禁用状态跟踪
- `GL_STATE_TRACKER_DEBUG`：启用调试日志

### 6.2 调试功能
```cpp
#ifdef GL_STATE_TRACKER_DEBUG
void GLStateTracker::dumpState() const {
    LOG_D("=== GL State Tracker Dump ===");
    LOG_D("Current Program: %u", currentProgram);
    LOG_D("Element Array Buffer: %u", bufferBindings.elementArrayBuffer);
    LOG_D("Active Texture Unit: %u", textureState.activeUnit);
    // ... 其他状态 ...
}
#endif
```

---

## 7. 测试计划

### 7.1 单元测试
- 测试状态更新和查询
- 测试脏标记机制
- 测试状态同步

### 7.2 性能测试
- 对比有/无状态跟踪的性能
- 测量查询延迟
- 内存占用分析

### 7.3 兼容性测试
- 验证复杂场景下的状态一致性
- 测试边界条件
- Minecraft 实际游戏测试

---

## 8. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 状态不一致 | 高 | 中 | 提供强制同步接口，保守策略 |
| 遗漏的状态更新 | 中 | 低 | 全面测试，代码审查 |
| 内存增加 | 低 | 高 | 仅跟踪高频查询的状态 |
| 初始化时机问题 | 中 | 低 | 明确初始化点，懒加载 |

---

## 9. 文件修改清单

### 新增文件
- `gl/state_tracker.h`：状态跟踪器头文件
- `gl/state_tracker.cpp`：状态跟踪器实现

### 修改文件
- `gl/mg.h`：集成状态跟踪器
- `gl/gl.cpp`：拦截 GL 函数
- `gl/buffer.cpp`：使用状态跟踪
- `gl/texture.cpp`：使用状态跟踪
- `gl/drawing.cpp`：使用状态跟踪
- `gl/multidraw.cpp`：使用状态跟踪
- `gl/framebuffer.cpp`：使用状态跟踪
- 其他相关文件

---

## 10. 后续优化方向

1. **更多状态跟踪**：扩展到其他高频查询的状态
2. **状态快照**：支持保存/恢复状态
3. **状态验证**：调试模式下验证状态一致性
4. **性能统计**：收集状态查询命中率等指标

---

## 总结

本设计通过实现一个本地 GL 状态跟踪系统，可以显著减少昂贵的 `glGet*` 调用，预期可减少 50-70% 的相关调用开销，提升绘制路径性能。
