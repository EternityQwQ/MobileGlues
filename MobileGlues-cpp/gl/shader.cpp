// MobileGlues - gl/shader.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU Lesser General Public License v2.1:
//   https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt
// SPDX-License-Identifier: LGPL-2.1-only
// End of Source File Header

#include "shader.h"
#include "../includes.h"
#include "glsl/glsl_for_es.h"
#include "log.h"
#include "../gles/loader.h"
#include "mg.h"
#include "GL/gl.h"
#include <regex>
#include <cstring>
#include <string>

#define DEBUG 0

// ---------------------------------------------------------------------------
// Determine if shader is already ES-compatible (direct passthrough)
// ---------------------------------------------------------------------------
static bool is_direct_shader(const char* glsl_code) {
    std::string code = glsl_code;
    int version = getGLSLVersion(glsl_code);
    if (version == -1) return false;
    // Only pass through shaders that are already ESSL
    return (version == 100 || version == 300 || version == 310 || version == 320);
}

// ---------------------------------------------------------------------------
// Determine if shader is a geometry shader based on content analysis
// ---------------------------------------------------------------------------
static bool is_geometry_shader(const char* glsl_code) {
    std::string code = glsl_code;
    // Check for geometry shader layout qualifiers
    return (code.find("layout(triangles)") != std::string::npos ||
            code.find("layout(triangle_strip)") != std::string::npos ||
            code.find("layout(points)") != std::string::npos ||
            code.find("layout(lines)") != std::string::npos ||
            code.find("layout(line_strip)") != std::string::npos ||
            code.find("layout(quads)") != std::string::npos ||
            code.find("layout(isolines)") != std::string::npos ||
            code.find("max_vertices") != std::string::npos ||
            code.find("EmitVertex()") != std::string::npos ||
            code.find("EndPrimitive()") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Determine if shader is a tessellation shader based on content analysis
// ---------------------------------------------------------------------------
static bool is_tessellation_shader(const char* glsl_code) {
    std::string code = glsl_code;
    return (code.find("layout(vertices") != std::string::npos ||
            code.find("gl_TessLevel") != std::string::npos ||
            code.find("gl_TessCoord") != std::string::npos ||
            code.find("patch") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Determine if shader is a compute shader based on content analysis
// ---------------------------------------------------------------------------
static bool is_compute_shader(const char* glsl_code) {
    std::string code = glsl_code;
    return (code.find("layout(local_size_x") != std::string::npos ||
            code.find("gl_WorkGroupSize") != std::string::npos ||
            code.find("gl_WorkGroupID") != std::string::npos ||
            code.find("gl_LocalInvocationID") != std::string::npos ||
            code.find("gl_GlobalInvocationID") != std::string::npos ||
            code.find("gl_NumWorkGroups") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Detect the actual shader type from GLSL source content
// ---------------------------------------------------------------------------
static GLenum detect_shader_type_from_source(const char* glsl_code) {
    if (is_geometry_shader(glsl_code)) return GL_GEOMETRY_SHADER;
    if (is_tessellation_shader(glsl_code)) return GL_TESS_EVALUATION_SHADER;
    if (is_compute_shader(glsl_code)) return GL_COMPUTE_SHADER;
    return GL_FRAGMENT_SHADER; // default
}

// ---------------------------------------------------------------------------
// glShaderSource hook
// ---------------------------------------------------------------------------
void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length) {
    LOG()
    LOG_D("glShaderSource hook, shader=%d, count=%d", shader, count)

    // Get the shader type (set during glCreateShader)
    GLenum shaderType = GL_FRAGMENT_SHADER;
    GLES.glGetShaderiv(shader, GL_SHADER_TYPE, (GLint*)&shaderType);

    if (count <= 0 || !string || !string[0]) {
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    std::string glsl_code = string[0];

    // If the shader type is unknown (e.g. Iris sets it after source), detect from content
    if (shaderType == 0 || shaderType == GL_FRAGMENT_SHADER) {
        GLenum detected = detect_shader_type_from_source(glsl_code.c_str());
        if (detected != GL_FRAGMENT_SHADER) {
            shaderType = detected;
            LOG_D("Detected shader type from source: %d", shaderType)
        }
    }

    LOG_D("glShaderSource: shaderType=%d", shaderType)

    // Check if it's already an ES shader (passthrough)
    if (is_direct_shader(glsl_code.c_str())) {
        LOG_D("Direct ES shader, passing through")
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    // Convert GLSL → GLSL ES
    uint essl_version = 320; // target ES 3.2
    int return_code = -1;
    std::string converted = GLSLtoGLSLES(glsl_code.c_str(), shaderType, essl_version, 0, return_code);

    if (return_code < 0) {
        // Conversion failed, pass through original
        LOG_E("GLSL conversion failed, passing through original")
        GLES.glShaderSource(shader, count, string, length);
        return;
    }

    LOG_D("Converted GLSL ES:\n%s", converted.c_str())

    // Set the converted GLSL ES source
    const char* converted_code = converted.c_str();
    GLES.glShaderSource(shader, 1, &converted_code, nullptr);
}