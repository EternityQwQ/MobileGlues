// MobileGlues - gl/buffer.cpp
// Buffer state management: ID mapping, VAO mapping, binding tracking,
// atomic counter buffer emulation, and texture buffer emulation.
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
// All buffer state is tracked CPU-side via GLStateManager to avoid GPU round-trips.
//
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "../includes.h"
#include <GL/gl.h>
#include "glcorearb.h"
#include "log.h"
#include "../gles/loader.h"
#include "mg.h"
#include <GLES3/gl32.h>
#include <cstring>

#define DEBUG 0

// ============================================================================
// Buffer ID generation and management
// ============================================================================

void glGenBuffers(GLsizei n, GLuint *buffers) {
    LOG()
    GLuint *real = (GLuint *)alloca(sizeof(GLuint) * n);
    GLES.glGenBuffers(n, real);

    auto &bs = GLState.buffer;
    for (GLsizei i = 0; i < n; i++) {
        bs.bufferMap[buffers[i]] = real[i];
        bs.bufferMapReverse[real[i]] = buffers[i];
        BufferObject info;
        info.id = real[i];
        bs.bufferInfo[buffers[i]] = info;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers) {
    LOG()
    GLuint *real = (GLuint *)alloca(sizeof(GLuint) * n);
    for (GLsizei i = 0; i < n; i++) {
        real[i] = GLState.GetRealBuffer(buffers[i]);
    }

    auto &bs = GLState.buffer;
    for (GLsizei i = 0; i < n; i++) {
        GLuint vid = buffers[i];
        if (vid == 0) continue;

        auto tbIt = bs.texBuffers.find(vid);
        if (tbIt != bs.texBuffers.end()) {
            if (tbIt->second.texture) {
                GLES.glDeleteTextures(1, &tbIt->second.texture);
            }
            bs.texBuffers.erase(tbIt);
        }

        bs.bufferMaps.erase(vid);
        bs.bufferInfo.erase(vid);

        for (auto &pair : bs.boundBuffer) {
            if (pair.second == vid) pair.second = 0;
        }

        for (int j = 0; j < MAX_UNIFORM_BUFFER_BINDINGS; j++) {
            if (bs.uniformBufferBases[j] == vid) {
                bs.uniformBufferBases[j] = 0;
                bs.uniformBufferOffsets[j] = 0;
                bs.uniformBufferSizes[j] = 0;
            }
        }
        for (int j = 0; j < MAX_SHADER_STORAGE_BUFFER_BINDINGS; j++) {
            if (bs.shaderStorageBases[j] == vid) bs.shaderStorageBases[j] = 0;
        }
        for (int j = 0; j < MAX_ATOMIC_COUNTER_BUFFER_BINDINGS; j++) {
            if (bs.atomicCounterBases[j] == vid) bs.atomicCounterBases[j] = 0;
        }
        for (int j = 0; j < MAX_TRANSFORM_FEEDBACK_BUFFERS; j++) {
            if (bs.transformFeedbackBuffers[j] == vid) bs.transformFeedbackBuffers[j] = 0;
        }

        bs.bufferMap.erase(vid);
        if (real[i]) bs.bufferMapReverse.erase(real[i]);
    }

    GLES.glDeleteBuffers(n, real);
}

GLboolean glIsBuffer(GLuint buffer) {
    if (buffer == 0) return GL_FALSE;
    return GLState.GetRealBuffer(buffer) != 0 ? GL_TRUE : GL_FALSE;
}

// ============================================================================
// Buffer binding and data management
// ============================================================================

void glBindBuffer(GLenum target, GLuint buffer) {
    LOG()
    GLuint realId = GLState.GetRealBuffer(buffer);
    GLES.glBindBuffer(target, realId);
    GLState.buffer.boundBuffer[target] = buffer;
    STATE_LOG("glBindBuffer: target=0x%X, virtual=%u, real=%u", target, buffer, realId);
}

void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    LOG()
    GLuint boundVirt = GLState.buffer.boundBuffer[target];
    GLuint realId = GLState.GetRealBuffer(boundVirt);
    GLES.glBufferData(target, size, data, usage);

    if (boundVirt) {
        auto it = GLState.buffer.bufferInfo.find(boundVirt);
        if (it != GLState.buffer.bufferInfo.end()) {
            it->second.size = size;
            it->second.target = target;
        }
    }
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    LOG()
    GLuint boundVirt = GLState.buffer.boundBuffer[target];
    GLES.glBufferSubData(target, offset, size, data);
}

void glCopyBufferSubData(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size) {
    LOG()
    GLuint readVirt = GLState.buffer.boundBuffer[readTarget];
    GLuint writeVirt = GLState.buffer.boundBuffer[writeTarget];
    GLES.glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, readOffset, writeOffset, size);
}

