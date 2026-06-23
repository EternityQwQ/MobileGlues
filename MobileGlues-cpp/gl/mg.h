// MobileGlues - gl/mg.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#ifndef MOBILEGLUES_MG_H
#define MOBILEGLUES_MG_H

typedef unsigned int uint;

#include <cstring>
#include <cstdlib>
#include <malloc.h>

#ifdef __ANDROID__
#include <android/log.h>
#endif

#include <GL/gl.h>
#include "../gles/gles.h"
#include "log.h"
#include "../gles/loader.h"
#include "../includes.h"
#include "glsl/glsl_for_es.h"
#include "../config/config.h"

#ifdef __cplusplus
extern "C"
{
#endif

// ============================================================================
// MobileGlues Architecture for OpenGL ES 3.2
//
// Rule:
//  - OpenGL ES 3.2 natively supports: Direct pass-through (no CPU emulation)
//  - OpenGL ES 3.2 does NOT support: CPU emulation / stub
// ============================================================================

// State tracking for immediate mode emulation (only needed for deprecated features)
struct immediate_state_s {
    bool in_begin;
    GLenum begin_mode;
    GLuint vertex_count;
    float vertices[4096 * 4];  // position
    float colors[4096 * 4];    // color
    float texcoords[4096 * 4]; // tex coords
    float normals[4096 * 3];   // normals
};

// Matrix stack for legacy transformation emulation
#define MAX_MATRIX_STACK 32

struct matrix_stack_s {
    int depth;
    int max_depth;
    float* stack;  // 16 floats per matrix
};

struct matrix_mode_state_s {
    GLenum current_mode;  // GL_MODELVIEW, GL_PROJECTION, GL_TEXTURE
    matrix_stack_s modelview;
    matrix_stack_s projection;
    matrix_stack_s texture;
    float current_matrix[16];
};

// Display list emulation state
struct display_list_s {
    bool is_compiled;
    void* commands;  // TODO: command buffer
    size_t cmd_size;
};

#define FUNC_GL_STATE_SIZEI(name)                                                                                      \
    void set_gl_state_##name(GLsizei value) {                                                                          \
        gl_state->name = value;                                                                                        \
        LOG_D(" -> gl_state: %s is %d", #name, value);                                                                 \
    }
#define FUNC_GL_STATE_ENUM(name)                                                                                       \
    void set_gl_state_##name(GLenum value) {                                                                           \
        gl_state->name = value;                                                                                        \
        LOG_D(" -> gl_state: %s is %d", #name, value);                                                                 \
    }
#define FUNC_GL_STATE_UINT(name)                                                                                       \
    void set_gl_state_##name(GLuint value) {                                                                           \
        gl_state->name = value;                                                                                        \
        LOG_D(" -> gl_state: %s is %d", #name, value);                                                                 \
    }
#define FUNC_GL_STATE_SIZEI_DECLARATION(name) void set_gl_state_##name(GLsizei value);
#define FUNC_GL_STATE_ENUM_DECLARATION(name) void set_gl_state_##name(GLenum value);
#define FUNC_GL_STATE_UINT_DECLARATION(name) void set_gl_state_##name(GLuint value);

    FUNC_GL_STATE_SIZEI_DECLARATION(proxy_width)
    FUNC_GL_STATE_SIZEI_DECLARATION(proxy_height)
    FUNC_GL_STATE_ENUM_DECLARATION(proxy_intformat)
    FUNC_GL_STATE_UINT_DECLARATION(current_program)
    FUNC_GL_STATE_UINT_DECLARATION(current_tex_unit)
    FUNC_GL_STATE_UINT_DECLARATION(current_draw_fbo)

    struct hardware_s {
        unsigned int es_version;      // ES version in hundreds (e.g. 320 = 3.2)
        bool emulate_texture_buffer;  // Always false on ES 3.2 (native support)
    };
    typedef struct hardware_s* hardware_t;
    extern hardware_t hardware;

    struct gl_state_s {
        GLsizei proxy_width;
        GLsizei proxy_height;
        GLenum proxy_intformat;

        GLuint current_program;
        GLuint current_tex_unit;
        GLuint current_draw_fbo;

        // Immediate mode emulation (GL 1.x begin/end) - not supported by ES 3.2
        immediate_state_s immediate;

        // Matrix stack emulation (GL 1.x) - not supported by ES 3.2
        matrix_mode_state_s matrix;
    };
    typedef struct gl_state_s* gl_state_t;
    extern gl_state_t gl_state;

    GLenum pname_convert(GLenum pname);
    GLenum map_tex_target(GLenum target);
    void start_log();
    void write_log(const char* format, ...);
    void write_log_n(const char* format, ...);
    void clear_log();

    // Initialize emulation state for unsupported features
    void init_emulation_state();
    // Flush immediate mode vertex buffer and draw
    void flush_immediate();

#ifdef __cplusplus
}
#endif

void prepareForDraw();

#endif // MOBILEGLUES_MG_H
