// MobileGlues - gl/drawing.cpp
// Draw call dispatch: sampler buffer emulation, atomic counter handling.
//
// Architecture principle: "ES 3.2 native → native, ES 3.2 not native → CPU simulation"
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

#define DEBUG 0

// ---------------------------------------------------------------------------
// Module-level state: tracked 2D texture bindings per unit
// (referenced from texture.cpp and drawing.cpp)
// ---------------------------------------------------------------------------

GLuint g_tracked_tex2d_binding[32] = {0};

// ============================================================================
// Atomic counter buffer emulation
// ============================================================================

static void syncAtomicCounters() {
    auto &bs = GLState.buffer;
    if (bs.atomicCounterBufferBinding == 0) return;
    if (bs.atomicCounterData.empty()) return;

    GLuint realBuf = GLState.GetRealBuffer(bs.atomicCounterBufferBinding);
    if (realBuf) {
        GLES.glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, realBuf);
        GLES.glBufferSubData(GL_ATOMIC_COUNTER_BUFFER,
                             bs.atomicCounterBufferOffset,
                             bs.atomicCounterBufferSize,
                             bs.atomicCounterData.data());
    }
}

static void readbackAtomicCounters() {
    auto &bs = GLState.buffer;
    if (bs.atomicCounterBufferBinding == 0) return;

    GLuint realBuf = GLState.GetRealBuffer(bs.atomicCounterBufferBinding);
    if (realBuf) {
        GLES.glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, realBuf);
        void *ptr = GLES.glMapBufferRange(GL_ATOMIC_COUNTER_BUFFER,
                                          bs.atomicCounterBufferOffset,
                                          bs.atomicCounterBufferSize,
                                          GL_MAP_READ_BIT);
        if (ptr) {
            memcpy(bs.atomicCounterData.data(), ptr, (size_t)bs.atomicCounterBufferSize);
            GLES.glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
        }
    }
}

// ============================================================================
// Draw call dispatch
// ============================================================================

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawArrays(mode, first, count);
    CHECK_GL_ERROR
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawElements(mode, count, type, indices);
    CHECK_GL_ERROR
}

void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawArraysInstanced(mode, first, count, instancecount);
    CHECK_GL_ERROR
}

void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawElementsInstanced(mode, count, type, indices, instancecount);
    CHECK_GL_ERROR
}

void glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawRangeElements(mode, start, end, count, type, indices);
    CHECK_GL_ERROR
}

void glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
    CHECK_GL_ERROR
}

void glDrawArraysIndirect(GLenum mode, const void *indirect) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawArraysIndirect(mode, indirect);
    CHECK_GL_ERROR
}

void glDrawElementsIndirect(GLenum mode, GLenum type, const void *indirect) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDrawElementsIndirect(mode, type, indirect);
    CHECK_GL_ERROR
}

// ============================================================================
// Compute shader dispatch
// ============================================================================

void glDispatchCompute(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z) {
    LOG()
    PREPARE_FOR_DRAW();
    syncAtomicCounters();
    GLES.glDispatchCompute(num_groups_x, num_groups_y, num_groups_z);
    readbackAtomicCounters();
    CHECK_GL_ERROR
}

// ============================================================================
// glDrawBuffers - track draw buffer state
// ============================================================================

void glDrawBuffers(GLsizei n, const GLenum *bufs) {
    LOG()
    GLES.glDrawBuffers(n, bufs);

    auto &fb = GLState.framebuffer;
    fb.drawBufferCount = n;
    for (GLsizei i = 0; i < n && i < MAX_DRAW_BUFFERS; i++) {
        fb.drawBuffers[i] = bufs[i];
    }
}

// ============================================================================
// glBindImageTexture - track image unit state
// ============================================================================

void glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format) {
    LOG()
    GLES.glBindImageTexture(unit, texture, level, layered, layer, access, format);

    if (unit < MAX_IMAGE_UNITS) {
        auto &img = GLState.image.imageUnits[unit];
        img.texture = texture;
        img.level = level;
        img.layered = layered;
        img.layer = layer;
        img.access = access;
        img.format = format;
    }
}

// ============================================================================
// glMemoryBarrier - handle atomic counter buffer sync
// ============================================================================

void glMemoryBarrier(GLbitfield barriers) {
    LOG()
    if (barriers & GL_ATOMIC_COUNTER_BARRIER_BIT) {
        syncAtomicCounters();
    }
    GLES.glMemoryBarrier(barriers);
}