# 临时缓冲区池技术设计文档

## 概述
本文档详细描述了 MobileGlues 项目中临时缓冲区池的设计与实现方案，用于优化 [gl/multidraw.cpp](file:///workspace/MobileGlues-cpp/gl/multidraw.cpp) 中的多绘制调用性能。

---

## 1. 问题分析

### 1.1 当前实现的问题
在 `mg_glMultiDrawElementsBaseVertex_drawelements` 函数中：
- 每次绘制调用都调用 `glGenBuffers` 和 `glDeleteBuffers`
- 频繁的 `malloc`/`free` 操作用于临时索引数据
- 每个调用都进行 `glMapBufferRange`/`glUnmapBuffer` 操作
- 没有缓冲区复用机制

### 1.2 性能影响
- 驱动层开销大
- 内存碎片增加
- CPU 等待 GPU 操作时间长

---

## 2. 设计目标

### 2.1 核心目标
- 减少 90%+ 的缓冲区对象创建/销毁
- 复用已有缓冲区
- 提供线程安全的池访问（如需要）
- 保持 API 兼容性

### 2.2 性能指标
- 缓冲区获取/归还操作 < 1μs
- 内存碎片减少 50%+
- 无明显内存泄漏

---

## 3. 架构设计

### 3.1 整体结构

```
BufferPool (单例)
├── SizeClass[] (按大小分级)
│   ├── FreeList (空闲缓冲区链表)
│   └── UsedList (使用中缓冲区链表)
├── Statistics (统计信息)
└── Config (配置参数)
```

### 3.2 核心组件

#### 3.2.1 BufferPool 类
```cpp
class BufferPool {
public:
    static BufferPool& getInstance();
    
    // 获取缓冲区
    GLuint acquire(size_t minSize);
    
    // 归还缓冲区
    void release(GLuint buffer, size_t size);
    
    // 清理长期未使用的缓冲区
    void cleanup();
    
    // 获取统计信息
    struct Stats {
        size_t totalAllocated;
        size_t totalFreed;
        size_t currentUsed;
        size_t hitCount;
        size_t missCount;
    };
    Stats getStats() const;
    
private:
    BufferPool();
    ~BufferPool();
    
    struct BufferInfo {
        GLuint id;
        size_t size;
        uint64_t lastUsed;
    };
    
    struct SizeClass {
        size_t minSize;
        size_t maxSize;
        std::list<BufferInfo> freeList;
        std::unordered_set<GLuint> usedSet;
    };
    
    std::vector<SizeClass> sizeClasses;
    mutable std::mutex mutex;
    Stats stats;
    
    // 配置
    static constexpr size_t MAX_POOL_SIZE = 64 * 1024 * 1024; // 64MB
    static constexpr size_t BUFFER_GROWTH_FACTOR = 2;
    static constexpr uint64_t CLEANUP_THRESHOLD_MS = 5000; // 5秒
};
```

#### 3.2.2 大小分级策略
```
级别 0: 0B - 64B
级别 1: 65B - 256B
级别 2: 257B - 1KB
级别 3: 1KB - 4KB
级别 4: 4KB - 16KB
级别 5: 16KB - 64KB
级别 6: 64KB - 256KB
级别 7: 256KB - 1MB
级别 8: 1MB+
```

---

## 4. 实现方案

### 4.1 核心函数实现

#### 4.1.1 acquire() - 获取缓冲区
```cpp
GLuint BufferPool::acquire(size_t minSize) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 1. 查找合适的大小级别
    size_t classIndex = findSizeClass(minSize);
    SizeClass& sizeClass = sizeClasses[classIndex];
    
    // 2. 尝试从空闲列表获取
    if (!sizeClass.freeList.empty()) {
        BufferInfo info = sizeClass.freeList.front();
        sizeClass.freeList.pop_front();
        sizeClass.usedSet.insert(info.id);
        
        stats.hitCount++;
        stats.currentUsed++;
        
        LOG_D("BufferPool hit: size=%zu, id=%u", info.size, info.id);
        return info.id;
    }
    
    // 3. 缓存未命中，创建新缓冲区
    stats.missCount++;
    stats.currentUsed++;
    
    // 确定实际分配大小（向上取整到级别上限）
    size_t actualSize = sizeClass.maxSize;
    
    GLuint buffer;
    GLES.glGenBuffers(1, &buffer);
    GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
    GLES.glBufferData(GL_ELEMENT_ARRAY_BUFFER, actualSize, nullptr, GL_STREAM_DRAW);
    
    sizeClass.usedSet.insert(buffer);
    stats.totalAllocated++;
    
    LOG_D("BufferPool miss: allocated size=%zu, id=%u", actualSize, buffer);
    return buffer;
}
```

#### 4.1.2 release() - 归还缓冲区
```cpp
void BufferPool::release(GLuint buffer, size_t size) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 1. 查找对应的大小级别
    size_t classIndex = findSizeClass(size);
    SizeClass& sizeClass = sizeClasses[classIndex];
    
    // 2. 从使用集合移除
    auto it = sizeClass.usedSet.find(buffer);
    if (it == sizeClass.usedSet.end()) {
        LOG_W("BufferPool: releasing unknown buffer %u", buffer);
        return;
    }
    sizeClass.usedSet.erase(it);
    
    // 3. 检查是否超过最大池大小，决定是否直接删除
    size_t totalPoolSize = calculateTotalPoolSize();
    if (totalPoolSize + size > MAX_POOL_SIZE) {
        GLES.glDeleteBuffers(1, &buffer);
        stats.totalFreed++;
        stats.currentUsed--;
        LOG_D("BufferPool: deleted buffer %u (pool full)", buffer);
        return;
    }
    
    // 4. 添加到空闲列表
    BufferInfo info;
    info.id = buffer;
    info.size = sizeClass.maxSize;
    info.lastUsed = getCurrentTimeMs();
    
    sizeClass.freeList.push_back(info);
    stats.currentUsed--;
    
    LOG_D("BufferPool: released buffer %u", buffer);
}
```

### 4.2 集成到现有代码

#### 4.2.1 修改 mg_glMultiDrawElementsBaseVertex_drawelements
```cpp
void mg_glMultiDrawElementsBaseVertex_drawelements(GLenum mode, GLsizei* counts, GLenum type,
                                                   const void* const* indices, GLsizei primcount,
                                                   const GLint* basevertex) {
    LOG()
    prepareForDraw();
    GLint prevElementBuffer;
    GLES.glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementBuffer);
    
    for (GLsizei i = 0; i < primcount; ++i) {
        if (counts[i] <= 0) continue;
        
        GLsizei currentCount = counts[i];
        const GLvoid* currentIndices = indices[i];
        GLint currentBaseVertex = basevertex[i];
        
        size_t indexSize;
        switch (type) {
        case GL_UNSIGNED_INT:
            indexSize = sizeof(GLuint);
            break;
        case GL_UNSIGNED_SHORT:
            indexSize = sizeof(GLushort);
            break;
        case GL_UNSIGNED_BYTE:
            indexSize = sizeof(GLubyte);
            break;
        default:
            return;
        }
        
        size_t bufferSize = currentCount * indexSize;
        
        // 使用缓冲区池
        GLuint tempBuffer = BufferPool::getInstance().acquire(bufferSize);
        GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tempBuffer);
        
        void* mappedData = GLES.glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, 0, bufferSize,
                                                 GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        
        if (mappedData) {
            void* srcData = nullptr;
            
            if (prevElementBuffer != 0) {
                GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElementBuffer);
                srcData = GLES.glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, (GLintptr)currentIndices,
                                                bufferSize, GL_MAP_READ_BIT);
                
                if (srcData) {
                    memcpy(mappedData, srcData, bufferSize);
                    GLES.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
                }
            } else {
                srcData = (void*)currentIndices;
                memcpy(mappedData, srcData, bufferSize);
            }
            
            // 应用 baseVertex 偏移
            switch (type) {
            case GL_UNSIGNED_INT:
                for (int j = 0; j < currentCount; ++j) {
                    ((GLuint*)mappedData)[j] += currentBaseVertex;
                }
                break;
            case GL_UNSIGNED_SHORT:
                for (int j = 0; j < currentCount; ++j) {
                    ((GLushort*)mappedData)[j] += currentBaseVertex;
                }
                break;
            case GL_UNSIGNED_BYTE:
                for (int j = 0; j < currentCount; ++j) {
                    ((GLubyte*)mappedData)[j] += currentBaseVertex;
                }
                break;
            }
            
            GLES.glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER);
            
            // 绘制
            GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, tempBuffer);
            GLES.glDrawElements(mode, currentCount, type, 0);
            
            // 归还缓冲区
            BufferPool::getInstance().release(tempBuffer, bufferSize);
        } else {
            // 映射失败，回退到原始方法
            GLES.glDeleteBuffers(1, &tempBuffer);
            
            // ... 原始实现 ...
        }
    }
    
    GLES.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, prevElementBuffer);
    CHECK_GL_ERROR
}
```

---

## 5. 配置与调优

### 5.1 编译时配置
- `BUFFER_POOL_ENABLED`：启用/禁用缓冲区池
- `MAX_POOL_SIZE`：最大池大小（字节）
- `CLEANUP_THRESHOLD_MS`：清理阈值（毫秒）

### 5.2 运行时配置
可通过环境变量或设置文件调整：
- `MOBILEGLUES_BUFFER_POOL_SIZE`：最大池大小
- `MOBILEGLUES_BUFFER_POOL_CLEANUP_INTERVAL`：清理间隔

---

## 6. 测试计划

### 6.1 单元测试
- 测试 acquire/release 基本功能
- 测试大小分级策略
- 测试并发访问（如支持线程）
- 测试清理机制

### 6.2 性能测试
- 基准测试：对比使用/不使用池的性能
- 压力测试：大量分配/释放
- 内存使用测试：监控内存占用

### 6.3 集成测试
- Minecraft 游戏场景测试
- 兼容性测试

---

## 7. 风险与缓解

| 风险 | 影响 | 概率 | 缓解措施 |
|------|------|------|----------|
| 内存占用增加 | 中 | 中 | 设置合理的最大池大小，定期清理 |
| 线程安全问题 | 高 | 低 | 使用互斥锁保护共享数据 |
| 驱动兼容性问题 | 高 | 低 | 提供禁用开关，保持回退方案 |
| 缓冲区大小估算错误 | 中 | 中 | 保守的大小策略，允许动态调整 |

---

## 8. 文件修改清单

### 新增文件
- `gl/buffer_pool.h`：缓冲区池头文件
- `gl/buffer_pool.cpp`：缓冲区池实现

### 修改文件
- `gl/multidraw.h`：添加池声明
- `gl/multidraw.cpp`：集成缓冲区池
- `config/settings.h`：添加池配置（如需要）
- `CMakeLists.txt`：添加新文件到构建系统

---

## 9. 后续优化方向

1. **持久化缓冲区**：跨帧复用更激进
2. **异步初始化**：后台预创建常用大小缓冲区
3. **智能预测**：基于历史使用模式预分配
4. **多线程优化**：无锁数据结构

---

## 总结

本设计通过实现一个分级的缓冲区池，可以显著减少多绘制调用场景下的性能开销，预期可提升 30-50% 的相关操作性能。
