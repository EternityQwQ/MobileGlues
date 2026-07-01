// MobileGlues - gl/gl.cpp
// Core GL state wrappers: glClearDepth, glClear, glHint, glDrawBuffer, glReadBuffer.
// NOTE: glDepthRange, glPolygonMode, glPointSize are in gl_stub.cpp
//       glViewport is in FSR1/FSR1.cpp
//       glGenTextures is in gl_native.cpp
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
#include "framebuffer.h"
#include "../gles/loader.h"
#include "mg.h"
#include <GLES3/gl32.h>

#define DEBUG 0

// ============================================================================
// glClearDepth - desktop uses double, ES uses float
// ============================================================================

void glClearDepth(GLclampd depth) {
    LOG()
    GLES.glClearDepthf((float)depth);
    GLState.legacy.clearDepth = (GLfloat)depth;
    CHECK_GL_ERROR
}

// ============================================================================
// glClear - handles legacy clear mask conversion
// ============================================================================

void glClear(GLbitfield mask) {
    LOG()
    // GL_ACCUM_BUFFER_BIT is not supported in ES, strip it
    mask &= ~0x00000200; // GL_ACCUM_BUFFER_BIT
    GLES.glClear(mask);
    CHECK_GL_ERROR
}

// ============================================================================
// glHint - pass through (ES supports basic hints)
// ============================================================================

void glHint(GLenum target, GLenum mode) {
    LOG()
    GLES.glHint(target, mode);
    CHECK_GL_ERROR
}

// ============================================================================
// glDrawBuffer - maps to glDrawBuffers in ES
// ============================================================================

extern "C" void glDrawBuffer(GLenum mode) {
    LOG()
    if (mode == GL_NONE) {
        GLenum none = GL_NONE;
        GLES.glDrawBuffers(1, &none);
        GLState.framebuffer.drawBuffers[0] = GL_NONE;
        GLState.framebuffer.drawBufferCount = 1;
    } else if (mode >= GL_COLOR_ATTACHMENT0 && mode <= GL_COLOR_ATTACHMENT0 + 31) {
        GLES.glDrawBuffers(1, &mode);
        GLState.framebuffer.drawBuffers[0] = mode;
        GLState.framebuffer.drawBufferCount = 1;
    } else if (mode == GL_BACK || mode == GL_FRONT) {
        GLenum back = GL_BACK;
        GLES.glDrawBuffers(1, &back);
        GLState.framebuffer.drawBuffers[0] = GL_BACK;
        GLState.framebuffer.drawBufferCount = 1;
    }
    CHECK_GL_ERROR
}

// ============================================================================
// glReadBuffer - track read buffer state
// ============================================================================

extern "C" void glReadBuffer(GLenum mode) {
    LOG()
    GLES.glReadBuffer(mode);
    GLState.framebuffer.readBuffer = mode;
    CHECK_GL_ERROR
}