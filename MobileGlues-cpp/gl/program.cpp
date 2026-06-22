// MobileGlues - gl/program.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "../includes.h"
#include "program.h"
#include "../gles/loader.h"
#include "log.h"
#include "mg.h"
#include <GL/gl.h>
#include <cstring>

#define DEBUG 0

// ---------------------------------------------------------------------------
// Iris Shader Pipeline: Program/Shader Management
// ---------------------------------------------------------------------------
// Iris uses a multi-stage shader pipeline that includes vertex, geometry,
// tessellation, and fragment shaders. MobileGlues hooks these to intercept
// GLSL conversion through the GLSL → SPIR-V → GLSL ES pipeline.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// glAttachShader: Track attached shaders for pipeline validation
// ---------------------------------------------------------------------------
void glAttachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glAttachShader: program=%d, shader=%d", program, shader)

    GLES.glAttachShader(program, shader);
}

// ---------------------------------------------------------------------------
// glLinkProgram: Link the program (runs after all shaders are attached)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// glUseProgram: Activate the program for rendering
// ---------------------------------------------------------------------------
void glUseProgram(GLuint program) {
    LOG()
    GLES.glUseProgram(program);
}

// ---------------------------------------------------------------------------
// glDeleteProgram: Clean up program resources
// (handled by gl_native.cpp)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// glCreateProgram: Create a new program object
// ---------------------------------------------------------------------------
GLuint glCreateProgram() {
    LOG()
    GLuint program = GLES.glCreateProgram();
    LOG_D("glCreateProgram: created program=%d", program)
    return program;
}

// ---------------------------------------------------------------------------
// glCreateShader: Create a new shader object
// ---------------------------------------------------------------------------
GLuint glCreateShader(GLenum shaderType) {
    LOG()
    LOG_D("glCreateShader: type=%d (%s)", shaderType, glEnumToString(shaderType))

    GLuint shader = GLES.glCreateShader(shaderType);
    LOG_D("glCreateShader: created shader=%d", shader)
    return shader;
}

// ---------------------------------------------------------------------------
// glDeleteShader: Delete a shader object
// (handled by gl_native.cpp)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// glValidateProgram: Validate the program for current GL state
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Uniform management / Program queries / glBindAttribLocation / glGet* / glProgramUniform*
// These are handled by gl_native.cpp (NATIVE_FUNCTION_HEAD macros).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// glBindFragDataLocation: Map to EXT variant for OpenGL ES compatibility
// ---------------------------------------------------------------------------
void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) { GLES.glBindFragDataLocationEXT(program, color, name); }

// ---------------------------------------------------------------------------
// glGetProgramiv / glGetShaderiv: Stub implementations (native version commented out in gl_native.cpp)
// ---------------------------------------------------------------------------
void glGetProgramiv(GLuint program, GLenum pname, GLint* params) { GLES.glGetProgramiv(program, pname, params); }
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) { GLES.glGetShaderiv(shader, pname, params); }

// ---------------------------------------------------------------------------
// glShaderSource: Forward declaration (defined in shader.cpp with GLSL conversion)
// ---------------------------------------------------------------------------
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
void glCompileShader(GLuint shader) {
    LOG()
    GLES.glCompileShader(shader);

    GLint compileStatus = 0;
    GLES.glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
    if (compileStatus == GL_FALSE) {
        GLint infoLogLength = 0;
        GLES.glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
        if (infoLogLength > 1) {
            char* infoLog = new char[infoLogLength];
            GLES.glGetShaderInfoLog(shader, infoLogLength, nullptr, infoLog);
            LOG_E("glCompileShader FAILED (shader=%d): %s", shader, infoLog)
            delete[] infoLog;
        }
    } else {
        LOG_D("glCompileShader: shader=%d compiled successfully", shader)
    }
}