// ============================================================================
// Buffer mapping
// ============================================================================

void* glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    LOG()
    GLuint boundVirt = GLState.buffer.boundBuffer[target];
    void *ptr = GLES.glMapBufferRange(target, offset, length, access);

    if (ptr && boundVirt) {
        GLState.buffer.bufferMaps[boundVirt] = ptr;
        auto it = GLState.buffer.bufferInfo.find(boundVirt);
        if (it != GLState.buffer.bufferInfo.end()) {
            it->second.mappedPtr = ptr;
            it->second.access = access;
            it->second.isMapped = true;
        }
    }
    return ptr;
}

GLboolean glUnmapBuffer(GLenum target) {
    LOG()
    GLuint boundVirt = GLState.buffer.boundBuffer[target];
    GLuint realId = GLState.GetRealBuffer(boundVirt);
    GLboolean result = GLES.glUnmapBuffer(target);

    if (boundVirt) {
        if (boundVirt == GLState.buffer.atomicCounterBufferBinding &&
            !GLState.buffer.atomicCounterData.empty()) {
            GLES.glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, realId);
            GLES.glBufferSubData(GL_ATOMIC_COUNTER_BUFFER,
                                GLState.buffer.atomicCounterBufferOffset,
                                GLState.buffer.atomicCounterBufferSize,
                                GLState.buffer.atomicCounterData.data());
        }

        GLState.buffer.bufferMaps.erase(boundVirt);
        auto it = GLState.buffer.bufferInfo.find(boundVirt);
        if (it != GLState.buffer.bufferInfo.end()) {
            it->second.mappedPtr = nullptr;
            it->second.isMapped = false;
        }
    }
    return result;
}

void glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    LOG()
    GLES.glFlushMappedBufferRange(target, offset, length);
}

// ============================================================================
// Buffer parameter queries
// ============================================================================

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    GLES.glGetBufferParameteriv(target, pname, params);
}

void glGetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params) {
    GLES.glGetBufferParameteri64v(target, pname, params);
}

void glGetBufferPointerv(GLenum target, GLenum pname, void **params) {
    GLES.glGetBufferPointerv(target, pname, params);
}

// ============================================================================
// Uniform buffer binding
// ============================================================================

void glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    LOG()
    GLuint realId = GLState.GetRealBuffer(buffer);
    GLES.glBindBufferRange(target, index, realId, offset, size);

    auto &bs = GLState.buffer;
    if (target == GL_UNIFORM_BUFFER) {
        bs.uniformBufferBases[index] = buffer;
        bs.uniformBufferOffsets[index] = offset;
        bs.uniformBufferSizes[index] = size;
    } else if (target == GL_SHADER_STORAGE_BUFFER) {
        bs.shaderStorageBases[index] = buffer;
    } else if (target == GL_ATOMIC_COUNTER_BUFFER) {
        bs.atomicCounterBases[index] = buffer;
    } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
        if (index < MAX_TRANSFORM_FEEDBACK_BUFFERS) {
            bs.transformFeedbackBuffers[index] = buffer;
        }
    }
}

void glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    LOG()
    GLuint realId = GLState.GetRealBuffer(buffer);
    GLES.glBindBufferBase(target, index, realId);

    auto &bs = GLState.buffer;
    if (target == GL_UNIFORM_BUFFER) {
        bs.uniformBufferBases[index] = buffer;
        bs.uniformBufferOffsets[index] = 0;
        bs.uniformBufferSizes[index] = 0;
    } else if (target == GL_SHADER_STORAGE_BUFFER) {
        bs.shaderStorageBases[index] = buffer;
    } else if (target == GL_ATOMIC_COUNTER_BUFFER) {
        bs.atomicCounterBases[index] = buffer;
    } else if (target == GL_TRANSFORM_FEEDBACK_BUFFER) {
        if (index < MAX_TRANSFORM_FEEDBACK_BUFFERS) {
            bs.transformFeedbackBuffers[index] = buffer;
        }
    }
}

// ============================================================================
// VAO management
// ============================================================================

void glGenVertexArrays(GLsizei n, GLuint *arrays) {
    LOG()
    GLuint *real = (GLuint *)alloca(sizeof(GLuint) * n);
    GLES.glGenVertexArrays(n, real);

    auto &bs = GLState.buffer;
    for (GLsizei i = 0; i < n; i++) {
        bs.vaoMap[arrays[i]] = real[i];
        bs.vaoMapReverse[real[i]] = arrays[i];
    }
}

