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
// glDetachShader: Track detached shaders
// ---------------------------------------------------------------------------
void glDetachShader(GLuint program, GLuint shader) {
    LOG()
    LOG_D("glDetachShader: program=%d, shader=%d", program, shader)

    GLES.glDetachShader(program, shader);
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
// ---------------------------------------------------------------------------
void glDeleteProgram(GLuint program) {
    LOG()
    GLES.glDeleteProgram(program);
}

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
// ---------------------------------------------------------------------------
void glDeleteShader(GLuint shader) {
    LOG()
    GLES.glDeleteShader(shader);
}

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
// Uniform management
// ---------------------------------------------------------------------------
GLint glGetUniformLocation(GLuint program, const GLchar* name) {
    return GLES.glGetUniformLocation(program, name);
}

void glUniform1f(GLint location, GLfloat v0) { GLES.glUniform1f(location, v0); }
void glUniform1i(GLint location, GLint v0) { GLES.glUniform1i(location, v0); }
void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) { GLES.glUniform1fv(location, count, value); }
void glUniform1iv(GLint location, GLsizei count, const GLint* value) { GLES.glUniform1iv(location, count, value); }
void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { GLES.glUniform2f(location, v0, v1); }
void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) { GLES.glUniform2fv(location, count, value); }
void glUniform2i(GLint location, GLint v0, GLint v1) { GLES.glUniform2i(location, v0, v1); }
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { GLES.glUniform3f(location, v0, v1, v2); }
void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) { GLES.glUniform3fv(location, count, value); }
void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { GLES.glUniform3i(location, v0, v1, v2); }
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { GLES.glUniform4f(location, v0, v1, v2, v3); }
void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) { GLES.glUniform4fv(location, count, value); }
void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { GLES.glUniform4i(location, v0, v1, v2, v3); }
void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { GLES.glUniformMatrix2fv(location, count, transpose, value); }
void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { GLES.glUniformMatrix3fv(location, count, transpose, value); }
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { GLES.glUniformMatrix4fv(location, count, transpose, value); }

void glGetProgramiv(GLuint program, GLenum pname, GLint* params) { GLES.glGetProgramiv(program, pname, params); }
void glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog) { GLES.glGetProgramInfoLog(program, bufSize, length, infoLog); }
void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) { GLES.glGetShaderiv(shader, pname, params); }
void glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog) { GLES.glGetShaderInfoLog(shader, bufSize, length, infoLog); }

void glBindAttribLocation(GLuint program, GLuint index, const GLchar* name) { GLES.glBindAttribLocation(program, index, name); }
void glBindFragDataLocation(GLuint program, GLuint color, const GLchar* name) { GLES.glBindFragDataLocation(program, color, name); }
GLint glGetAttribLocation(GLuint program, const GLchar* name) { return GLES.glGetAttribLocation(program, name); }
GLint glGetFragDataLocation(GLuint program, const GLchar* name) { return GLES.glGetFragDataLocation(program, name); }

void glProgramUniform1f(GLuint program, GLint location, GLfloat v0) { GLES.glProgramUniform1f(program, location, v0); }
void glProgramUniform1i(GLuint program, GLint location, GLint v0) { GLES.glProgramUniform1i(program, location, v0); }
void glProgramUniform1fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) { GLES.glProgramUniform1fv(program, location, count, value); }
void glProgramUniform1iv(GLuint program, GLint location, GLsizei count, const GLint* value) { GLES.glProgramUniform1iv(program, location, count, value); }
void glProgramUniform2f(GLuint program, GLint location, GLfloat v0, GLfloat v1) { GLES.glProgramUniform2f(program, location, v0, v1); }
void glProgramUniform2fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) { GLES.glProgramUniform2fv(program, location, count, value); }
void glProgramUniform3f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { GLES.glProgramUniform3f(program, location, v0, v1, v2); }
void glProgramUniform4f(GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { GLES.glProgramUniform4f(program, location, v0, v1, v2, v3); }
void glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { GLES.glProgramUniformMatrix4fv(program, location, count, transpose, value); }

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