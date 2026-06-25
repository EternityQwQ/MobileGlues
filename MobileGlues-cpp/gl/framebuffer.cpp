// MobileGlues - gl/framebuffer.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// ES 3.2 native → native, ES 3.2 not native → CPU simulation
// ============================================================================

#include "framebuffer.h"
#include "log.h"
#include "mg.h"
#include "../config/settings.h"
#include "FSR1/FSR1.h"

#define DEBUG 0

// ============================================================================
// Internal State: Framebuffer Map Management
// ============================================================================

static GLint MAX_COLOR_ATTACHMENTS = 0;
static GLint MAX_DRAW_BUFFERS = 0;
GLuint current_draw_fbo = 0;
GLuint current_read_fbo = 0;
std::vector<framebuffer_t> framebuffers;

// ============================================================================
// Temporary FBO pool: avoids create/destroy overhead in hot paths
// (glCopyTexImage2D, glCopyTexSubImage2D, glGetTexImage, glClearTexImage)
// ============================================================================
static std::vector<GLuint> g_tempFboPool;
static size_t g_tempFboPoolIndex = 0;

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

void ensure_max_attachments() {
    if (MAX_COLOR_ATTACHMENTS == 0) {
        GLES.glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &MAX_COLOR_ATTACHMENTS);
        MAX_COLOR_ATTACHMENTS = MAX_COLOR_ATTACHMENTS > 0 ? MAX_COLOR_ATTACHMENTS : 8;
    }
    if (MAX_DRAW_BUFFERS == 0) {
        GLES.glGetIntegerv(GL_MAX_DRAW_BUFFERS, &MAX_DRAW_BUFFERS);
        MAX_DRAW_BUFFERS = MAX_DRAW_BUFFERS > 0 ? MAX_DRAW_BUFFERS : 8;
    }
}

framebuffer_t& get_framebuffer(GLuint id) {
    if (id >= framebuffers.size()) {
        // Exponential growth: double the size until it covers id, with a minimum of 16
        size_t new_size = framebuffers.size() > 0 ? framebuffers.size() : 16;
        while (new_size <= id) new_size *= 2;
        framebuffers.resize(new_size);
    }
    return framebuffers[id];
}

void InitFramebufferMap(size_t expectedSize) {
    framebuffers.reserve(expectedSize);
}

void init_framebuffer(framebuffer_t& fbo) {
    if (!fbo.initialized) {
        fbo.color_attachments = new attachment_t[MAX_COLOR_ATTACHMENTS];
        memset(fbo.color_attachments, 0, sizeof(attachment_t) * MAX_COLOR_ATTACHMENTS);
        fbo.initialized = true;
    }
}

// ============================================================================
// Framebuffer Object Lifecycle (ES 3.2 native, with FBO map)
// ============================================================================

void glGenFramebuffers(GLsizei n, GLuint* framebuffers_out) {
    LOG()
    LOG_D("glGenFramebuffers(%i, %p)", n, framebuffers_out)
    GLES.glGenFramebuffers(n, framebuffers_out);
    CHECK_GL_ERROR
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers_in) {
    LOG()
    LOG_D("glDeleteFramebuffers(%i, %p)", n, framebuffers_in)
    for (int i = 0; i < n; ++i) {
        GLuint id = framebuffers_in[i];
        if (id < framebuffers.size()) {
            framebuffer_t& fbo = framebuffers[id];
            if (fbo.initialized) {
                delete[] fbo.color_attachments;
                fbo.color_attachments = nullptr;
                fbo.initialized = false;
            }
            fbo = framebuffer_t{};
        }
    }
    GLES.glDeleteFramebuffers(n, framebuffers_in);
    CHECK_GL_ERROR
}

// ============================================================================
// Framebuffer Binding (ES 3.2 native, with FBO map)
// ============================================================================

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    ensure_max_attachments();
    framebuffer_t& fbo = get_framebuffer(framebuffer);

    if (framebuffer == 0 && target != GL_READ_FRAMEBUFFER) {
        framebuffer = FSR1_Context::g_renderFBO;
        FSR1_Context::g_dirty = true;
    }

    if (target != GL_READ_FRAMEBUFFER) {
        set_gl_state_current_draw_fbo(framebuffer);
    }

    if (framebuffer != 0) {
        init_framebuffer(fbo);
    }
    if (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        current_draw_fbo = framebuffer;
    }
    if (target == GL_READ_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        current_read_fbo = framebuffer;
    }
    GLES.glBindFramebuffer(target, framebuffer);
}

// ============================================================================
// Attachment Tracking Helpers
// ============================================================================

