// MobileGlues - gl/mg.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include <unistd.h>
#include <cstring>
#include "mg.h"

#define DEBUG 0

hardware_t hardware;
gl_state_t gl_state;

FUNC_GL_STATE_SIZEI(proxy_width)
FUNC_GL_STATE_SIZEI(proxy_height)
FUNC_GL_STATE_ENUM(proxy_intformat)
FUNC_GL_STATE_UINT(current_program)
FUNC_GL_STATE_UINT(current_tex_unit)
FUNC_GL_STATE_UINT(current_draw_fbo)

void gl_state_bump_state() { gl_state->state_generation++; }
void gl_state_bump_program() {
    gl_state->program_generation++;
    gl_state_bump_state();
}
void gl_state_bump_texture() {
    gl_state->texture_generation++;
    gl_state_bump_state();
}

static int binding_target_to_glstate_index(GLenum target) {
    switch (target) {
    case GL_ARRAY_BUFFER:             return 0;
    case GL_ATOMIC_COUNTER_BUFFER:    return 1;
    case GL_COPY_READ_BUFFER:         return 2;
    case GL_COPY_WRITE_BUFFER:        return 3;
    case GL_DRAW_INDIRECT_BUFFER:     return 4;
    case GL_DISPATCH_INDIRECT_BUFFER: return 5;
    case GL_ELEMENT_ARRAY_BUFFER:     return 6;
    case GL_PIXEL_PACK_BUFFER:        return 7;
    case GL_PIXEL_UNPACK_BUFFER:      return 8;
    case GL_SHADER_STORAGE_BUFFER:    return 9;
    case GL_TRANSFORM_FEEDBACK_BUFFER:return 10;
    case GL_UNIFORM_BUFFER:           return 11;
    case GL_TEXTURE_BUFFER:           return 12;
    default:                          return -1;
    }
}

void gl_state_set_bound_buffer(GLenum target, GLuint buffer) {
    int idx = binding_target_to_glstate_index(target);
    if (idx >= 0 && idx < MG_BINDING_COUNT) gl_state->bound_ssbo[idx] = buffer;
    gl_state_bump_state();
}

GLuint gl_state_get_bound_buffer(GLenum target) {
    int idx = binding_target_to_glstate_index(target);
    if (idx >= 0 && idx < MG_BINDING_COUNT) return gl_state->bound_ssbo[idx];
    return 0;
}

#ifndef __APPLE__
FILE* file;
#endif

void start_log() {
#ifndef __APPLE__
    file = fopen(log_file_path, "a");
#endif
}

void write_log(const char* format, ...) {
#ifndef __APPLE__
    if (file == nullptr) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fprintf(file, "\n");
    fflush(file);
#if FORCE_SYNC_WITH_LOG_FILE == 1
    int fd = fileno(file);
    fsync(fd);
#endif
    // Todo: close file
    // fclose(file);
#endif
}

void write_log_n(const char* format, ...) {
#ifndef __APPLE__
    if (file == NULL) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    // Todo: close file
    fflush(file);
#endif
}

void clear_log() {
#ifndef __APPLE__
    file = fopen(log_file_path, "w");
    if (file == nullptr) {
        return;
    }
    fclose(file);
#endif
}

GLenum pname_convert(GLenum pname) {
    switch (pname) {
    // TODO: Realize GL_TEXTURE_LOD_BIAS for other devices.
    case GL_TEXTURE_LOD_BIAS:
        return GL_TEXTURE_LOD_BIAS_QCOM;
    }
    return pname;
}

GLenum map_tex_target(GLenum target) {
    switch (target) {
    case GL_TEXTURE_1D:
    case GL_TEXTURE_3D:
    case GL_TEXTURE_RECTANGLE_ARB:
        return GL_TEXTURE_2D;

    case GL_PROXY_TEXTURE_1D:
    case GL_PROXY_TEXTURE_3D:
    case GL_PROXY_TEXTURE_RECTANGLE_ARB:
        return GL_PROXY_TEXTURE_2D;

    default:
        return target;
    }
}