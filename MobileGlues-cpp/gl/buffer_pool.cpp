// MobileGlues - gl/buffer_pool.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only

#include "buffer_pool.h"
#include "../gles/gles.h"
#include "../includes.h"
#include "log.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <list>
#include <unordered_map>
#include <vector>

#define DEBUG 0

static const size_t SIZE_CLASSES[] = {
    64,      256,     1024,     4096,
    16384,   65536,   262144,   1048576,
    4194304
};

static const size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);

static const size_t MAX_POOL_TOTAL_SIZE = 64 * 1024 * 1024;
static const uint64_t CLEANUP_AGE_MS = 5000;

struct PooledBuffer {
    GLuint id;
    GLenum target;
    GLsizeiptr size;
    uint64_t lastUsed;
};

struct SizeClassPool {
    std::list<PooledBuffer> freeList;
};

static std::vector<SizeClassPool> g_sizeClassPools;
static std::unordered_map<GLuint, size_t> g_usedBufferClass;
static bool g_initialized = false;
static size_t g_totalPoolSize = 0;

static size_t findSizeClassIndex(GLenum target, size_t minSize) {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (minSize <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return NUM_SIZE_CLASSES - 1;
}

static uint64_t getTimeMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void BufferPool_Init() {
    if (g_initialized) return;
    g_sizeClassPools.resize(NUM_SIZE_CLASSES);
    g_initialized = true;
    LOG_D("BufferPool initialized with %zu size classes", NUM_SIZE_CLASSES);
}

GLuint BufferPool_Acquire(GLenum target, GLsizeiptr minSize, GLenum usage) {
    if (!g_initialized) BufferPool_Init();

    size_t classIdx = findSizeClassIndex(target, (size_t)minSize);
    SizeClassPool& pool = g_sizeClassPools[classIdx];

    if (!pool.freeList.empty()) {
        PooledBuffer pb = pool.freeList.front();
        pool.freeList.pop_front();
        g_totalPoolSize -= pb.size;
        g_usedBufferClass[pb.id] = classIdx;

        LOG_D("BufferPool: hit class=%zu id=%u size=%ld", classIdx, pb.id, (long)pb.size);
        return pb.id;
    }

    GLuint buffer;
    GLES.glGenBuffers(1, &buffer);
    GLES.glBindBuffer(target, buffer);
    GLES.glBufferData(target, SIZE_CLASSES[classIdx], nullptr, usage);

    g_usedBufferClass[buffer] = classIdx;

    LOG_D("BufferPool: miss class=%zu id=%u allocated=%zu", classIdx, buffer, SIZE_CLASSES[classIdx]);
    return buffer;
}

void BufferPool_Release(GLuint buffer, GLsizeiptr usedSize) {
    if (!g_initialized) {
        GLES.glDeleteBuffers(1, &buffer);
        return;
    }

    auto it = g_usedBufferClass.find(buffer);
    if (it == g_usedBufferClass.end()) {
        GLES.glDeleteBuffers(1, &buffer);
        return;
    }

    size_t classIdx = it->second;
    g_usedBufferClass.erase(it);

    if (classIdx >= NUM_SIZE_CLASSES) classIdx = NUM_SIZE_CLASSES - 1;

    size_t bufSize = SIZE_CLASSES[classIdx];

    if (g_totalPoolSize + bufSize > MAX_POOL_TOTAL_SIZE) {
        GLES.glDeleteBuffers(1, &buffer);
        LOG_D("BufferPool: pool full, deleted buffer %u", buffer);
        return;
    }

    SizeClassPool& pool = g_sizeClassPools[classIdx];
    PooledBuffer pb;
    pb.id = buffer;
    pb.target = 0;
    pb.size = bufSize;
    pb.lastUsed = getTimeMs();

    pool.freeList.push_back(pb);
    g_totalPoolSize += bufSize;

    LOG_D("BufferPool: released buffer %u class=%zu", buffer, classIdx);
}

void BufferPool_Cleanup(size_t maxTotalSize) {
    if (!g_initialized) return;

    uint64_t now = getTimeMs();
    size_t effectiveMax = (maxTotalSize > 0) ? maxTotalSize : MAX_POOL_TOTAL_SIZE;

    for (size_t i = NUM_SIZE_CLASSES; i > 0; --i) {
        size_t idx = i - 1;
        SizeClassPool& pool = g_sizeClassPools[idx];

        while (!pool.freeList.empty() && g_totalPoolSize > effectiveMax) {
            PooledBuffer& pb = pool.freeList.front();
            if (now - pb.lastUsed < CLEANUP_AGE_MS && g_totalPoolSize <= effectiveMax) break;

            GLES.glDeleteBuffers(1, &pb.id);
            g_totalPoolSize -= pb.size;
            pool.freeList.pop_front();
            LOG_D("BufferPool: cleanup deleted buffer %u class=%zu", pb.id, idx);
        }
    }
}

void BufferPool_Destroy() {
    if (!g_initialized) return;

    for (auto& pool : g_sizeClassPools) {
        for (auto& pb : pool.freeList) {
            GLES.glDeleteBuffers(1, &pb.id);
        }
        pool.freeList.clear();
    }
    g_usedBufferClass.clear();
    g_totalPoolSize = 0;
    g_initialized = false;

    LOG_D("BufferPool destroyed");
}