void update_attachment(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    GLuint current_fbo = (target == GL_READ_FRAMEBUFFER) ? current_read_fbo : current_draw_fbo;
    if (current_fbo == 0) return;
    framebuffer_t& fbo = framebuffers[current_fbo];
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + MAX_COLOR_ATTACHMENTS) {
        int index = attachment - GL_COLOR_ATTACHMENT0;
        fbo.color_attachments[index] = {textarget, texture, level};
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo.depth_attachment = {textarget, texture, level};
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo.stencil_attachment = {textarget, texture, level};
    }
}

// ============================================================================
// Framebuffer Texture Attachments (ES 3.2 native)
// ============================================================================

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {
    update_attachment(target, attachment, textarget, texture, level);
    // Invalidate slot mapping cache for this FBO
    GLuint fbo = (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER) ? current_draw_fbo : current_read_fbo;
    if (fbo != 0 && attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + MAX_COLOR_ATTACHMENTS) {
        framebuffers[fbo].last_physical_slot_mapping[attachment - GL_COLOR_ATTACHMENT0] = 0;
        framebuffers[fbo].last_read_buffer_src = 0;
    }
    GLES.glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    update_attachment(target, attachment, GL_TEXTURE_2D, texture, level);
    GLES.glFramebufferTexture(target, attachment, texture, level);
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {
    LOG()
    LOG_D("glFramebufferTextureLayer, target = %s, attachment = %s, texture = %d, level = %d, layer = %d",
          glEnumToString(target), glEnumToString(attachment), texture, level, layer)
    GLES.glFramebufferTextureLayer(target, attachment, texture, level, layer);
    CHECK_GL_ERROR
}

// ============================================================================
// Framebuffer Renderbuffer Attachment (ES 3.2 native)
// ============================================================================

void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {
    LOG()
    LOG_D("glFramebufferRenderbuffer, target = %s, attachment = %s, renderbuffertarget = %s, renderbuffer = %d",
          glEnumToString(target), glEnumToString(attachment), glEnumToString(renderbuffertarget), renderbuffer)
    GLES.glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
    CHECK_GL_ERROR
}

// ============================================================================
// Framebuffer Status Check (ES 3.2 native)
// ============================================================================

GLenum glCheckFramebufferStatus(GLenum target) {
    GLenum status = GLES.glCheckFramebufferStatus(target);
    if (global_settings.ignore_error == IgnoreErrorLevel::Full && status != GL_FRAMEBUFFER_COMPLETE) {
        return GL_FRAMEBUFFER_COMPLETE;
    }
    return status;
}

// ============================================================================
// Framebuffer Blit (ES 3.2 native)
// ============================================================================

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    LOG()
    LOG_D("glBlitFramebuffer, src = (%d,%d,%d,%d), dst = (%d,%d,%d,%d), mask = 0x%x, filter = %s",
          srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, glEnumToString(filter))
    GLES.glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
    CHECK_GL_ERROR
}

// ============================================================================
// Framebuffer Invalidation (ES 3.2 native)
// ============================================================================

void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments) {
    LOG()
    LOG_D("glInvalidateFramebuffer, target = %s, numAttachments = %d", glEnumToString(target), numAttachments)
    GLES.glInvalidateFramebuffer(target, numAttachments, attachments);
    CHECK_GL_ERROR
}

void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments, const GLenum* attachments,
                                 GLint x, GLint y, GLsizei width, GLsizei height) {
    LOG()
    LOG_D("glInvalidateSubFramebuffer, target = %s, numAttachments = %d, rect = (%d,%d,%d,%d)",
          glEnumToString(target), numAttachments, x, y, width, height)
    GLES.glInvalidateSubFramebuffer(target, numAttachments, attachments, x, y, width, height);
    CHECK_GL_ERROR
}

// ============================================================================
// Framebuffer Parameters (ES 3.2 native)
// ============================================================================

void glFramebufferParameteri(GLenum target, GLenum pname, GLint param) {
    LOG()
    LOG_D("glFramebufferParameteri, target = %s, pname = %s, param = %d", glEnumToString(target), glEnumToString(pname), param)
    GLES.glFramebufferParameteri(target, pname, param);
    CHECK_GL_ERROR
}

void glGetFramebufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    LOG()
    LOG_D("glGetFramebufferParameteriv, target = %s, pname = %s", glEnumToString(target), glEnumToString(pname))
    GLES.glGetFramebufferParameteriv(target, pname, params);
    CHECK_GL_ERROR
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint* params) {
    LOG()
    LOG_D("glGetFramebufferAttachmentParameteriv, target = %s, attachment = %s, pname = %s",
          glEnumToString(target), glEnumToString(attachment), glEnumToString(pname))
    GLES.glGetFramebufferAttachmentParameteriv(target, attachment, pname, params);
    CHECK_GL_ERROR
}

