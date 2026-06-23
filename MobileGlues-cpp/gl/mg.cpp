// MobileGlues - gl/mg.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include <unistd.h>
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

UnorderedMap<GLuint, bool> program_map_is_sampler_buffer_emulated;
UnorderedMap<GLuint, bool> program_map_is_atomic_counter_emulated;

FILE* file;

void start_log() {
    file = fopen(log_file_path, "a");
}

void write_log(const char* format, ...) {
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
}

void write_log_n(const char* format, ...) {
    if (file == NULL) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fflush(file);
}

void clear_log() {
    file = fopen(log_file_path, "w");
    if (file == nullptr) {
        return;
    }
    fclose(file);
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
    case GL_TEXTURE_RECTANGLE_ARB:
        return GL_TEXTURE_2D;

    case GL_PROXY_TEXTURE_1D:
    case GL_PROXY_TEXTURE_RECTANGLE_ARB:
        return GL_PROXY_TEXTURE_2D;

    default:
        return target;
    }
}