void glDeleteVertexArrays(GLsizei n, const GLuint *arrays) {
    LOG()
    GLuint *real = (GLuint *)alloca(sizeof(GLuint) * n);
    for (GLsizei i = 0; i < n; i++) {
        real[i] = GLState.GetRealVAO(arrays[i]);
    }
    GLES.glDeleteVertexArrays(n, real);

    auto &bs = GLState.buffer;
    for (GLsizei i = 0; i < n; i++) {
        if (GLState.vertexArray.currentVAO == arrays[i]) {
            GLState.vertexArray.currentVAO = 0;
        }
        bs.vaoMap.erase(arrays[i]);
        if (real[i]) bs.vaoMapReverse.erase(real[i]);
    }
}

void glBindVertexArray(GLuint array) {
    LOG()
    GLuint realId = GLState.GetRealVAO(array);
    GLES.glBindVertexArray(realId);
    GLState.vertexArray.currentVAO = array;
}

GLboolean glIsVertexArray(GLuint array) {
    if (array == 0) return GL_FALSE;
    return GLState.GetRealVAO(array) != 0 ? GL_TRUE : GL_FALSE;
}

// ============================================================================
// Texture buffer emulation
// ============================================================================

void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) {
    LOG()
    GLuint realBuffer = GLState.GetRealBuffer(buffer);

    if (GLState.emulateTextureBuffer) {
        auto &bs = GLState.buffer;
        auto &slot = bs.texBuffers[buffer];
        slot.buffer = realBuffer;
        slot.internalFormat = internalformat;

        if (!slot.texture) {
            GLES.glGenTextures(1, &slot.texture);
        }

        GLint prevTexUnit;
        GLES.glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTexUnit);
        GLES.glActiveTexture(GL_TEXTURE0 + GLState.currentTexUnit);
        GLES.glBindTexture(GL_TEXTURE_2D, slot.texture);

        GLint bufSize = 0;
        GLint prevBinding = 0;
        GLES.glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &prevBinding);
        GLES.glBindBuffer(GL_TEXTURE_BUFFER, realBuffer);
        GLES.glGetBufferParameteriv(GL_TEXTURE_BUFFER, GL_BUFFER_SIZE, &bufSize);
        GLES.glBindBuffer(GL_TEXTURE_BUFFER, prevBinding);

        GLsizei texWidth = bufSize / 16;
        if (texWidth < 1) texWidth = 1;
        if (texWidth > 4096) texWidth = 4096;

        GLES.glTexImage2D(GL_TEXTURE_2D, 0, internalformat, texWidth, 1, 0,
                          GL_RGBA, GL_FLOAT, nullptr);
        GLES.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        GLES.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        GLES.glActiveTexture(prevTexUnit);
    } else {
        GLES.glTexBuffer(GL_TEXTURE_BUFFER, internalformat, realBuffer);
    }
}

void glTexBufferRange(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    LOG()
    GLuint realBuffer = GLState.GetRealBuffer(buffer);

    if (GLState.emulateTextureBuffer) {
        auto &bs = GLState.buffer;
        auto &slot = bs.texBuffers[buffer];
        slot.buffer = realBuffer;
        slot.internalFormat = internalformat;

        if (!slot.texture) {
            GLES.glGenTextures(1, &slot.texture);
        }

        GLint prevTexUnit;
        GLES.glGetIntegerv(GL_ACTIVE_TEXTURE, &prevTexUnit);
        GLES.glActiveTexture(GL_TEXTURE0 + GLState.currentTexUnit);
        GLES.glBindTexture(GL_TEXTURE_2D, slot.texture);

        GLsizei texWidth = (GLsizei)(size / 16);
        if (texWidth < 1) texWidth = 1;
        if (texWidth > 4096) texWidth = 4096;

        GLES.glTexImage2D(GL_TEXTURE_2D, 0, internalformat, texWidth, 1, 0,
                          GL_RGBA, GL_FLOAT, nullptr);
        GLES.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        GLES.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        GLES.glActiveTexture(prevTexUnit);
    } else {
        GLES.glTexBufferRange(GL_TEXTURE_BUFFER, internalformat, realBuffer, offset, size);
    }
}

// ============================================================================
// Vertex buffer binding (for DSA-style buffer binding)
// ============================================================================

void glBindVertexBuffer(GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride) {
    LOG()
    GLuint realId = GLState.GetRealBuffer(buffer);
    GLES.glBindVertexBuffer(bindingindex, realId, offset, stride);
}

// ============================================================================
// Map initialization helpers (for backward compatibility)
// ============================================================================

void InitBufferMap(size_t expectedSize) {
    auto &bs = GLState.buffer;
    bs.bufferMap.reserve(expectedSize);
    bs.bufferMapReverse.reserve(expectedSize);
    bs.bufferInfo.reserve(expectedSize);
}

void InitVertexArrayMap(size_t expectedSize) {
    auto &bs = GLState.buffer;
    bs.vaoMap.reserve(expectedSize);
    bs.vaoMapReverse.reserve(expectedSize);
}