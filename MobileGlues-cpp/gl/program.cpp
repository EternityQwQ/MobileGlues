// MobileGlues - gl/program.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

// ============================================================================
// ES 3.2 native → native, ES 3.2 not native → CPU simulation
// ============================================================================

#include "../includes.h"
#include "program.h"
#include "../gles/loader.h"
#include "log.h"
#include "mg.h"
#include <GL/gl.h>
#include <cstring>

#define DEBUG 0

// ============================================================================
// Iris Shader Pipeline: Program/Shader Management
// ============================================================================
// Iris uses a multi-stage shader pipeline that includes vertex, geometry,
// tessellation, and fragment shaders. MobileGlues hooks these to intercept
// GLSL conversion through the GLSL → SPIR-V → GLSL ES pipeline.
// ============================================================================

// ============================================================================
// Program Object Lifecycle (ES 3.2 native)
// ============================================================================

GLuint glCreateProgram() {
    LOG()
    GLuint program = GLES.glCreateProgram();
    LOG_D("glCreateProgram: created program=%d", program)
    return program;
}

void glDeleteProgram(GLuint program) {
    LOG()
    LOG_D("glDeleteProgram: program=%d", program)
    GLES.glDeleteProgram(program);
    CHECK_GL_ERROR
}

// ============================================================================
// Shader Object Lifecycle (ES 3.2 native)
// ============================================================================

// glCreateShader is handled in shader.cpp (GLSL conversion pipeline)

void glDeleteShader(GLuint shader) {
    LOG()
    LOG_D("glDeleteShader: shader=%d", shader)
    GLES.glDeleteShader(shader);
    CHECK_GL_ERROR
}

// ============================================================================
// Shader Compilation (ES 3.2 native)
// glShaderSource is defined in shader.cpp with GLSL conversion
// ============================================================================

void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);

// glCompileShader is handled in shader.cpp (GLSL conversion pipeline)

// ============================================================================
// Program Linking & Validation (ES 3.2 native)
// ============================================================================

void glAttachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glAttachShader: program=%d, shader=%d", program, shader)
    GLES.glAttachShader(program, shader);
}

void glDetachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glDetachShader: program=%d, shader=%d", program, shader)
    GLES.glDetachShader(program, shader);
    CHECK_GL_ERROR
}

void glLinkProgram(GLuint program) {
    LOG()
    GLES.glLinkProgram(program);

    GLint linkStatus = 0;
    GLES.glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);

    if (linkStatus == GL_FALSE) {
        GLint infoLogLength = 0;
        GLES.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength > 1) {
            char* infoLog = new char[infoLogLength];
            GLES.glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog);
            LOG_E("glLinkProgram FAILED: %s", infoLog)
            delete[] infoLog;
        }
    } else {
        LOG_D("glLinkProgram: program=%d linked successfully", program)
    }
}

void glValidateProgram(GLuint program) {
    LOG()
    GLES.glValidateProgram(program);

    GLint validateStatus = 0;
    GLES.glGetProgramiv(program, GL_VALIDATE_STATUS, &validateStatus);

    if (validateStatus == GL_FALSE) {
        GLint infoLogLength = 0;
        GLES.glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength > 1) {
            char* infoLog = new char[infoLogLength];
            GLES.glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog);
            LOG_E("glValidateProgram FAILED: %s", infoLog)
            delete[] infoLog;
        }
    }
}

// ============================================================================
// Program Usage (ES 3.2 native)
// ============================================================================

void glUseProgram(GLuint program) {
    LOG()
    GLES.glUseProgram(program);
}

// ============================================================================
// Program/Shader Queries (ES 3.2 native)
// ============================================================================

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) {
    GLES.glGetProgramiv(program, pname, params);
}

void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) {
    GLES.glGetShaderiv(shader, pname, params);
}

void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    LOG()
    LOG_D("glGetProgramInfoLog: program=%d, bufSize=%d", program, bufSize)
    GLES.glGetProgramInfoLog(program, bufSize, length, infoLog);
    CHECK_GL_ERROR
}

void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) {
    LOG()
    LOG_D("glGetShaderInfoLog: shader=%d, bufSize=%d", shader, bufSize)
    GLES.glGetShaderInfoLog(shader, bufSize, length, infoLog);
    CHECK_GL_ERROR
}

// ============================================================================
// Attribute Location (ES 3.2 native)
// ============================================================================

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    LOG()
    LOG_D("glBindAttribLocation: program=%d, index=%d, name=%s", program, index, name)
    GLES.glBindAttribLocation(program, index, name);
    CHECK_GL_ERROR
}

GLint glGetAttribLocation(GLuint program, const GLchar* name) {
    LOG()
    LOG_D("glGetAttribLocation: program=%d, name=%s", program, name)
    GLint result = GLES.glGetAttribLocation(program, name);
    CHECK_GL_ERROR
    return result;
}

// ============================================================================
// Uniform Location (ES 3.2 native)
// ============================================================================

GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    LOG()
    LOG_D("glGetUniformLocation: program=%d, name=%s", program, name)
    GLint result = GLES.glGetUniformLocation(program, name);
    CHECK_GL_ERROR
    return result;
}

// ============================================================================
// Active Attribute/Uniform Queries (ES 3.2 native)
// ============================================================================

void glGetActiveAttrib(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size,
                       GLenum* type, GLchar* name) {
    LOG()
    LOG_D("glGetActiveAttrib: program=%d, index=%d", program, index)
    GLES.glGetActiveAttrib(program, index, bufSize, length, size, type, name);
    CHECK_GL_ERROR
}

void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei* length, GLint* size,
                        GLenum* type, GLchar* name) {
    LOG()
    LOG_D("glGetActiveUniform: program=%d, index=%d", program, index)
    GLES.glGetActiveUniform(program, index, bufSize, length, size, type, name);
    CHECK_GL_ERROR
}

void glGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count, GLuint* shaders) {
    LOG()
    LOG_D("glGetAttachedShaders: program=%d, maxCount=%d", program, maxCount)
    GLES.glGetAttachedShaders(program, maxCount, count, shaders);
    CHECK_GL_ERROR
}

// ============================================================================
// Frag Data Location (ES 3.2 native via EXT)
// ============================================================================

void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) {
    GLES.glBindFragDataLocationEXT(program, color, name);
}