// ============================================================================
// Draw Buffers (ES 3.2 native, with attachment remapping)
// ============================================================================

void glDrawBuffer(GLenum buffer) {
    LOG()
    LOG_D("glDrawBuffer %d", buffer)

    if (current_draw_fbo == 0) {
        GLenum buffers[] = {buffer};
        glDrawBuffers(1, buffers);
    } else {
        // Use cached MAX_COLOR_ATTACHMENTS (set by ensure_max_attachments on first FBO gen)
        GLint maxAttachments = MAX_COLOR_ATTACHMENTS;

        // Pre-sized thread_local buffer to avoid per-frame allocation
        static thread_local std::vector<GLenum> tls_buffers;
        if (tls_buffers.size() < (size_t)maxAttachments) [[unlikely]]
            tls_buffers.resize(maxAttachments, GL_NONE);

        if (buffer == GL_NONE) {
            framebuffers[current_draw_fbo].color_attachments_all_none = true;
            std::fill(tls_buffers.begin(), tls_buffers.begin() + maxAttachments, GL_NONE);
            glDrawBuffers(maxAttachments, tls_buffers.data());
        } else if (buffer >= GL_COLOR_ATTACHMENT0 && buffer < GL_COLOR_ATTACHMENT0 + maxAttachments) {
            framebuffers[current_draw_fbo].color_attachments_all_none = false;
            std::fill(tls_buffers.begin(), tls_buffers.begin() + maxAttachments, GL_NONE);
            tls_buffers[buffer - GL_COLOR_ATTACHMENT0] = buffer;
            glDrawBuffers(maxAttachments, tls_buffers.data());
        }
    }
    CHECK_GL_ERROR;
}

void glDrawBuffers(GLsizei n, const GLenum* bufs) {
    LOG()
    if (current_draw_fbo == 0) {
        GLES.glDrawBuffers(n, bufs);
        return;
    }

    framebuffer_t& fbo = framebuffers[current_draw_fbo];

    bool all_none = true;
    for (int i = 0; i < n; ++i) {
        if (bufs[i] != GL_NONE) {
            all_none = false;
            break;
        }
    }

    if (all_none) {
        LOG_D("glDrawBuffers, fb %d all_none true", current_draw_fbo)
        fbo.color_attachments_all_none = true;
        GLES.glDrawBuffers(n, bufs);
        return;
    } else {
        LOG_D("glDrawBuffers, fb %d all_none false", current_draw_fbo)
        fbo.color_attachments_all_none = false;
    }

    // Pre-sized thread_local static buffer to avoid per-frame allocation
    static thread_local std::vector<GLenum> new_bufs;
    if (new_bufs.size() < (size_t)n) [[unlikely]]
        new_bufs.resize(n);
    for (int i = 0; i < n; i++) {
        if (bufs[i] >= GL_COLOR_ATTACHMENT0 && bufs[i] < GL_COLOR_ATTACHMENT0 + MAX_COLOR_ATTACHMENTS) {
            GLenum logical_attachment = bufs[i];
            GLenum physical_attachment = GL_COLOR_ATTACHMENT0 + i;
            new_bufs[i] = physical_attachment;
            int index = logical_attachment - GL_COLOR_ATTACHMENT0;
            // Skip redundant rebind if the attachment is already in the correct physical slot
            if (fbo.last_physical_slot_mapping[index] != physical_attachment) {
                attachment_t& attach = fbo.color_attachments[index];
                GLES.glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, physical_attachment, attach.textarget, attach.texture,
                                            attach.level);
                fbo.last_physical_slot_mapping[index] = physical_attachment;
            }
        } else {
            new_bufs[i] = bufs[i];
        }
    }
    GLES.glDrawBuffers(n, new_bufs.data());
}

// ============================================================================
// Read Buffer (ES 3.2 native, with attachment remapping)
// ============================================================================

void glReadBuffer(GLenum src) {
    if (current_read_fbo != 0 && src >= GL_COLOR_ATTACHMENT0 && src < GL_COLOR_ATTACHMENT0 + MAX_COLOR_ATTACHMENTS) {
        framebuffer_t& fbo = framebuffers[current_read_fbo];
        int index = src - GL_COLOR_ATTACHMENT0;
        // Skip redundant rebind if the same attachment is already mapped
        if (fbo.last_read_buffer_src != src) {
            attachment_t& attach = fbo.color_attachments[index];
            GLES.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, attach.textarget, attach.texture,
                                        attach.level);
            fbo.last_read_buffer_src = src;
        }
        GLES.glReadBuffer(GL_COLOR_ATTACHMENT0);
    } else {
        GLES.glReadBuffer(src);
    }
}