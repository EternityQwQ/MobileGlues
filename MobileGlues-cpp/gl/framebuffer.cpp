// MobileGlues - gl/framebuffer.cpp
// Framebuffer state management: FBO ID mapping, attachment tracking,
// draw buffer remapping, and temp FBO pool for format conversion.
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
#include <cstring>
#include <vector>

#define DEBUG 0

// ---------------------------------------------------------------------------
// Temporary FBO pool for hot-path texture operations
// (acquireTempFBO / releaseTempFBO — used by texture.cpp)
// ---------------------------------------------------------------------------

static std::vector<GLuint> g_tempFboPool;
static size_t g_tempFboPoolIndex = 0;

// ============================================================================
// FBO ID management
// ============================================================================

void glGenFramebuffers(GLsizei n, GLuint *framebuffers) {
    LOG()
    GLES.glGenFramebuffers(n, framebuffers);

    auto &fb = GLState.framebuffer;
    for (GLsizei i = 0; i < n; i++) {
        fb.fboMap[framebuffers[i]] = framebuffers[i];
        fb.fboMapReverse[framebuffers[i]] = framebuffers[i];
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
    LOG()
    GLES.glDeleteFramebuffers(n, framebuffers);

    auto &fb = GLState.framebuffer;
    for (GLsizei i = 0; i < n; i++) {
        GLuint id = framebuffers[i];
        if (id == 0) continue;

        if (fb.drawFBO == id) fb.drawFBO = 0;
        if (fb.readFBO == id) fb.readFBO = 0;

        fb.fboMap.erase(id);
        fb.fboMapReverse.erase(id);
        fb.attachments.erase(id);
    }
}

// ============================================================================
// glBindFramebuffer
// ============================================================================

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    LOG()
    GLES.glBindFramebuffer(target, framebuffer);

    auto &fb = GLState.framebuffer;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        fb.drawFBO = framebuffer;
        GLState.currentDrawFBO = framebuffer;
    }
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER) {
        fb.readFBO = framebuffer;
    }

    STATE_LOG("glBindFramebuffer: target=0x%X, fbo=%u", target, framebuffer);
}

// ============================================================================
// glFramebufferTexture2D / glFramebufferTexture - attachment tracking
// ============================================================================

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    LOG()
    GLenum esTexTarget = GLStateManager::ConvertTextureTarget(textarget);
    GLES.glFramebufferTexture2D(target, attachment, esTexTarget, texture, level);

    // Track attachment
    GLuint currentFBO = (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
                        ? GLState.framebuffer.drawFBO
                        : GLState.framebuffer.readFBO;

    if (currentFBO) {
        auto &att = GLState.framebuffer.attachments[currentFBO][attachment];
        att.fbo = currentFBO;
        att.attachment = attachment;
    }
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    LOG()
    GLES.glFramebufferTexture(target, attachment, texture, level);

    GLuint currentFBO = (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
                        ? GLState.framebuffer.drawFBO
                        : GLState.framebuffer.readFBO;

    if (currentFBO) {
        auto &att = GLState.framebuffer.attachments[currentFBO][attachment];
        att.fbo = currentFBO;
        att.attachment = attachment;
    }
}

// ============================================================================
// glFramebufferTextureLayer - native passthrough
// ============================================================================

void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    LOG()
    GLES.glFramebufferTextureLayer(target, attachment, texture, level, layer);
    CHECK_GL_ERROR
}

// ============================================================================
// glFramebufferRenderbuffer - native passthrough
// ============================================================================

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    LOG()
    GLES.glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
    CHECK_GL_ERROR
}

// ============================================================================
// glCheckFramebufferStatus
// ============================================================================

GLenum glCheckFramebufferStatus(GLenum target) {
    LOG()
    GLenum status = GLES.glCheckFramebufferStatus(target);
    // Ignore incomplete errors for applications that don't fully set up FBOs
    if (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT ||
        status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT) {
        status = GL_FRAMEBUFFER_COMPLETE;
    }
    return status;
}

// ============================================================================
// glFramebufferParameteri - native passthrough
// ============================================================================

void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
    LOG()
    GLES.glFramebufferParameteri(target, pname, param);
    CHECK_GL_ERROR
}

// ============================================================================
// glGetFramebufferAttachmentParameteriv - native passthrough
// ============================================================================

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params) {
    LOG()
    GLES.glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);
    CHECK_GL_ERROR
}

// ============================================================================
// glGetFramebufferParameteriv - native passthrough
// ============================================================================

void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint *params) {
    LOG()
    GLES.glGetFramebufferParameteriv(target, pname, params);
    CHECK_GL_ERROR
}

// ============================================================================
// glBlitFramebuffer - native passthrough
// ============================================================================

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    LOG()
    // Strip GL_ACCUM_BUFFER_BIT
    mask &= ~0x00000200;
    GLES.glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    CHECK_GL_ERROR
}

// ============================================================================
// glInvalidateFramebuffer / glInvalidateSubFramebuffer - native passthrough
// ============================================================================

void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments) {
    LOG()
    GLES.glInvalidateFramebuffer(target, numAttachments, attachments);
    CHECK_GL_ERROR
}

void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum *attachments,
                                GLint x, GLint y, GLsizei width, GLsizei height) {
    LOG()
    GLES.glInvalidateSubFramebuffer(target, numAttachments, attachments, x, y, width, height);
    CHECK_GL_ERROR
}

// ============================================================================
// Map initialization helpers (for backward compatibility)
// ============================================================================

void InitFramebufferMap(size_t expectedSize) {
    auto &fb = GLState.framebuffer;
    fb.fboMap.reserve(expectedSize);
    fb.fboMapReverse.reserve(expectedSize);
}

// ============================================================================
// Temp FBO pool — for hot-path texture operations (glCopyTexImage2D, etc.)
// ============================================================================

GLuint acquireTempFBO() {
    if (g_tempFboPoolIndex < g_tempFboPool.size()) {
        return g_tempFboPool[g_tempFboPoolIndex++];
    }
    GLuint fbo;
    GLES.glGenFramebuffers(1, &fbo);
    g_tempFboPool.push_back(fbo);
    g_tempFboPoolIndex++;
    return fbo;
}

void releaseTempFBO(GLuint fbo) {
    // Simple: just decrement index. FBO is not deleted, just returned to pool.
    if (g_tempFboPoolIndex > 0) g_tempFboPoolIndex--;
}

void releaseAllTempFBOs() {
    g_tempFboPoolIndex = 0;
}

void cleanupTempFBOs() {
    for (GLuint fbo : g_tempFboPool) {
        GLES.glDeleteFramebuffers(1, &fbo);
    }
    g_tempFboPool.clear();
    g_tempFboPoolIndex = 0;
}